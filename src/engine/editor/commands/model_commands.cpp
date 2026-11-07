#include "editor/commands/components/component_edit.hpp"
#include "editor/resource_access.hpp"
#include "runtime/resource/types/model_resource.hpp"
#include "runtime/resource/types/prefab_resource.hpp"

#include "runtime/resource/resource_system.hpp"
#include "runtime/scene/camera.hpp"

#include <algorithm>
#include <set>
#include <vector>

namespace engine::editor::components
{
namespace
{
bool snapshot(const Node& node, schema::NodeDataT& output)
{
    output = {};
    if (node.transient())
    {
        return false;
    }
    output.info = std::make_unique<schema::NodeInfoT>();
    if (!node.build_info(*output.info))
    {
        output = {};
        return false;
    }
    std::vector<Component> components;
    if (!node.collect_components(components))
    {
        return false;
    }
    for (const auto& component : components)
    {
        if (component.transient())
        {
            continue;
        }
        auto data = std::make_unique<schema::ComponentDataT>();
        data->flags = std::make_unique<schema::ComponentFlagsT>();
        if (!component.build_data(*data))
        {
            output = {};
            return false;
        }
        output.components.push_back(std::move(data));
    }
    return true;
}

class ModelInstantiationEdit final : public EditorEdit
{
public:
    ModelInstantiationEdit(WorldSystem& world, NodeAliases& aliases, std::uint64_t scene,
                           std::string path, std::string name,
                           std::vector<schema::AssetEntryT> versions, bool prefab = false)
        : world_(world), aliases_(aliases), scene_(scene), path_(std::move(path)),
          name_(std::move(name)), versions_(std::move(versions)), prefab_(prefab)
    {
    }

    bool apply(std::string& error) override
    {
        auto* scene = world_.scene();
        auto* loader = world_.resource_loader();
        if (!scene || scene->id() != scene_ || !loader)
        {
            error = "Model instance targets a stale scene";
            return false;
        }
        std::vector<ResourceReference> pins;
        if (!resource_access::load_versions(loader, versions_, pins))
        {
            error = "Model resources changed; the prepared edit is stale";
            return false;
        }
        std::vector<Node> nodes;
        if (!scene->collect_nodes(nodes))
        {
            error = "Cannot collect scene entities";
            return false;
        }
        if (std::any_of(nodes.begin(), nodes.end(),
                        [this](const auto& node) { return node.name() == name_; }))
        {
            error = "A node with this name already exists";
            return false;
        }
        std::vector<Node> created;
        auto root = prefab_ ? scene->instantiate_prefab(path_, name_, &created)
                            : scene->instantiate_model(path_, name_, &created);
        if (!root)
        {
            error = "Cannot instantiate the model resource";
            return false;
        }
        const auto count = created.size();
        std::vector<schema::NodeDataT> snapshots(count);
        if (!identities_.empty() && count != identities_.size())
        {
            (void)scene->destroy_node(root.id());
            error = "Model hierarchy changed since the edit";
            return false;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!snapshot(created[index], snapshots[index]))
            {
                (void)scene->destroy_node(root.id());
                error = "Cannot retain the instantiated model snapshot";
                return false;
            }
        }
        if (identities_.empty())
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                identities_.push_back(std::make_shared<std::uint64_t>(0));
            }
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto id = created[index].id();
            *identities_[index] = id;
            aliases_[id] = identities_[index];
            if (id == root.id())
            {
                root_index_ = index;
            }
        }
        if (prefab_)
        {
            std::vector<schema::PrefabInstanceDataT> instances;
            if (!scene->collect_prefab_instances(instances))
            {
                (void)scene->destroy_node(root.id());
                error = "Cannot retain prefab metadata";
                return false;
            }
            for (const auto& instance : instances)
            {
                if (instance.root_id == root.local_id())
                {
                    prefab_snapshot_ = instance;
                }
            }
        }
        snapshots_ = std::move(snapshots);
        return true;
    }

    bool revert(std::string& error) override
    {
        auto* scene = world_.scene();
        if (!scene || scene->id() != scene_ || identities_.empty())
        {
            error = "Model instance targets a stale scene";
            return false;
        }
        std::set<std::uint64_t> members;
        if (prefab_)
        {
            std::vector<schema::PrefabInstanceDataT> instances;
            if (!scene->collect_prefab_instances(instances))
            {
                error = "Cannot read prefab metadata";
                return false;
            }
            const auto found =
                std::find_if(instances.begin(), instances.end(), [this](const auto& value)
                             { return prefab_snapshot_ && value == *prefab_snapshot_; });
            if (found == instances.end())
            {
                error = "Prefab association changed outside this edit";
                return false;
            }
        }
        for (std::size_t index = 0; index < identities_.size(); ++index)
        {
            const auto node = scene->find_node(*identities_[index]);
            schema::NodeDataT current;
            if (!node || !snapshot(node, current) || current != snapshots_[index])
            {
                error = "Model nodes changed outside this edit; undo is stale";
                return false;
            }
            members.insert(node.id());
        }
        if (scene->active_camera() && members.contains(scene->active_camera().node().id()))
        {
            error = "Model hierarchy contains the active camera";
            return false;
        }
        std::vector<Node> nodes;
        if (!scene->collect_nodes(nodes))
        {
            error = "Cannot collect scene entities";
            return false;
        }
        for (const auto& node : nodes)
        {
            if (node.parent() && members.contains(node.parent().id()) &&
                !members.contains(node.id()))
            {
                error = "Model has unrelated children; undo would remove them";
                return false;
            }
        }
        if (!scene->destroy_node(*identities_[root_index_]))
        {
            error = "Cannot remove model instance";
            return false;
        }
        for (const auto& identity : identities_)
        {
            aliases_.erase(*identity);
        }
        return true;
    }

