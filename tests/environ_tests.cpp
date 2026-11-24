#include "precomp.h"

#include "AppSettings.h"
#include "Base16Theme.h"
#include "EnvExport.h"
#include "EnvGridModel.h"
#include "EnvStore.h"
#include "EnvWriter.h"
#include "KnowledgeBase.h"
#include "SnapshotStore.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using namespace Environ::core;

int g_passed{0};
int g_failed{0};

void check(bool cond, const char* expr, const char* file, int line)
{
    if (cond)
    {
        ++g_passed;
    }
    else
    {
        ++g_failed;
        std::fprintf(stderr, "FAIL [%s:%d]: %s\n", file, line, expr);
    }
}

#define CHECK(expr) check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

EnvVariable make_scalar(std::wstring name, std::wstring value, bool expandable = false)
{
    EnvVariable v{};
    v.name = std::move(name);
    v.value = std::move(value);
    v.kind = EnvVariableKind::Scalar;
    v.is_expandable = expandable;
    return v;
}

EnvVariable make_path(std::wstring name, std::vector<std::wstring> segments, bool expandable = true)
{
    EnvVariable v{};
    v.name = std::move(name);
    v.segments = std::move(segments);
    v.value = join_segments(v.segments);
    v.kind = EnvVariableKind::PathList;
    v.is_expandable = expandable;
    v.segment_valid.assign(v.segments.size(), true);
    v.segment_duplicate.assign(v.segments.size(), L"");
    v.expanded_segments = v.segments;
    return v;
}

std::filesystem::path make_temp_dir(const wchar_t* tag)
{
    auto dir{std::filesystem::temp_directory_path() / L"environ_tests" / tag};
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void test_env_store_helpers()
{
    std::vector<std::wstring> segs;
    CHECK(classify_variable(L"C:\\Windows;C:\\Windows\\System32", segs) == EnvVariableKind::PathList);
    CHECK(segs.size() == 2);
    CHECK(segs[0] == L"C:\\Windows");
    CHECK(segs[1] == L"C:\\Windows\\System32");

    segs.clear();
    CHECK(classify_variable(L"hello_world", segs) == EnvVariableKind::Scalar);
    CHECK(segs.size() == 1);

    // join_segments & apply_segment_edits preserve trailing/empty semicolons
    CHECK(join_segments({L"A", L"B", L"C"}) == L"A;B;C");
    CHECK(apply_segment_edits(L"A;;B;", {L"A", L"B2"}) == L"A;;B2;");
    CHECK(apply_segment_edits(L"A;B", {L"A", L"B", L"C"}) == L"A;B;C");

    // preserve_env_form keeps %WINDIR% when expanded path matches
    wchar_t windir[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"WINDIR", windir, MAX_PATH) > 0)
    {
        CHECK(preserve_env_form(L"%WINDIR%", windir) == L"%WINDIR%");
        CHECK(preserve_env_form(L"%WINDIR%", L"C:\\SomewhereElse") == L"C:\\SomewhereElse");
    }

    // detect_duplicates across user and machine
    std::vector<EnvVariable> u{make_path(L"PATH", {L"C:\\Tools", L"C:\\tools"})};
    std::vector<EnvVariable> m{make_path(L"PATH", {L"C:\\Windows", L"C:\\Tools"})};
    expand_and_validate(u);
    expand_and_validate(m);
    detect_duplicates(u, m);
    CHECK(!u[0].segment_duplicate[0].empty());
    CHECK(!u[0].segment_duplicate[1].empty());
    CHECK(!m[0].segment_duplicate[1].empty());
    CHECK(m[0].segment_duplicate[0].empty());
}

