# OpenSpec

Living specification for markdown-tool, following the OpenSpec conventions used across
the other repos in this workspace: `specs/<domain>/spec.md` is the source of truth,
`changes/<date>-<type>-<slug>/` records completed changes.

## Bilingual convention

Every spec exists in two parallel files in the same directory:

| File | Language | Role |
|---|---|---|
| `spec.md` | English | **Canonical.** RFC 2119 keywords (SHALL / SHOULD / MAY) are English by definition, so the normative text lives here. |
| `spec.zh-TW.md` | 繁體中文 | Translation kept in step with `spec.md`. |

When a requirement changes, **both files must be updated in the same commit**. If they
disagree, `spec.md` wins.

`./check-bilingual.sh` enforces this structurally — it compares section, requirement and
scenario counts (and the per-requirement scenario counts) between each pair. It does not compare
wording, since headings are translated.

```
$ openspec/check-bilingual.sh
ok  document-rendering     11 Requirement / 13 Scenario
ok  markdown-parsing        8 Requirement / 16 Scenario
ok  mermaid-diagrams       10 Requirement / 12 Scenario
ok  theming                 8 Requirement / 10 Scenario
ok  workspace              17 Requirement / 22 Scenario
```

## Domains

| Domain | Covers |
|---|---|
| `markdown-parsing` | md4c callback renderer, anchors, syntax highlighting, Qt rich-text compatibility |
| `document-rendering` | QTextBrowser backend, layout, images, contrast enforcement, zoom, performance |
| `theming` | White/black themes, WCAG contrast guarantees, palette, tab states |
| `mermaid-diagrams` | External mmdc rendering, PNG-over-SVG decision, cache, degradation |
| `workspace` | Tabs, split panes, path bar, sidebar, file watching, drag & drop, persistence |

## Verification

Every requirement below is backed by an automated test unless the scenario explicitly says
otherwise. Suites live in `tests/`; run them with `ctest --test-dir build --output-on-failure`.
Where a behaviour cannot be exercised automatically (real X11 drag & drop, visual appearance),
the spec says so rather than implying coverage that does not exist.
