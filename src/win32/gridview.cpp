#include "precomp.h"

// GridView — grid + inline editor + detail strip + footer, extracted from MainWindow.

#include "gridview.h"
#include "EnvStore.h"
#include "KnowledgeBase.h"

namespace
{
    COLORREF ToColorRef(const D2D1_COLOR_F& c)
    {
        return RGB(static_cast<BYTE>(c.r * 255.0f), static_cast<BYTE>(c.g * 255.0f), static_cast<BYTE>(c.b * 255.0f));
    }

    // Context menu item IDs.
    constexpr UINT kMenuCopy{1001};
    constexpr UINT kMenuInsert{1002};
    constexpr UINT kMenuRemove{1003};
    constexpr UINT kMenuBrowse{1004};

    // Segoe Fluent Icons glyphs for the in-cell browse button.
    constexpr wchar_t kGlyphFolder{0xE8B7}; // "Folder"
    constexpr wchar_t kGlyphFile{0xE8A5};   // "Document"

    // Owner-drawn menu item data -- stored in MENUITEMINFO::dwItemData.
    struct MenuItemData
    {
        std::wstring label;
        std::wstring accel;
    };

    bool InRect(const D2D1_RECT_F& r, float x, float y)
    {
        return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
    }

    // Modern shell folder/file picker. Returns the chosen filesystem path, or nullopt if
    // cancelled/failed. `initial` (may contain %VARS%) seeds the starting location.
    std::optional<std::wstring> BrowseForPath(HWND owner, bool pickFolder, const std::wstring& initial)
    {
        IFileOpenDialog* dlg{nullptr};
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dlg))) || !dlg)
            return std::nullopt;

        DWORD opts{};
        dlg->GetOptions(&opts);
        opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        if (pickFolder) opts |= FOS_PICKFOLDERS;
        dlg->SetOptions(opts);

        // Seed the dialog from the current value (expanded). For a file, seed its folder
        // and pre-fill the name; for a folder, seed the folder itself.
        if (!initial.empty())
        {
            wchar_t expanded[1024]{};
            ExpandEnvironmentStringsW(initial.c_str(), expanded, 1024);
            std::filesystem::path p{expanded};
            const std::filesystem::path dir{pickFolder ? p : p.parent_path()};
            IShellItem* item{nullptr};
            if (!dir.empty() &&
                SUCCEEDED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&item))) && item)
            {
                dlg->SetFolder(item);
                item->Release();
            }
            if (!pickFolder && p.has_filename())
                dlg->SetFileName(p.filename().c_str());
        }

        std::optional<std::wstring> result;
        if (SUCCEEDED(dlg->Show(owner)))
        {
            IShellItem* item{nullptr};
            if (SUCCEEDED(dlg->GetResult(&item)) && item)
            {
                PWSTR path{nullptr};
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    result = path;
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
        return result;
    }
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ui::GridView::GridView(Grid& grid, theme::ThemeSet& theme,
                       const Environ::core::KnowledgeBase& knowledge)
    : m_grid{grid}, m_theme{theme}, m_knowledge{knowledge}
{
}

ui::GridView::~GridView()
{
    if (m_editBrush)    DeleteObject(m_editBrush);
    if (m_editFont)     DeleteObject(m_editFont);
    if (m_editFontName) DeleteObject(m_editFontName);
}

// ---------------------------------------------------------------------------
// View overrides
// ---------------------------------------------------------------------------

void ui::GridView::Activate(const ViewContext& ctx)
{
    EnsureSearchControl(ctx);
    if (m_searchEdit) ShowWindow(m_searchEdit, SW_SHOW);
}

void ui::GridView::Deactivate()
{
    if (m_searchEdit)
    {
        ShowWindow(m_searchEdit, SW_HIDE);
        SetWindowTextW(m_searchEdit, L"");
    }
    m_grid.SetFilter(L"");
    EndEdit(true);
}

void ui::GridView::Paint(const ViewContext& ctx, const D2D1_RECT_F& bounds)
{
    const theme::ColorScheme& s{*ctx.scheme};

    // Search bar sits at the top of the bounds (same height as the grid header row).
    const float searchH{32.0f * ctx.zoom};
    // Grid occupies bounds minus search bar at top and the detail strip at bottom. The
    // strip must scale with the detail font (zoom * fontScale) or descenders get clipped.
    const float uiScale{ctx.zoom * ctx.fontScale};
    const float detailH{22.0f * uiScale};
    const float stripReserve{detailH + 6.0f * uiScale};
    const D2D1_RECT_F gridBounds{D2D1::RectF(bounds.left, bounds.top + searchH,
                                              bounds.right, bounds.bottom - stripReserve)};

    // Paint search bar background (same as header band).
    {
        const D2D1_RECT_F searchBg{D2D1::RectF(bounds.left, bounds.top,
                                                bounds.right, bounds.top + searchH)};
        ctx.brush->SetColor(s.header.fill);
        ctx.rt->FillRectangle(searchBg, ctx.brush);
        if (s.header.borderWidth > 0.0f)
        {
            ctx.brush->SetColor(s.header.border);
            ctx.rt->DrawLine(D2D1::Point2F(bounds.left, bounds.top + searchH - 0.5f),
                             D2D1::Point2F(bounds.right, bounds.top + searchH - 0.5f),
                             ctx.brush, s.header.borderWidth);
        }
        PositionSearchEdit(ctx, bounds);
    }

    m_grid.Paint(ctx.rt, ctx.brush,
                 GridFonts{ctx.fmtName, ctx.fmtValue, ctx.fmtHeader, ctx.dwrite, ctx.fmtGlyph},
                 s, gridBounds);

    // Detail strip: expanded path / validity / duplicate info for the selected row.
    if (const auto detail{m_grid.GetSelectionDetail()})
    {
        std::wstring text;
        D2D1_COLOR_F color{s.headerSubtext};

        if (!detail->valid)
        {
            text = L"\x2717 " + detail->expandedPath + L" \x2014 path not found";
            color = s.rowInvalid.text;
        }
        else if (!detail->duplicateDesc.empty())
        {
            text = L"\x2192 " + detail->expandedPath + L" \x2014 " + detail->duplicateDesc;
            color = s.rowDuplicate.text;
        }
        else if (detail->expandedPath != detail->displayPath)
        {
            text = L"\x2192 " + detail->expandedPath;
        }

        // Nothing path-specific to show -> fall back to the variable's description, if known.
        // Colored with the accent so it reads as distinct from path/expansion details.
        if (text.empty())
        {
            const std::wstring desc{m_knowledge.describe(m_grid.SelectedVariableName())};
            if (!desc.empty())
            {
                text = L"\x2139 " + desc; // U+2139 information source
                color = s.accent;
            }
        }

        if (!text.empty())
        {
            ui::DrawString(ctx, text, ctx.fmtSub,
                           D2D1::RectF(bounds.left, bounds.bottom - detailH,
                                       bounds.right, bounds.bottom),
                           color);
        }
    }

    // In-cell browse button for the selected folder/file row (drawn over the value cell's
    // right edge; the selection fill masks any value text beneath it).
    if (const auto btn{BrowseButtonRect()})
    {
        const bool folder{SelectedPathRole() == Environ::core::KnowledgeBase::PathRole::Folder};
        ctx.brush->SetColor(s.rowSelected.fill);
        ctx.rt->FillRectangle(*btn, ctx.brush);
        const std::wstring glyph(1, folder ? kGlyphFolder : kGlyphFile);
        ui::DrawString(ctx, glyph, ctx.fmtGlyph, *btn, s.rowSelected.text);
    }
}

bool ui::GridView::OnMouseMove(const ViewContext& /*ctx*/, float x, float y)
{
    return m_grid.OnMouseMove(x, y);
}

bool ui::GridView::OnMouseLeave()
{
    return m_grid.OnMouseLeave();
}

bool ui::GridView::OnLButtonDown(const ViewContext& ctx, float x, float y,
                                 bool shift, bool ctrl)
{
    // The in-cell browse button takes priority over normal grid selection.
    if (const auto btn{BrowseButtonRect()}; btn && InRect(*btn, x, y))
    {
        BrowseSelected(ctx); // invalidates if the value changed
        return false;
    }
    return m_grid.OnLButtonDown(x, y, shift, ctrl);
}

bool ui::GridView::OnLButtonUp()
{
    return m_grid.OnLButtonUp();
}

bool ui::GridView::OnLButtonDblClk(const ViewContext& ctx, float x, float y)
{
    BeginEditAt(ctx, x, y);
    return false; // BeginEditAt already invalidates
}

bool ui::GridView::OnRButtonDown(const ViewContext& /*ctx*/, float x, float y,
                                 bool shift, bool ctrl)
{
    return m_grid.OnRButtonDown(x, y, shift, ctrl);
}

bool ui::GridView::OnWheel(const ViewContext& /*ctx*/, int delta)
{
    if (m_grid.IsEditing()) EndEdit(true);
    return m_grid.OnWheel(delta);
}

bool ui::GridView::OnHWheel(const ViewContext& /*ctx*/, int delta)
{
    if (m_grid.IsEditing()) EndEdit(true);
    return m_grid.OnHWheel(delta);
}

bool ui::GridView::OnKey(const ViewContext& ctx, int vk)
{
    if (vk == VK_RETURN) { BeginEditFromGrid(ctx); return false; }
    if (vk == VK_INSERT)
    {
        if (m_grid.SelectedIsPathEntry())
        {
            if (m_grid.AddEntry()) { InvalidateRect(ctx.hwnd, nullptr, FALSE); BeginEditFromGrid(ctx); }
        }
        else if (m_grid.SelectionEditable() || !m_grid.HasSelection())
        {
            if (m_grid.AddVariable()) { InvalidateRect(ctx.hwnd, nullptr, FALSE); BeginEditNameFromGrid(ctx); }
        }
        return false;
    }
    if (vk == VK_DELETE)
    {
        if (m_grid.SelectedIsPathEntry())
            return m_grid.RemoveEntry();
        else
            return m_grid.RemoveVariable();
    }
    return m_grid.OnKey(vk);
}

bool ui::GridView::OnSysKey(const ViewContext& /*ctx*/, int vk, LPARAM lp)
{
    if ((vk == VK_UP || vk == VK_DOWN) && (lp & (1 << 29)))
    {
        return m_grid.MoveEntry(vk == VK_UP ? -1 : 1);
    }
    return false;
}

bool ui::GridView::OnContextMenu(const ViewContext& ctx, int screenX, int screenY)
{
    if (m_grid.IsEditing()) EndEdit(true);
    if (!m_grid.HasSelection()) return false;
    ShowGridContextMenu(ctx, screenX, screenY);
    return false;
}

void ui::GridView::OnSize(const ViewContext& ctx)
{
    if (m_grid.IsEditing()) EndEdit(true);
    InvalidateRect(ctx.hwnd, nullptr, FALSE);
}

void ui::GridView::OnDpiChanged(const ViewContext& ctx)
{
    if (m_grid.IsEditing()) EndEdit(true);
    RefreshEditFont(ctx);
    if (m_searchEdit)
        SendMessageW(m_searchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFontName), TRUE);
}

