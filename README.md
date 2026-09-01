# markdown-tool

> **English** · [繁體中文](README.zh-TW.md)

A minimal markdown viewer. **md4c + QTextBrowser — no browser engine, no JavaScript.**

It started because the Chrome extension "Markdown Reader" ships a 5.8 MB content script plus
0.98 MB of CSS, all of which is loaded into a Chromium renderer for every `.md` tab you open.

## Design

- **Display**: `QTextBrowser`, Qt's `QTextDocument` rich-text engine. QtWebEngine was deliberately
  ruled out: it plateaus at 150–250 MB and every tuning knob available saves only 10–20 MB.
  The reasoning is written up in
  `docs/superpowers/specs/2026-08-31-markdown-tool-design.md`.
- **Parsing**: a hand-written md4c callback renderer that intercepts mermaid fences, generates
  GitHub-style heading anchors, resolves relative image paths and rewrites the constructs Qt's
  rich text cannot render — all in one pass.
- **Mermaid**: rendered by the external `mmdc` to **PNG** and cached by SHA-1 under
  `~/.cache/markdown-tool/mermaid/`. The render queue is deliberately serialised: each mmdc run
  starts a headless Chromium (measured peak around 106 MB). When `mmdc` is absent the diagram is
  shown as a code block — that is a degrade path, not an error.

  **Why PNG and not SVG**: Qt's `QSvgRenderer` implements SVG Tiny 1.2, which has no `<marker>`.
  Rendering mermaid's SVG through Qt loses **every connector and arrowhead**, puts node labels at
  the top edge of their boxes and leaves a stray black triangle at the origin. Measured, counting
  dark pixels in the band between two nodes: **0** via Qt's SVG, **249** via Chromium's PNG.
  `svgOutputLosesEdgesInQt()` guards this — if a future Qt supports `<marker>`, that test fails.
- **Swappable engine**: `IRenderBackend` is a deliberate seam. If Qt's CSS subset ever proves
  insufficient, a litehtml backend is one new implementation away.

## Build

```
sudo apt install cmake g++-12 qt6-base-dev qt6-svg-dev \
                 libgl1-mesa-dev libglx-dev libopengl-dev
./build.sh
```

`build.sh` pins `g++-12`: Ubuntu 22.04's libstdc++6 runtime is 12.x, Qt 6.2.4 is built against it,
and linking with the default `g++-11` fails on a missing `GLIBCXX_3.4.30`.

Mermaid support additionally needs:

```
npm i -g @mermaid-js/mermaid-cli
```

## Install

The executable is named `mkdv`. Installing into `~/.local` needs no root, and
`~/.local/bin` is already on `PATH` on most desktops:

```
./build.sh
cmake --install build --prefix ~/.local
```

That places `~/.local/bin/mkdv` and a desktop entry at
`~/.local/share/applications/mkdv.desktop`, so `.md` files can also be opened from a file manager.
For a system-wide install use `sudo cmake --install build --prefix /usr/local`.

To uninstall, delete those two files; CMake writes no uninstall target.

The target and the application name stay `markdown-tool` — the settings directory
`~/.config/markdown-tool` is already in use, and renaming it would orphan existing settings.

## Usage

```
mkdv docs/sample.md          # installed
./build/mkdv docs/sample.md  # straight from the build directory
```

| Shortcut | Action |
|---|---|
| `Ctrl+L` | **Focus the path bar** (selects all, so you can just type over it) |
| `Ctrl+O` | Open via file dialog |
| `F5` | Reload |
| `Ctrl+W` | Close tab |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous tab |
| `Alt+1`…`Alt+9` | Jump to tab N |
| `Ctrl+Shift+1`…`4` | Pane count (1 = no split) |
| `Ctrl+Shift+←` / `→` | Move the tab to the left / right pane |
| `F9` | Show / hide the sidebar |
| `Alt+Shift+1` | White theme |
| `Alt+Shift+2` | Black theme |
| `Alt+Shift+T` | Toggle theme |
| `Ctrl+=` / `Ctrl++` | Zoom in |
| `Ctrl+-` | Zoom out |
| `Ctrl+0` | Original size |

The interface itself is in 繁體中文.

### Path bar

The bar across the top behaves like a browser's address bar:

