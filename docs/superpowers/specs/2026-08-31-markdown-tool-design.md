# markdown-tool v0.1 設計文件

日期：2026-08-31
狀態：已與使用者確認架構，待實作

## 1. 動機

現用的 Chrome extension「Markdown Reader」(md-reader.github.io, v3.6.28) 難用且吃資源：
content script 單檔 5.8MB JS + 0.98MB CSS，每開一個 `.md` 分頁就整包載入 Chromium renderer。

目標是一個原生 Qt/C++ 檢視器，**穩態記憶體 40MB 以下**，冷啟動 200ms 以內，
且仍能顯示 mermaid 圖表。

## 2. 引擎決策與被排除的選項

| 方案 | 預估 RSS | 判定 |
|---|---|---|
| md4c → HTML → QTextBrowser | 20–40MB | **採用** |
| litehtml + md4c | 25–40MB | 保留為第二後端，撞到 CSS 天花板再換 |
| Qt Quick 虛擬化 ListView | 30–50MB | 未採用；巨檔才需要 |
| Sciter | 15–30MB | 排除：閉源、授權待查 |
| Ultralight | 60–120MB | 排除：閉源、授權待查、膠水層成本高 |
| QtWebEngine | 200MB+ | 排除：見下 |

QtWebEngine 被排除的理由不是「會漏記憶體」，而是**地板太高且旋鈕太小**。
它會 plateau 在 150–250MB（多行程加總），成因是 Chromium 刻意的設計：解碼圖片 cache、
Skia/GPU cache、字型 cache、V8 無壓力時不 GC、PartitionAlloc 不急著還頁給 OS。
`--renderer-process-limit=1`、`--process-per-site`、`--in-process-gpu`、
`--js-flags=--max-old-space-size=64`、`NoCache` profile 每項約省 10–20MB，
沒有任何組合能把 200MB 變 40MB。附帶成本還有 binary +100–150MB、冷啟動 300–800ms、
以及為了看本機檔案而背上整個 Chromium 的 CVE 更新義務。

而它唯一的優勢（mermaid 行內跑）已被「外部渲染 + 快取」方案取代，穩態成本為零。
故對本需求 QtWebEngine 在所有維度上都被支配。

## 3. Okular 前例驗證

本機 okular 21.12.3 內含 `okularGenerator_md.so`，連結 `libmarkdown.so.2`(Discount)，
證實 KDE 的 markdown 檢視走的正是「MD → HTML → QTextDocument」路線，無瀏覽器引擎。
從其 `generators/markdown/converter.cpp` 吸收四項做法：

1. **內容欄寬上限 980px**（其 `CONTENT_WIDTH`），圖片超寬等比縮至欄寬內；順帶實現置中閱讀。
2. **`setHtml()` 後再走一遍 document tree 修圖片**：相對路徑對 md 檔所在目錄解析、
   `QUrl::fromPercentEncoding()` 解碼檔名、只給單邊尺寸時維持長寬比、檔案不存在退回 alt text。
   單靠 `loadResource()` 不足，此 tree walk 為必要補充。
3. **內部 anchor 兩段式解析**：`#anchor` 目標可能出現在後文，先收集再於全文建完後解析。
4. **產 HTML 時就換掉 Qt rich-text 不支援的標籤**（Okular 將 `<del>` 換成 `<s>`）。

## 4. 架構

```
檔案 → MarkdownParser(md4c) → Document{ html, toc[], mermaidBlocks[] }
                                  ↓  mermaid fence → <img src="mermaid://<key>">
                       IRenderBackend ← TextBrowserBackend(QTextBrowser)
                                  ↓  loadResource()
                       MermaidCache → ~/.cache/markdown-tool/<key>.svg
                                  ↓  miss
                       IMermaidRenderer → MmdcRenderer(QProcess)
```

兩道接縫刻意保留：
- `IRenderBackend`：換 litehtml 只動一個檔，上游完全無感。
- `IMermaidRenderer`：日後加 graphviz 譯法（零外部依賴、只覆蓋 flowchart）僅新增一個 class。

