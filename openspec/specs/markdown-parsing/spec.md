# Markdown Parsing

Conversion of markdown source into the HTML subset that Qt's rich-text engine understands.
Implemented in `src/core/MarkdownParser.cpp` on top of a vendored copy of md4c 0.5.2
(`third_party/md4c/`). The parser deliberately implements its own `MD_PARSER` callbacks
rather than calling md4c's bundled `md_html()`, because mermaid interception, heading anchors,
syntax highlighting and image path rewriting all have to happen in the same pass — doing them
by post-processing an HTML string with regular expressions is fragile.

Syntax highlighting lives in `src/core/CodeHighlighter.cpp` and emits inline `<span style>`
colouring; it has no external dependency.

## Document structure

### Requirement: Heading extraction
The parser SHALL emit one `TocEntry` per heading, carrying its level (1–6), its plain text with
markup stripped, and a slug anchor. The document title SHALL be the text of the first H1, or
empty when the document has no H1.

#### Scenario: Nested headings
- GIVEN a document containing `# One`, `## Two`, `### Three`
- WHEN it is parsed
- THEN the TOC has three entries with levels 1, 2 and 3 and texts `One`, `Two`, `Three`

#### Scenario: Title falls back to empty
- GIVEN a document whose only heading is `## sub`
- WHEN it is parsed
- THEN `Document::title` is empty and the caller uses the file name instead

### Requirement: GitHub-compatible anchors
Heading slugs SHALL follow GitHub's rules: lower-cased, whitespace replaced by `-`, punctuation
dropped, `-` and `_` preserved, and non-ASCII characters (including CJK) kept as-is. Repeated
slugs SHALL be disambiguated with the suffixes `-1`, `-2`, … in document order.

#### Scenario: Punctuation and spacing
- GIVEN the heading `## Hello, World! (v2)`
- WHEN it is parsed
- THEN the anchor is `hello-world-v2`

#### Scenario: CJK is preserved
- GIVEN the headings `## 安裝說明` and `## 使用 方式`
- WHEN they are parsed
- THEN the anchors are `安裝說明` and `使用-方式`

#### Scenario: Duplicate headings
- GIVEN three headings all named `重複`
- WHEN they are parsed
- THEN the anchors are `重複`, `重複-1`, `重複-2`

## Qt rich-text compatibility

Qt's rich text supports only a subset of HTML. Constructs it cannot render SHALL be rewritten at
parse time rather than emitted and hoped for.

### Requirement: Unsupported tags are rewritten
Strikethrough SHALL be emitted as `<s>`, never `<del>`. Task-list items SHALL be emitted as the
characters `☑` (U+2611) and `☐` (U+2610); the parser SHALL NOT emit `<input type="checkbox">`,
which Qt does not render.

#### Scenario: Strikethrough
- GIVEN the source `~~gone~~`
- WHEN it is parsed
- THEN the HTML contains `<s>gone</s>` and does not contain `<del>`

#### Scenario: Task list
- GIVEN the source `- [x] done` and `- [ ] todo`
- WHEN it is parsed
- THEN the HTML contains `☑` and `☐` and contains no `<input`

### Requirement: Text is escaped
Text content SHALL be HTML-escaped so that markup inside the document is displayed literally
rather than interpreted.

#### Scenario: Angle brackets and ampersands
- GIVEN the source `a < b & c > d`
- WHEN it is parsed
- THEN the HTML contains `a &lt; b &amp; c &gt; d`

#### Scenario: Script tag inside a fenced code block
- GIVEN a fenced code block containing `<script>alert(1)</script>`
- WHEN it is parsed and rendered
- THEN the tag is displayed as literal text and never interpreted

## Images and links

### Requirement: Relative image paths are resolved
Image sources that are relative SHALL be resolved against the directory of the markdown file and
rewritten to an absolute `file://` URL, with percent-encoding decoded first. Sources that already
carry a scheme SHALL be left untouched.

#### Scenario: Relative path
- GIVEN a document at base directory `/tmp/base` containing `![alt](sub/pic.png)`
- WHEN it is parsed
- THEN the HTML contains `src="file:///tmp/base/sub/pic.png"`

#### Scenario: Absolute URL
- GIVEN `![a](https://example.com/y.png)`
- WHEN it is parsed
- THEN the `src` is unchanged

## Mermaid interception

### Requirement: Mermaid fences become image placeholders
When mermaid support is enabled, a ` ```mermaid ` fence SHALL be removed from the HTML and
replaced in place by `<img src="mermaid://<key>">`, where `<key>` is the SHA-1 hex digest of the
diagram source. The diagram source SHALL be recorded in `Document::mermaid`.

#### Scenario: Fence is replaced
- GIVEN a document with one mermaid fence
- WHEN it is parsed with mermaid enabled
- THEN `Document::mermaid` has one entry, the HTML contains `<img src="mermaid://<key>"`, and the
  diagram source no longer appears as a code block

#### Scenario: Mermaid disabled
- GIVEN the same document parsed with `Options::mermaidEnabled == false`
- WHEN it is parsed
- THEN `Document::mermaid` is empty, the HTML contains no `mermaid://`, and the diagram is shown
  as an ordinary code block

## Syntax highlighting

### Requirement: Fenced code is highlighted without external dependencies
Fenced code SHALL be wrapped in `<pre>` with its content HTML-escaped and coloured with inline
`<span style="color:…">`. Supported languages are C/C++, Python, JavaScript/TypeScript, JSON,
Bash/shell and CMake. An unrecognised language SHALL fall back to monospaced, uncoloured text.

#### Scenario: Known language
- GIVEN a `cpp` fence containing `return nullptr;`
- WHEN it is highlighted
- THEN the output contains coloured spans for `return` and `nullptr`

#### Scenario: Unknown language
- GIVEN a fence tagged `nosuchlang` containing `some plain <text>`
- WHEN it is highlighted
- THEN the output escapes the angle brackets and contains no `<span`

### Requirement: A malformed string literal does not swallow the file
An unterminated single-line string SHALL stop at the end of its line rather than colouring the
remainder of the document as string content.

#### Scenario: Missing closing quote
- GIVEN the C++ source `a = "oops` followed on the next line by `return 1;`
- WHEN it is highlighted
- THEN `return` is still coloured as a keyword
