#pragma once

#include "runtime/schema/model_generated.h"
#include "runtime/schema/prefab_generated.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace engine::editor
{

class PrefabImporter final
{
public:
    [[nodiscard]] static bool read(const std::filesystem::path& source, schema::PrefabDataT& output,
                                   std::string& error);
    [[nodiscard]] static bool write(const std::filesystem::path& destination,
                                    const schema::PrefabDataT& data, std::string& error);
    // 保留 Model 来源、稳定映射和覆盖；失败保留原候选。
    [[nodiscard]] static bool from_model(const schema::ModelDataT& model,
                                         std::string_view default_material_guid,
                                         schema::PrefabDataT& in_out, std::string& error);
    [[nodiscard]] static bool validate(const schema::PrefabDataT& data, std::string& error);
    // first_free_id 大于调用方场景的全部现有和预留 ID；只在成功时更新实例映射。
    [[nodiscard]] static bool
    expand_instance(const schema::PrefabDataT& prefab, schema::PrefabInstanceDataT& instance,
                    std::uint64_t first_free_id,
                    std::vector<std::unique_ptr<schema::NodeDataT>>& output, std::string& error);
    // removed 由调用方处理；其余仅应用 fields 中明确列出的 codec 顶层属性。
    [[nodiscard]] static bool
    apply_component_override(schema::ComponentDataT& in_out,
                             const schema::PrefabComponentOverrideT& override_data,
                             std::string& error);
};

} // namespace engine::editor
