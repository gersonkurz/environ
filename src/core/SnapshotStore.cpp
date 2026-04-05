#include "SnapshotStore.h"

#include <sqlite3.h>
#include <pnq/sqlite/sqlite.h>
#include <pnq/unicode.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <format>

#include <windows.h>
#include <shlobj.h>

namespace Environ::core {

namespace {

constexpr int kSchemaVersion{1};
constexpr int kMaxSnapshots{500};

std::filesystem::path database_path() {
    wchar_t* appdata{nullptr};
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appdata) != S_OK) {
        CoTaskMemFree(appdata);
        return {};
    }
    std::filesystem::path dir{appdata};
    CoTaskMemFree(appdata);
    dir /= L"environ";
    std::filesystem::create_directories(dir);
    return dir / L"environ.db";
}

std::string utc_timestamp() {
    const auto now{std::chrono::system_clock::now()};
    return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(now));
}

} // namespace

struct SnapshotStore::Impl {
    pnq::sqlite::Database db;
};

SnapshotStore::SnapshotStore() = default;
SnapshotStore::~SnapshotStore() = default;
SnapshotStore::SnapshotStore(SnapshotStore&&) noexcept = default;
SnapshotStore& SnapshotStore::operator=(SnapshotStore&&) noexcept = default;

bool SnapshotStore::open() {
    m_impl = std::make_unique<Impl>();

    auto path{database_path()};
    if (path.empty()) {
        spdlog::error("Failed to resolve database path");
        return false;
    }

    auto path_utf8{path.string()};
    if (!m_impl->db.open(path_utf8)) {
        spdlog::error("Failed to open database: {}", m_impl->db.last_error());
        return false;
    }

    m_impl->db.execute("PRAGMA journal_mode=WAL;");
    m_impl->db.execute("PRAGMA foreign_keys=ON;");

    if (!ensure_schema()) {
        spdlog::error("Failed to initialize database schema");
        return false;
    }

    prune_older_than(90);
    return true;
}

bool SnapshotStore::ensure_schema() {
    // Check schema version
    pnq::sqlite::Statement version_stmt{m_impl->db, "PRAGMA user_version;"};
    int current_version{0};
    if (version_stmt.execute()) {
        current_version = version_stmt.get_int32(0);
    }

    if (current_version >= kSchemaVersion) {
        return true;
    }

    // Create tables for version 1
    bool ok{true};
    ok = ok && m_impl->db.execute(
        "CREATE TABLE IF NOT EXISTS snapshots ("
        "  id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp   TEXT NOT NULL,"
        "  label       TEXT NOT NULL,"
        "  scope_mask  INTEGER NOT NULL"
        ");");

    ok = ok && m_impl->db.execute(
        "CREATE TABLE IF NOT EXISTS snapshot_variables ("
        "  snapshot_id   INTEGER NOT NULL REFERENCES snapshots(id) ON DELETE CASCADE,"
        "  scope         INTEGER NOT NULL,"
        "  name          TEXT NOT NULL,"
        "  value         TEXT NOT NULL,"
        "  is_expandable INTEGER NOT NULL,"
        "  PRIMARY KEY (snapshot_id, scope, name)"
        ");");

    if (ok) {
        ok = m_impl->db.execute(
            std::format("PRAGMA user_version = {};", kSchemaVersion));
    }

    return ok;
}