void test_env_writer()
{
    std::vector<EnvVariable> orig{
        make_scalar(L"KEEP", L"1"),
        make_scalar(L"MOD", L"old"),
        make_scalar(L"DEL", L"gone"),
        make_scalar(L"OLDNAME", L"val"),
    };

    EnvVariable renamed{make_scalar(L"NEWNAME", L"val")};
    renamed.original_name = L"OLDNAME";

    std::vector<EnvVariable> cur{
        make_scalar(L"KEEP", L"1"),
        make_scalar(L"MOD", L"new"),
        renamed,
        make_scalar(L"ADDED", L"fresh"),
    };

    auto diff{compute_diff(orig, cur)};
    CHECK(diff.size() == 4);

    int adds{0}, mods{0}, dels{0}, renames{0};
    for (const auto& c : diff)
    {
        switch (c.kind)
        {
        case EnvChange::Kind::Add:    ++adds; break;
        case EnvChange::Kind::Modify: ++mods; break;
        case EnvChange::Kind::Delete: ++dels; break;
        case EnvChange::Kind::Rename: ++renames; break;
        }
    }
    CHECK(adds == 1 && mods == 1 && dels == 1 && renames == 1);
    CHECK(!summarize_changes(diff).empty());

    auto table{build_diff_table(L"Before", L"After", orig, {}, cur, {})};
    CHECK(!table.empty());

    // Validation
    CHECK(validate_variables(cur).empty());
    CHECK(!validate_variables({make_scalar(L"", L"x")}).empty());
    CHECK(!validate_variables({make_scalar(L"A=B", L"x")}).empty());
    CHECK(!validate_variables({make_scalar(L"FOO", L"1"), make_scalar(L"foo", L"2")}).empty());
}

void test_env_export()
{
    std::vector<EnvVariable> u{
        make_scalar(L"PLAIN", L"abc", false),
        make_scalar(L"EXP", L"%USERPROFILE%\\bin", true),
        make_scalar(L"WITH SPACE", L"val", false),
    };
    std::vector<EnvVariable> m{
        make_path(L"PATH", {L"C:\\Windows", L"C:\\Windows\\System32"}, true),
    };

    const std::string toml_text{export_toml(u, m)};
    CHECK(!toml_text.empty());

    auto imported{import_toml(toml_text)};
    CHECK(imported.has_value());
    CHECK(imported->user.size() == 3);
    CHECK(imported->machine.size() == 1);

    bool found_exp{false};
    for (const auto& v : imported->user)
    {
        if (v.name == L"EXP")
        {
            found_exp = true;
            CHECK(v.is_expandable);
            CHECK(v.value == L"%USERPROFILE%\\bin");
        }
    }
    CHECK(found_exp);

    auto merged{merge_variables(u, {make_scalar(L"plain", L"updated"), make_scalar(L"EXTRA", L"2")})};
    CHECK(merged.size() == 4);
}

void test_snapshot_store()
{
    const auto dir{make_temp_dir(L"snapshots")};
    const std::string db_path{(dir / "test.db").string()};

    SnapshotStore store;
    CHECK(store.open(db_path));

    std::vector<EnvVariable> u{make_scalar(L"FOO", L"bar", false)};
    std::vector<EnvVariable> m{make_path(L"PATH", {L"C:\\Windows", L"C:\\Windows\\System32"}, true)};

    const int64_t id{store.create_snapshot("unit-test", u, m)};
    CHECK(id > 0);

    auto list{store.list_snapshots()};
    CHECK(list.size() == 1);
    CHECK(list[0].label == "unit-test");
    CHECK(list[0].scope_mask == 3);

    auto vars{store.load_snapshot(id)};
    CHECK(vars.size() == 2);

    auto recon_u{reconstruct_variables(vars, Scope::User)};
    auto recon_m{reconstruct_variables(vars, Scope::Machine)};
    CHECK(recon_u.size() == 1 && recon_u[0].name == L"FOO" && recon_u[0].value == L"bar");
    CHECK(recon_m.size() == 1 && recon_m[0].kind == EnvVariableKind::PathList && recon_m[0].segments.size() == 2);

    CHECK(store.delete_snapshot(id));
    CHECK(store.list_snapshots().empty());
}

