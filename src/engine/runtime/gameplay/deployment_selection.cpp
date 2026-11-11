#include "runtime/gameplay/deployment_selection.hpp"
#include "runtime/gameplay/deployment_internal.hpp"

namespace engine::gameplay_detail
{
bool select_published_deployment(const std::filesystem::path& persistent_root,
                                 std::string_view target_platform, ResourceCatalog& catalog,
                                 MountList& mounts, std::string& revision, std::string& entry,
                                 std::string& error)
{
    catalog = {};
    mounts.clear();
    entry.clear();
    revision.clear();
    error.clear();
    DeploymentRead deployment;
    deployment.target_platform = target_platform;
    if (!deployment.read(persistent_root))
    {
        error = std::move(deployment.error);
        return false;
    }
    if (deployment.available)
    {
        catalog = std::move(deployment.available->catalog);
        mounts = std::move(deployment.available->mounts);
        entry = std::move(deployment.available->entry);
        revision = std::move(deployment.available->revision);
    }
    return true;
}
} // namespace engine::gameplay_detail
