#pragma once

#include "runtime/scene/scene.hpp"

#include <cstdlib>
#include <memory>
#include <vector>

namespace test_support
{
inline std::vector<engine::Node> scene_nodes(const engine::Scene& scene)
{
    std::vector<engine::Node> output;
    if (!scene.collect_nodes(output))
    {
        std::abort();
    }
    return output;
}

inline std::vector<engine::schema::PrefabInstanceDataT> prefab_instances(const engine::Scene& scene)
{
    std::vector<engine::schema::PrefabInstanceDataT> output;
    if (!scene.collect_prefab_instances(output))
    {
        std::abort();
    }
    return output;
}

inline std::unique_ptr<engine::schema::NodeInfoT> node_info()
{
    auto output = std::make_unique<engine::schema::NodeInfoT>();
    output->properties = std::make_unique<engine::schema::NodePropertiesT>();
    return output;
}

inline std::unique_ptr<engine::schema::ComponentDataT> component_data()
{
    auto output = std::make_unique<engine::schema::ComponentDataT>();
    output->flags = std::make_unique<engine::schema::ComponentFlagsT>();
    return output;
}
} // namespace test_support