### 檔案佈局

```
CMakeLists.txt
third_party/md4c/            vendored md4c 原始碼（MIT，4 檔）
src/
  main.cpp
  MainWindow.{h,cpp}         視窗、選單、快捷鍵、狀態列
  Sidebar.{h,cpp}            QTabWidget，裝下列兩個 panel
  TocPanel.{h,cpp}           段落樹
  FileBrowserPanel.{h,cpp}   檔案樹
  render/IRenderBackend.h
  render/TextBrowserBackend.{h,cpp}
  core/Document.h
  core/MarkdownParser.{h,cpp}
  core/CodeHighlighter.{h,cpp}
  core/MermaidCache.{h,cpp}
  core/IMermaidRenderer.h
  core/MmdcRenderer.{h,cpp}
  core/Theme.{h,cpp}
  core/FileWatcher.{h,cpp}
tests/                       QtTest
```

**md4c 用 vendored 原始碼**，不依賴系統套件：本機只有 `libmd4c.so` 而無 header，
且 vendoring 讓專案可整包搬移。Qt 6 內部雖也 bundle md4c
（`QTextDocument::setMarkdown` 即基於它），但未公開 API，且我們需要攔截 mermaid fence、
注入語法高亮、產生 anchor id，故需直接控制 parser。

## 5. 模組職責

