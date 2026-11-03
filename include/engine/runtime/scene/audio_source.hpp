#pragma once

#include "runtime/scene/component.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace engine::schema
{
struct AudioClipDataT;
}

namespace engine
{

class AudioSystem;
class Resource;

enum class AudioPlayback
{
    stopped,
    pending,
    playing,
    paused,
    failed
};

class AudioSource final : public Component
{
public:
    static constexpr ComponentType component_type = ComponentType::AudioSourceData;
    [[nodiscard]] static bool valid_settings(const schema::AudioSourceDataT& settings) noexcept;
    [[nodiscard]] const schema::AudioSourceDataT& settings() const noexcept;
    [[nodiscard]] bool set_settings(const schema::AudioSourceDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data) override;
    [[nodiscard]] bool set_clip(std::string_view path);
    [[nodiscard]] std::string_view clip_path() const noexcept;
    [[nodiscard]] const schema::AudioClipDataT* clip_data() const noexcept;
    // 成功表示请求已接受；真实输出成功由 AudioSystem 将 pending 改为 playing。
    [[nodiscard]] bool play() noexcept;
    [[nodiscard]] bool pause() noexcept;
    [[nodiscard]] bool stop() noexcept;
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] AudioPlayback playback() const noexcept;

private:
    friend class Node;
    friend class AudioSystem;
    explicit AudioSource(Node& node);

    std::shared_ptr<const Resource> clip_;
    std::uint64_t revision_ = 0;
    AudioPlayback playback_ = AudioPlayback::stopped;
    bool awake_pending_ = true;
};

} // namespace engine
