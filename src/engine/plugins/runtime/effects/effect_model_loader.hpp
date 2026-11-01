#pragma once

#include <Effekseer.h>

#include <string>

namespace engine::plugins::effects::detail
{

class EffectResources;

class EffectModelLoader final : public Effekseer::ModelLoader
{
public:
    // 官方内存加载器由本对象持有；resources 在本加载器销毁前有效。
    EffectModelLoader(Effekseer::ModelLoaderRef loader, EffectResources& resources) noexcept;
    Effekseer::ModelRef Load(const char16_t* path) override;
    void Unload(Effekseer::ModelRef model) override;

private:
    Effekseer::ModelLoaderRef loader_;
    EffectResources& resources_;
};

} // namespace engine::plugins::effects::detail
