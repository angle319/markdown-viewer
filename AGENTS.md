# Working on this repository

Read this before making changes. Every rule here exists because the mistake it prevents was
actually made while building this project — the entries are not hypothetical.

中：這份是給後續開發者／agent 的約定。每一條都對應一個真的犯過的錯，不是預想出來的。

---

## Start here

```sh
./scripts/setup-dev.sh     # enables the committed git hooks (build.sh also runs this)
./build.sh                 # configures and builds into build/
```

Before you say a change is done:

```sh
./scripts/check-repo.sh                              # repository conventions
QT_QPA_PLATFORM=offscreen ctest --test-dir build     # 8 suites
```

Two layers enforce the conventions:

| Layer | What it checks | Can it be bypassed? |
|---|---|---|
| `scripts/check-repo.sh` | conventions, run by hand | yes, by not running it |
| `.githooks/` (`pre-commit`, `commit-msg`) | the same checks, plus the commit message | yes, `--no-verify`, or a clone that never ran setup |

There is deliberately no CI: this project is built and verified locally. That
makes both layers skippable, so the build-and-test step above is not optional —
**run it before you report a change as done.**

`.githooks/commit-msg` uses `scripts/check-commit-msg.py` rather than commitlint,
so that committing works offline. Before pushing anything unusual, check the real
thing:

```sh
npx --yes --package @commitlint/cli --package @commitlint/config-conventional \
  commitlint --from <ref> --to HEAD
```

---

## Conventions

- **Code comments in English.** A one-line `中：` summary may follow on a hard-won trap.
- **User-facing strings in 繁體中文** — menus, dialogs, the status bar.
- **Documentation in pairs**: `README.md` / `README.zh-TW.md`, `spec.md` / `spec.zh-TW.md`.
  English is canonical. `openspec/check-bilingual.sh` fails if the pair drifts structurally.
- **Conventional Commits, in English.** Subjects start lower case (`subject-case` forbids
  sentence-case); merge commits use `chore:` so a changelog does not list the work twice.
  A body line starting with `word: ` is parsed as a git trailer and trips `footer-leading-blank`
  — reword it.
- **No internal or employer-specific identifiers, and no absolute home paths.** This repository
  is public. Tests take fixture paths from the `SAMPLE_MD` / `HEADINGS_MD` compile definitions.
- **Never push, force-push, or rewrite history unless the user asked for it in this session.**

---

## The failure mode to guard against

Several real defects in this project passed a green test suite. In each case the assertion was
true and the picture was wrong:

- Mermaid SVG rendered with **no connector lines** at all. An ink-density differential test
  passed, because ink density does not know what an arrow is. Found by exporting a PNG and
  looking at it. (`QSvgRenderer` is SVG Tiny 1.2 — no `<marker>`. The renderer outputs PNG now.)
- Tab labels and the menu bar were **invisible** on the black theme. Every contrast assertion was
  green; the palette simply had roles it never set.
- The active-pane accent line **never drew**. The test asserted the property, not the pixels.
- Body text was 9pt while the docs said 11pt, for weeks. Only heading sizes were ever asserted.

So: **when you change anything visual, dump a screenshot and look at it.**

```sh
MD_E2E_DUMP=/tmp/md QT_QPA_PLATFORM=offscreen ./build/test_e2e_viewer
```

And write assertions that pin the exact value — `QCOMPARE(colour, QColor("#1f2328"))`, not
`QVERIFY(contrast > 4.5)`. A weak assertion is worse than none, because it buys false confidence.
One bug here survived precisely because a later contrast fix-up rewrote the colour the test
checked.

---

## Qt rich-text traps

`QTextBrowser` is not a browser. The CSS subset is small and quiet about what it ignores.

- `body { font-size }` has **no effect** on a `QTextDocument`. Set sizes per block.
- `QTextFormat::FontSizeAdjustment` **overrides** `FontPointSize`, including when it is 0.
  Call `clearProperty()`, and use `setCharFormat` — `mergeCharFormat` cannot remove a property.
- No `max-width`, and no borders on block elements. Content width and heading rules are painted
  in `paintEvent()`; table cell borders need `QTextTableFormat::setBorderCollapse(true)`.
- `QString::arg` with several markers substitutes the **lowest-numbered marker present**, not
  `%N` → Nth argument. Deleting a `%4` silently shifts everything after it. The stylesheets use
  named tokens (`@TEXT@`) for this reason — keep it that way.
- Loading into a **hidden** widget means `loadResource()` is never called and every image sizes
  to 0. Add the tab and show it before loading.
- Wrap bulk document edits in `beginEditBlock`/`endEditBlock` with `setUndoRedoEnabled(false)`.
  Without it a wide table re-lays out per row: one real file went from 2146 ms to 23 ms.

## Other traps

- **Contrast ratio does not mean "distinguishable".** Purple against blue is 1.13:1 and perfectly
  legible. Syntax-colour separation is asserted on hue difference (>= 60 degrees) instead.
- **`grep` here may be ugrep.** It understands `\x{4e00}` ranges; GNU grep does not, and would
  read the pattern as literal characters. A check that silently passes on one machine is worse
  than no check — the CJK-comment check is written in Python for exactly this reason.
- **Measure counts, do not remember them.** The test count in the README was wrong three times.
  `check-repo.sh` now compares it against `-functions` output.