std::wstring ui::GridView::GetStatusText(const ViewContext& ctx) const
{
    const theme::ColorScheme& s{*ctx.scheme};
    std::wstring footer = std::format(
        L"{}   \x2022   {} user, {} machine, {} process{}",
        std::wstring(s.name.begin(), s.name.end()), m_userCount, m_machineCount, m_processCount,
        m_elevated ? L"" : L"  (machine read-only)");
    if (m_grid.HasFilter())
        footer += std::format(L"   \x2022   {} shown (filtered)", m_grid.FilteredRowCount());
    if (ctx.zoom != 1.0f)
        footer += std::format(L"   \x2022   {}%", static_cast<int>(ctx.zoom * 100));
    return footer;
}

// ---------------------------------------------------------------------------
// GridView-specific API
// ---------------------------------------------------------------------------

void ui::GridView::LoadData(bool elevated)
{
    using namespace Environ::core;
    m_elevated = elevated;
    std::vector<EnvVariable> userVars{read_variables(Scope::User, &m_knowledge)};
    std::vector<EnvVariable> machineVars{read_variables(Scope::Machine, &m_knowledge)};
    expand_and_validate(userVars);
    expand_and_validate(machineVars);
    detect_duplicates(userVars, machineVars);

    // Read-only "process extras" — the effective env vars Windows sets (USERPROFILE,
    // LOCALAPPDATA, ProgramFiles, ...) that aren't persistent User/Machine values.
    std::vector<EnvVariable> processVars{read_process_extras(userVars, machineVars)};
    expand_and_validate(processVars);

    m_userCount = userVars.size();
    m_machineCount = machineVars.size();
    m_processCount = processVars.size();
    m_grid.SetData(userVars, machineVars, processVars, m_elevated);
}

