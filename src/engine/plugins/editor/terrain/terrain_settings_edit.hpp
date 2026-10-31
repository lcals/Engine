#pragma once

#include "editor/commands/editor_edit.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/schema/scene_generated.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace engine
{
class WorldSystem;
}

namespace engine::plugins::terrain
{

class TerrainSettingsEdit final : public editor::EditorEdit
{
public:
    TerrainSettingsEdit(WorldSystem& world, std::uint64_t scene,
                        std::shared_ptr<std::uint64_t> node,
                        std::optional<schema::ComponentDataT> before, schema::ComponentDataT after,
                        ByteBuffer before_state, ByteBuffer after_state);
    [[nodiscard]] bool apply(std::string& error) override;
    [[nodiscard]] bool revert(std::string& error) override;

private:
    [[nodiscard]] bool replace(const std::optional<schema::ComponentDataT>& expected,
                               const std::optional<schema::ComponentDataT>& replacement,
                               const ByteBuffer& expected_state,
                               const ByteBuffer& replacement_state, std::string& error);
    WorldSystem& world_;
    std::uint64_t scene_;
    std::shared_ptr<std::uint64_t> node_;
    std::optional<schema::ComponentDataT> before_;
    schema::ComponentDataT after_;
    ByteBuffer before_state_;
    ByteBuffer after_state_;
};

} // namespace engine::plugins::terrain
