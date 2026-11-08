#pragma once

#include "runtime/scene/component.hpp"
#include "runtime/scene/scene.hpp"

#include <cstdio>
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
// 测试夹具只读取应当有效的组件；错误直接使测试失败，不掩盖为默认设置。
template <class Component> auto read_settings(Component component)
{
    typename Component::Data output{};
    if (!component.get_settings(output))
    {
        std::fputs("Cannot read settings from the test component\n", stderr);
        std::exit(EXIT_FAILURE);
    }
    return output;
}
inline engine::schema::ComponentDataT read_data(engine::Component component)
{
    engine::schema::ComponentDataT output;
    if (!component.get_data(output))
    {
        std::fputs("Cannot read data from the test component\n", stderr);
        std::exit(EXIT_FAILURE);
    }
    return output;
}
} // namespace test_support
