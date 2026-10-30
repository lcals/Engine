#include "plugins/runtime/script/vm/script_vm.hpp"

#include "runtime/utils/path_utils.hpp"

#include <filesystem>

namespace engine::plugins::script::vm
{

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
