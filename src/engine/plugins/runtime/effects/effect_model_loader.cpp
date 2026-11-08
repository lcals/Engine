#include "plugins/runtime/effects/effect_model_loader.hpp"
#include "runtime/resource/types/source_resource.hpp"

#include "plugins/runtime/effects/effect_resources.hpp"

#include <limits>
#include <utility>

namespace engine::plugins::effects::detail
{

EffectModelLoader::EffectModelLoader(Effekseer::ModelLoaderRef loader,
                                     EffectResources& resources) noexcept
    : loader_(std::move(loader)), resources_(resources)
{
}

Effekseer::ModelRef EffectModelLoader::Load(const char16_t* path)
{
    ResourceReference resource;
    const auto logical = effect_utf8(path);
    if (!loader_ || !resources_.resolve(logical, schema::AssetKind::source, resource) ||
        (*resource.data<SourceResource>()).empty() ||
        (*resource.data<SourceResource>()).size() >
            static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
    {
        if (resources_.error.empty())
        {
            resources_.error = "Invalid effect model resource: " + logical;
        }
        return nullptr;
    }
    const auto bytes = (*resource.data<SourceResource>());
    auto model = loader_->Load(bytes.data(), static_cast<std::int32_t>(bytes.size()));
    if (!model)
    {
        resources_.error = "Effekseer could not decode model resource: " + logical;
    }
    return model;
}

void EffectModelLoader::Unload(Effekseer::ModelRef model)
{
    if (loader_ && model)
    {
        loader_->Unload(std::move(model));
    }
}

} // namespace engine::plugins::effects::detail