void ui::GridView::OnEditEnd(const ViewContext& ctx, bool commit, bool tab, bool shift)
{
    const bool wasName{m_grid.IsEditingName()};
    EndEdit(commit);
    if (tab)
    {
        if (wasName && !shift) // forward Tab from name -> edit value of same row
            BeginEditFromGrid(ctx);
        else if (m_grid.SelectNextEditable(shift ? -1 : 1))
            BeginEditFromGrid(ctx);
    }
}

LRESULT ui::GridView::OnCtlColorEdit(const ViewContext& ctx, WPARAM wp)
{
    const theme::ColorScheme& s{*ctx.scheme};
    SetTextColor(reinterpret_cast<HDC>(wp), ToColorRef(s.edit.text));
    SetBkColor(reinterpret_cast<HDC>(wp), ToColorRef(s.edit.fill));
    if (!m_editBrush) RefreshEditBrush();
    return reinterpret_cast<LRESULT>(m_editBrush);
}

bool ui::GridView::HasChanges() const
{
    return m_grid.HasChanges();
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

void ui::GridView::EnsureEditControl(const ViewContext& ctx)
{
    if (m_edit) return;
    m_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0,
                             ctx.hwnd, nullptr,
                             reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(ctx.hwnd, GWLP_HINSTANCE)), nullptr);
    if (!m_edit)
    {
        spdlog::error("inline edit control creation failed");
        return;
    }
    // Subclass intercepts Enter/Esc/Tab and posts WM_APP_EDIT_END back to the parent.
    struct EditSub {
        static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
        {
            HWND parent{reinterpret_cast<HWND>(ref)};
            constexpr UINT WM_APP_EDIT_END{WM_APP + 1};
            if (msg == WM_KEYDOWN)
            {
                if (wp == VK_RETURN) { PostMessageW(parent, WM_APP_EDIT_END, 1, 0); return 0; }
                if (wp == VK_ESCAPE) { PostMessageW(parent, WM_APP_EDIT_END, 0, 0); return 0; }
                if (wp == VK_TAB)    { PostMessageW(parent, WM_APP_EDIT_END, 2, (GetKeyState(VK_SHIFT) < 0) ? 1 : 0); return 0; }
                if (wp == VK_F1 || wp == VK_F2 || wp == VK_F3)
                {
                    PostMessageW(parent, WM_APP_EDIT_END, 1, 0);
                    PostMessageW(parent, WM_KEYDOWN, wp, lp);
                    return 0;
                }
            }
            else if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_TAB))
            {
                return 0;
            }
            return DefSubclassProc(h, msg, wp, lp);
        }
    };
    if (!SetWindowSubclass(m_edit, EditSub::Proc, 1, reinterpret_cast<DWORD_PTR>(ctx.hwnd)))
    {
        spdlog::error("SetWindowSubclass for edit control failed");
        DestroyWindow(m_edit);
        m_edit = nullptr;
        return;
    }
    RefreshEditFont(ctx);
    SendMessageW(m_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
}