int64_t SnapshotStore::create_snapshot(
    std::string_view label,
    std::vector<EnvVariable> const& user_vars,
    std::vector<EnvVariable> const& machine_vars) {

    if (!m_impl) return -1;

    int scope_mask{0};
    if (!user_vars.empty()) scope_mask |= 1;
    if (!machine_vars.empty()) scope_mask |= 2;

    pnq::sqlite::Transaction tx{m_impl->db};
    if (!tx) return -1;

    auto ts{utc_timestamp()};

    pnq::sqlite::Statement insert_snap{m_impl->db,
        "INSERT INTO snapshots (timestamp, label, scope_mask) VALUES (?, ?, ?);"};
    insert_snap.bind(std::string_view{ts});
    insert_snap.bind(label);
    insert_snap.bind(static_cast<int32_t>(scope_mask));
    if (!insert_snap.execute()) return -1;

    auto snapshot_id{m_impl->db.last_insert_rowid()};

    pnq::sqlite::Statement insert_var{m_impl->db,
        "INSERT INTO snapshot_variables (snapshot_id, scope, name, value, is_expandable) "
        "VALUES (?, ?, ?, ?, ?);"};

    auto insert_vars = [&](Scope scope, std::vector<EnvVariable> const& vars) {
        for (const auto& var : vars) {
            insert_var.reset();
            insert_var.bind(snapshot_id);
            insert_var.bind(static_cast<int32_t>(scope == Scope::User ? 0 : 1));
            insert_var.bind(pnq::unicode::to_utf8(var.name));
            insert_var.bind(pnq::unicode::to_utf8(var.value));
            insert_var.bind(static_cast<int32_t>(var.is_expandable ? 1 : 0));
            if (!insert_var.execute()) return false;
        }
        return true;
    };

    if (!insert_vars(Scope::User, user_vars)) return -1;
    if (!insert_vars(Scope::Machine, machine_vars)) return -1;

    // Enforce max snapshot count
    pnq::sqlite::Statement count_stmt{m_impl->db, "SELECT COUNT(*) FROM snapshots;"};
    if (count_stmt.execute() && count_stmt.get_int64(0) > kMaxSnapshots) {
        m_impl->db.execute(std::format(
            "DELETE FROM snapshots WHERE id IN "
            "(SELECT id FROM snapshots ORDER BY timestamp ASC LIMIT {});",
            count_stmt.get_int64(0) - kMaxSnapshots));
    }

    tx.commit();
    spdlog::info("Created snapshot {} ({})", snapshot_id, label);
    return snapshot_id;
}

std::vector<SnapshotInfo> SnapshotStore::list_snapshots() {
    std::vector<SnapshotInfo> result;
    if (!m_impl) return result;

    pnq::sqlite::Statement stmt{m_impl->db,
        "SELECT id, timestamp, label, scope_mask FROM snapshots ORDER BY timestamp DESC;"};

    if (stmt.execute()) {
        do {
            result.push_back(SnapshotInfo{
                .id{stmt.get_int64(0)},
                .timestamp{stmt.get_text(1)},
                .label{stmt.get_text(2)},
                .scope_mask{stmt.get_int32(3)},
            });
        } while (stmt.next());
    }

    return result;
}

std::vector<SnapshotVariable> SnapshotStore::load_snapshot(int64_t snapshot_id) {
    std::vector<SnapshotVariable> result;
    if (!m_impl) return result;

    pnq::sqlite::Statement stmt{m_impl->db,
        "SELECT scope, name, value, is_expandable "
        "FROM snapshot_variables WHERE snapshot_id = ?;"};
    stmt.bind(snapshot_id);

    if (stmt.execute()) {
        do {
            result.push_back(SnapshotVariable{
                .scope{stmt.get_int32(0) == 0 ? Scope::User : Scope::Machine},
                .name{pnq::unicode::to_utf16(stmt.get_text(1))},
                .value{pnq::unicode::to_utf16(stmt.get_text(2))},
                .is_expandable{stmt.get_int32(3) != 0},
            });
        } while (stmt.next());
    }

    return result;
}

bool SnapshotStore::delete_snapshot(int64_t snapshot_id) {
    if (!m_impl) return false;

    pnq::sqlite::Statement stmt{m_impl->db,
        "DELETE FROM snapshots WHERE id = ?;"};
    stmt.bind(snapshot_id);
    return stmt.execute();
}

int SnapshotStore::prune_older_than(int days) {
    if (!m_impl) return 0;

    auto cutoff{std::chrono::system_clock::now() - std::chrono::hours{24 * days}};
    auto ts{std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(cutoff))};

    pnq::sqlite::Statement stmt{m_impl->db,
        "DELETE FROM snapshots WHERE timestamp < ?;"};
    stmt.bind(std::string_view{ts});
    stmt.execute();

    auto deleted{m_impl->db.changes_count()};
    if (deleted > 0) {
        spdlog::info("Pruned {} snapshots older than {} days", deleted, days);
    }
    return deleted;
}

} // namespace Environ::core
