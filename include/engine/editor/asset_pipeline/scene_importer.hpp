#pragma once

#include "runtime/schema/scene_generated.h"

#include <toml++/toml.hpp>

#include <filesystem>
#include <string>

namespace engine::editor
{

class SceneImporter final
{
public:
    [[nodiscard]] static bool validate(const schema::SceneDataT& data, std::string& error);
    [[nodiscard]] static bool read_nodes(const toml::array& nodes,
                                         std::vector<std::unique_ptr<schema::NodeDataT>>& output,
                                         std::string& error);
    [[nodiscard]] static bool
    write_nodes(const std::vector<std::unique_ptr<schema::NodeDataT>>& nodes, toml::array& output,
                std::string& error);
    [[nodiscard]] static bool read_component(const toml::table& table,
                                             schema::ComponentDataT& output, std::string& error);
    [[nodiscard]] static bool write_component(const schema::ComponentDataT& component,
                                              toml::table& output, std::string& error);
    [[nodiscard]] static bool read(const std::filesystem::path& source, schema::SceneDataT& output,
                                   std::string& error);
    [[nodiscard]] static bool write(const std::filesystem::path& destination,
                                    const schema::SceneDataT& data, std::string& error);
};

} // namespace engine::editor
