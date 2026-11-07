#include "plugins/editor/scene_bake/scene_bake_files.hpp"
#include "editor/file_text.hpp"
#include "runtime/resource/virtual_file_system.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/importers/mesh_importer.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "plugins/editor/scene_bake/lighting_bake_job.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <flatbuffers/flatbuffers.h>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <sstream>
#include <system_error>

namespace engine::plugins::scene_bake
{
namespace
{
namespace fs = std::filesystem;
namespace io = editor::asset_pipeline;

bool safe_path(const fs::path& root, const fs::path& relative, fs::path& output, std::string& error)
{
    if (relative.empty() || relative.is_absolute() || relative.has_root_name())
    {
        error = "Bake path must stay inside the project";
        return false;
    }
    output = root;
    for (const auto& part : relative)
    {
        const auto text = PathUtils::to_utf8(part);
        if (text.empty() || text == "." || text == ".." ||
            text.find_first_of("\\:\0", 0, 3) != std::string::npos)
        {
            error = "Bake path contains a forbidden component";
            return false;
        }
        output /= part;
        std::error_code code;
        const auto status = fs::symlink_status(output, code);
        if (code == std::errc::no_such_file_or_directory)
        {
            code.clear();
        }
        if (code || fs::is_symlink(status))
        {
            error = "Bake path contains a symlink or inaccessible component";
            return false;
        }
    }
    return true;
}

bool asset_path(const fs::path& root, std::string_view logical, fs::path& output,
                std::string& error)
{
    if (!GuidMap::valid_path(logical) || logical.find(':') != logical.npos)
    {
        error = "Scene baking requires assets-relative source resource paths";
        return false;
    }
    return safe_path(root, fs::path("assets") / PathUtils::from_utf8(logical), output, error);
}

bool fingerprint(const fs::path& path, bool& exists, std::string& hash, std::string& error)
{
    std::error_code code;
    const auto status = fs::symlink_status(path, code);
    exists = !code && fs::exists(status);
    if (code == std::errc::no_such_file_or_directory || (!code && !exists))
    {
        hash.clear();
        return true;
    }
    if (code || fs::is_symlink(status) || !fs::is_regular_file(status))
    {
        error = "Bake source or output is not an accessible regular file";
        return false;
    }
    ByteBuffer bytes;
    if (!VirtualFileSystem::read_file(path, bytes))
    {
        error = "Cannot read a bake source or existing output";
        return false;
    }
    hash = io::hash_bytes(bytes);
    return true;
}

bool same_file(const fs::path& path, bool existed, const std::string& expected, std::string& error)
{
    bool exists = false;
    std::string current;
    if (!fingerprint(path, exists, current, error))
    {
        return false;
    }
    if (exists != existed || current != expected)
    {
        error = "Bake source or output changed during the job: " + PathUtils::to_utf8(path);
        return false;
    }
    return true;
}
} // namespace

bool SceneBakeFiles::initialize(const fs::path& root, std::string_view scene,
                                std::string_view input, std::string_view job,
                                ResourceSystem& resources, std::string& error)
{
    std::error_code code;
    root_ = fs::canonical(root, code);
    if (code || !asset_path(root_, scene, scene_, error) || scene_.extension() != ".scene" ||
        !asset_path(root_, input, input_, error) ||
        !safe_path(root_, fs::path("cache/bake") / PathUtils::from_utf8(job), staging_, error))
    {
        if (error.empty())
        {
            error = "Cannot resolve project scene or bake staging directory";
        }
        return false;
    }
    bool input_exists = false;
    auto input_meta = input_;
    input_meta += ".meta";
    if (!fingerprint(input_, input_exists, input_fingerprint_, error) || !input_exists ||
        !fingerprint(input_meta, input_exists, input_meta_fingerprint_, error) || !input_exists ||
        !resources.catalog().identities().path_to_guid(input, input_guid_))
    {
        if (error.empty())
        {
            error = "IBL input source and meta must exist and be imported";
        }
        return false;
    }
    const auto* entry = resources.catalog().find(input_guid_);
    if (entry == nullptr || entry->kind != schema::AssetKind::texture)
    {
        error = "The IBL input must be an imported texture";
        return false;
    }
    input_content_hash_ = entry->content_hash;
    fs::create_directories(staging_, code);
    if (code)
    {
        error = "Cannot create the bake staging directory: " + code.message();
        return false;
    }
    outputs_.resize(3);
    const auto output_directory = scene_.parent_path() / scene_.stem();
    const std::array names{"ibl-irradiance.ktx", "ibl-prefilter.ktx", "ibl-lut.ktx"};
    for (std::size_t index = 0; index < outputs_.size(); ++index)
    {
        const auto path = output_directory / names[index];
        const auto relative = path.lexically_relative(root_);
        fs::path checked;
        if (!safe_path(root_, relative, checked, error))
        {
            return false;
        }
        auto meta = path;
        meta += ".meta";
        for (const auto& destination : {path, meta})
        {
            File file;
            file.target = destination;
            file.staged = staging_ / (std::to_string(files_.size()) + ".new");
            file.backup = staging_ / (std::to_string(files_.size()) + ".old");
            if (!fingerprint(destination, file.existed, file.fingerprint, error))
            {
                return false;
            }
            files_.push_back(std::move(file));
        }
        auto& result = outputs_[index];
        result.path = PathUtils::to_utf8(path.lexically_relative(root_ / "assets"));
        if (files_.back().existed)
        {
            std::string text;
            if (!engine::editor::file_detail::read_text(meta, text))
            {
                error = "Cannot read existing IBL metadata";
                return false;
            }
            const auto parsed = toml::parse(text);
            if (!parsed || parsed["importer"].value_or(std::string{}) != "texture_ktx")
            {
                error = "Existing IBL metadata must use the texture_ktx importer";
                return false;
            }
            result.guid = parsed["guid"].value_or(std::string{});
            if (!GuidMap::valid_guid(result.guid))
            {
                error = "Existing IBL metadata has an invalid GUID";
                return false;
            }
        }
        else
        {
            Guid known;
            if (files_[files_.size() - 2].existed ||
                resources.catalog().identities().path_to_guid(result.path, known))
            {
                error = "Existing IBL resource lost its meta; restore its identity before baking";
                return false;
            }
            result.guid = io::new_guid();
        }
        std::string existing;
        if (resources.catalog().identities().guid_to_path(result.guid, existing) &&
            existing != result.path)
        {
            error = "IBL output GUID belongs to another resource";
            return false;
        }
    }
    File scene_file;
    scene_file.target = scene_;
    scene_file.staged = staging_ / "scene.new";
    scene_file.backup = staging_ / "scene.old";
    if (!fingerprint(scene_, scene_file.existed, scene_file.fingerprint, error) ||
        !scene_file.existed)
    {
        error = "The requested scene must already be saved on disk";
        return false;
    }
    files_.push_back(std::move(scene_file));
    return true;
}

bool SceneBakeFiles::unchanged(ResourceSystem& resources, std::string& error) const
{
    if (!catalog_revision_.empty())
    {
        if (resources.catalog().revision() != catalog_revision_)
        {
            error = "The imported resource version changed during lighting bake";
            return false;
        }
        for (const auto& source : sources_)
        {
            if (!same_file(source.target, source.existed, source.fingerprint, error))
            {
                return false;
            }
        }
    }
    else
    {
        const auto* entry = resources.catalog().find(input_guid_);
        if (entry == nullptr || entry->content_hash != input_content_hash_)
        {
            error = "The imported IBL input changed during baking";
            return false;
        }
        auto input_meta = input_;
        input_meta += ".meta";
        if (!same_file(input_, true, input_fingerprint_, error) ||
            !same_file(input_meta, true, input_meta_fingerprint_, error))
        {
            return false;
        }
    }
    for (const auto& file : files_)
    {
        fs::path checked;
        if (!safe_path(root_, file.target.lexically_relative(root_), checked, error) ||
            !same_file(file.target, file.existed, file.fingerprint, error))
        {
            return false;
        }
    }
    return true;
}

bool SceneBakeFiles::stage(const std::array<std::vector<std::uint8_t>, 3>& images,
                           std::string& error)
{
    for (std::size_t index = 0; index < outputs_.size(); ++index)
    {
        if (images[index].empty() ||
            !io::write_bytes(files_[index * 2].staged, std::as_bytes(std::span(images[index])),
                             error))
        {
            return false;
        }
        std::string meta;
        if (files_[index * 2 + 1].existed)
        {
            if (!engine::editor::file_detail::read_text(files_[index * 2 + 1].target, meta))
            {
                error = "Cannot preserve the existing IBL metadata";
                return false;
            }
        }
        else
        {
            meta =
                "guid = \"" + outputs_[index].guid +
                "\"\nimporter = \"texture_ktx\"\nmeta_version = 1\n[texture]\nformat = \"auto\"\n"
                "compression = \"none\"\nsrgb = false\n";
        }
        if (!io::write_text(files_[index * 2 + 1].staged, meta, error))
        {
            return false;
        }
    }
    schema::SceneDataT scene;
    if (!editor::SceneImporter::read(scene_, scene, error))
    {
        return false;
    }
    schema::EnvironmentDataT* environment = nullptr;
    schema::EnvironmentDataT* disabled_environment = nullptr;
    std::size_t disabled_count = 0;
    std::uint64_t maximum = 0;
    for (const auto& node : scene.nodes)
    {
        maximum = std::max(maximum, node->info->properties->id);
        for (const auto& component : node->components)
        {
            auto* candidate = component->payload.AsEnvironmentData();
            if (candidate != nullptr && component->flags->enabled)
            {
                if (environment != nullptr)
                {
                    error = "The scene must not have multiple enabled environment components";
                    return false;
                }
                environment = candidate;
            }
            else if (candidate != nullptr)
            {
                disabled_environment = candidate;
                ++disabled_count;
            }
        }
    }
    if (environment == nullptr && disabled_count > 1)
    {
        error = "Multiple disabled environment components make the bake target ambiguous";
        return false;
    }
    if (environment == nullptr && disabled_count == 1)
    {
        environment = disabled_environment;
    }
    if (environment == nullptr)
    {
        if (maximum == std::numeric_limits<std::uint64_t>::max())
        {
            error = "Cannot allocate an environment node identity";
            return false;
        }
        auto node = std::make_unique<schema::NodeDataT>();
        node->info = std::make_unique<schema::NodeInfoT>();
        node->info->properties = std::make_unique<schema::NodePropertiesT>();
        node->info->properties->id = maximum + 1;
        node->info->properties->name = "Environment";
        auto transform = std::make_unique<schema::ComponentDataT>();
        transform->flags = std::make_unique<schema::ComponentFlagsT>();
        transform->payload.Set(schema::TransformDataT{});
        node->components.push_back(std::move(transform));
        auto component = std::make_unique<schema::ComponentDataT>();
        component->flags = std::make_unique<schema::ComponentFlagsT>();
        component->payload.Set(schema::EnvironmentDataT{});
        environment = component->payload.AsEnvironmentData();
        node->components.push_back(std::move(component));
        scene.nodes.push_back(std::move(node));
    }
    environment->ibl_irradiance_guid = outputs_[0].guid;
    environment->ibl_prefilter_guid = outputs_[1].guid;
    environment->ibl_lut_guid = outputs_[2].guid;
    // ibl_enabled 和强度保持场景原值，不把完成烘焙等同于开启效果。
    return editor::SceneImporter::write(files_.back().staged, scene, error);
}

bool SceneBakeFiles::add_output(std::string_view filename, std::string_view importer,
                                ResourceSystem& resources, std::string& error)
{
    const auto path = scene_.parent_path() / scene_.stem() / PathUtils::from_utf8(filename);
    fs::path checked;
    if (!safe_path(root_, path.lexically_relative(root_), checked, error))
    {
        return false;
    }
    auto meta = path;
    meta += ".meta";
    for (const auto& destination : {path, meta})
    {
        File file;
        file.target = destination;
        if (destination == meta)
        {
            // Staged source assets obey the same adjacent-meta contract as published
            // assets. The real importer must read the exact metadata being published.
            file.staged = files_.back().staged;
            file.staged += ".meta";
        }
        else
        {
            file.staged = staging_ / (std::to_string(files_.size()) + ".new");
        }
        file.backup = staging_ / (std::to_string(files_.size()) + ".old");
        if (!fingerprint(destination, file.existed, file.fingerprint, error))
        {
            return false;
        }
        files_.push_back(std::move(file));
    }
    BakeOutput output;
    output.path = PathUtils::to_utf8(path.lexically_relative(root_ / "assets"));
    if (files_.back().existed)
    {
        std::string text;
        if (!engine::editor::file_detail::read_text(meta, text))
        {
            error = "Cannot read bake metadata";
            return false;
        }
        const auto parsed = toml::parse(text);
        if (!parsed || parsed["importer"].value_or(std::string{}) != importer)
        {
            error = "Existing bake metadata has a different importer";
            return false;
        }
        output.guid = parsed["guid"].value_or(std::string{});
        if (!GuidMap::valid_guid(output.guid))
        {
            error = "Invalid bake output GUID";
            return false;
        }
    }
    else
    {
        Guid previous;
        if (files_[files_.size() - 2].existed ||
            resources.catalog().identities().path_to_guid(output.path, previous))
        {
            error = "Existing bake output lost its metadata";
            return false;
        }
        output.guid = io::new_guid();
    }
    std::string existing;
    if (resources.catalog().identities().guid_to_path(output.guid, existing) &&
        existing != output.path)
    {
        error = "Bake output GUID belongs to another resource";
        return false;
    }
    outputs_.push_back(std::move(output));
    return true;
}

bool SceneBakeFiles::initialize_lighting(const fs::path& root, std::string_view scene,
                                         std::string_view job, ResourceSystem& resources,
                                         LightingBakeSettings& settings, std::string& error)
{
    std::error_code code;
    root_ = fs::canonical(root, code);
    if (code || !asset_path(root_, scene, scene_, error) || scene_.extension() != ".scene" ||
        !safe_path(root_, fs::path("cache/bake") / PathUtils::from_utf8(job), staging_, error))
    {
        if (error.empty())
        {
            error = "Cannot resolve lighting bake paths";
        }
        return false;
    }
    schema::SceneDataT data;
    if (!editor::SceneImporter::read(scene_, data, error))
    {
        return false;
    }
    fs::create_directories(staging_, code);
    if (code)
    {
        error = "Cannot create lighting bake staging directory";
        return false;
    }
    catalog_revision_ = resources.catalog().revision();
    for (const auto& node : data.nodes)
    {
        for (const auto& component : node->components)
        {
            if (const auto* mesh = component->payload.AsMeshRendererData();
                component->flags->enabled && mesh && mesh->bake_static)
            {
                const auto stem = "lighting-" + std::to_string(node->info->properties->id);
                if (!add_output(stem + ".mesh", "mesh", resources, error) ||
                    !add_output(stem + ".ktx", "texture_ktx", resources, error))
                {
                    return false;
                }
            }
        }
    }
    for (const auto& probe : settings.probes)
    {
        if (!add_output("probes-" + std::to_string(probe.node_id) + ".lightprobe", "light_probe",
                        resources, error))
        {
            return false;
        }
    }
    File file;
    file.target = scene_;
    file.staged = staging_ / "scene.new";
    file.backup = staging_ / "scene.old";
    if (!fingerprint(scene_, file.existed, file.fingerprint, error) || !file.existed)
    {
        error = "Lighting bake requires a saved scene";
        return false;
    }
    settings.source_fingerprint = file.fingerprint + ":" + catalog_revision_;
    files_.push_back(std::move(file));
    // Capture source files as well as imported hashes; pending unsaved imports cannot publish stale
    // lighting.
    std::set<std::string> visited;
    std::vector<std::string> pending;
    Guid scene_guid;
    if (!resources.catalog().identities().path_to_guid(scene, scene_guid))
    {
        error = "Lighting bake scene is not imported";
        return false;
    }
    pending.push_back(scene_guid);
    while (!pending.empty())
    {
        const auto guid = pending.back();
        pending.pop_back();
        if (!visited.insert(guid).second)
        {
            continue;
        }
        const auto* entry = resources.catalog().find(guid);
        if (!entry)
        {
            error = "Lighting bake dependency is missing";
            return false;
        }
        pending.insert(pending.end(), entry->dependencies.begin(), entry->dependencies.end());
        if (entry->path.starts_with("builtin://"))
        {
            continue;
        }
        fs::path source = root_ / "assets" / PathUtils::from_utf8(entry->path);
        // Virtual model subresources share the nearest source file and its metadata.
        for (auto parent = source; parent != root_ / "assets"; parent = parent.parent_path())
        {
            const auto status = fs::symlink_status(parent, code);
            if (!code && fs::is_regular_file(status))
            {
                source = parent;
                break;
            }
            code.clear();
        }
        fs::path checked;
        if (!safe_path(root_, source.lexically_relative(root_), checked, error))
        {
            return false;
        }
        auto meta = source;
        meta += ".meta";
        for (const auto& path : {source, meta})
        {
            File snapshot;
            snapshot.target = path;
            if (!fingerprint(path, snapshot.existed, snapshot.fingerprint, error))
            {
                return false;
            }
            sources_.push_back(std::move(snapshot));
        }
    }
    return true;
}

bool SceneBakeFiles::stage_meta(std::size_t index, std::string_view importer, std::string& error)
{
    const auto& file = files_[index * 2 + 1];
    std::string text;
    if (file.existed)
    {
        if (!engine::editor::file_detail::read_text(file.target, text))
        {
            error = "Cannot preserve bake metadata";
            return false;
        }
    }
    else
    {
        text = "guid = \"" + outputs_[index].guid + "\"\nimporter = \"" + std::string(importer) +
               "\"\nmeta_version = 1\n";
        if (importer == "texture_ktx")
        {
            text += "[texture]\nformat = \"auto\"\ncompression = \"none\"\nsrgb = false\n";
        }
    }
    return io::write_text(file.staged, text, error);
}

bool SceneBakeFiles::stage_lighting(const LightingBakeOutputs& output, std::string& error)
{
    if (outputs_.size() != output.meshes.size() * 2 + output.probes.size())
    {
        error = "Lighting bake output identities changed";
        return false;
    }
    schema::SceneDataT scene;
    if (!editor::SceneImporter::read(scene_, scene, error))
    {
        return false;
    }
    std::size_t index = 0;
    for (const auto& item : output.meshes)
    {
        schema::MeshRendererDataT* renderer = nullptr;
        for (const auto& node : scene.nodes)
        {
            if (node->info->properties->id == item.node_id)
            {
                for (const auto& c : node->components)
                {
                    if (auto* r = c->payload.AsMeshRendererData())
                    {
                        renderer = r;
                    }
                }
            }
        }
        if (!renderer || !item.mesh || !item.lightmap)
        {
            error = "Lighting bake output has no matching mesh renderer";
            return false;
        }
        const auto& mesh = *item.mesh;
        toml::table table;
        toml::array vertices, indices, uv, uv2, tangents, colors, submeshes;
        for (const auto& v : mesh.vertices)
        {
            vertices.push_back(toml::array{double(v.x()), double(v.y()), double(v.z()),
                                           double(v.nx()), double(v.ny()), double(v.nz())});
        }
        for (auto i : mesh.indices)
        {
            indices.push_back(std::int64_t(i));
        }
        for (const auto& v : mesh.texcoords)
        {
            uv.push_back(toml::array{double(v.x()), double(v.y())});
        }
        for (const auto& v : mesh.lightmap_texcoords)
        {
            uv2.push_back(toml::array{double(v.x()), double(v.y())});
        }
        for (const auto& v : mesh.tangents)
        {
            tangents.push_back(
                toml::array{double(v.x()), double(v.y()), double(v.z()), double(v.w())});
        }
        for (const auto& v : mesh.colors)
        {
            colors.push_back(
                toml::array{double(v.r()), double(v.g()), double(v.b()), double(v.a())});
        }
        for (const auto& s : mesh.submeshes)
        {
            submeshes.push_back(toml::table{{"first_index", std::int64_t(s.first_index())},
                                            {"index_count", std::int64_t(s.index_count())},
                                            {"material_slot", std::int64_t(s.material_slot())}});
        }
        table.insert("vertices", std::move(vertices));
        table.insert("indices", std::move(indices));
        table.insert("lightmap_texcoords", std::move(uv2));
        if (!uv.empty())
        {
            table.insert("texcoords", std::move(uv));
        }
        if (!tangents.empty())
        {
            table.insert("tangents", std::move(tangents));
        }
        if (!colors.empty())
        {
            table.insert("colors", std::move(colors));
        }
        if (!submeshes.empty())
        {
            table.insert("submeshes", std::move(submeshes));
        }
        std::ostringstream text;
        text << table;
        if (!io::write_text(files_[index * 2].staged, text.str(), error) ||
            !stage_meta(index, "mesh", error))
        {
            return false;
        }
        // Use the real importer to record the exact compiled entity hash, including its envelope.
        GuidMap identities;
        std::map<Guid, schema::AssetKind> kinds;
        io::ImportContext context{
            identities, kinds, root_, root_ / "assets", staging_ / "compiled", {}, nullptr, {}};
        std::error_code code;
        fs::create_directories(context.output, code);
        io::ImportAsset asset;
        asset.guid = outputs_[index].guid;
        asset.path = outputs_[index].path;
        asset.source = files_[index * 2].staged;
        asset.kind = schema::AssetKind::mesh;
        io::MeshImporter importer;
        ByteBuffer payload, entity;
        std::uint32_t header_size = 0;
        if (code || !importer.convert(asset, context, error) ||
            !VirtualFileSystem::read_file(context.output / asset.guid, payload) ||
            !ResourceSystem::encode_resource(asset.guid, asset.kind, payload, entity, header_size))
        {
            if (error.empty())
            {
                error = "Cannot compile generated UV2 mesh";
            }
            return false;
        }
        renderer->mesh_guid = asset.guid;
        renderer->lightmap_mesh_hash = io::hash_bytes(entity);
        renderer->lightmap_transform.assign(item.transform.begin(), item.transform.end());
        ++index;
        renderer->lightmap_guid = outputs_[index].guid;
        if (!io::write_bytes(files_[index * 2].staged,
                             std::as_bytes(std::span(item.lightmap->data)), error) ||
            !stage_meta(index, "texture_ktx", error))
        {
            return false;
        }
        ++index;
    }
    for (const auto& item : output.probes)
    {
        schema::LightProbeVolumeDataT* component = nullptr;
        for (const auto& node : scene.nodes)
        {
            if (node->info->properties->id == item.node_id)
            {
                for (const auto& c : node->components)
                {
                    if (auto* p = c->payload.AsLightProbeVolumeData())
                    {
                        component = p;
                    }
                }
            }
        }
        if (!component)
        {
            error = "Probe bake target has no LightProbeVolume component";
            return false;
        }
        auto probes = item.data;
        probes.path = outputs_[index].path;
        flatbuffers::FlatBufferBuilder builder;
        schema::FinishLightProbeDataBuffer(builder, schema::LightProbeData::Pack(builder, &probes));
        if (!io::write_bytes(
                files_[index * 2].staged,
                std::as_bytes(std::span(builder.GetBufferPointer(), builder.GetSize())), error) ||
            !stage_meta(index, "light_probe", error))
        {
            return false;
        }
        component->probes_guid = outputs_[index].guid;
        ++index;
    }
    return editor::SceneImporter::write(files_.back().staged, scene, error);
}

bool SceneBakeFiles::rollback(std::string& error)
{
    bool success = true;
    for (auto item = files_.rbegin(); item != files_.rend(); ++item)
    {
        std::error_code code;
        if (item->installed)
        {
            fs::remove(item->target, code);
            success = !code && success;
            if (!code)
            {
                item->installed = false;
            }
        }
        if (item->moved)
        {
            code.clear();
            fs::rename(item->backup, item->target, code);
            success = !code && success;
            if (!code)
            {
                item->moved = false;
            }
        }
    }
    if (!success)
    {
        error += "; cannot restore all previous bake files; backups remain in " +
                 PathUtils::to_utf8(staging_);
    }
    return success;
}

bool SceneBakeFiles::publish(const std::function<bool(std::string&)>& refresh, std::string& error)
{
    {
        io::ProjectLock lock;
        bool acquired = false;
        if (!lock.try_acquire(root_ / "cache/.asset-build.lock", acquired, error) || !acquired)
        {
            if (error.empty())
            {
                error = "The project resource pipeline is busy; bake publication was not applied";
            }
            return false;
        }
        for (auto& file : files_)
        {
            fs::path checked;
            if (!safe_path(root_, file.target.lexically_relative(root_), checked, error) ||
                !same_file(file.target, file.existed, file.fingerprint, error))
            {
                // rollback 同时把恢复失败的具体位置追加到 error，备份仍保留。
                (void)rollback(error);
                return false;
            }
            std::error_code code;
            fs::create_directories(file.target.parent_path(), code);
            if (!code && file.existed)
            {
                fs::rename(file.target, file.backup, code);
                file.moved = !code;
            }
            if (!code)
            {
                fs::rename(file.staged, file.target, code);
                file.installed = !code;
            }
            if (code)
            {
                error = "Cannot publish the scene bake files: " + code.message();
                // rollback 同时把恢复失败的具体位置追加到 error，备份仍保留。
                (void)rollback(error);
                return false;
            }
        }
    }
    if (!refresh(error))
    {
        bool restored = false;
        {
            io::ProjectLock lock;
            bool acquired = false;
            std::string lock_error;
            if (!lock.try_acquire(root_ / "cache/.asset-build.lock", acquired, lock_error) ||
                !acquired)
            {
                error += "; cannot acquire the project lock for bake rollback: " + lock_error;
                return false;
            }
            restored = rollback(error);
        }
        if (restored)
        {
            std::string refresh_error;
            if (!refresh(refresh_error))
            {
                error += "; previous source files restored but resource refresh failed: " +
                         refresh_error;
            }
            else
            {
                error += "; previous source files and resource version restored";
            }
        }
        return false;
    }
    for (auto& file : files_)
    {
        file.moved = false;
        file.installed = false;
    }
    cleanup();
    return true;
}

const std::vector<BakeOutput>& SceneBakeFiles::outputs() const noexcept
{
    return outputs_;
}
void SceneBakeFiles::cleanup() noexcept
{
    // 回滚失败时保留备份，不让后续清理丢失用户源文件。
    if (std::any_of(files_.begin(), files_.end(),
                    [](const auto& file) { return file.moved || file.installed; }))
    {
        return;
    }
    std::error_code code;
    if (!staging_.empty())
    {
        fs::remove_all(staging_, code);
    }
}
} // namespace engine::plugins::scene_bake
