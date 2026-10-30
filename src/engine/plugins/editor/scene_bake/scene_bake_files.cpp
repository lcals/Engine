#include "plugins/editor/scene_bake/scene_bake_files.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "editor/asset_pipeline/project_lock.hpp"
#include "editor/asset_pipeline/scene_importer.hpp"
#include "runtime/core/guid_map.hpp"
#include "runtime/resource/resource_system.hpp"
#include "runtime/utils/path_utils.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <span>
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
    if (!logical.starts_with("project/"))
    {
        error = "Scene baking requires project/ source resource paths";
        return false;
    }
    return safe_path(root, fs::path("assets") / PathUtils::from_utf8(logical.substr(8)), output,
                     error);
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
    if (!ResourceSystem::read_binary(path, bytes))
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
        result.path = "project/" + PathUtils::to_utf8(path.lexically_relative(root_ / "assets"));
        if (files_.back().existed)
        {
            std::string text;
            if (!ResourceSystem::read_text(meta, text))
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
            if (!ResourceSystem::read_text(files_[index * 2 + 1].target, meta))
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
        maximum = std::max(maximum, node->info->id);
        for (const auto& component : node->components)
        {
            auto* candidate = component->payload.AsEnvironmentData();
            if (candidate != nullptr && component->enabled)
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
        node->info->id = maximum + 1;
        node->info->name = "Environment";
        auto transform = std::make_unique<schema::ComponentDataT>();
        transform->payload.Set(schema::TransformDataT{});
        node->components.push_back(std::move(transform));
        auto component = std::make_unique<schema::ComponentDataT>();
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

const std::array<BakeOutput, 3>& SceneBakeFiles::outputs() const noexcept
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
