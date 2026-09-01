# Workspace

Everything around the document itself: tabs, split panes, the path bar, the sidebar, file
watching, drag & drop and persistence. Implemented in `src/DocumentView.cpp` (one document),
`src/PaneGroup.cpp` (one pane and its tabs), `src/DocumentArea.cpp` (the panes) and
`src/MainWindow.cpp` (the shell).

The split model is VS Code's editor groups: a document belongs to exactly one pane, and each pane
shows its own tab bar. An earlier design with a single global tab bar was rejected after use —
with the bar above the leftmost pane only, there was no way to tell which tab belonged to which
pane.

## Documents and tabs

### Requirement: One open file, one tab
Opening a file SHALL create a tab in the active pane. Opening a file that is already open — in
any pane — SHALL switch to its existing tab instead of creating a second one. Path comparison
SHALL use absolute paths.

#### Scenario: Two files
- GIVEN two different files opened
- WHEN the tabs are inspected
- THEN there are two tabs holding two distinct documents

#### Scenario: Reopening
- GIVEN a file already open in the first tab, with the second tab active
- WHEN the same file is opened again, including via a non-canonical path such as `./a.md`
- THEN no tab is added and the first tab becomes active

### Requirement: Tab titles come from the document
A tab's title SHALL be the document's first H1, falling back to the file name when there is none,
and SHALL follow the document when it changes.

#### Scenario: Heading and fallback
- GIVEN one file with `# 我的標題` and one with no heading
- WHEN both are opened
- THEN the tabs read `我的標題` and the second file's name, and both tooltips carry the full path

### Requirement: Closing the last tab leaves a usable empty state
Closing every tab SHALL clear the window title, the path bar and the table of contents, and SHALL
show a hint naming the ways to open a file. Opening a file afterwards SHALL work normally.

#### Scenario: Close the only tab
- GIVEN one open document
- WHEN its tab is closed
- THEN there is no active view, the window title is `markdown-tool`, the TOC is empty, and a
  visible hint mentions opening a file

### Requirement: Each document watches its own file
Every document SHALL watch its own file and reload on change with a 150 ms debounce, preserving
the scroll position, whether or not it is the active tab. Watching SHALL survive an editor's
atomic save (write to a temporary file, then rename over the original).

#### Scenario: Background tab is edited
- GIVEN two documents open with the second active
- WHEN the first document's file gains a heading on disk
- THEN the first document reloads and its TOC grows, while the active tab is unaffected

### Requirement: Link navigation stays in the same tab
Following a link to another markdown file SHALL replace the content of the tab the link was
clicked in, rather than opening a new tab. An index page with dozens of links would otherwise
spawn dozens of tabs. New tabs come from the path bar, the file tree or drag & drop.

#### Scenario: Relative link
- GIVEN a document containing a relative link to another markdown file
- WHEN the link is activated
- THEN the tab count is unchanged, the tab's title and the path bar update to the target

## Split panes

### Requirement: Each pane owns its tab bar
Every pane SHALL display a tab bar listing exactly the documents in that pane. The active pane
SHALL be marked with an accent line under its tab bar, shown only when more than one pane exists.

#### Scenario: Two panes
- GIVEN three documents split into two panes
- WHEN the panes are inspected
- THEN each has its own tab bar whose entries match its own documents, no document is lost, and
  exactly one pane shows the active marker

### Requirement: Pane count is bounded by content
There SHALL be at most 4 panes and never more panes than documents, so no pane is ever empty.
Reducing the count SHALL merge documents back rather than close them.

#### Scenario: More panes than documents
- GIVEN two documents
- WHEN four panes are requested
- THEN there are two panes

#### Scenario: Merging back
- GIVEN two documents in two panes
- WHEN one pane is requested
- THEN there is one pane containing both documents

### Requirement: Dragging a tab splits or merges
Dragging a tab out of its tab bar SHALL start a cross-pane drag. Dropping on the left or right
quarter of a pane (at least 40 px) SHALL create a new pane on that side; dropping in the middle
SHALL move the tab into that pane. A drop target SHALL show an indicator during the drag.

Qt's `QTabBar` only reorders within itself, so the drag SHALL be initiated manually when the
cursor leaves the bar **vertically** — horizontal movement is left to Qt's own reordering.

#### Scenario: Drop on an edge
- GIVEN two documents in one pane
- WHEN a tab is dropped on the right quarter of that pane
- THEN a second pane is created on the right holding that document

#### Scenario: Drop in the middle of another pane
- GIVEN one document in each of two panes
- WHEN the right pane's tab is dropped in the middle of the left pane
- THEN both documents are in the left pane and the empty right pane is removed

> The real X11 drag gesture is not covered by automated tests; the drop logic is exercised
> through `DocumentArea::moveTabToPane()`, and the wiring is asserted separately.

### Requirement: Panes stay usable in size
Panes SHALL have a minimum width of 240 px and SHALL be given comparable widths after any
structural change. `QSplitter` gives a newly inserted widget only its size hint, which left
drag-created panes about 150 px wide — narrow enough to wrap tables and inline code one character
per line.

#### Scenario: Three panes
- GIVEN three documents split into three panes in a 1400 px window
- WHEN their widths are measured
- THEN none is below 200 px and the widest is no more than twice the narrowest

