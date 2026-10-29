#pragma once

#include "runtime/schema/asset_catalog_generated.h"

#include <string>

namespace engine::editor::asset_pipeline
{
struct ImportSubresource
{
    std::string key;
    schema::AssetKind kind = schema::AssetKind::source;
};
} // namespace engine::editor::asset_pipeline
