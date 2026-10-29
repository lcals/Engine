#include "editor/asset_pipeline/asset_database.hpp"

#include "runtime/utils/path_utils.hpp"

#include <sqlite3.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace engine::asset_pipeline
{
namespace
{

class Statement final
{
public:
    Statement() noexcept = default;
    ~Statement() noexcept
    {
        sqlite3_finalize(statement_);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) = delete;
    Statement& operator=(Statement&&) = delete;

    [[nodiscard]] bool prepare(sqlite3* database, const char* sql)
    {
        if (statement_ != nullptr || database == nullptr || sql == nullptr)
        {
            return false;
        }
        return sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr) == SQLITE_OK;
    }

    [[nodiscard]] sqlite3_stmt* get() const noexcept
    {
        return statement_;
    }
    [[nodiscard]] bool bind(int index, const std::string& value)
    {
        return statement_ != nullptr &&
               value.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
               sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()),
                                 SQLITE_TRANSIENT) == SQLITE_OK;
    }
    [[nodiscard]] bool bind(int index, std::uint64_t value)
    {
        return statement_ != nullptr &&
               value <= static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()) &&
               sqlite3_bind_int64(statement_, index, static_cast<sqlite3_int64>(value)) ==
                   SQLITE_OK;
    }
    [[nodiscard]] std::string text(int index) const
    {
        const auto* value = sqlite3_column_text(statement_, index);
        return value != nullptr ? reinterpret_cast<const char*>(value) : std::string{};
    }

private:
    sqlite3_stmt* statement_ = nullptr;
};

} // namespace

AssetDatabase::~AssetDatabase() noexcept
{
    rollback();
    sqlite3_close(database_);
}

bool AssetDatabase::execute(const char* sql, std::string& error)
{
    char* diagnostics = nullptr;
    const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &diagnostics);
    if (result != SQLITE_OK)
    {
        error = "asset database: " +
                std::string(diagnostics != nullptr ? diagnostics : sqlite3_errmsg(database_));
    }
    sqlite3_free(diagnostics);
    return result == SQLITE_OK;
}

bool AssetDatabase::open(const std::filesystem::path& path, std::string& error)
{
    if (sqlite3_open_v2(PathUtils::to_utf8(path).c_str(), &database_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK)
    {
        error = "cannot open asset database: " + std::string(sqlite3_errmsg(database_));
        return false;
    }
    Statement version;
    if (!version.prepare(database_, "PRAGMA user_version;") ||
        sqlite3_step(version.get()) != SQLITE_ROW)
    {
        error = "cannot read asset database version";
        return false;
    }
    const int number = sqlite3_column_int(version.get(), 0);
    if (number != 0 && number != 1 && number != 2)
    {
        error = "unsupported asset database version; rebuild cache/assets.sqlite";
        return false;
    }
    return execute(
        "PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;"
        "CREATE TABLE IF NOT EXISTS assets("
        "guid TEXT PRIMARY KEY, path TEXT NOT NULL UNIQUE, kind INTEGER NOT NULL,"
        "source_hash TEXT NOT NULL, meta_hash TEXT NOT NULL, build_key TEXT NOT NULL,"
        "output_hash TEXT NOT NULL, output_size INTEGER NOT NULL, module_path TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS dependencies("
        "asset_guid TEXT NOT NULL REFERENCES assets(guid) ON DELETE CASCADE,"
        "dependency_guid TEXT NOT NULL, PRIMARY KEY(asset_guid,dependency_guid));"
        "CREATE TABLE IF NOT EXISTS runtime_dependencies("
        "asset_guid TEXT NOT NULL REFERENCES assets(guid) ON DELETE CASCADE,"
        "dependency_guid TEXT NOT NULL, PRIMARY KEY(asset_guid,dependency_guid));"
        "CREATE TABLE IF NOT EXISTS state(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "PRAGMA user_version=2;",
        error);
}

bool AssetDatabase::load(std::map<Guid, AssetRecord>& records, std::string& error)
{
    records.clear();
    Statement statement;
    if (!statement.prepare(database_,
                           "SELECT guid,path,kind,source_hash,meta_hash,build_key,output_hash,"
                           "output_size,module_path FROM assets ORDER BY path;"))
    {
        error = "cannot read asset database rows";
        return false;
    }
    std::map<Guid, AssetRecord> loaded;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW)
    {
        AssetRecord record;
        record.asset.guid = statement.text(0);
        record.asset.path = statement.text(1);
        record.asset.kind = static_cast<schema::AssetKind>(sqlite3_column_int(statement.get(), 2));
        record.source_hash = statement.text(3);
        record.meta_hash = statement.text(4);
        record.build_key = statement.text(5);
        record.output_hash = statement.text(6);
        record.output_size = static_cast<std::uint64_t>(sqlite3_column_int64(statement.get(), 7));
        record.module_path = statement.text(8);
        loaded.emplace(record.asset.guid, std::move(record));
    }
    if (result != SQLITE_DONE)
    {
        error = "cannot finish reading asset database rows";
        return false;
    }
    for (const bool runtime : {false, true})
    {
        Statement dependencies;
        const char* sql =
            runtime
                ? "SELECT asset_guid,dependency_guid FROM runtime_dependencies ORDER BY "
                  "dependency_guid;"
                : "SELECT asset_guid,dependency_guid FROM dependencies ORDER BY dependency_guid;";
        if (!dependencies.prepare(database_, sql))
        {
            error = "cannot read asset dependencies";
            return false;
        }
        while ((result = sqlite3_step(dependencies.get())) == SQLITE_ROW)
        {
            const auto iterator = loaded.find(dependencies.text(0));
            if (iterator == loaded.end())
            {
                error = "asset database contains an orphan dependency";
                return false;
            }
            auto& list = runtime ? iterator->second.asset.runtime_dependencies
                                 : iterator->second.asset.build_dependencies;
            list.push_back(dependencies.text(1));
        }
        if (result != SQLITE_DONE)
        {
            error = "cannot finish reading asset dependencies";
            return false;
        }
    }
    records = std::move(loaded);
    return true;
}

