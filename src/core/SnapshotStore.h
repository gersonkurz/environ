#pragma once

#include "EnvStore.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Environ::core {

struct SnapshotInfo {
    int64_t id;
    std::string timestamp;   // ISO 8601 UTC
    std::string label;
    int scope_mask;          // 1=User, 2=Machine, 3=Both
};

struct SnapshotVariable {
    Scope scope;
    std::wstring name;
    std::wstring value;
    bool is_expandable;
};

class SnapshotStore {
public:
    SnapshotStore();
    ~SnapshotStore();
    SnapshotStore(SnapshotStore&&) noexcept;
    SnapshotStore& operator=(SnapshotStore&&) noexcept;

    // Opens or creates the database at %LOCALAPPDATA%\environ\environ.db.
    bool open();

    // Create a snapshot from the given variables. Returns snapshot ID or -1.
    int64_t create_snapshot(
        std::string_view label,
        std::vector<EnvVariable> const& user_vars,
        std::vector<EnvVariable> const& machine_vars);

    // List all snapshots, newest first.
    std::vector<SnapshotInfo> list_snapshots();

    // Load all variables from a snapshot.
    std::vector<SnapshotVariable> load_snapshot(int64_t snapshot_id);

    // Delete a snapshot.
    bool delete_snapshot(int64_t snapshot_id);

    // Delete snapshots older than the given number of days.
    int prune_older_than(int days);

    // Compute what changed between this snapshot and the previous one.
    // Returns a human-readable list of change descriptions.
    std::vector<std::wstring> describe_snapshot_changes(int64_t snapshot_id);

    // Check if the given variables match the most recent snapshot exactly.
    bool matches_latest_snapshot(
        std::vector<EnvVariable> const& user_vars,
        std::vector<EnvVariable> const& machine_vars);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool ensure_schema();
};

} // namespace Environ::core
