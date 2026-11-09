#!/usr/bin/env python3
"""从 camera/character_controller Schema 生成边界 JSON 编解码，不维护第二份字段模型。"""
import argparse
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--schema-dir', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--declarations', type=Path)
parser.add_argument('--check-declarations', action='store_true')
args = parser.parse_args()
texts = [(name, re.sub(r'//[^\n]*', '', (args.schema_dir / (name + '.fbs')).read_text()))
         for name in ('camera', 'character_controller')]
enums = {}
tables = {}
for _, text in texts:
    for name, body in re.findall(r'enum\s+(\w+)\s*:\w+\s*\{([^}]+)\}', text):
        enums[name] = [v.strip().split('=')[0].strip() for v in body.split(',') if v.strip()]
    for name, body in re.findall(r'table\s+(\w+)\s*\{([^}]+)\}', text):
        fields = []
        for item in body.split(';'):
            if not item.strip(): continue
            match = re.match(r'\s*(\w+)\s*:\s*(\[\w+\]|\w+)', item)
            if not match: raise ValueError(item)
            fields.append(match.groups())
        tables[name] = fields

def camel(name):
    parts = name.split('_')
    return parts[0] + ''.join(p.title() for p in parts[1:])

out = ['''// Generated from runtime camera and character schemas. Do not edit.
#pragma once
#include "runtime/schema/scene_generated.h"
#include <nlohmann/json.hpp>
#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
namespace engine::schema::control_json
{
using Json=nlohmann::json;
inline bool reject(std::string& error,std::string_view path){error="Invalid control field: "+std::string(path);return false;}
inline bool read(const Json& j,std::string& out,std::string& e,bool,std::string_view p){if(!j.is_string())return reject(e,p);out=j.get<std::string>();return true;}
inline bool read(const Json& j,bool& out,std::string& e,bool,std::string_view p){if(!j.is_boolean())return reject(e,p);out=j.get<bool>();return true;}
template<class T> requires std::is_arithmetic_v<T> && (!std::is_same_v<T,bool>)
inline bool read(const Json& j,T& out,std::string& e,bool,std::string_view p)
{
    if constexpr(std::is_same_v<T,std::uint64_t>)
    {
        if(j.is_string()){const auto& s=j.get_ref<const std::string&>();T v{};const auto r=std::from_chars(s.data(),s.data()+s.size(),v);if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size())return reject(e,p);out=v;return true;}
    }
    if(!j.is_number()||j.is_boolean())return reject(e,p);
    const double v=j.get<double>();if(!std::isfinite(v)||v<static_cast<double>(std::numeric_limits<T>::lowest())||v>static_cast<double>(std::numeric_limits<T>::max()))return reject(e,p);
    if constexpr(std::is_integral_v<T>){if(std::floor(v)!=v)return reject(e,p);}
    if constexpr(std::is_same_v<T,std::uint64_t>){if(v>=18446744073709551616.0)return reject(e,p);}
    out=j.get<T>();return true;
}
inline Json write(const std::string& v,bool){return v;}
inline Json write(bool v,bool){return v;}
template<class T> requires std::is_arithmetic_v<T> && (!std::is_same_v<T,bool>)
inline Json write(T v,bool script)
{
    if constexpr(std::is_same_v<T,std::uint64_t>){if(script)return Json(v);return std::to_string(v);}
    else if constexpr(std::is_integral_v<T>)return Json(static_cast<std::int64_t>(v));
    else return Json(static_cast<double>(v));
}
''']
for n in ('Vec2','Vec3'):
    axes=list('xyz')[:int(n[-1])]
    out += [f'inline bool read(const Json& j,{n}& out,std::string& e,bool script,std::string_view p){{',
            f'if(!j.is_object()||j.size()!={len(axes)})return reject(e,p);double '+','.join(a+'=0' for a in axes)+';',
            'if('+ '||'.join(f'!j.contains("{a}")||!read(j["{a}"],{a},e,script,p)' for a in axes)+')return false;',
            f'out={n}('+','.join(axes)+');return true;}',
            f'inline Json write(const {n}& v,bool script){{return Json{{'+','.join('{"'+a+'",write(v.'+a+'(),script)}' for a in axes)+'};}']
for name, vals in enums.items():
    out += [f'inline bool read(const Json& j,{name}& out,std::string& e,bool script,std::string_view p){{',
            'if(script){std::int32_t v=0;if(!read(j,v,e,script,p))return false;',
            f'if(v<0||v>={len(vals)})return reject(e,p);out=static_cast<{name}>(v);return true;}}',
            'if(!j.is_string())return reject(e,p);const auto& value=j.get_ref<const std::string&>();']
    for val in vals: out += [f'if(value=="{val}"){{out={name}::{val};return true;}}']
    out += ['return reject(e,p);}',f'inline Json write({name} v,bool script){{if(script)return static_cast<int>(v);return EnumName{name}(v);}}']
for name in tables:
    out += [f'inline bool read(const Json&, {name}T&,std::string&,bool,std::string_view);',
            f'inline Json write(const {name}T&,bool);']
