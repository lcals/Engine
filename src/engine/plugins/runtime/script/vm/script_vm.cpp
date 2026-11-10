#include "plugins/runtime/script/vm/script_vm.hpp"

#include "runtime/utils/path_utils.hpp"

#include <filesystem>

namespace engine::plugins::script::vm
{

#if defined(ENGINE_ENABLE_TEST_SUPPORT)
std::string project_script_wrapper(std::string_view javascript)
{
    std::string source = R"JS((function(require, gameplay, argumentsJson) {
"use strict";
const parse = JSON.parse;
const stringify = JSON.stringify;
const NativePromise = Promise;
const promiseThen = Promise.prototype.then;
const isArray = Array.isArray;
const prototype = Object.getPrototypeOf;
const objectPrototype = Object.prototype;
const descriptors = Object.getOwnPropertyDescriptors;
const owns = Object.prototype.hasOwnProperty;
const keys = Reflect.ownKeys;
const finite = Number.isFinite;
const module = { exports: {} };
const args = parse(argumentsJson);
(function(exports, module, require) {
"use strict";
)JS";
    source.append(javascript);
    source += R"JS(
})(module.exports, module, require);
const execute = module.exports.default;
if (typeof execute !== "function")
    throw new TypeError("Project script must default-export a synchronous function");
const value = execute(args, gameplay);
if (value instanceof NativePromise) {
    promiseThen.call(value, undefined, () => {});
    throw new TypeError("Project script result must be synchronous, not a Promise");
}
if (value !== null && (typeof value === "object" || typeof value === "function") &&
    typeof value.then === "function")
    throw new TypeError("Project script result must not be a thenable");
const visiting = new Set();
function validate(item, depth) {
    if (depth > 64) throw new TypeError("Project script JSON nesting exceeds 64 levels");
    if (item === null || typeof item === "boolean" || typeof item === "string") return;
    if (typeof item === "number" && finite(item)) return;
    if (typeof item !== "object") throw new TypeError("Project script result is not JSON data");
    const array = isArray(item);
    if (!array && prototype(item) !== objectPrototype && prototype(item) !== null)
        throw new TypeError("Project script result must contain only plain JSON objects");
    if (visiting.has(item)) throw new TypeError("Project script result contains a cycle");
    visiting.add(item);
    const properties = descriptors(item);
    if (array) {
        if (keys(properties).length !== item.length + 1)
            throw new TypeError("Project script JSON array contains extra properties or holes");
        for (let index = 0; index < item.length; ++index) {
            const property = properties[index];
            if (!property || !owns.call(property, "value"))
                throw new TypeError("Project script JSON array contains a hole or accessor");
            validate(property.value, depth + 1);
        }
    } else {
        for (const key of keys(properties)) {
            const property = properties[key];
            if (typeof key !== "string" || !property.enumerable ||
                !owns.call(property, "value"))
                throw new TypeError("Project script JSON object contains unsupported properties");
            validate(property.value, depth + 1);
        }
    }
    visiting.delete(item);
}
validate(value, 0);
return stringify(value);
}))JS";
    return source;
}

#endif

bool resolve_module_path(std::string_view referrer, std::string_view request, std::string& output)
{
    output.clear();
    if (request == "engine")
    {
        output = request;
        return true;
    }
    if ((!request.starts_with("./") && !request.starts_with("../")) ||
        request.find_first_of("\\:") != std::string_view::npos ||
        request.find('\0') != std::string_view::npos)
    {
        return false;
    }
    const auto path = (PathUtils::from_utf8(referrer).parent_path() / PathUtils::from_utf8(request))
                          .lexically_normal();
    if (path.is_absolute() || path.has_root_path())
    {
        return false;
    }
    for (const auto& part : path)
    {
        if (part == "..")
        {
            return false;
        }
    }
    output = PathUtils::to_utf8(path);
    return !output.empty() && output != "." && output.back() != '/';
}

} // namespace engine::plugins::script::vm
