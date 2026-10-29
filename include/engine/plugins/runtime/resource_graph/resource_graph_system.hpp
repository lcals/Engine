#pragma once

#include "runtime/resource/resource_loader.hpp"
#include "runtime/system/system.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace engine
{

class ResourceSystem;

class ResourceGraphSystem final : public System, public ResourceLoader
{
public:
    // 核心先初始化、后关闭；有效期须覆盖本系统。
    explicit ResourceGraphSystem(ResourceSystem& resources) noexcept;
    ~ResourceGraphSystem() noexcept override;

    [[nodiscard]] bool load(std::string_view guid, ResourcePtr& output) override;
    [[nodiscard]] ResourcePtr find(std::string_view guid) const noexcept override;
    [[nodiscard]] const std::string& last_error() const noexcept override;

protected:
    [[nodiscard]] bool on_initialize(const EngineConfig& config) override;
    void on_shutdown() noexcept override;

private:
    ResourceSystem& resources_;
    std::uint64_t generation_ = 0;
    std::map<std::string, std::weak_ptr<const Resource>, std::less<>> loaded_;
    std::string last_error_;
};

} // namespace engine