void ui::GridView::EndEdit(bool commit)
{
    if (!m_edit || !IsWindowVisible(m_edit)) return;
    if (commit)
    {
        const int len{GetWindowTextLengthW(m_edit)};
        std::wstring text(static_cast<size_t>(len) + 1, L'\0');
        const int copied{GetWindowTextW(m_edit, text.data(), len + 1)};
        text.resize(static_cast<size_t>(copied));
        m_grid.CommitEdit(text);
    }
    else
    {
        m_grid.CancelEdit();
    }
    ShowWindow(m_edit, SW_HIDE);
    HWND parent{GetParent(m_edit)};
    SetFocus(parent);
    InvalidateRect(parent, nullptr, FALSE);
}

void ui::GridView::PositionEditor(const ViewContext& ctx, const Grid::EditTarget& target)
{
    const float scale{ctx.dipScale};
    const auto px = [scale](float dip) { return static_cast<int>(dip * scale); };
    const D2D1_RECT_F& c{target.cell};

    HFONT font{target.isName ? m_editFontName : m_editFont};
    SendMessageW(m_edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    TEXTMETRICW tm{};
    if (const HDC dc{GetDC(m_edit)})
    {
        const HGDIOBJ prev{SelectObject(dc, font)};
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, prev);
        ReleaseDC(m_edit, dc);
    }
    const int cellTop{px(c.top)};
    const int cellH{px(c.bottom) - cellTop};
    const int editH{tm.tmHeight > 0 ? static_cast<int>(tm.tmHeight) : px(16.0f)};
    MoveWindow(m_edit, px(c.left), cellTop + (cellH - editH) / 2,
               px(c.right) - px(c.left), editH, FALSE);

    SetWindowTextW(m_edit, target.text.c_str());
    ShowWindow(m_edit, SW_SHOW);
    SetFocus(m_edit);
    SendMessageW(m_edit, EM_SETSEL, 0, -1);
    InvalidateRect(ctx.hwnd, nullptr, FALSE);
}

