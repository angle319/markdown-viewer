#!/usr/bin/env bash
#
# Repository conventions, as executable checks.
#
# Every check here exists because the problem it catches actually happened while
# building this project. Run it before committing; the pre-commit hook calls it
# too.
#
#   scripts/check-repo.sh            check the working tree
#
set -uo pipefail
cd "$(dirname "$0")/.."

fail=0
note() { printf '\033[1;31m✗\033[0m %s\n' "$1"; fail=1; }
ok()   { printf '\033[1;32m✓\033[0m %s\n' "$1"; }

SRC_GLOBS=(--include='*.cpp' --include='*.h')
SRC_DIRS=(src tests)

# --- 1. Code comments are English -------------------------------------------
#
# The convention is English comments, with a deliberate one-line `中：` summary
# allowed on hard-won traps. User-facing strings stay in Traditional Chinese, so
# only comment lines are checked.
# Implemented in Python: this machine's `grep` is ugrep, which understands
# \x{...} ranges, but GNU grep does not. A check that silently passes on one
# machine and not another is worse than no check.
offenders=$(python3 - <<'EOF'
import pathlib, re
cjk = re.compile(r'[\u4e00-\u9fff]')
comment = re.compile(r'^\s*(//|///|\*)')
for d in ("src", "tests"):
    for f in sorted(pathlib.Path(d).rglob("*")):
        if f.suffix not in (".cpp", ".h"):
            continue
        for n, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
            if not cjk.search(line) or not comment.search(line):
                continue
            if "中：" in line or "presented in the UI as" in line:
                continue
            print(f"{f}:{n}:{line.strip()[:90]}")
EOF
)
if [[ -n "$offenders" ]]; then
    note "Chinese in code comments (use English; a one-line 中： summary is allowed):"
    echo "$offenders" | sed 's/^/    /'
else
    ok "code comments are English"
fi

# --- 2. No internal or employer-specific identifiers ------------------------
#
# This is a public repository. An internal repo name and an internal document
# title used as a test fixture both had to be scrubbed before the first push.
internal=$(grep -rniE 'viewsonic|atlassian|confluence|myviewboard|classswift|edu-mvb|edu-confluence' \
    --include='*.md' --include='*.cpp' --include='*.h' --include='*.sh' --include='*.yml' \
    . 2>/dev/null | grep -v '^\./build/' | grep -v third_party | grep -v 'scripts/check-repo.sh' || true)
if [[ -n "$internal" ]]; then
    note "internal identifiers must not appear in a public repository:"
    echo "$internal" | sed 's/^/    /'
else
    ok "no internal identifiers"
fi

# --- 3. No machine-specific absolute paths ----------------------------------
#
# A hard-coded /home/... path in a test was both a privacy leak and a portability
# bug. Tests should use the SAMPLE_MD / HEADINGS_MD compile definitions.
paths=$(grep -rnE '"/(home|Users)/' --include='*.cpp' --include='*.h' --include='*.md' \
    --include='*.sh' . 2>/dev/null | grep -v '^\./build/' | grep -v third_party \
    | grep -v 'scripts/check-repo.sh' || true)
if [[ -n "$paths" ]]; then
    note "absolute home paths are not portable:"
    echo "$paths" | sed 's/^/    /'
else
    ok "no absolute home paths"
fi

# --- 4. Bilingual specs stay structurally in step ---------------------------
if [[ -x openspec/check-bilingual.sh ]]; then
    if out=$(openspec/check-bilingual.sh 2>&1); then
        ok "openspec bilingual pairs are in step"
    else
        note "openspec bilingual pairs have drifted:"
        echo "$out" | sed 's/^/    /'
    fi
fi

# --- 5. Both READMEs exist and cross-link -----------------------------------
if [[ -f README.md && -f README.zh-TW.md ]]; then
    if grep -q 'README.zh-TW.md' README.md && grep -q 'README.md' README.zh-TW.md; then
        ok "README pair cross-links"
    else
        note "README.md and README.zh-TW.md must link to each other"
    fi
else
    note "both README.md and README.zh-TW.md must exist"
fi

# --- 6. The documented test count matches reality ---------------------------
#
# The count in the README was wrong three times during development, each time
# because it was written from memory rather than measured.
if [[ -d build ]]; then
    total=0
    missing=0
    for t in markdownparser codehighlighter mermaidcache mmdc_integration theme \
             e2e_viewer e2e_regression e2e_tabs; do
        if [[ -x "build/test_$t" ]]; then
            n=$(QT_QPA_PLATFORM=offscreen "build/test_$t" -functions 2>/dev/null | wc -l)
            total=$((total + n))
        else
            missing=1
        fi
    done
    if [[ $missing -eq 1 ]]; then
        printf '\033[1;33m-\033[0m %s\n' "test count not checked (build incomplete)"
    else
        bad=0
        for f in README.md README.zh-TW.md; do
            claimed=$(grep -oE '[0-9]+ (test functions|個測試函式)' "$f" | head -1 | grep -oE '[0-9]+' || echo "")
            if [[ -n "$claimed" && "$claimed" != "$total" ]]; then
                note "$f claims $claimed test functions but there are $total"
                bad=1
            fi
        done
        [[ $bad -eq 0 ]] && ok "documented test count matches ($total)"
    fi
else
    printf '\033[1;33m-\033[0m %s\n' "test count not checked (no build directory)"
fi

echo
if [[ $fail -eq 0 ]]; then
    echo "All repository checks passed."
else
    echo "Repository checks failed. See above."
fi
exit $fail
