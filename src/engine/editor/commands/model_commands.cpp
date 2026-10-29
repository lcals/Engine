#include "editor/commands/components/component_edit.hpp"

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
    output.info = std::make_unique<schema::NodeInfoT>(node.info());
    for (const auto& component : node.components())
    {
        auto data = std::make_unique<schema::ComponentDataT>();
        if (!component->build_data(*data))
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
                           std::vector<schema::AssetEntryT> versions)
        : world_(world), aliases_(aliases), scene_(scene), path_(std::move(path)),
          name_(std::move(name)), versions_(std::move(versions))
    {
    }

    bool apply(std::string& error) override
    {
        auto* scene = world_.scene();
        auto* resources = ResourceSystem::instance();
        if (!scene || scene->id() != scene_ || !resources)
        {
            error = "Model instance targets a stale scene";
            return false;
        }
        for (const auto& version : versions_)
        {
            const auto* current = resources->catalog().find(version.guid);
            if (!current || *current != version)
            {
                error = "Model resources changed; the prepared edit is stale";
                return false;
            }
        }
        if (std::any_of(scene->nodes().begin(), scene->nodes().end(),
                        [this](const auto& node) { return node->name() == name_; }))
        {
            error = "A node with this name already exists";
            return false;
        }
        const auto previous_count = scene->nodes().size();
        auto* root = scene->instantiate_model(path_, name_);
        if (!root)
        {
            error = "Cannot instantiate the model resource";
            return false;
        }
        const auto count = scene->nodes().size() - previous_count;
        std::vector<schema::NodeDataT> snapshots(count);
        if (!identities_.empty() && count != identities_.size())
        {
            (void)scene->destroy_node(root->id());
            error = "Model hierarchy changed since the edit";
            return false;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            if (!snapshot(*scene->nodes()[previous_count + index], snapshots[index]))
            {
                (void)scene->destroy_node(root->id());
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
            const auto id = scene->nodes()[previous_count + index]->id();
            *identities_[index] = id;
            aliases_[id] = identities_[index];
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
        for (std::size_t index = 0; index < identities_.size(); ++index)
        {
            const auto* node = scene->find_node(*identities_[index]);
            schema::NodeDataT current;
            if (!node || !snapshot(*node, current) || current != snapshots_[index])
            {
                error = "Model nodes changed outside this edit; undo is stale";
                return false;
            }
            members.insert(node->id());
        }
        if (scene->active_camera() && members.contains(scene->active_camera()->node().id()))
        {
            error = "Model hierarchy contains the active camera";
            return false;
        }
        for (const auto& node : scene->nodes())
        {
            if (node->parent() && members.contains(node->parent()->id()) &&
                !members.contains(node->id()))
            {
                error = "Model has unrelated children; undo would remove them";
                return false;
            }
        }
        if (!scene->destroy_node(*identities_.front()))
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
};

bool versions(const ResourceCatalog& catalog, std::string_view root,
              std::vector<schema::AssetEntryT>& output)
{
    std::set<std::string> visited;
    std::vector<std::string> pending{std::string(root)};
    while (!pending.empty())
    {
        auto guid = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(guid).second)
        {
            continue;
        }
        const auto* entry = catalog.find(guid);
        if (!entry)
        {
            return false;
        }
        output.push_back(*entry);
        pending.insert(pending.end(), entry->dependencies.begin(), entry->dependencies.end());
    }
    return true;
}
} // namespace

bool prepare_model(WorldSystem& world, NodeAliases& aliases, const Json& arguments, Json& result,
                   std::unique_ptr<EditorEdit>& edit, std::string& error)
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
    auto* resources = ResourceSystem::instance();
    Guid guid;
    ResourcePtr model;
    std::vector<schema::AssetEntryT> retained;
    if (!resources || !ResourceSystem::path_to_guid(path, guid) ||
        !resources->loader().load(guid, model) || !model->model() ||
        !versions(resources->catalog(), guid, retained))
    {
        error = "Cannot resolve the model resource and its dependencies";
        return false;
    }
    result = {{"sceneId", arguments["sceneId"]}, {"name", name}, {"path", path}};
    edit = std::make_unique<ModelInstantiationEdit>(world, aliases, scene->id(), path, name,
                                                    std::move(retained));
    return true;
}
} // namespace engine::editor::components
