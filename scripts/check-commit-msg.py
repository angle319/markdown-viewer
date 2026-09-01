#!/usr/bin/env python3
"""Offline check of a commit message against Conventional Commits.

This mirrors the rules of @commitlint/config-conventional that matter here. It is
deliberately *not* a call to commitlint: running npx on every commit needs the
network, and a hook that blocks when you are offline is a hook people disable.
Run the real commitlint by hand before pushing if a message is unusual; the
command is in the README. This is the gate that runs every time.

    scripts/check-commit-msg.py .git/COMMIT_EDITMSG
"""
import re
import sys

TYPES = {
    "build", "chore", "ci", "docs", "feat", "fix",
    "perf", "refactor", "revert", "style", "test",
}

HEADER = re.compile(r"^(?P<type>[a-z]+)(?:\((?P<scope>[^)]+)\))?(?P<bang>!)?: (?P<subject>.+)$")
FOOTER_TOKEN = re.compile(r"^[A-Za-z][A-Za-z0-9_-]*: ")
MAX_HEADER = 100
MAX_LINE = 100


def check(text):
    problems = []

    lines = [ln.rstrip("\n") for ln in text.split("\n")]
    # Drop comment lines git adds to the editor buffer, and trailing blanks.
    lines = [ln for ln in lines if not ln.startswith("#")]
    while lines and not lines[-1].strip():
        lines.pop()

    if not lines or not lines[0].strip():
        return ["the message is empty"]

    header = lines[0]

    # Merge commits produced by git itself are ignored by commitlint too.
    if re.match(r"^(Merge pull request|Merge branch|Merge remote-tracking|Revert )", header):
        return []

    if len(header) > MAX_HEADER:
        problems.append(f"header is {len(header)} characters, limit is {MAX_HEADER}")

    m = HEADER.match(header)
    if not m:
        problems.append(
            "header must be 'type(scope): subject' with a lower-case type from "
            + ", ".join(sorted(TYPES))
        )
        return problems

    if m.group("type") not in TYPES:
        problems.append(
            f"'{m.group('type')}' is not a valid type; use one of "
            + ", ".join(sorted(TYPES))
        )

    subject = m.group("subject")
    if subject.endswith("."):
        problems.append("subject must not end with a full stop")
    if subject[:1].isupper():
        problems.append(
            f"subject must not start upper case (subject-case forbids sentence-case): {subject[:40]!r}"
        )

    if len(lines) > 1 and lines[1].strip():
        problems.append("a blank line must separate the header from the body")

    for i, ln in enumerate(lines[1:], start=2):
        if len(ln) > MAX_LINE:
            problems.append(f"line {i} is {len(ln)} characters, limit is {MAX_LINE}")

    # A body line that looks like `word: ...` is parsed as a git trailer, which
    # splits body from footer and then trips footer-leading-blank. This normally
    # happens by accident when a line wraps.
    for i, ln in enumerate(lines[2:], start=3):
        if FOOTER_TOKEN.match(ln) and not ln.startswith(("Co-Authored-By:", "Signed-off-by:",
                                                         "BREAKING CHANGE:", "Refs:", "Closes:",
                                                         "Fixes:", "Reviewed-by:")):
            if lines[i - 2].strip():
                problems.append(
                    f"line {i} starts with '{ln.split(':')[0]}:' so it is read as a footer; "
                    "reword it or put a blank line before it"
                )

    return problems


def main():
    if len(sys.argv) != 2:
        print("usage: check-commit-msg.py <file>", file=sys.stderr)
        return 2

    with open(sys.argv[1], encoding="utf-8") as fh:
        problems = check(fh.read())

    if not problems:
        return 0

    print("\nCommit message does not follow Conventional Commits:\n", file=sys.stderr)
    for p in problems:
        print(f"  - {p}", file=sys.stderr)
    print(
        "\n  Format:  type(scope): subject\n"
        "  Types:   " + ", ".join(sorted(TYPES)) + "\n"
        "  Subject: lower case, no trailing full stop, header <= 100 chars\n"
        "  Merges:  use 'chore:' so a changelog does not list the work twice\n",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