- `Ctrl+L` focuses it and selects all; type a path and press Enter
- Entering a **directory** switches the sidebar to its Files tab and re-roots there, rather than
  trying to open the directory as markdown
- Accepts `~`, paths relative to the current document, and pasted `file://` URLs
- Completion comes from `QFileSystemModel`
- `Escape` restores the current path and returns focus to the document
- A path that does not exist is reported in the status bar; the open document is left alone

### Tabs and split panes

VS Code's editor group model: **every split pane has its own tab bar**, and a document belongs to
exactly one pane. That is what makes "which tab is in which pane" self-evident. The active pane
shows a thin accent line under its tab bar (hidden when there is only one pane).

Tab selection is separated by three cues at once: **selected** is the page background plus body
text, bold, with a top accent line; **unselected** sits one step back with secondary text.
`QTabBar`'s default selected state differs by only a faint shade, which is not enough to see where
focus is. A single tab is also capped in width, so one long title cannot stretch across the pane.

There are three ways to split:

1. The menu, or `Ctrl+Shift+1..4`, to choose a pane count
2. `Ctrl+Shift+←/→` to move the current tab to the neighbouring pane (creating one if needed)
3. **Dragging a tab to a pane's left or right edge** — it splits on that side; dropping in the
   middle merges it into that pane instead

Empty panes are removed automatically, and there are at most 4. Panes are **not** scroll-synced and
differences are **not** highlighted; this is side-by-side reading, not a diff tool.

Right-clicking a tab offers: close, close others, close all to the right, close this pane, move to
the right pane, move to the left pane.

Link navigation **replaces the content of the same tab** rather than opening a new one: an index
page with dozens of links would otherwise spawn dozens of tabs. New tabs come from the path bar,
the file tree or drag and drop.

### Drag and drop

Drop a markdown file or a directory on the window to open or re-root. When several items are
dropped at once, **a markdown file wins** over a directory.

### Sidebar

Two tabs: **Paragraphs** (the heading tree, which highlights the heading you are currently reading)
and **Files** (directories and markdown-like files only). Editing the open file externally reloads
it and preserves the scroll position.

## Typography

The baseline was taken by measuring the Chrome extension's actual computed styles — serving the
same `docs/sample.md` to it over a local HTTP server and reading `getComputedStyle` — rather than
by eye.

