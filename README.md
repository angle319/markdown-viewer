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
- **mermaid**：外部 `mmdc` 渲染成 **PNG** 後以 sha1 快取在
  `~/.cache/markdown-tool/mermaid/`。渲染佇列刻意序列化（同時只跑一個），
  因為每次 mmdc 會拉起一個 headless Chromium（實測峰值約 106MB）。
  `mmdc` 不在時退回顯示原始碼，不視為錯誤。

  **為什麼是 PNG 而不是 SVG**：原本設計走 SVG，但 Qt 的 `QSvgRenderer` 只支援
  SVG Tiny 1.2、不支援 `<marker>`，實測結果是 mermaid 的**連線與箭頭整批消失**、
  文字被畫到方框上緣、原點殘留黑色三角形。量化證據：兩節點之間的中央帶深色
  像素數，SVG 經 Qt 是 **0**，PNG 經 Chromium 是 **249**。
  由 `svgOutputLosesEdgesInQt()` 這支測試盯住 —— Qt 哪天修好了它會失敗。
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

## 記憶體

量整個行程樹的 PSS（`/proc/<pid>/smaps_rollup`）。RSS 會把共享函式庫頁面
重複計算，不作為結論。

| 情境 | PSS | RSS |
|---|---|---|
| 三行小檔（基準） | 32.7 MB | 65.7 MB |
| `docs/sample.md`（含 2 張 mermaid） | 38.1 MB | 80.5 MB |

其中一個關鍵設定是 `QT_XCB_GL_INTEGRATION=none`（已寫在 `src/main.cpp`）：
xcb QPA 的 GL 整合會把 Mesa 的 llvmpipe 連帶 `libLLVM` 拉進行程，光那一顆就
**13.2 MB PSS**，而這個 app 全程 raster 繪製、完全不用 OpenGL。設掉之後
基準從 49.1MB 降到 32.7MB。

## 測試

```
ctest --test-dir build --output-on-failure
```

71 個測試函式、6 個套件：

| 套件 | 函式數 | 內容 |
|---|---|---|
| markdownparser | 15 | 錨點規則、mermaid 抽取、轉義、圖片路徑 |
| codehighlighter | 8 | 各語言著色、退化、未閉合字串 |
| mermaidcache | 8 | key 敏感度、佇列序列化、degrade 路徑 |
| mmdc_integration | 9 | 真的跑 mmdc；SVG-vs-PNG 的連線墨水差分 |
| e2e_viewer | 14 | 驅動真正的 MainWindow 走使用者流程 |
| e2e_regression | 17 | 以 sample.md 為語料庫釘住 pipeline 不變式 |

e2e 用 `QT_QPA_PLATFORM=offscreen` 跑，不需要 X／Wayland。`mmdc` 不在時
整合測試與 mermaid e2e 會自己 skip，不算失敗。

視覺檢查（自動化斷言驗結構，驗不了「看起來對不對」）：

```
MD_E2E_DUMP=/tmp/shots QT_QPA_PLATFORM=offscreen ./build/test_e2e_regression
```

會輸出明暗主題下的頂端、程式碼、表格、mermaid 畫面 PNG。

## 狀態

v0.1 功能完成，已在真實 X11 與 offscreen 下驗證。

`docs/superpowers/specs/2026-08-31-markdown-tool-design.md` 第 12 節記錄了
所有實測結果，包含 SVG 路線失敗的完整證據。
