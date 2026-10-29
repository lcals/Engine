#pragma once

#include "runtime/schema/scene_generated.h"

#include <filesystem>
#include <string>

namespace engine
{

class SceneImporter final
{
public:
    [[nodiscard]] static bool read(const std::filesystem::path& source, schema::SceneDataT& output,
                                   std::string& error);
    [[nodiscard]] static bool write(const std::filesystem::path& destination,
                                    const schema::SceneDataT& data, std::string& error);
};

} // namespace engine
