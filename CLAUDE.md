# markdown-viewer

Read **[AGENTS.md](AGENTS.md)** before changing anything in this repository. It carries the
conventions, the enforcement layers, and the Qt rich-text traps that cost real time here.

中：動手前先讀 [AGENTS.md](AGENTS.md)。

Quick check before claiming a change is done:

```sh
./scripts/check-repo.sh
QT_QPA_PLATFORM=offscreen ctest --test-dir build
```
