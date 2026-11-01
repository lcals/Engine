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

namespace engine::plugins::destruction
{

class DestructionSettingsEdit final : public editor::EditorEdit
{
public:
    DestructionSettingsEdit(WorldSystem& world, std::uint64_t scene,
                            std::shared_ptr<std::uint64_t> node,
                            std::shared_ptr<std::uint64_t> revision,
                            std::optional<schema::ComponentDataT> before,
                            schema::ComponentDataT after, ByteBuffer before_state);
    [[nodiscard]] bool apply(std::string& error) override;
    [[nodiscard]] bool revert(std::string& error) override;

private:
    [[nodiscard]] bool replace(const std::optional<schema::ComponentDataT>& expected,
                               const std::optional<schema::ComponentDataT>& replacement,
                               const ByteBuffer& replacement_state, bool capture,
                               std::string& error);
    WorldSystem& world_;
    std::uint64_t scene_;
    std::shared_ptr<std::uint64_t> node_;
    std::shared_ptr<std::uint64_t> revision_;
    std::optional<schema::ComponentDataT> before_;
    schema::ComponentDataT after_;
    ByteBuffer before_state_;
    ByteBuffer after_state_;
    bool captured_ = false;
};

} // namespace engine::plugins::destruction
