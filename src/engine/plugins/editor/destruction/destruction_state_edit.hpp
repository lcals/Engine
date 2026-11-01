#pragma once

#include "editor/commands/editor_edit.hpp"
#include "runtime/resource/virtual_file_system.hpp"
#include "runtime/scene/scene_types.hpp"

#include <cstdint>
#include <memory>

namespace engine
{
class WorldSystem;
}

namespace engine::plugins::destruction
{
class DestructionComponent;

class DestructionStateEdit final : public editor::EditorEdit
{
public:
    DestructionStateEdit(WorldSystem& world, std::uint64_t scene,
                         std::shared_ptr<std::uint64_t> node,
                         std::shared_ptr<std::uint64_t> revision, schema::ComponentDataT settings,
                         ByteBuffer before, bool reset, Vec3 center, float radius, float damage,
                         float impulse);
    DestructionStateEdit(WorldSystem& world, std::uint64_t scene,
                         std::shared_ptr<std::uint64_t> node,
                         std::shared_ptr<std::uint64_t> revision, schema::ComponentDataT settings,
                         ByteBuffer before, std::shared_ptr<std::uint64_t> support, float max_gap,
                         float min_coverage);
    DestructionStateEdit(WorldSystem& world, std::uint64_t scene,
                         std::shared_ptr<std::uint64_t> node,
                         std::shared_ptr<std::uint64_t> revision, schema::ComponentDataT settings,
                         ByteBuffer before, Vec3 gravity, float elastic_limit, float fatal_limit,
                         std::uint32_t iterations);
    [[nodiscard]] bool affects_scene() const noexcept override;
    [[nodiscard]] bool apply(std::string& error) override;
    [[nodiscard]] bool revert(std::string& error) override;

private:
    [[nodiscard]] DestructionComponent* target(std::string& error);
    WorldSystem& world_;
    std::uint64_t scene_;
    std::shared_ptr<std::uint64_t> node_;
    std::shared_ptr<std::uint64_t> revision_;
    schema::ComponentDataT settings_;
    ByteBuffer before_;
    ByteBuffer after_;
    std::shared_ptr<std::uint64_t> support_;
    float max_gap_ = 0;
    float min_coverage_ = 0;
    bool no_change_ = false;
    bool stress_ = false;
    Vec3 gravity_;
    float elastic_limit_ = 0;
    float fatal_limit_ = 0;
    std::uint32_t iterations_ = 0;
    bool reset_;
    Vec3 center_;
    float radius_;
    float damage_;
    float impulse_;
};

} // namespace engine::plugins::destruction
