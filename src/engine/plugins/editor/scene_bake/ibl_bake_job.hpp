#pragma once

#include "runtime/schema/texture_generated.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace engine::plugins::scene_bake
{
enum class BakeProgress
{
    running,
    complete,
    cancelled,
    failed
};

class IblBakeJob final
{
public:
    IblBakeJob();
    ~IblBakeJob() noexcept;
    IblBakeJob(const IblBakeJob&) = delete;
    IblBakeJob& operator=(const IblBakeJob&) = delete;
    [[nodiscard]] bool start(const schema::TextureDataT& source, std::uint16_t resolution,
                             std::uint16_t samples, std::uint16_t lut_size, std::string& error);
    [[nodiscard]] BakeProgress tick(std::uint32_t frame, std::string& error);
    void cancel() noexcept;
    // 停止GPU对象，保留可能仍在写入的CPU缓冲直到bgfx关闭后析构。
    void shutdown() noexcept;
    [[nodiscard]] std::uint32_t completed() const noexcept;
    [[nodiscard]] std::uint32_t total() const noexcept;
    [[nodiscard]] const std::array<std::vector<std::uint8_t>, 3>& outputs() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace engine::plugins::scene_bake
