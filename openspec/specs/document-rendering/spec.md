# Document Rendering

Display of a parsed `Document`. Implemented in `src/render/TextBrowserBackend.cpp` on top of
`QTextBrowser` (Qt's `QTextDocument` rich-text engine) — **no browser engine, no JavaScript**.
`IRenderBackend` is a deliberate seam: if Qt's CSS subset ever proves insufficient, a litehtml
backend can be added without touching the parser, the sidebar or the window.

Qt's rich text supports neither `max-width` nor block-level borders, and it ignores several CSS
properties silently. Everything this spec calls a "document tree walk" happens after `setHtml()`
and compensates for those gaps.

## Layout

### Requirement: Content column is capped and centred
The reading column SHALL be capped at 980 logical pixels (`Theme::ContentWidth`, the value used
by Okular's markdown generator) and centred in the viewport. Because Qt rich text has no
`max-width`, this SHALL be done with viewport margins.

#### Scenario: Wide window
- GIVEN a window 1400 px wide showing one document
- WHEN the layout settles
- THEN the text viewport is no wider than 990 px

### Requirement: Comfortable line height
Body text, list items and table cells SHALL use a line height of 155 % (`Typography::LineHeightPercent`).
Qt's default is approximately single spacing, which is too tight for CJK text.

#### Scenario: Paragraph line height
- GIVEN a rendered document
- WHEN its paragraph blocks are inspected
- THEN each has `lineHeightType == ProportionalHeight` and `lineHeight == 155`

### Requirement: Font sizes are set explicitly, not through CSS
Body point size SHALL be applied with `QTextDocument::setDefaultFont()` and heading point sizes
with an explicit tree walk. Neither SHALL rely on stylesheet `font-size`.

Two Qt behaviours make CSS unusable here, both established by measurement:
`body { font-size }` has no effect on a `QTextDocument` at all, and for headings the
`QTextFormat::FontSizeAdjustment` property overrides `FontPointSize` whenever it is present —
even when its value is 0. The walk SHALL therefore `clearProperty()` that property (setting it to
0 is not enough) and use `setCharFormat`, since `mergeCharFormat` cannot remove a property.

#### Scenario: Heading scale
- GIVEN a document containing H1 through H6
- WHEN it is rendered
- THEN the rendered point sizes are 23, 17, 14, 12.5, 11.5 and 11, none of them carries a
  `FontSizeAdjustment` property, all are bold, the scale is strictly decreasing, and H6 is not
  smaller than the 11 pt body text

## Images

### Requirement: Images are sized from their intrinsic dimensions
After `setHtml()`, the backend SHALL walk the document and give every image an explicit display
size derived from its intrinsic size, scaled down proportionally when wider than the content
column. If the layout has not run yet (for example the view was hidden when the document loaded)
the walk SHALL request the resource explicitly so the size is known.

#### Scenario: Every image has a positive size
- GIVEN `docs/sample.md` rendered
- WHEN its image fragments are inspected
- THEN each has width and height greater than 0 and width no greater than 980

### Requirement: A missing image degrades to text
An image whose file cannot be loaded SHALL be replaced by italic text naming the missing file,
not left as an empty box.

#### Scenario: Broken image reference
- GIVEN a document containing `![替代文字](nope.png)` where the file does not exist
- WHEN it is rendered
- THEN the plain text contains `[缺少圖片: nope.png]`

### Requirement: Low-contrast images get a backdrop
An image with an alpha channel whose visible pixels average too close to the page background
SHALL be composited onto a neutral card with padding, so that it stays visible. Mermaid diagrams
are exempt: their theme is chosen by the application and already matches the page.

#### Scenario: Dark line art on a black page
- GIVEN a transparent PNG whose visible pixels are dark
- WHEN it is rendered on the black theme
- THEN it is drawn on a light card rather than disappearing

## Readability enforcement

### Requirement: No text may be unreadable against its background
For every text fragment the backend SHALL compute the WCAG contrast ratio between its foreground
and its **effective** background — the fragment's own background, else the block's, else the page
colour — and SHALL replace the foreground with a readable colour when the ratio is below 4.5:1.
The replacement SHALL use `mergeCharFormat` so that font, weight and other formatting survive.

This exists because markdown may embed raw HTML with hard-coded colours, which no palette choice
can protect against.

#### Scenario: Hard-coded black text on the black theme
- GIVEN a document containing `<span style="color:#000000">隱形候選</span>`
- WHEN it is rendered on the black theme
- THEN the fragment's rendered foreground has a contrast ratio of at least 4.5:1 against black

#### Scenario: Whole-document invariant
- GIVEN `docs/sample.md`, which deliberately contains hard-coded colours
- WHEN it is rendered on either theme
- THEN no text fragment has a contrast ratio below 4.5:1 against its effective background

## Structures Qt cannot draw

### Requirement: Headings and blockquotes are painted by hand
Qt rich text has no block-level `border-bottom` or `border-left`. The H1/H2 underline and the
blockquote bar SHALL therefore be painted in `paintEvent()` after the text, and SHALL scan only
the visible block range rather than the whole document.

Blockquote blocks SHALL be identified by `blockFormat().leftMargin()` equalling
`Theme::BlockquoteIndentPx`, a constant shared by the stylesheet and the painter. This is a
contract: list items are excluded because they have a `textList()`, and headings because their
left margin is 0. Consecutive blockquote blocks SHALL be merged into one bar.

#### Scenario: Blockquote marker contract
- GIVEN a rendered document containing a two-paragraph blockquote
- WHEN block formats are inspected
- THEN exactly the two blockquote blocks have `leftMargin == Theme::BlockquoteIndentPx` and no
  `textList()`, and every heading block has `leftMargin == 0`

### Requirement: Tables use horizontal rules only
Tables SHALL be drawn with horizontal rules and no vertical or outer borders, with a heavier rule
under the header row and 8 px cell padding. This SHALL use Qt's native cell borders, which
require `QTextTableFormat::setBorderCollapse(true)` — without that call Qt does not render
cell-level borders at all.

#### Scenario: Table borders
- GIVEN a rendered table
- WHEN its format is inspected
- THEN `border() == 0`, `borderCollapse()` is true, `cellPadding() == 8`, every cell has zero left
  and right borders, the header row has a bottom border of at least 2, and every other row has a
  top border of 1

## Zoom

### Requirement: Zoom scales headings and body together
Zoom SHALL be implemented as an explicit factor owned by the backend — 1.1 per step, clamped to
0.5×–3.0× — applied to both the document's default font and the explicit heading sizes, and
SHALL NOT use `QTextEdit::zoomIn()`, which only changes the widget font and therefore leaves
explicitly sized headings untouched.

#### Scenario: Proportional scaling
- GIVEN a rendered document
- WHEN zoom in is triggered twice
- THEN both body and H1 grow, and their growth ratios differ by less than 2 %

#### Scenario: Clamping and reset
- GIVEN zoom in triggered 40 times and then zoom out triggered 80 times
- WHEN the sizes are inspected
- THEN they never exceed 3× nor fall below 0.5× of the base size, and reset restores the original

## Performance

### Requirement: Document mutations are batched
Every document tree walk SHALL wrap its mutations in a single
`beginEditBlock()`/`endEditBlock()`, and the document SHALL have undo disabled
(`setUndoRedoEnabled(false)`). Without both, each `setCharFormat` or `setFormat` triggers a full
re-layout and pushes an undo command, which is quadratic in the number of cells or fragments.

Measured: a 6.9 KB, 72-line file containing a 65-row table (325 cells) took 2146 ms to open
before this change and 23 ms after.

#### Scenario: Wide table
- GIVEN a synthetic document with a table of at least 1500 cells
- WHEN it is opened
- THEN it completes in under 1500 ms