### Requirement: Structural changes leave no stale paint
After any structural change the area SHALL explicitly repaint its panes. X11 does not reliably
deliver expose events after a drop, which left the previous frame's content visibly overlapping
the tab bar.

#### Scenario: Geometry invariants
- GIVEN any sequence of splits, merges and tab moves
- WHEN the panes are inspected
- THEN every document is inside some pane's stacked widget, only each pane's current document is
  visible, and every visible document's top edge is below its pane's tab bar

### Requirement: Tabs offer a context menu
Right-clicking a tab SHALL offer: close, close others, close all to the right, close this pane,
move to the right pane, move to the left pane. Entries that cannot apply SHALL be disabled.

#### Scenario: Disabled entries
- GIVEN three documents in a single pane, right-clicking the middle tab
- WHEN the menu is built
- THEN close-others and close-to-the-right are enabled, close-this-pane is disabled, and for the
  last tab close-to-the-right is disabled

## Navigation

### Requirement: The path bar behaves like an address bar
The path bar SHALL show the active document's path, SHALL be focused and fully selected by
`Ctrl+L`, SHALL open the entered path on Enter, and SHALL restore the current path and return
focus to the document on Escape. It SHALL accept `~`, paths relative to the current document's
directory, and `file://` URLs. Completion SHALL be offered from the file system.

#### Scenario: Path resolution
- GIVEN the path bar
- WHEN `~`, `~/x.md`, `other.md` (relative to `/tmp/base`), `../up.md` (relative to
  `/tmp/base/sub`), a padded absolute path, and `file:///tmp/x.md` are resolved
- THEN they produce the home directory, `<home>/x.md`, `/tmp/base/other.md`, `/tmp/base/up.md`,
  the trimmed absolute path, and `/tmp/x.md`

#### Scenario: A directory is not opened as markdown
- GIVEN a directory typed into the path bar
- WHEN it is submitted
- THEN the sidebar switches to its Files tab rooted at that directory and the current document
  stays open

#### Scenario: A missing path is reported, not destructive
- GIVEN a path that does not exist
- WHEN it is submitted
- THEN the status bar says so and the current document stays open

### Requirement: The sidebar has a TOC and a file tree
The sidebar SHALL have two tabs. The Paragraphs tab SHALL show the active document's headings
nested by level, scroll to a heading when clicked, and highlight the heading currently at the top
of the viewport. The Files tab SHALL show only directories and markdown-like files
(`.md`, `.markdown`, `.mdx`, `.mdc`, `.mkd`, `.txt`).

#### Scenario: Heading tree
- GIVEN a document with one H1, three H2s and an H3 under the first H2
- WHEN the TOC is built
- THEN it has one top-level entry with three children, the first of which has one child

#### Scenario: File filter
- GIVEN a directory containing `main.md`, `other.md`, `notes.txt` and `ignore.cpp`
- WHEN the Files tab is rooted there
- THEN it lists the three markdown-like files and not `ignore.cpp`

### Requirement: Files can be dropped onto the window
Dropping a markdown file SHALL open it; dropping a directory SHALL re-root the Files tab. When
several items are dropped, a markdown file SHALL take priority over a directory. Dropping
anything else SHALL report it in the status bar without closing the current document.

Child widgets SHALL NOT accept drops, or the event never reaches the window.

#### Scenario: Mixed drop
- GIVEN a drop containing both a directory and a markdown file
- WHEN it is handled
- THEN the markdown file is opened

> As with tab dragging, the real X11 drop gesture is not covered automatically; the logic is
> exercised through `MainWindow::openFromUrls()` and the child `acceptDrops` flags are asserted.

## Persistence

### Requirement: The session is restored
Window geometry, splitter sizes, theme, sidebar visibility and tab, file-tree root, pane count,
the list of open documents and the active one SHALL be saved on close and restored on start.
Command-line arguments SHALL take precedence and MAY name several files, one tab each.

#### Scenario: Restart
- GIVEN a session closed with three documents open
- WHEN the application starts with no arguments
- THEN the same three documents are reopened

## Shortcuts

### Requirement: Documented shortcuts are bound
The application SHALL bind: `Ctrl+L` path bar, `Ctrl+O` open dialog, `F5` reload, `Ctrl+W` close
tab, `Ctrl+Tab`/`Ctrl+Shift+Tab` next/previous tab, `Alt+1`–`Alt+9` jump to tab, `Ctrl+Shift+1`–`4`
pane count, `Ctrl+Shift+←`/`→` move tab between panes, `F9` sidebar, `Alt+Shift+1`/`2` white/black
theme, `Alt+Shift+T` toggle theme, `Ctrl+=`/`Ctrl++` zoom in, `Ctrl+-` zoom out, `Ctrl+0` reset.

### Requirement: Zoom in is reachable on an ordinary keyboard
Zoom in SHALL bind `Ctrl+=` in addition to `QKeySequence::ZoomIn`. On Linux that standard
sequence resolves to `Ctrl++`, which requires `Ctrl+Shift+=` on most layouts — so binding it
alone means the natural keystroke does nothing.

#### Scenario: Ctrl+= is bound
- GIVEN the zoom-in action
- WHEN its shortcuts are inspected
- THEN they include both `Ctrl+=` and `QKeySequence::ZoomIn`
