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

struct AudioSourceRuntime;

class AudioSource final : public Component
{
public:
    using Data = schema::AudioSourceDataT;
    AudioSource() noexcept = default;
    explicit AudioSource(Component component) noexcept : Component(component, component_type)
    {
    }
    [[nodiscard]] static bool register_type(ComponentRegistry& registry);
    using Runtime = AudioSourceRuntime;
    static constexpr ComponentType component_type = ComponentType::AudioSourceData;
    [[nodiscard]] static bool valid_settings(const schema::AudioSourceDataT& settings) noexcept;
    [[nodiscard]] schema::AudioSourceDataT settings() const noexcept;
    [[nodiscard]] bool get_settings(Data& output) const noexcept;
    [[nodiscard]] bool set_settings(const schema::AudioSourceDataT& settings);
    [[nodiscard]] bool restore_data(const schema::ComponentDataT& data);
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
    template <class T> friend auto engine::ecs_detail::component_runtime(T component) noexcept;
    [[nodiscard]] Runtime* runtime() const noexcept;
};

} // namespace engine
