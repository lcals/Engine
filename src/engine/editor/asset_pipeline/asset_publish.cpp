#include "editor/asset_pipeline/asset_publish.hpp"

#include "editor/asset_pipeline/asset_io.hpp"
#include "runtime/resource/resource_system.hpp"

#include <string>
#include <system_error>
#include <toml++/toml.hpp>
#include <vector>

namespace engine::asset_pipeline
{
namespace
{

bool restore(const std::filesystem::path& cache, const toml::table& journal, std::string& error)
{
    const auto workspace = cache / ".asset-publish";
    std::error_code filesystem_error;
    for (const auto& name : {std::string("gen"), std::string("catalog.bin")})
    {
        const auto backup = workspace / ("previous-" + name);
        const auto current = cache / name;
        const bool had_original = journal["had_" + name].value_or(false);
        if (std::filesystem::exists(backup, filesystem_error))
        {
            std::filesystem::remove_all(current, filesystem_error);
            if (!filesystem_error)
            {
                std::filesystem::rename(backup, current, filesystem_error);
            }
        }
        else if (!filesystem_error && !had_original)
        {
            std::filesystem::remove_all(current, filesystem_error);
        }
        if (filesystem_error)
        {
            error = "cannot restore previous asset publication: " + filesystem_error.message();
            return false;
        }
    }
    std::filesystem::remove_all(workspace, filesystem_error);
    if (filesystem_error)
    {
        error = "cannot clear recovered asset publication: " + filesystem_error.message();
        return false;
    }
    return true;
}

} // namespace

bool recover_publish(const std::filesystem::path& cache, AssetDatabase& database,
                     std::string& error)
{
    const auto workspace = cache / ".asset-publish";
    std::error_code filesystem_error;
    if (!std::filesystem::exists(workspace, filesystem_error))
    {
        if (filesystem_error)
        {
            error = "cannot inspect asset publication: " + filesystem_error.message();
            return false;
        }
        return true;
    }
    const auto journal_path = workspace / "journal.toml";
    if (!std::filesystem::exists(journal_path, filesystem_error))
    {
        // 先生成完整产物，最后写 journal；无 journal 时从未交换过正式目录。
        if (!filesystem_error)
        {
            std::filesystem::remove_all(workspace, filesystem_error);
        }
        if (filesystem_error)
        {
            error = "cannot discard interrupted asset staging: " + filesystem_error.message();
            return false;
        }
        return true;
    }
    std::string journal_text;
    if (!ResourceSystem::read_text(journal_path, journal_text))
    {
        error = "cannot read asset publication recovery journal";
        return false;
    }
    auto parsed = toml::parse(journal_text);
    if (!parsed || !parsed["revision"].is_string() || !parsed["had_gen"].is_boolean() ||
        !parsed["had_catalog.bin"].is_boolean())
    {
        error = "invalid asset publication recovery journal; previous artifacts retained";
        return false;
    }
    std::string committed_revision;
    if (!database.revision(committed_revision, error))
    {
        return false;
    }
    if (committed_revision == parsed["revision"].value_or(std::string{}))
    {
        std::filesystem::remove_all(workspace, filesystem_error);
        if (filesystem_error)
        {
            error =
                "cannot finish committed asset publication cleanup: " + filesystem_error.message();
            return false;
        }
        return true;
    }
    return restore(cache, parsed.table(), error);
}

bool publish_assets(const std::filesystem::path& cache, AssetDatabase& database,
                    const std::vector<AssetRecord>& records, const std::string& revision,
                    std::string& error)
{
    const auto workspace = cache / ".asset-publish";
    std::error_code filesystem_error;
    const bool had_gen = std::filesystem::exists(cache / "gen", filesystem_error);
    if (filesystem_error)
    {
        error = "cannot inspect existing generated assets: " + filesystem_error.message();
        return false;
    }
    const bool had_catalog = std::filesystem::exists(cache / "catalog.bin", filesystem_error);
    if (filesystem_error)
    {
        error = "cannot inspect existing resource catalog: " + filesystem_error.message();
        return false;
    }
    const std::string journal_text =
        "revision = \"" + revision + "\"\nhad_gen = " + (had_gen ? "true" : "false") +
        "\n\"had_catalog.bin\" = " + (had_catalog ? "true" : "false") + "\n";
    // journal 临时文件先完整写入再改名，避免进程中断留下半截恢复记录。
    if (!write_text(workspace / "journal.pending", journal_text, error))
    {
        return false;
    }
    std::filesystem::rename(workspace / "journal.pending", workspace / "journal.toml",
                            filesystem_error);
    if (filesystem_error || !database.begin_update(records, revision, error))
    {
        if (filesystem_error)
        {
            error = "cannot prepare asset publication journal: " + filesystem_error.message();
        }
        database.rollback();
        return false;
    }
    bool success = true;
    for (const auto& name : {std::string("gen"), std::string("catalog.bin")})
    {
        const bool existed = name == "gen" ? had_gen : had_catalog;
        if (existed)
        {
            std::filesystem::rename(cache / name, workspace / ("previous-" + name),
                                    filesystem_error);
        }
        if (!filesystem_error)
        {
            std::filesystem::rename(workspace / ("staged-" + name), cache / name, filesystem_error);
        }
        if (filesystem_error)
        {
            error = "cannot publish generated assets: " + filesystem_error.message();
            success = false;
            break;
        }
    }
    if (success)
    {
        success = database.commit(error);
    }
    if (!success)
    {
        database.rollback();
        std::string recovery_error;
        if (!recover_publish(cache, database, recovery_error))
        {
            error += "; " + recovery_error;
        }
        return false;
    }
    std::filesystem::remove_all(workspace, filesystem_error);
    if (filesystem_error)
    {
        // SQL revision 已提交，正式文件也已替换；下次持锁后继续清理备份。
        return true;
    }
    return true;
}

} // namespace engine::asset_pipeline