void test_knowledge_base()
{
    KnowledgeBase kb;
    CHECK(kb.load("knowledge.toml"));
    CHECK(!kb.describe(L"PATH").empty());
    CHECK(kb.is_volatile(L"USERPROFILE"));
    CHECK(kb.classify_override(L"PSModulePath") == KnowledgeBase::ClassHint::ForcePath);

    const auto dir{make_temp_dir(L"kb")};
    const std::string learned_path{(dir / "user_knowledge.toml").string()};

    kb.learn_folder(L"MY_CUSTOM_DIR_TEST");
    kb.learn_file(L"MY_CUSTOM_FILE_TEST");
    kb.learn_path_like(L"MY_CUSTOM_PATH_TEST");
    CHECK(kb.has_learned());
    CHECK(kb.save_learned(learned_path));
    CHECK(!kb.has_learned());

    KnowledgeBase kb2;
    CHECK(kb2.load(learned_path));
    CHECK(kb2.path_role(L"MY_CUSTOM_DIR_TEST") == KnowledgeBase::PathRole::Folder);
    CHECK(kb2.path_role(L"MY_CUSTOM_FILE_TEST") == KnowledgeBase::PathRole::File);
    CHECK(kb2.classify_override(L"MY_CUSTOM_PATH_TEST") == KnowledgeBase::ClassHint::ForcePath);
}

void test_base16_theme()
{
    CHECK(hex_to_color("#ff8000").r8() == 255);
    CHECK(hex_to_color("#ff8000").g8() == 128);
    CHECK(hex_to_color("#ff8000").b8() == 0);
    CHECK(hex_to_color("invalid").r8() == 0);

    Base16ThemeSet ts;
    ts.load_from_directory(L"themes");
    CHECK(ts.count() > 5);

    auto themes{ts.themes()};
    bool has_dark{false};
    bool has_light{false};
    for (const auto& t : themes)
    {
        if (t.dark) has_dark = true;
        else has_light = true;
    }
    CHECK(has_dark && has_light);

    const std::string first_name{ts.names().front()};
    CHECK(ts.select_by_name(first_name));
    CHECK(ts.current().name == first_name);
    CHECK(!ts.select_by_name("NonExistentThemeNameXYZ"));

    // Fallback when directory is missing
    Base16ThemeSet missing;
    missing.load_from_directory(L"themes_does_not_exist_xyz");
    CHECK(missing.count() == 1);
    CHECK(missing.current().name == "Catppuccin Mocha");
}

void test_app_settings()
{
    const auto dir{make_temp_dir(L"settings")};
    const std::string cfg{(dir / "environ.toml").string()};

    AppSettings s1;
    s1.window.x.set(120);
    s1.window.y.set(-40); // negative multi-monitor coordinate
    s1.window.width.set(1280);
    s1.window.height.set(800);
    s1.window.maximized.set(true);
    s1.appearance.theme.set("Nord");
    s1.appearance.zoom.set(125);
    s1.appearance.uiFont.set("Cascadia Code");
    CHECK(s1.save(cfg));

    AppSettings s2;
    s2.load(cfg);
    CHECK(s2.window.x.get() == 120);
    CHECK(s2.window.y.get() == -40);
    CHECK(s2.window.width.get() == 1280);
    CHECK(s2.window.height.get() == 800);
    CHECK(s2.window.maximized.get() == true);
    CHECK(s2.appearance.theme.get() == "Nord");
    CHECK(s2.appearance.zoom.get() == 125);
    CHECK(s2.appearance.uiFont.get() == "Cascadia Code");
}

