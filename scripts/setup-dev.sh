#!/usr/bin/env bash
#
# Points git at the hooks committed in .githooks/.
#
# Hooks live in the repository rather than .git/hooks so that a fresh clone can
# enable them with one command. build.sh calls this, so anyone who builds gets
# them. There is no CI, so run scripts/check-repo.sh by hand if you skip a hook.
set -euo pipefail
cd "$(dirname "$0")/.."

git config core.hooksPath .githooks
chmod +x .githooks/* scripts/*.sh scripts/*.py 2>/dev/null || true

echo "git hooks enabled (core.hooksPath = .githooks)"
echo "  commit-msg  -> scripts/check-commit-msg.py"
echo "  pre-commit  -> scripts/check-repo.sh"