void ui::GridView::BeginEditFromGrid(const ViewContext& ctx)
{
    if (!m_grid.SelectionEditable()) return;
    EnsureEditControl(ctx);
    if (!m_edit) return;
    if (const auto target{m_grid.BeginEdit()}) PositionEditor(ctx, *target);
}

void ui::GridView::BeginEditNameFromGrid(const ViewContext& ctx)
{
    if (!m_grid.SelectionEditable()) return;
    EnsureEditControl(ctx);
    if (!m_edit) return;
    if (const auto target{m_grid.BeginEditName()}) PositionEditor(ctx, *target);
}

void ui::GridView::BeginEditAt(const ViewContext& ctx, float x, float y)
{
    EnsureEditControl(ctx);
    if (!m_edit) return;
    const auto target{m_grid.BeginEditAt(x, y)};
    InvalidateRect(ctx.hwnd, nullptr, FALSE);
    if (target) PositionEditor(ctx, *target);
}

void ui::GridView::RefreshEditBrush()
{
    if (m_editBrush) { DeleteObject(m_editBrush); m_editBrush = nullptr; }
    m_editBrush = CreateSolidBrush(ToColorRef(m_theme.Current().edit.fill));
}

void ui::GridView::RefreshEditFont(const ViewContext& ctx)
{
    const float scale{ctx.dipScale};
    const auto px = [scale](float dip) { return static_cast<int>(dip * scale); };
    if (m_editFont) DeleteObject(m_editFont);
    if (m_editFontName) DeleteObject(m_editFontName);
    m_editFont = CreateFontW(-px(12.0f * ctx.zoom * ctx.fontScale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, ctx.uiFontFamily);
    m_editFontName = CreateFontW(-px(14.0f * ctx.zoom * ctx.fontScale), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, ctx.uiFontFamily);
    if (m_searchEdit)
        SendMessageW(m_searchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFontName), TRUE);
}

// ---------------------------------------------------------------------------
// Menu / clipboard
// ---------------------------------------------------------------------------