void test_env_grid_model()
{
    std::vector<EnvVariable> user_vars{
        make_scalar(L"ALPHA", L"one"),
        make_path(L"PATH", {L"C:\\UserBin", L"C:\\UserTools"}),
    };
    std::vector<EnvVariable> machine_vars{
        make_scalar(L"OS", L"Windows_NT"),
    };
    EnvVariable proc_session{make_scalar(L"SESSION_VAR", L"win")};
    EnvVariable proc_inherited{make_scalar(L"SHELL_VAR", L"pwsh")};
    proc_inherited.inherited = true;

    EnvGridModel model;
    model.set_data(user_vars, machine_vars, {proc_session, proc_inherited}, false);

    // 2 user vars (ALPHA + PATH with 2 segments = 3 rows) + 1 machine + 2 process = 6 rows
    CHECK(model.total_row_count() == 6);
    CHECK(model.visible_row_count() == 6);
    CHECK(!model.has_changes());

    auto counts{model.group_counts()};
    CHECK(counts.user == 2 && counts.machine == 1 && counts.session == 1 && counts.inherited == 1);

    // Default sort is Name ascending: ALPHA, OS, PATH(0), PATH(1), SESSION_VAR, SHELL_VAR
    CHECK(model.row(0).col1 == L"ALPHA");
    CHECK(model.row(1).col1 == L"OS");
    CHECK(model.row(1).read_only == true); // unelevated machine var
    CHECK(model.row(2).col1 == L"PATH");
    CHECK(model.row(3).kind == GridRow::Kind::Segment);

    // Toggle sort descending preserves selection
    model.select_single(0); // ALPHA
    model.toggle_sort(SortColumn::Name);
    CHECK(!model.sort_ascending());
    CHECK(model.selected_variable_name() == L"ALPHA");
    model.toggle_sort(SortColumn::Name); // back to ascending
    CHECK(model.sort_ascending());

    // Filter promotes matching segment to display variable name
    model.set_filter(L"UserTools");
    CHECK(model.visible_row_count() == 1);
    const int promoted_actual{model.visible_to_actual(0)};
    CHECK(model.is_promoted_segment(promoted_actual));
    CHECK(model.display_name(promoted_actual) == L"PATH");
    model.set_filter(L"");
    CHECK(model.visible_row_count() == 6);

    // Inline editing: edit ALPHA value, commit, verify dirty & CurrentVars
    model.select_single(0);
    auto init_val{model.begin_edit_value()};
    CHECK(init_val.has_value() && *init_val == L"one");
    model.commit_edit(L"two");
    CHECK(model.has_changes());
    CHECK(model.row_value_dirty(0));

    // Inline rename
    auto init_name{model.begin_edit_name()};
    CHECK(init_name.has_value() && *init_name == L"ALPHA");
    model.commit_edit(L"ALPHA_RENAMED");
    CHECK(model.row_name_dirty(0));

    // Read-only row cannot be edited
    model.select_single(1); // OS (machine, unelevated)
    CHECK(!model.selection_editable());
    CHECK(!model.begin_edit_value().has_value());

    // SelectNextEditable skips read-only OS row
    model.select_single(0);
    CHECK(model.select_next_editable(+1));
    CHECK(model.selected_variable_name() == L"PATH");

    // Path-list structural edits: add, edit, move, remove
    CHECK(model.selected_is_path_entry());
    CHECK(model.add_entry());
    CHECK(model.set_selected_value(L"C:\\Inserted"));
    CHECK(model.move_entry(-1)); // move C:\Inserted to first segment
    CHECK(model.selected_value_text() == L"C:\\Inserted");
    CHECK(model.remove_entry()); // removes first segment, promotes next

    // Add two new variables: must not collapse into one unit
    CHECK(model.add_variable(Scope::User));
    auto n1{model.begin_edit_name()};
    CHECK(n1.has_value());
    model.commit_edit(L"NEW_ONE");
    CHECK(model.set_selected_value(L"v1"));

    CHECK(model.add_variable(Scope::User));
    auto n2{model.begin_edit_name()};
    CHECK(n2.has_value());
    model.commit_edit(L"NEW_TWO");
    CHECK(model.set_selected_value(L"v2"));

    auto cur_user{model.current_vars(Scope::User)};
    CHECK(cur_user.size() == 4); // ALPHA_RENAMED, PATH, NEW_ONE, NEW_TWO

    // Copy text for scalar variable
    model.select_single(0);
    CHECK(model.copy_text() == L"ALPHA_RENAMED=two");

    // Restore staging
    EnvGridModel restore_model;
    restore_model.set_data_for_restore(
        {make_scalar(L"A", L"cur"), make_scalar(L"ONLY_CUR", L"x")},
        {},
        {make_scalar(L"A", L"snap"), make_scalar(L"ONLY_SNAP", L"y")},
        {},
        false);
    CHECK(restore_model.has_changes());
    auto restore_diff{compute_diff(
        restore_model.original_vars(Scope::User),
        restore_model.current_vars(Scope::User))};
    CHECK(restore_diff.size() == 3); // Modify A, Delete ONLY_CUR, Add ONLY_SNAP
}

} // namespace

int main()
{
    spdlog::set_level(spdlog::level::err);

    test_env_store_helpers();
    test_env_writer();
    test_env_export();
    test_snapshot_store();
    test_knowledge_base();
    test_base16_theme();
    test_app_settings();
    test_env_grid_model();

    std::printf("environ_tests: %d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