| Element | Treatment |
|---|---|
| Inline `` `code` `` | Its own magenta, a background chip, monospace |
| Links | Link colour plus an **underline** (not colour alone) |
| H1 / H2 | A 1px rule underneath, painted by hand |
| Blockquote | A 4px bar on the left, painted by hand; consecutive paragraphs share one bar |
| Tables | Horizontal rules only (`borderCollapse` plus per-row top borders), heavier under the header |
| Nested lists | `setIndentWidth(20)` |
| Heading sizes | 23 / 17 / 14 / 12.5 / 11.5 / 11 pt against 11 pt body; H6 uses the secondary colour |
| Line height | 155% (Qt's default is roughly single spacing, cramped for CJK); code blocks match |
| Zoom | 1.1 per step, 0.5×–3×; headings and body scale together |
| Paragraph spacing | 10px above and below; list items 0, lists 4px |

**Font sizes are never set through CSS.** Body size comes from
`QTextDocument::setDefaultFont()` and heading sizes from an explicit tree walk. Measured, a
stylesheet's `body { font-size }` has **no effect at all** on a QTextDocument, and for headings the
`QTextFormat::FontSizeAdjustment` property overrides `FontPointSize` whenever it is present — even
when set to 0 — which had H5 rendering *smaller than body text*. The walk clears that property
(setting it to 0 is not enough). `Theme::headingPointSize()` is the single definition, guarded by
`headingSizesFollowThemeScale()`.

**Heading rules and the blockquote bar are painted by hand**, because Qt rich text has no
block-level `border`. `MdTextBrowser::paintEvent()` adds them after the text and scans only the
visible block range. Blockquotes are identified by `blockFormat().leftMargin()` matching
`Theme::BlockquoteIndentPx` — a constant shared by the stylesheet and the painter, which
`blockquoteMarkerContractHolds()` pins. List items are excluded because they have a `textList()`,
headings because their left margin is 0.

## Theming

Two themes: **white** (`#ffffff`) and **black** (`#000000`), **black by default**.

The theme is applied unconditionally at startup rather than only on a switch. Otherwise the whole
application keeps the system GTK palette and clashes with the document colours — observed as a
slate-blue window that was neither theme.

Contrast is a hard requirement, not an aesthetic preference. Every pair is computed as a WCAG 2.1
ratio and pinned by tests: body text ≥ 7:1, secondary text and links ≥ 4.5:1, non-text elements
such as borders ≥ 3:1. `tests/test_theme.cpp` checks the theme colours, every `QPalette` role, the
tab states and every syntax-highlighting colour (208 pairs, extracted from real generated HTML).

Because markdown may embed arbitrary HTML, correct palette choices are not enough on their own.
Two runtime protections back them up:

1. **Text contrast fixup** — for each fragment, the WCAG ratio against its *effective* background
   (fragment → block → page) is computed, and anything below 4.5:1 has its foreground replaced with
   a colour readable there. This is what rescues `<span style="color:#000000">` on the black theme.
2. **Low-contrast image backdrop** — a transparent image whose content is too close in brightness
   to the page is composited onto a neutral card. Mermaid is exempt; its theme already matches.

The "對比保護" section of `docs/sample.md` is the corpus for these cases, and
`everyTextFragmentIsReadableInBothThemes()` sweeps the whole document in both themes.

A trap worth recording: setting only `Window`, `Base` and `Text` on the `QPalette` is not enough.
`QTabBar` and `QMenuBar` paint with `Button`/`ButtonText`, so on the black theme that produced
invisible tab labels and an invisible menu bar. Every role is set now, guarded by
`paletteHasNoDefaultLightRolesInBlackTheme()`.

## Performance

Mutations in the document tree walks (image sizing, heading scale, table styling, contrast fixup)
**must** be wrapped in a single `beginEditBlock()`/`endEditBlock()`, and the document must have undo
disabled (`setUndoRedoEnabled(false)`). Without both, every `setCharFormat` or `setFormat` triggers
a full re-layout and pushes an undo command, which is quadratic in the number of cells or fragments.

Measured: a 6.9 KB, 72-line file containing a 65-row table (325 cells) took **2146 ms** to open
before the fix and **23 ms** after. `wideTableOpensQuickly()` guards it with a synthetic 1505-cell
table.

## Memory

Measured as PSS across the whole process tree (`/proc/<pid>/smaps_rollup`). RSS is not used as the
headline number because it double-counts shared library pages.

| Case | PSS | RSS |
|---|---|---|
| Three-line file (baseline) | 33.5 MB | 66.4 MB |
| `docs/sample.md` (two mermaid diagrams) | 41.6 MB | 87.1 MB |
| Three tabs (including the 325-cell table document) | 43.7 MB | 92.2 MB |

Extra tabs are cheap — three cost only 2.1 MB more than one. The Qt libraries dominate; each
`QTextDocument` is comparatively small.

One setting matters a lot: `QT_XCB_GL_INTEGRATION=none`, set in `src/main.cpp`. The xcb QPA's GL
integration drags in Mesa's llvmpipe and `libLLVM`, **13.2 MB of PSS on its own**, for an
application that paints entirely through the raster engine. Disabling it took the baseline from
49.1 MB to 32.7 MB.

## Tests

```
ctest --test-dir build --output-on-failure
```

140 test functions across 8 suites:

| Suite | Functions | Covers |
|---|---|---|
| markdownparser | 15 | Anchors, mermaid extraction, escaping, image paths |
| codehighlighter | 8 | Per-language colouring, fallback, unterminated strings |
| mermaidcache | 8 | Key sensitivity, queue serialisation, degrade path |
| mmdc_integration | 9 | Really runs mmdc; the SVG-vs-PNG connector ink differential |
| theme | 17 | WCAG thresholds: colours, palette roles, syntax, inline code, tab states |
| e2e_viewer | 26 | Drives a real MainWindow; path bar, theme and drag-and-drop flows |
| e2e_regression | 29 | Pins pipeline and styling invariants against sample.md / headings.md |
| e2e_tabs | 28 | Tabs, split panes, drag-split, geometry invariants, context menu |

The e2e suites run under `QT_QPA_PLATFORM=offscreen`, so no X or Wayland is required. When `mmdc`
is missing the integration suite and the mermaid e2e test skip themselves rather than failing.

For visual checks — automated assertions verify structure, not whether something *looks* right:

```
MD_E2E_DUMP=/tmp/shots QT_QPA_PLATFORM=offscreen ./build/test_e2e_regression
```

That writes the top, code, mermaid, table and contrast sections in both themes. A different
document and set of sections can be aimed at:

```
MD_E2E_DUMP=/tmp/shots MD_E2E_DOC=docs/headings.md \
  MD_E2E_ANCHORS=h1-文件標題,h2-混合內容 \
  QT_QPA_PLATFORM=offscreen ./build/test_e2e_regression
```

**Not covered automatically**: the real X11 drag gestures (dragging a tab between panes, dropping a
file on the window). The logic behind both is exercised through `DocumentArea::moveTabToPane()` and
`MainWindow::openFromUrls()`, and the wiring is asserted separately, but the gesture itself needs a
human.

## Specifications

`openspec/specs/<domain>/spec.md` is the source of truth for behaviour, written to the OpenSpec
conventions (`### Requirement:` / `#### Scenario:` with GIVEN/WHEN/THEN). Each has a
`spec.zh-TW.md` companion; both must be updated together, and
`openspec/check-bilingual.sh` checks they stay structurally in step.

| Domain | Scope |
|---|---|
| `markdown-parsing` | md4c callback renderer, anchors, highlighting, Qt compatibility |
| `document-rendering` | QTextBrowser backend, layout, images, contrast, zoom, performance |
| `theming` | White/black themes, WCAG guarantees, palette, tab states |
| `mermaid-diagrams` | External mmdc rendering, the PNG-over-SVG decision, cache, degradation |
| `workspace` | Tabs, split panes, path bar, sidebar, watching, drag and drop, persistence |

`docs/superpowers/specs/2026-08-31-markdown-tool-design.md` is the **development record** — the
decisions, the measurements, the traps that were hit. The two complement each other: the specs say
what the behaviour *is*, the design document says *why it ended up that way*.

## Conventions

- **Code comments are English.** A handful of hard-won traps carry a one-line `中：` summary as
  well, because those are the notes people come back to.
- **User-facing strings are 繁體中文** — menus, dialogs, the status bar.
- **Documentation is bilingual**: `README.md` / `README.zh-TW.md` and
  `spec.md` / `spec.zh-TW.md`. English is canonical, since RFC 2119's SHALL/SHOULD are English
  keywords.
- **Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/)**, in
  English, checked with commitlint:

  ```
  npx --yes --package @commitlint/cli --package @commitlint/config-conventional \
    commitlint --from <ref> --to HEAD
  ```

  Two conventions worth knowing: merge commits use `chore:` so a generated changelog does not
  list the same work twice, and subjects start lower case because config-conventional's
  `subject-case` rule forbids sentence-case.

