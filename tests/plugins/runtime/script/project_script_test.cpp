#include "plugins/runtime/script/vm/script_vm.hpp"

#include <cstdio>
#include <map>
#include <string>
#include <string_view>

namespace
{
using engine::ProjectScriptRequest;
using engine::plugins::script::vm::create_script_vm;

bool expect(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "project script test: %s\n", message);
    }
    return value;
}
} // namespace

int main()
{
    const std::map<std::string, std::string, std::less<>> sources{
        {"engine", "export class Gameplay {}"},
        {"scripts/state.js", "export let count=0; export function bump(){return ++count;}"},
        {"scripts/game.js", R"JS(
import {Gameplay} from "engine";
import {bump} from "./state.js";
export default class Game extends Gameplay {
    constructor(){ super(); this.created=bump(); this.value=10; }
    OnInitialize(){ return true; }
    Update(){}
    OnShutdown(){}
})JS"}};
    std::size_t reads = 0;
    auto machine = create_script_vm();
    if (!expect(machine && machine->initialize(nullptr) && machine->seal_bindings() &&
                    machine->prepare("scripts/game.js",
                                     [&](std::string_view path, std::string& source)
                                     {
                                         const auto found = sources.find(path);
                                         if (found == sources.end())
                                         {
                                             return false;
                                         }
                                         ++reads;
                                         source = found->second;
                                         return true;
                                     }) &&
                    machine->start() && machine->call_gameplay("OnInitialize", true),
                "prepare one live Gameplay instance"))
    {
        return 1;
    }
    const auto modules = machine->loaded_modules();
    const auto source_reads = reads;
    std::string output;
    const auto execute = [&](std::string_view source, std::string_view args = "null")
    {
        const bool success =
            machine->execute_script({1, "scripts/mcp-check.js", source, args}, output);
        if (!success)
        {
            std::fprintf(stderr, "  request diagnostic: %s\n", machine->last_error().c_str());
        }
        return success;
    };
    if (!expect(execute(R"JS(
const state = require("./state.js");
exports.default = (args, game) => {
    game.value += args;
    state.bump();
    return {count:state.count, created:game.created, value:game.value,
            same:game instanceof require("engine").Gameplay};
};)JS", "7") && output == R"({"count":2,"created":1,"value":17,"same":true})",
                "reuse live exports and existing Gameplay instance"))
    {
        return 1;
    }
    for (int index = 0; index < 300; ++index)
    {
        if (!expect(execute("exports.default = args => args;", std::to_string(index)) &&
                        output == std::to_string(index),
                    "temporary scripts do not hit the module graph limit"))
        {
            return 1;
        }
    }
    if (!expect(machine->loaded_modules() == modules && reads == source_reads,
                "temporary CommonJS does not reload or add ES modules") ||
        !expect(execute("exports.default = () => 'fresh';") && output == "\"fresh\"",
                "same logical path executes new submitted code") ||
        !expect(execute("exports.default = () => [null, true, 2, {name:'value'}];") &&
                    output == "[null,true,2,{\"name\":\"value\"}]",
                "JSON values round trip"))
    {
        return 1;
    }
    for (const auto source : {
             "const = invalid;",
             "exports.default = () => { throw Error('temporary error'); };",
             "exports.default = () => require('./missing.js');",
             "exports.default = () => require('node:fs');",
             "exports.default = 4;",
             "exports.default = () => undefined;",
             "exports.default = () => NaN;",
             "exports.default = () => 1n;",
             "exports.default = () => {const a={}; a.a=a; return a;};",
             "exports.default = () => new Date();",
             "exports.default = () => new Uint8Array(1);",
             "exports.default = () => ({get x(){return 1;}});",
             "exports.default = () => [,1];",
             "exports.default = () => {const a=[1]; a.toJSON=()=>2; return a;};",
             "exports.default = () => Promise.resolve(1);",
             "exports.default = () => Promise.reject(Error('returned rejection'));",
             "exports.default = () => ({then(){}});",
             "exports.default = () => 'a'.repeat(262145);"})
    {
        if (!expect(!execute(source) && output.empty() && !machine->last_error().empty(),
                    "invalid temporary request fails and clears its output") ||
            !expect(machine->drain_jobs(), "returned Promise rejection is consumed") ||
            !expect(execute("exports.default = (args,game) => game.created;") && output == "1",
                    "a failed request preserves the live VM"))
        {
            return 1;
        }
    }
    if (!expect(execute("globalThis.savedRequire = require; exports.default = () => null;") &&
                    !execute("exports.default = () => savedRequire('engine');"),
                "require retained by an older request cannot borrow the new execution window") ||
        !expect(!execute("exports.default = (args,game) => "
                         "{game.value=99; throw Error('late');};") &&
                    execute("exports.default = (args,game) => game.value;") && output == "99",
                "request errors do not roll back preceding side effects") ||
        !expect(!execute("exports.default = args => args;", "{") && output.empty() &&
                    execute("exports.default = () => true;") && output == "true",
                "JSON parse failure does not poison the next call"))
    {
        return 1;
    }
    std::puts("Project script PASS: same Run/module instances, JSON, failures "
              "and temporary cleanup");
    return 0;
}
