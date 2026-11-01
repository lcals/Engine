#include "plugins/runtime/effects/effect_validation.hpp"

#include <Effekseer.h>
#include <Effekseer/Renderer/Effekseer.GpuParticles.h>

#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

namespace engine::plugins::effects
{
namespace
{

class GpuParticleDetector final : public Effekseer::GpuParticleFactory
{
public:
    Effekseer::GpuParticles::ResourceRef CreateResource(const Effekseer::GpuParticles::ParamSet&,
                                                        const Effekseer::Effect*) override
    {
        found = true;
        return nullptr;
    }
    bool found = false;
};

bool valid_dependency_path(const char16_t* path)
{
    if (path == nullptr)
    {
        return false;
    }
    const std::u16string_view text(path);
    // 官方 EffectFactory 当前通过 FixedSizeString<char16_t,512> 传递依赖路径。
    if (text.empty() || text.size() >= 512)
    {
        return false;
    }
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const auto unit = text[index];
        if (unit >= 0xd800 && unit <= 0xdbff)
        {
            if (++index >= text.size() || text[index] < 0xdc00 || text[index] > 0xdfff)
            {
                return false;
            }
        }
        else if (unit >= 0xdc00 && unit <= 0xdfff)
        {
            return false;
        }
    }
    return true;
}

} // namespace

bool validate_effect(ByteView bytes, std::string& error)
{
    error.clear();
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        error = "Effekseer data is empty or too large";
        return false;
    }
    auto settings = Effekseer::Setting::Create();
    auto detector = Effekseer::MakeRefPtr<GpuParticleDetector>();
    settings->SetGpuParticleFactory(detector);
    const auto effect =
        Effekseer::Effect::Create(settings, bytes.data(), static_cast<std::int32_t>(bytes.size()));
    if (!effect || effect->GetRoot() == nullptr)
    {
        error = "Invalid Effekseer runtime data";
        return false;
    }
    if (detector->found || effect->GetMaterialCount() != 0 || effect->GetWaveCount() != 0 ||
        effect->GetCurveCount() != 0)
    {
        error =
            "Effects do not yet support GPU particles, custom materials, sound or external curves";
        return false;
    }
    const auto valid_paths = [](int count, const auto& get)
    {
        for (int index = 0; index < count; ++index)
        {
            if (!valid_dependency_path(get(index)))
            {
                return false;
            }
        }
        return true;
    };
    if (!valid_paths(effect->GetColorImageCount(),
                     [&](int index) { return effect->GetColorImagePath(index); }) ||
        !valid_paths(effect->GetNormalImageCount(),
                     [&](int index) { return effect->GetNormalImagePath(index); }) ||
        !valid_paths(effect->GetDistortionImageCount(),
                     [&](int index) { return effect->GetDistortionImagePath(index); }) ||
        !valid_paths(effect->GetModelCount(),
                     [&](int index) { return effect->GetModelPath(index); }))
    {
        error = "Effect dependency paths require valid UTF-16 and at most 511 code units";
        return false;
    }
    std::vector<const Effekseer::EffectNode*> nodes{effect->GetRoot()};
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        const auto* node = nodes[index];
        const auto basic = node->GetBasicRenderParameter();
        if (basic.Distortion ||
            basic.MaterialType == Effekseer::RendererMaterialType::BackDistortion ||
            basic.MaterialType == Effekseer::RendererMaterialType::File ||
            !std::isfinite(basic.SoftParticleDistanceFar) ||
            !std::isfinite(basic.SoftParticleDistanceNear) ||
            !std::isfinite(basic.SoftParticleDistanceNearOffset) ||
            basic.SoftParticleDistanceFar != 0.0F || basic.SoftParticleDistanceNear != 0.0F ||
            basic.SoftParticleDistanceNearOffset != 0.0F)
        {
            error = "Effects do not yet support background distortion, soft particles or custom "
                    "materials";
            return false;
        }
        const auto count = node->GetChildrenCount();
        if (count < 0 || static_cast<std::size_t>(count) > 65536U - nodes.size())
        {
            error = "Effekseer node hierarchy exceeds the supported size";
            return false;
        }
        for (int child = 0; child < count; ++child)
        {
            const auto* value = node->GetChild(child);
            if (value == nullptr)
            {
                error = "Effekseer node hierarchy contains an invalid child";
                return false;
            }
            nodes.push_back(value);
        }
    }
    return true;
}

} // namespace engine::plugins::effects