### Enforcement

The conventions above are checked, not just written down:

| Layer | What it runs | Bypassable? |
|---|---|---|
| `scripts/check-repo.sh` | six repository checks, by hand | yes, by not running it |
| `.githooks/pre-commit`, `.githooks/commit-msg` | the same checks, plus the commit message | yes, `--no-verify` |

Enable the hooks with `./scripts/setup-dev.sh`; `build.sh` calls it, so building is enough. The
hook checks the commit message with `scripts/check-commit-msg.py` rather than commitlint, so that
committing works offline; run the commitlint command above by hand before pushing an unusual one.

There is no CI. The project is built and verified locally, which means the checks are only as good
as the habit of running them — `./scripts/check-repo.sh` and the test suite before calling a change
done.

Each of the six checks exists because that exact problem happened here: Chinese left in code
comments, internal identifiers in a public repository, an absolute home path hard-coded in a test,
bilingual specs drifting apart, READMEs losing their cross-links, and a documented test count that
was wrong three times because it was written from memory.

[AGENTS.md](AGENTS.md) states the conventions for anyone — human or agent — picking the project up.

## Status

v0.2. Verified under both real X11 and offscreen.

Known gap: `.txt` files are parsed as markdown, so a plain-text report gets reflowed into a wall of
text (soft line breaks are whitespace in markdown). Plain text should keep its line breaks and use
a monospaced face.
