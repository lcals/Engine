#pragma once

#include "editor/asset_pipeline/import_context.hpp"
#include "runtime/schema/material_generated.h"
#include "runtime/schema/shader_generated.h"

#include <toml++/toml.hpp>

#include <array>
#include <initializer_list>
#include <string_view>

namespace engine::asset_pipeline
{
[[nodiscard]] bool read_table(const std::filesystem::path& path, toml::table& table,
                              std::string& error);
[[nodiscard]] bool reference(const Guid& guid, schema::AssetKind kind, const ImportContext& context,
                             std::vector<Guid>& dependencies, std::string& error);
[[nodiscard]] bool parameter_values(const schema::MaterialValueUnion& value,
                                    std::array<double, 4>& values, std::size_t& size);
[[nodiscard]] bool valid_parameter(const schema::ShaderParameterT& definition,
                                   const schema::MaterialValueUnion& value);
[[nodiscard]] bool file_hash(const std::filesystem::path& path, std::string& hash,
                             std::string& error);
[[nodiscard]] bool hash_optional_file(const std::filesystem::path& path, std::string& hash,
                                      std::string& error);
[[nodiscard]] bool schema_fingerprint(std::initializer_list<std::string_view> names,
                                      std::string& fingerprint, std::string& error);
[[nodiscard]] std::string source_extension(const std::filesystem::path& path);
[[nodiscard]] std::string generic_path(const std::filesystem::path& path);
} // namespace engine::asset_pipeline