Environ::core::KnowledgeBase::PathRole ui::GridView::SelectedPathRole() const
{
    using PathRole = Environ::core::KnowledgeBase::PathRole;
    if (!m_grid.SelectionEditable()) return PathRole::None;
    if (m_grid.SelectedIsPathEntry()) return PathRole::Folder; // path segments are directories
    return m_knowledge.path_role(m_grid.SelectedVariableName());
}

std::optional<D2D1_RECT_F> ui::GridView::BrowseButtonRect() const
{
    if (m_grid.IsEditing()) return std::nullopt; // editor occupies the cell
    if (SelectedPathRole() == Environ::core::KnowledgeBase::PathRole::None) return std::nullopt;
    const auto cell{m_grid.SelectedValueCellRect()};
    if (!cell) return std::nullopt;
    const float h{cell->bottom - cell->top}; // square button at the cell's right edge
    return D2D1::RectF(cell->right - h, cell->top, cell->right, cell->bottom);
}

void ui::GridView::BrowseSelected(const ViewContext& ctx)
{
    using PathRole = Environ::core::KnowledgeBase::PathRole;
    const PathRole role{SelectedPathRole()};
    if (role == PathRole::None) return;
    const std::wstring original{m_grid.SelectedValueText()};
    if (auto picked{BrowseForPath(ctx.hwnd, role == PathRole::Folder, original)})
    {
        // Keep the %VAR% form if the pick resolves to the same place as the old value.
        const std::wstring value{Environ::core::preserve_env_form(original, *picked)};
        if (m_grid.SetSelectedValue(value))
            InvalidateRect(ctx.hwnd, nullptr, FALSE);
    }
}

void ui::GridView::ShowGridContextMenu(const ViewContext& ctx, int screenX, int screenY)
{
    const bool editable{m_grid.SelectionEditable()};
    const bool isPath{m_grid.SelectedIsPathEntry()};
    const auto role{SelectedPathRole()};

    HMENU hMenu{CreatePopupMenu()};
    if (!hMenu) return;

    const int selCount{m_grid.SelectionCount()};
    MenuItemData dataCopy{selCount > 1 ? std::format(L"Copy {} rows", selCount) : std::wstring{L"Copy"}, L"Ctrl+C"};
    MenuItemData dataInsert{isPath ? L"Insert entry" : L"New variable", L"Ins"};
    MenuItemData dataRemove{isPath ? L"Remove entry" : L"Delete variable", L"Del"};
    MenuItemData dataBrowse{role == Environ::core::KnowledgeBase::PathRole::File
                                ? L"Browse for file\x2026" : L"Browse for folder\x2026", L""};

    const auto addItem = [&](UINT id, MenuItemData* data, bool enabled) {
        MENUITEMINFOW mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_DATA;
        mi.fType = MFT_OWNERDRAW;
        mi.fState = enabled ? MFS_ENABLED : MFS_GRAYED;
        mi.wID = id;
        mi.dwItemData = reinterpret_cast<ULONG_PTR>(data);
        InsertMenuItemW(hMenu, id, FALSE, &mi);
    };

    addItem(kMenuCopy, &dataCopy, true);
    if (role != Environ::core::KnowledgeBase::PathRole::None)
        addItem(kMenuBrowse, &dataBrowse, true);
    addItem(kMenuInsert, &dataInsert, editable);
    addItem(kMenuRemove, &dataRemove, editable);

    const theme::ColorScheme& s{*ctx.scheme};
    HBRUSH menuBg{CreateSolidBrush(ToColorRef(s.card.fill))};
    MENUINFO mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIM_BACKGROUND;
    mi.hbrBack = menuBg;
    SetMenuInfo(hMenu, &mi);

    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   screenX, screenY, 0, ctx.hwnd, nullptr);
    DestroyMenu(hMenu);
    DeleteObject(menuBg);
}