private:
    WorldSystem& world_;
    NodeAliases& aliases_;
    std::uint64_t scene_;
    std::string path_;
    std::string name_;
    std::vector<schema::AssetEntryT> versions_;
    std::vector<std::shared_ptr<std::uint64_t>> identities_;
    std::vector<schema::NodeDataT> snapshots_;
    std::optional<schema::PrefabInstanceDataT> prefab_snapshot_;
    std::size_t root_index_ = 0;
    bool prefab_ = false;
};

} // namespace

bool prepare_hierarchy(WorldSystem& world, NodeAliases& aliases, const Json& arguments,
                       Json& result, std::unique_ptr<EditorEdit>& edit, std::string& error,
                       bool prefab)
{
    auto* scene = world.scene();
    if (!scene || !arguments.is_object() || arguments.size() != 3 ||
        !arguments.contains("sceneId") || arguments["sceneId"] != std::to_string(scene->id()) ||
        !arguments.contains("path") || !arguments["path"].is_string() ||
        !arguments.contains("name") || !arguments["name"].is_string())
    {
        error = "instantiate_model requires the current sceneId, a path and a name";
        return false;
    }
    const auto name = arguments["name"].get<std::string>();
    const auto path = arguments["path"].get<std::string>();
    if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos)
    {
        error = "Model instance name must contain 1..128 bytes";
        return false;
    }
    ResourceReference model;
    std::vector<schema::AssetEntryT> retained;
    if (!resource_access::load(world.resource_loader(), path, model) ||
        (prefab ? !model.data<PrefabResource>() : !model.data<ModelResource>()) ||
        !resource_access::collect_versions(model, retained))
    {
        error = "Cannot resolve the model resource and its dependencies";
        return false;
    }
    result = {{"sceneId", arguments["sceneId"]}, {"name", name}, {"path", path}};
    edit = std::make_unique<ModelInstantiationEdit>(world, aliases, scene->id(), path, name,
                                                    std::move(retained), prefab);
    return true;
}
bool prepare_model(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                   std::unique_ptr<EditorEdit>& edit, std::string& error)
{
    return prepare_hierarchy(world, aliases, arguments, result, edit, error, false);
}

bool prepare_prefab(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                    std::unique_ptr<EditorEdit>& edit, std::string& error)
{
    return prepare_hierarchy(world, aliases, arguments, result, edit, error, true);
}
} // namespace engine::editor::components
