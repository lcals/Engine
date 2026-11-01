#pragma once

#include "editor/commands/editor_edit.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/schema/scene_generated.h"

#include <cstdint>
#include <memory>

namespace engine
{
class WorldSystem;
}

namespace engine::plugins::terrain
{

class TerrainStateEdit final : public editor::EditorEdit
{
public:
    TerrainStateEdit(WorldSystem& world, std::uint64_t scene, std::shared_ptr<std::uint64_t> node,
                     schema::ComponentDataT settings, ByteBuffer before, ByteBuffer after);
    [[nodiscard]] bool affects_scene() const noexcept override;
    [[nodiscard]] bool apply(std::string& error) override;
    [[nodiscard]] bool revert(std::string& error) override;

private:
    [[nodiscard]] bool replace(const ByteBuffer& expected, const ByteBuffer& value,
                               std::string& error);
    WorldSystem& world_;
    std::uint64_t scene_;
    std::shared_ptr<std::uint64_t> node_;
    schema::ComponentDataT settings_;
    ByteBuffer before_;
    ByteBuffer after_;
};

} // namespace engine::plugins::terrain