void ui::GridView::CopyToClipboard(const std::wstring& text)
{
    if (text.empty()) return;
    if (!OpenClipboard(m_edit ? GetParent(m_edit) : nullptr)) return;
    EmptyClipboard();
    const size_t bytes{(text.size() + 1) * sizeof(wchar_t)};
    HGLOBAL hMem{GlobalAlloc(GMEM_MOVEABLE, bytes)};
    if (hMem)
    {
        void* dst{GlobalLock(hMem)};
        if (dst)
        {
            memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        else
        {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

// ---------------------------------------------------------------------------
// Search bar
// ---------------------------------------------------------------------------

void ui::GridView::EnsureSearchControl(const ViewContext& ctx)
{
    if (m_searchEdit) return;
    m_searchEdit = CreateWindowExW(0, L"EDIT", L"",
                                    WS_CHILD | ES_AUTOHSCROLL,
                                    0, 0, 0, 0,
                                    ctx.hwnd, nullptr,
                                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(ctx.hwnd, GWLP_HINSTANCE)),
                                    nullptr);
    if (!m_searchEdit)
    {
        spdlog::error("search edit control creation failed");
        return;
    }
    // Subclass: Esc clears text, Down/Enter moves focus to grid.
    struct SearchSub {
        static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
        {
            HWND parent{reinterpret_cast<HWND>(ref)};
            constexpr UINT WM_APP_SEARCH_CLOSE{WM_APP + 2};
            constexpr UINT WM_APP_SEARCH_GRID{WM_APP + 3};
            if (msg == WM_KEYDOWN)
            {
                if (wp == VK_ESCAPE) { PostMessageW(parent, WM_APP_SEARCH_CLOSE, 0, 0); return 0; }
                if (wp == VK_DOWN || wp == VK_RETURN) { PostMessageW(parent, WM_APP_SEARCH_GRID, 0, 0); return 0; }
            }
            else if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE))
            {
                return 0; // suppress ding
            }
            return DefSubclassProc(h, msg, wp, lp);
        }
    };
    if (!SetWindowSubclass(m_searchEdit, SearchSub::Proc, 2, reinterpret_cast<DWORD_PTR>(ctx.hwnd)))
    {
        spdlog::error("SetWindowSubclass for search edit failed");
        DestroyWindow(m_searchEdit);
        m_searchEdit = nullptr;
        return;
    }
    RefreshEditFont(ctx);
    if (m_editFontName)
        SendMessageW(m_searchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFontName), TRUE);
    SendMessageW(m_searchEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(4, 4));
}

void ui::GridView::FocusSearch()
{
    if (!m_searchEdit) return;
    if (m_grid.IsEditing()) EndEdit(true);
    SetFocus(m_searchEdit);
    SendMessageW(m_searchEdit, EM_SETSEL, 0, -1);
}

void ui::GridView::ClearSearch(const ViewContext& ctx)
{
    if (!m_searchEdit) return;
    SetWindowTextW(m_searchEdit, L"");
    m_grid.SetFilter(L"");
    SetFocus(ctx.hwnd);
    InvalidateRect(ctx.hwnd, nullptr, FALSE);
}

void ui::GridView::OnSearchTextChanged(const ViewContext& ctx)
{
    if (!m_searchEdit) return;
    const int len{GetWindowTextLengthW(m_searchEdit)};
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    const int copied{GetWindowTextW(m_searchEdit, text.data(), len + 1)};
    text.resize(static_cast<size_t>(copied));
    m_grid.SetFilter(text);
    InvalidateRect(ctx.hwnd, nullptr, FALSE);
}

void ui::GridView::PositionSearchEdit(const ViewContext& ctx, const D2D1_RECT_F& bounds)
{
    if (!m_searchEdit) return;
    const float scale{ctx.dipScale};
    const auto px = [scale](float dip) { return static_cast<int>(dip * scale); };
    const float searchH{32.0f * ctx.zoom};
    const float padX{12.0f};
    const float padY{4.0f * ctx.zoom};

    MoveWindow(m_searchEdit,
               px(bounds.left + padX),
               px(bounds.top + padY),
               px(bounds.right - bounds.left - 2.0f * padX),
               px(searchH - 2.0f * padY), FALSE);
}
