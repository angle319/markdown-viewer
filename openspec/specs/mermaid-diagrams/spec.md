# Mermaid Diagrams

Mermaid rendering without embedding a JavaScript engine. The application shells out to
`mmdc` (mermaid-cli) once per unique diagram, caches the result on disk, and displays it as an
ordinary image. Implemented in `src/core/MermaidCache.cpp` and `src/core/MmdcRenderer.cpp`.

The steady-state cost of a rendered diagram is one cached image file; the browser engine that
mmdc uses lives in a separate short-lived process and never sits inside the viewer.

## Output format

### Requirement: Diagrams are rendered to PNG, not SVG
`MmdcRenderer` SHALL default to PNG output rasterised by mmdc itself. SVG output remains
selectable but is known to be incompatible with Qt.

Qt's `QSvgRenderer` implements SVG Tiny 1.2, which has no `<marker>`. Rendering mermaid's SVG
through Qt loses every connector and arrowhead, places node labels at the top edge of their
boxes, draws grey blocks beside edge labels, and leaves a stray black triangle at the origin.

Measured on `flowchart LR  A[AAAAAAAA] --> B[BBBBBBBB]`, counting dark pixels in the central
horizontal band between the two nodes — a region only a connector can occupy:

| Path | Ink in the central band |
|---|---|
| SVG via `QSvgRenderer` | **0** |
| PNG via Chromium | **249** |

#### Scenario: PNG draws the connectors
- GIVEN the two-node flowchart rendered with the default settings
- WHEN the central band of the resulting image is measured
- THEN it contains connector pixels

#### Scenario: The SVG path is still broken
- GIVEN the same diagram rendered with SVG output selected
- WHEN the central band is measured after rasterising through `QSvgRenderer`
- THEN it contains at least three times less ink than the PNG path, while the image as a whole is
  not blank — i.e. the shapes render but the connectors do not

This scenario is a tripwire: if a future Qt supports `<marker>`, it fails, and SVG can be
reconsidered.

### Requirement: HTML labels must be disabled
The renderer SHALL pass a mermaid config with `htmlLabels: false`. By default mermaid puts node
text inside a `<foreignObject>`, which Qt does not support at all, producing diagrams whose
shapes render but whose text is entirely blank.

#### Scenario: No foreignObject
- GIVEN a diagram with CJK labels rendered in SVG mode
- WHEN the output is inspected
- THEN it contains no `foreignObject`, contains real `<text>` elements, and contains the CJK
  label text

### Requirement: Rasterisation follows the display
PNG output SHALL be rasterised at the screen's device pixel ratio. Rendering at 2× on a 1× display
wastes memory for no visible benefit (measured: about 10 MB of PSS for two diagrams).

#### Scenario: Scale affects pixel size
- GIVEN the same diagram rendered at scale 1 and at scale 2
- WHEN both images are loaded
- THEN the second is twice as wide

## Cache

### Requirement: Cache keys cover content, theme and renderer
The cache key SHALL be `sha1(source + theme + rendererId)`, where `rendererId` includes the mmdc
version and the rasterisation scale. Theme is part of the key because light and dark diagrams
differ; the version and scale are part of it because their output differs.

#### Scenario: Key sensitivity
- GIVEN one diagram source
- WHEN keys are computed
- THEN different sources give different keys, the light and dark keys differ, and repeated calls
  with the same inputs give the same key

#### Scenario: A cache hit does not re-render
- GIVEN a diagram already present in the cache
- WHEN it is requested again
- THEN no renderer process is started and the cached file's modification time is unchanged

### Requirement: Renders are serialised
The cache SHALL run at most one render at a time. Each mmdc invocation starts a headless Chromium
(measured peak around 106 MB); running them in parallel for a document with several diagrams
would cancel out the memory advantage the whole design exists for.

#### Scenario: Three diagrams
- GIVEN three uncached diagrams requested in quick succession
- WHEN they are rendered
- THEN all three complete and the number of concurrently active renders never exceeds one

### Requirement: A failed render leaves no cache entry
On failure the partial output file SHALL be removed, so the next request retries rather than
treating a truncated file as a hit.

#### Scenario: Renderer failure
- GIVEN a renderer that fails
- WHEN a diagram is requested
- THEN a failure is reported and the cache reports the diagram as not cached

## Degradation

### Requirement: A missing mmdc is not an error
When `mmdc` cannot be found, mermaid fences SHALL be displayed as ordinary code blocks and the
status bar SHALL state the install command once. No error dialog, no failure signal.

#### Scenario: mmdc absent
- GIVEN a renderer reporting itself unavailable
- WHEN a diagram is requested
- THEN nothing is queued and neither a `rendered` nor a `failed` signal is emitted

### Requirement: mmdc is found outside PATH
Discovery SHALL check `MARKDOWN_TOOL_MMDC`, then `PATH`, then the nvm install locations under
`~/.nvm/versions/node/*/bin`. A desktop launcher's environment usually does not include nvm.

#### Scenario: Launched from a desktop entry
- GIVEN a session whose PATH lacks the nvm bin directory
- WHEN the renderer looks for mmdc
- THEN it still finds the nvm-installed executable

### Requirement: A render in flight does not outlive the window
On destruction the renderer SHALL terminate any running process and wait for it, rather than
letting `QProcess` be destroyed while running.

#### Scenario: Quit during a render
- GIVEN a render in progress
- WHEN the application closes
- THEN no "QProcess: Destroyed while process is still running" warning is emitted

## Display

### Requirement: Diagrams show a placeholder until ready
An uncached diagram SHALL display a placeholder immediately and SHALL be replaced by the real
image when the render completes, without losing the scroll position.

#### Scenario: First view of a diagram
- GIVEN a document with one uncached mermaid diagram
- WHEN it is opened
- THEN a placeholder of fixed height appears first, and is later replaced by an image of a
  different height that fits within the content column