### MarkdownParser
輸入 md 文字與檔案所在目錄，輸出 `Document`。責任：
- 透過 md4c 產生 HTML，只用 Qt rich-text 支援的標籤子集
- 抽取 heading 建 TOC，slug 採 GitHub 規則（小寫、空白轉 `-`、去標點），重複時依 GitHub 行為加 `-1`、`-2`…
- 把 ` ```mermaid ` fence 抽出成 `mermaidBlocks[]`，原位置換成 `<img src="mermaid://<key>">`
- 其他 fenced code 交給 `CodeHighlighter` 轉成 inline `<span style>` 上色
- 換掉不支援標籤：`<del>`→`<s>`；task list `- [x]` → `☑`/`☐` 字元（Qt 不渲染 checkbox input）

### CodeHighlighter
語言關鍵字著色，產出 inline style 的 HTML。v0.1 支援 C/C++、Python、JS/TS、JSON、
Bash、CMake、Markdown；未知語言只上等寬字與底色。不使用外部依賴。

### MermaidCache
key = `sha1(diagram text + theme + mmdc version)`；theme 進 key 是因為明暗主題產出不同 SVG。
命中直接回檔案內容。未命中則交給 `IMermaidRenderer` 在背景產生，先回佔位圖。
快取目錄 `~/.cache/markdown-tool/`，格式無關（`.svg` 或 `.png` 皆可）。

### MmdcRenderer
`QProcess` 呼叫 mmdc。實測可用參數（2026-08-31，mmdc 11.16.0）：

```
mmdc -i in.mmd -o out.svg -p puppeteer.json -c mermaid.json -t <default|dark> -b transparent
```

- `puppeteer.json` = `{"args":["--no-sandbox","--disable-dev-shm-usage"]}`
- `mermaid.json` = `{"htmlLabels":false,"flowchart":{"htmlLabels":false},"class":{"htmlLabels":false}}`

**`htmlLabels:false` 為必要參數**：mermaid 預設把節點文字放在 `<foreignObject>` 內的 HTML，
而 `QSvgRenderer` 只支援 SVG Tiny 1.2，不吃 foreignObject，會導致圖形正常但**文字全空白**。
關掉後產出真正的 `<text>` 元素。已實測驗證。

產出 SVG 的後處理：移除 `width="100%"`（改用 viewBox 的固有尺寸），
超過內容欄寬則等比縮放。

`mmdc` 不存在時**不視為錯誤**：退回顯示原始 mermaid 程式碼區塊，狀態列提示一次安裝指令。

### Theme
light / dark 兩套，以 CSS 字串注入 `QTextDocument::setDefaultStyleSheet()`。
切換主題需清除 mermaid 圖的顯示並以新 theme key 重查快取。

### FileWatcher
`QFileSystemWatcher` 監看目前檔案，變更後 150ms debounce 重新解析，保留捲動位置。
須處理編輯器的 atomic save（刪除+重建會使 watcher 失效，需重新 addPath）。

## 6. Sidebar

`QTabWidget`，兩個分頁。寬度、目前分頁、明暗主題存 `QSettings`，`F9` 收合。

**「段落」分頁**：`QTreeView` + 自訂 model，H1–H6 巢狀。點擊呼叫
`QTextBrowser::scrollToAnchor()`。捲動時反查目前視埠最上方的 heading 並高亮對應項目。

**「檔案」分頁**：`QFileSystemModel` + `QTreeView`，
filter 僅顯示資料夾與 `*.md *.markdown *.mdx *.mdc *.txt`。
根目錄預設為目前開啟檔案所在目錄，可按鈕改選，最後一次的根目錄記在 `QSettings`。
單擊預覽、雙擊開啟。

## 7. 測試策略

`MarkdownParser`、`CodeHighlighter`、`MermaidCache` 為純邏輯，先寫 QtTest 再寫實作：

- MarkdownParser：heading 抽取層級正確、slug 規則與重複去重、mermaid fence 抽取與
  `<img>` 置換、`<del>`→`<s>`、task list 轉字元、相對圖片路徑解析
- CodeHighlighter：各語言關鍵字著色、未知語言退化、HTML 特殊字元 escape
- MermaidCache：key 對內容/theme 敏感、命中不重跑、renderer 缺席時的 degrade 路徑
  （用假 renderer 注入，不真的跑 mmdc）

GUI 層薄，靠手動驗收。

## 8. 記憶體驗證方法

不以單一行程 RSS 為結論。開一份代表性文件（含 3 張 mermaid、10 張圖片、2000 行）後，
量測整個行程樹的 **PSS**（`/proc/<pid>/smaps_rollup`，含所有子行程），與 QtWebEngine
版本的同文件對照。數字實測後才寫入 README，不預先當結論宣稱。

## 9. 已知風險

1. ~~**Qt SVG Tiny 對 mermaid 產出的支援度未驗。**~~ **已有定論：撐不住，改用 PNG。**
   見第 12 節的實測結果。
2. **QTextBrowser 無 flex/grid**，版面只能單欄文件流。對 reader 足夠，但不能做並排版面。
3. **`QTextDocument` 建完整物件樹**，超過 10MB 的 md 會開始鈍。v0.1 接受；
   真要處理巨檔需換 Qt Quick 虛擬化路線。
4. **Qt rich-text 標籤子集的 gotcha 需持續累積**，目前已知 `<del>`、checkbox input。

## 10. v0.1 範圍外

分頁（多檔同時開）、編輯、匯出 PDF、全文搜尋、KaTeX、litehtml 後端、
graphviz mermaid 譯法、列印。

## 11. 環境依賴

已備妥：`g++`、`node v22.23.1`、`mmdc 11.16.0`（本次安裝並實測）、`graphviz dot`、
`libmd4c.so`（改用 vendored 原始碼，故非必需）。

待安裝（需 sudo，由使用者本人執行）：

```
sudo apt install cmake qt6-base-dev qt6-svg-dev qt6-tools-dev-tools
```

## 12. 實測結果（2026-08-31）

### 12.1 SVG 路線失敗，改用 PNG

原設計是「mmdc 產 SVG → QSvgRenderer 光柵化」。實測後放棄，改為
「mmdc 直接產 PNG @devicePixelRatio → QImage 載入」。

`htmlLabels:false` 確實消除了 `<foreignObject>`，文字也變成真正的 `<text>`，
差分測試（帶文字 vs 空標籤）顯示 Qt 的墨水密度 0.667 vs 0.482，證明**文字有被畫出來**。
但那個測試太弱，通過了卻沒抓到真正的問題。實際渲染出來的畫面是壞的：

- **所有連線與箭頭整批消失**（Qt SVG Tiny 不支援 `<marker>`）
- 節點文字被畫在方框上緣，而非垂直居中
- 邊標籤旁出現灰色實心方塊
- 原點殘留一個黑色三角形（marker 定義被畫在 (0,0)）

決定性的量化證據：對 `flowchart LR  A[AAAAAAAA] --> B[BBBBBBBB]`，
量測影像正中央水平帶（兩節點之間，只可能被連線佔用）的深色像素數 ——

| 路徑 | 中央帶墨水 |
|---|---|
| SVG 經 QSvgRenderer | **0** |
| PNG 經 Chromium | **249** |

改用 PNG 後畫面完全正確。此結論由 `tests/test_mmdc_integration.cpp` 的
`svgOutputLosesEdgesInQt()` 盯住：若哪天 Qt 支援了 `<marker>`，那支測試會失敗，
那就是重新評估 SVG 的時機。

光柵化倍率跟著螢幕的 `devicePixelRatio`（1x 螢幕用 2 倍等於白花一倍記憶體），
且倍率計入 `rendererId` 進而計入快取 key，換螢幕不會拿到錯解析度的舊圖。

### 12.2 記憶體實測

量整個行程樹的 PSS（`/proc/<pid>/smaps_rollup`）。RSS 會把共享函式庫頁面重複
計算，故不作為結論。

| 情境 | PSS | RSS |
|---|---|---|
| 三行小檔（基準） | **32.7 MB** | 65.7 MB |
| `docs/sample.md`（含 2 張 mermaid） | **38.1 MB** | 80.5 MB |

達成 40MB 目標，但過程中發現一個吃掉三分之一預算的東西：

**`QT_XCB_GL_INTEGRATION=none`**。依映射來源分組 PSS 後，最大單項是
`libLLVM-15.so.1` 佔 **13.2 MB** —— 那是 Mesa 的 llvmpipe，被 xcb QPA 的
GL 整合連帶拉進來的。這個 app 全程 raster 繪製，完全不需要 OpenGL。
在 `QApplication` 建立前設掉這個變數後：

| | 基準 | sample.md |
|---|---|---|
| 未設定 | 49.1 MB | 55.5 MB |
| `GL_INTEGRATION=none` | **32.7 MB** | **38.1 MB** |

已寫進 `src/main.cpp`（使用者若明確設過就尊重其設定）。

### 12.3 版面調整

`QTextDocument::setIndentWidth(20)`。Qt 預設縮排單位是 40px，巢狀清單與引用
區塊在中文字體下縮得很誇張，而 Qt rich-text 不吃 CSS 的 `margin-left`／
`padding-left` 來調清單縮排，文件層級的 `indentWidth` 是唯一有效的旋鈕。

### 12.4 測試

71 個測試函式，6 個套件，全數通過：

| 套件 | 函式數 | 內容 |
|---|---|---|
| markdownparser | 15 | 錨點規則、mermaid 抽取、轉義、圖片路徑 |
| codehighlighter | 8 | 各語言著色、退化、未閉合字串 |
| mermaidcache | 8 | key 敏感度、佇列序列化、degrade 路徑 |
| mmdc_integration | 9 | 真的跑 mmdc；SVG-vs-PNG 的連線墨水差分 |
| e2e_viewer | 14 | 驅動真正的 MainWindow 走使用者流程 |
| e2e_regression | 17 | 以 sample.md 為語料庫釘住 pipeline 不變式 |

e2e 以 `QT_QPA_PLATFORM=offscreen` 執行，不需要 X／Wayland。
設 `MD_E2E_DUMP=<目錄>` 可額外輸出畫面 PNG 供人眼檢查 —— 自動化斷言驗得了
結構，驗不了「看起來對不對」，SVG 那個坑就是這樣才被抓到的。

### 12.5 e2e 抓到的真 bug

`MainWindow::reparse()` 原本先 `setDocument()` 才 `setToc()`。後端在
`setDocument()` 內就會發出 `currentTocIndexChanged`，而 `TocPanel` 隨後被
`setToc()` 清空重建，於是捲動高亮永遠是空的。順序反過來即修好。
由 `scrollingUpdatesTocHighlight()` 盯住。
