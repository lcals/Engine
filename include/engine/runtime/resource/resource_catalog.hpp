#pragma once

#include "runtime/core/guid_map.hpp"
#include "runtime/schema/asset_catalog_generated.h"

#include <map>
#include <string_view>

namespace engine
{

class ResourceCatalog final
{
public:
    // 仅验证元数据；失败保留现有目录，不访问实体文件。
    [[nodiscard]] bool assign(const schema::AssetCatalogDataT& data);
    [[nodiscard]] const schema::AssetEntryT* find(std::string_view guid) const noexcept;
    [[nodiscard]] const GuidMap& identities() const noexcept;
    [[nodiscard]] const schema::AssetCatalogDataT& data() const noexcept;
    [[nodiscard]] std::string_view revision() const noexcept;

private:
    GuidMap identities_;
    schema::AssetCatalogDataT data_;
    std::map<Guid, std::size_t, std::less<>> entries_;
};

} // namespace engine
