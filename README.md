# markdown-tool

極簡 markdown 檢視器。**md4c + QTextBrowser，沒有瀏覽器引擎、沒有 JS。**

起因是 Chrome extension「Markdown Reader」的 content script 單檔 5.8MB JS
加 0.98MB CSS，每開一個 `.md` 分頁就整包載入 Chromium renderer。

## 設計要點

- **顯示**：`QTextBrowser`（QTextDocument 富文字引擎）。刻意排除 QtWebEngine：
  它會 plateau 在 150–250MB，而所有可調參數每項只省 10–20MB。
  理由詳見 `docs/superpowers/specs/2026-08-31-markdown-tool-design.md`。
- **解析**：自寫 md4c callback renderer，單趟完成 mermaid fence 攔截、
  GitHub 風格 heading anchor、圖片相對路徑解析、Qt rich-text 子集相容處理。
- **mermaid**：外部 `mmdc` 渲染成 SVG 後以 sha1 快取在
  `~/.cache/markdown-tool/mermaid/`。渲染佇列刻意序列化（同時只跑一個），
  因為每次 mmdc 會拉起一個 headless Chromium（實測峰值約 106MB）。
  `mmdc` 不在時退回顯示原始碼，不視為錯誤。
- **可換引擎**：`IRenderBackend` 是刻意留的接縫。若 Qt 的 CSS 子集不夠用，
  改成 litehtml 只需新增一個實作。

## 建置

```
sudo apt install cmake g++-12 qt6-base-dev qt6-svg-dev \
                 libgl1-mesa-dev libglx-dev libopengl-dev
./build.sh
```

`build.sh` 固定用 `g++-12`：Ubuntu 22.04 的 libstdc++6 執行期是 12.x，
Qt 6.2.4 對著它建，用預設 `g++-11` 連結會缺 `GLIBCXX_3.4.30`。

mermaid 支援需要：

```
npm i -g @mermaid-js/mermaid-cli
```

## 使用

```
./build/markdown-tool docs/sample.md
```

| 快捷鍵 | 動作 |
|---|---|
| `Ctrl+O` | 開啟 |
| `F5` | 重新載入 |
| `F9` | 側邊欄顯示／隱藏 |
| `Alt+Shift+T` | 明暗主題 |
| `Ctrl++` / `Ctrl+-` / `Ctrl+0` | 縮放 |

側邊欄有兩個分頁：**段落**（標題樹，隨捲動高亮）與**檔案**（只顯示資料夾與
markdown 類檔案）。檔案變更會自動重新載入並保留捲動位置。

`docs/sample.md` 涵蓋所有 v0.1 該處理的語法，用來手動驗收。

## 測試

```
ctest --test-dir build --output-on-failure
```

33 個 QtTest：parser 17、語法高亮 8、mermaid 快取 8。
GUI 層薄，靠 `docs/sample.md` 手動驗。

## 狀態

v0.1 進行中。已完成：解析、語法高亮、mermaid 快取、GUI 程式碼。
尚未實測：GUI 執行與記憶體量測（等 OpenGL 開發標頭補齊後進行）。