for name, fields in tables.items():
    out += [f'inline bool read(const Json& j,{name}T& out,std::string& e,bool script=false,std::string_view p="{name}"){{',
            'if(!j.is_object())return reject(e,p);',
            'for(auto it=j.begin();it!=j.end();++it){const auto& key=it.key();if('+ '&&'.join(f'key!=(script?"{camel(f)}":"{f}")' for f,_ in fields)+')return reject(e,std::string(p)+"."+key);}',
            f'{name}T next;']
    for field, ty in fields:
        key=f'(script?"{camel(field)}":"{field}")'
        out += [f'if(j.contains({key})){{const auto& v=j[{key}];const auto path=std::string(p)+"."+{key};']
        if ty.startswith('['):
            ty=ty[1:-1]; cpptype=(ty+'T') if ty in tables else {'string':'std::string'}.get(ty,ty)
            out += ['if(!v.is_array()||v.size()>65536)return reject(e,path);',
                    f'for(std::size_t i=0;i<v.size();++i){{{cpptype} item{{}};if(!read(v[i],item,e,script,path+"["+std::to_string(i)+"]"))return false;next.{field}.push_back(std::move(item));}}']
        elif ty in tables:
            out += [f'if(!v.is_null()){{next.{field}=std::make_unique<{ty}T>();if(!read(v,*next.{field},e,script,path))return false;}}']
        else: out += [f'if(!read(v,next.{field},e,script,path))return false;']
        out += ['}']
    out += ['out=std::move(next);return true;}',f'inline Json write(const {name}T& v,bool script=false){{Json out=Json::object();']
    for field,ty in fields:
        key=f'(script?"{camel(field)}":"{field}")'
        if ty.startswith('['):
            out += [f'out[{key}]=Json::array();for(const auto& item:v.{field})out[{key}].push_back(write(item,script));']
        elif ty in tables:
            out += [f'out[{key}]=v.{field}?write(*v.{field},script):Json(nullptr);']
        else: out += [f'out[{key}]=write(v.{field},script);']
    out += ['return out;}']
def describe(ty):
    if ty.startswith('['):
        return {'type': 'array', 'maxItems': 65536, 'items': describe(ty[1:-1])}
    if ty in tables:
        properties = {}
        for field, field_type in tables[ty]:
            value = describe(field_type)
            if field_type in tables:
                value['type'] = ['object', 'null']
            properties[field] = value
        return {'type': 'object', 'additionalProperties': False, 'properties': properties}
    if ty in enums:
        return {'type': 'string', 'enum': enums[ty]}
    if ty in ('Vec2', 'Vec3'):
        axes = list('xyz')[:int(ty[-1])]
        return {'type': 'object', 'additionalProperties': False, 'required': axes,
                'properties': {axis: {'type': 'number'} for axis in axes}}
    if ty == 'ulong':
        return {'type': 'string', 'minLength': 1, 'maxLength': 20,
                'description': 'Unsigned 64-bit identity, encoded as a decimal string.'}
    if ty == 'int':
        return {'type': 'integer', 'minimum': -2147483648, 'maximum': 2147483647}
    if ty == 'uint':
        return {'type': 'integer', 'minimum': 0, 'maximum': 4294967295}
    if ty not in ('string', 'bool', 'float', 'double'):
        raise ValueError(f'Unsupported control schema type: {ty}')
    return {'type': {'string': 'string', 'bool': 'boolean'}.get(ty, 'number')}

for root in ('CameraControllerData', 'CharacterControllerData', 'CharacterCameraData'):
    encoded = json.dumps(describe(root), separators=(',', ':'))
    out += [f'inline Json describe(const {root}T*){{return Json::parse(R"schema({encoded})schema",nullptr,false);}}']
out += ['} // namespace engine::schema::control_json']
args.output.parent.mkdir(parents=True, exist_ok=True)
content='\n'.join(out)+'\n'
if not args.output.exists() or args.output.read_text()!=content: args.output.write_text(content)

if args.declarations:
    lines = ["    // BEGIN GENERATED CONTROL SCHEMA", "    /** Generated object shapes; fields and enum values come only from the runtime schemas. */", "    export namespace ControlData {"]
    for dimension in (2, 3):
        lines.append(f"        export interface Vec{dimension}T {{ " + " ".join(f"{a}: number;" for a in 'xyz'[:dimension]) + " }")
    for name, values in enums.items():
        lines.append("        /** " + ", ".join(f"{i}={v}" for i,v in enumerate(values)) + " */")
        lines.append(f"        export type {name} = " + " | ".join(str(i) for i in range(len(values))) + ";")
    def ts_type(t):
        if t.startswith('['): return ts_type(t[1:-1]) + "[]"
        if t in tables: return t + "T"
        if t in enums: return t
        if t in ('Vec2','Vec3'): return t + 'T'
        return {'string':'string','bool':'boolean','ulong':'bigint'}.get(t,'number')
    for name, fields in tables.items():
        lines.append(f"        export interface {name}T {{")
        for field,t in fields:
            lines.append(f"            {camel(field)}: {ts_type(t)}" + (" | null" if t in tables else "") + ";")
        lines.append("        }")
    lines.extend(["    }", "    // END GENERATED CONTROL SCHEMA"])
    section='\n'.join(lines)
    current=args.declarations.read_text()
    pattern=r"    // BEGIN GENERATED CONTROL SCHEMA.*?    // END GENERATED CONTROL SCHEMA"
    if re.search(pattern,current,re.S): updated=re.sub(pattern,lambda _:section,current,flags=re.S)
    else:
        end=current.rfind('}')
        updated=current[:end]+section+'\n'+current[end:]
    if args.check_declarations:
        if updated!=current: raise SystemExit('engine.d.ts control schema is out of date; run generate_control_codecs.py --declarations include/engine.d.ts')
    elif updated!=current: args.declarations.write_text(updated)
