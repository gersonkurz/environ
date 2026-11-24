# Viewing and editing

[← Back to contents](index.md)

## Sorting and filtering

- Click the **NAME** or **VALUE** column header to sort the entire grid alphabetically
  (click again to reverse). All scopes merge into one A–Z list; the per-row icon keeps each
  variable's origin unmistakable.
- Drag the thin vertical splitter between **NAME** and **VALUE** to resize the frozen name
  column.
- Press `Ctrl+F` (or click the **Filter** box in the title bar) to filter by substring.
  When a filter matches a path-list segment rather than the first entry, Environ promotes
  the variable's name onto the first matching segment so you never lose context. Press `Esc`
  inside the filter box to clear it and return focus to the grid.

## Excel-style inline editing

Environ uses a single inline editor directly inside the grid cell:

- Press **`F2`** or **double-click** any editable value cell to edit it in place.
- **Double-click** a variable's name cell to rename the variable.
- Press **`Enter`** to commit, **`Esc`** to cancel.
- Press **`Tab`** / **`Shift+Tab`** to commit and jump straight into the next or previous
  editable cell (automatically skipping read-only rows).
- Attempting to edit a read-only row shows the reason on the right side of the status bar.

## Working with `PATH` and path lists

Semicolon-separated path variables (`PATH`, `PSModulePath`, `PATHEXT`, etc.) expand into
**one row per segment**:

- **`Ins`** — inserts a blank segment below the current entry and opens the inline editor.
- **`Del`** — removes the selected segment.
- **`Alt+↑` / `Alt+↓`** — moves the selected segment up or down within its variable.

In-place edits preserve the original separator structure in the registry; structural edits
(insert, delete, reorder) cleanly re-join the segments.

## Creating and deleting variables

- Click **`+ New ▾`** in the title bar, use **`Edit → New User Variable`** /
  **`Edit → New Machine Variable`**, or right-click any row.
- Press **`Ins`** on a scalar row to add a new variable in the current editable scope.
- Use **`Edit → Delete Variable`** or the right-click context menu to remove a variable
  (including all of its path segments).

## Browsing and revealing in Explorer

Select any path-list entry or folder/file variable to reveal a small icon button at the
right edge of the value cell:

- **Editable folder/file rows** open a native Windows folder or file picker. If the original
  value used `%USERPROFILE%` (or another `%VAR%` prefix) and you pick a subfolder inside it,
  Environ preserves the `%VAR%` prefix instead of flattening it to an absolute path.
- **Read-only rows** open or select the expanded path directly in File Explorer.

## Native row tooltips and diagnostics

Hover over any row to see a native Windows tooltip with:

- **Expanded value** (whenever the raw value contains `%VAR%` references)
- **Missing path warning** (rows whose target folder does not exist are also highlighted in red)
- **Duplicate entry warning** (flagged in orange, naming where the duplicate appears)
- **Shadowed warning** (*"Not in effect — overridden by Windows"*, showing the effective value)
- **Variable description & read-only notes** from the built-in knowledge base
