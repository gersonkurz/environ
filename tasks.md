# Environ 1.0 Tasks

## Critical

- [x] 1. No rollback on partial registry write failure — `apply_changes` keeps going after errors, can leave variables in inconsistent state (e.g. rename deletes old name but fails to set new name)
- [x] 2. `create_directories` calls can crash — `App.xaml.cpp`, `SnapshotStore.cpp`, `AppSettings.cpp` use throwing overload without try/catch
- [x] 3. Release builds have no global exception handler — `UnhandledException` handler is `#ifdef _DEBUG` only
- [ ] 4. "Restart as Admin" discards unsaved changes without warning

## Important

- [ ] 5. History page does O(N) SQL queries on load — `describe_snapshot_changes()` for every snapshot on UI thread
- [ ] 6. README promises features that don't exist — update to match reality
- [ ] 7. Cross-variable duplicate PATH detection not wired to UI — `detect_duplicates()` exists in core but unused
- [ ] 8. Off-screen window restore — saved coordinates not validated against current monitors
- [ ] 9. No UI to delete individual snapshots — API exists but no context menu option

## Minor

- [ ] 10. Zero accessibility markup — no `AutomationProperties` anywhere
- [ ] 11. Only Ctrl+S as keyboard accelerator — missing Ctrl+Z, Ctrl+F, Delete, Ctrl+N, F5
- [ ] 12. Zoom only applies to ListView — headers/filter/buttons stay at 100%
- [ ] 13. PathList heuristic inconsistency — HistoryPage uses simple semicolon check vs EnvStore's smarter classifier
- [ ] 14. `systemAIModels` capability declared but unused

## Housekeeping

- [ ] 15. README says "CMake" build system — should say vcxproj/slnx
- [ ] 16. README has `[username]` placeholder in GitHub URL
