#include "plugins/editor/modeling/modeling_source_io.hpp"
#include "editor/file_text.hpp"

#include "plugins/editor/modeling/editable_mesh.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <SDL3/SDL_filesystem.h>
#include <toml++/toml.hpp>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace engine::plugins::modeling::detail
{
namespace fs = std::filesystem;
namespace
{
constexpr std::size_t maximum_source_size = 64 * 1024 * 1024;
} // namespace

bool safe_asset_path(const fs::path& root, std::string_view name, fs::path& output,
                     std::string& error)
{
    output.clear();
    const auto relative = PathUtils::from_utf8(name);
    if (name.empty() || name.find('\0') != std::string_view::npos || relative.is_absolute() ||
        relative.has_root_name() || relative.extension() != ".mesh")
    {
        error = "Model path must be a relative .mesh path inside assets";
        return false;
    }
    for (const auto& component : relative)
    {
        if (component == ".." || component == ".")
        {
            error = "Model paths cannot traverse directories";
            return false;
        }
    }
    std::error_code ec;
    const auto canonical_root = fs::canonical(root, ec);
    const auto assets = canonical_root / "assets";
    if (ec || !fs::is_directory(assets, ec) || ec)
    {
        error = "Project assets directory is unavailable";
        return false;
    }
    auto candidate = assets / relative;
    fs::path cursor;
    for (const auto& component : candidate)
    {
        cursor /= component;
        const auto status = fs::symlink_status(cursor, ec);
        if (ec && ec != std::errc::no_such_file_or_directory)
        {
            error = "Cannot inspect model path";
            return false;
        }
        ec.clear();
        if (fs::is_symlink(status))
        {
            error = "Model paths cannot contain symbolic links";
            return false;
        }
    }
    output = std::move(candidate);
    return true;
}

bool save_source(const fs::path& path, const schema::EditableMeshDataT& source,
                 const schema::MeshDataT& mesh, std::string& error)
{
    std::vector<std::uint8_t> bytes;
    if (!mesh::serialize(source, bytes, error))
    {
        return false;
    }
    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "vertices = [\n";
    for (const auto& vertex : mesh.vertices)
    {
        output << "  [" << vertex.x() << ", " << vertex.y() << ", " << vertex.z() << ", "
               << vertex.nx() << ", " << vertex.ny() << ", " << vertex.nz() << "],\n";
    }
    output << "]\nindices = [";
    for (const auto value : mesh.indices)
    {
        output << value << ',';
    }
    output << "]\ntexcoords = [\n";
    for (const auto& value : mesh.texcoords)
    {
        output << "  [" << value.x() << ", " << value.y() << "],\n";
    }
    output << "]\ncolors = [\n";
    for (const auto& value : mesh.colors)
    {
        output << "  [" << value.r() << ", " << value.g() << ", " << value.b() << ", " << value.a()
               << "],\n";
    }
    output << "]\nsubmeshes = [\n";
    for (const auto& submesh : mesh.submeshes)
    {
        output << "  { first_index = " << submesh.first_index()
               << ", index_count = " << submesh.index_count()
               << ", material_slot = " << submesh.material_slot() << " },\n";
    }
    output << "]\n\n[modeling]\nformat = \"flatbuffers-hex\"\ndata = \"";
    constexpr std::string_view digits = "0123456789abcdef";
    for (const auto value : bytes)
    {
        output << digits[value >> 4] << digits[value & 15];
    }
    output << "\"\n";
    const auto text = output.str();
    if (text.size() > maximum_source_size)
    {
        error = "Model source exceeds the 64 MiB limit";
        return false;
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = "Cannot create model source directory";
        return false;
    }
    auto temporary = path;
    temporary += ".modeling-write.meta";
    const auto status = fs::symlink_status(temporary, ec);
    if ((!ec && status.type() != fs::file_type::not_found) ||
        (ec && ec != std::errc::no_such_file_or_directory))
    {
        error = "Model temporary destination already exists or cannot be inspected";
        return false;
    }
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file ||
        !SDL_RenamePath(PathUtils::to_utf8(temporary).c_str(), PathUtils::to_utf8(path).c_str()))
    {
        fs::remove(temporary, ec);
        error = "Cannot publish model source";
        return false;
    }
    return true;
}

bool load_source(const fs::path& path, schema::EditableMeshDataT& source, std::string& error)
{
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    std::string text;
    if (ec || size > maximum_source_size || !engine::editor::file_detail::read_text(path, text) ||
        text.size() > maximum_source_size)
    {
        error = "Cannot read model source or it exceeds 64 MiB";
        return false;
    }
    auto parsed = toml::parse(text);
    if (!parsed)
    {
        error = "Model source contains invalid TOML";
        return false;
    }
    const auto format = parsed["modeling"]["format"].value<std::string>();
    const auto encoded = parsed["modeling"]["data"].value<std::string>();
    if (!format || *format != "flatbuffers-hex" || !encoded || encoded->size() % 2 != 0)
    {
        error = "Model source requires modeling.format = flatbuffers-hex and encoded data";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(encoded->size() / 2);
    const auto digit = [](char value) -> int
    {
        if (value >= '0' && value <= '9')
        {
            return value - '0';
        }
        return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
    };
    for (std::size_t index = 0; index < encoded->size(); index += 2)
    {
        const auto high = digit((*encoded)[index]);
        const auto low = digit((*encoded)[index + 1]);
        if (high < 0 || low < 0)
        {
            error = "Model source has invalid hexadecimal data";
            return false;
        }
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
    }
    return mesh::deserialize(bytes, source, error);
}

} // namespace engine::plugins::modeling::detail