bool AssetDatabase::revision(std::string& output, std::string& error)
{
    output.clear();
    Statement statement;
    if (!statement.prepare(database_, "SELECT value FROM state WHERE key='revision';"))
    {
        error = "cannot read asset revision";
        return false;
    }
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_ROW)
    {
        output = statement.text(0);
        return true;
    }
    if (result != SQLITE_DONE)
    {
        error = "cannot finish reading asset revision";
        return false;
    }
    return true;
}

bool AssetDatabase::begin_update(const std::vector<AssetRecord>& records,
                                 const std::string& revision_value, std::string& error)
{
    if (transaction_ || !execute("BEGIN IMMEDIATE;", error))
    {
        return false;
    }
    transaction_ = true;
    if (!execute("DELETE FROM runtime_dependencies; DELETE FROM dependencies; DELETE FROM assets;",
                 error))
    {
        return false;
    }
    Statement insert;
    Statement dependency;
    Statement runtime_dependency;
    if (!insert.prepare(database_,
                        "INSERT INTO "
                        "assets(guid,path,kind,source_hash,meta_hash,build_key,output_hash,"
                        "output_size,module_path) VALUES(?,?,?,?,?,?,?,?,?);") ||
        !dependency.prepare(database_,
                            "INSERT INTO dependencies(asset_guid,dependency_guid) VALUES(?,?);") ||
        !runtime_dependency.prepare(
            database_, "INSERT INTO runtime_dependencies(asset_guid,dependency_guid) VALUES(?,?);"))
    {
        error = "cannot prepare asset database update: " + std::string(sqlite3_errmsg(database_));
        return false;
    }
    for (const auto& record : records)
    {
        if (!insert.bind(1, record.asset.guid) || !insert.bind(2, record.asset.path) ||
            !insert.bind(3, static_cast<std::uint64_t>(record.asset.kind)) ||
            !insert.bind(4, record.source_hash) || !insert.bind(5, record.meta_hash) ||
            !insert.bind(6, record.build_key) || !insert.bind(7, record.output_hash) ||
            !insert.bind(8, record.output_size) || !insert.bind(9, record.module_path) ||
            sqlite3_step(insert.get()) != SQLITE_DONE)
        {
            error = "cannot store asset database row: " + std::string(sqlite3_errmsg(database_));
            return false;
        }
        sqlite3_reset(insert.get());
        sqlite3_clear_bindings(insert.get());
        for (const bool runtime : {false, true})
        {
            auto& statement = runtime ? runtime_dependency : dependency;
            const auto& dependencies =
                runtime ? record.asset.runtime_dependencies : record.asset.build_dependencies;
            for (const auto& guid : dependencies)
            {
                if (!statement.bind(1, record.asset.guid) || !statement.bind(2, guid) ||
                    sqlite3_step(statement.get()) != SQLITE_DONE)
                {
                    error =
                        "cannot store asset dependency: " + std::string(sqlite3_errmsg(database_));
                    return false;
                }
                sqlite3_reset(statement.get());
                sqlite3_clear_bindings(statement.get());
            }
        }
    }
    Statement revision_statement;
    if (!revision_statement.prepare(database_,
                                    "INSERT INTO state(key,value) VALUES('revision',?) ON "
                                    "CONFLICT(key) DO UPDATE SET value=excluded.value;") ||
        !revision_statement.bind(1, revision_value) ||
        sqlite3_step(revision_statement.get()) != SQLITE_DONE)
    {
        error = "cannot store asset revision";
        return false;
    }
    return true;
}

bool AssetDatabase::commit(std::string& error)
{
    if (!transaction_ || !execute("COMMIT;", error))
    {
        return false;
    }
    transaction_ = false;
    return true;
}

void AssetDatabase::rollback() noexcept
{
    if (transaction_)
    {
        sqlite3_exec(database_, "ROLLBACK;", nullptr, nullptr, nullptr);
        transaction_ = false;
    }
}

} // namespace engine::asset_pipeline
