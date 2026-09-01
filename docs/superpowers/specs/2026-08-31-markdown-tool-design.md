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

## 13. 第二輪：雙主題對比保證與路徑列（2026-08-31）

### 13.1 白色／黑色主題

底色改為純 `#ffffff` 與純 `#000000`。所有顏色以 WCAG 2.1 相對亮度計算，
門檻寫成常數（`Theme::MinBodyTextContrast` 等）並由 `tests/test_theme.cpp` 驗證：
正文 ≥ 7:1、次要文字與連結 ≥ 4.5:1、非文字元素 ≥ 3:1。
語法高亮配色也重算過，對著各自的程式碼底色最低 4.5:1。

驗證方式刻意做成黑箱：真的產出高亮 HTML，用 regex 抽出 `<pre>` 的背景色與每一個
`color:`，逐組算對比（目前 208 組）。日後新增語言會自動被檢查，不需要另外開 API。

### 13.2 「不要有看不見的狀況」的三層保護

配色合格只解決了我們自己選的顏色。markdown 可以內嵌任意 HTML，所以還加了兩道
執行期保護：

1. **`MdTextBrowser::applyContrastFixups()`** —— 逐片段算它與「實際背景」的對比
   （判定順序：片段背景 → block 背景 → 頁面底色），不足 4.5:1 就用
   `Theme::readableOn()` 換成該背景上讀得到的顏色。用 `mergeCharFormat` 而非
   `setCharFormat`，只換前景色、保留字型與粗體等格式。
2. **`backdropIfLowContrast()`** —— 透明背景的圖片，取樣可見像素的平均亮度組成
   代表色，與頁面底色對比不足 3:1 就墊一層中性底色（含 8px padding）。
   mermaid 不走這條，它的主題由我們指定。

第三層是 `QPalette`。**踩過的坑**：只設 `Window`/`Base`/`Text` 是不夠的 ——
`QTabBar` 與 `QMenuBar` 會用預設的 `Button`/`ButtonText`（淺色系）去畫，
黑色主題下就是隱形的分頁標籤與隱形選單列。這是靠人眼看截圖發現的，
自動化斷言當時全綠。現在 role 設滿、套用到 `qApp` 層級，並由
`paletteRolePairsAreReadable()` 與 `paletteHasNoDefaultLightRolesInBlackTheme()`
盯住。

`docs/sample.md` 新增「對比保護」章節作為語料，
`everyTextFragmentIsReadableInBothThemes()` 在兩個主題下掃全文件；
`hardcodedColoursInSampleAreCorrected()` 另外確認那些片段真的被改過，
避免前一支測試因為「沒有東西需要修正」而空過。

### 13.3 路徑列

視窗最上方的 `PathBar`，行為比照瀏覽器網址列。`Ctrl+L` 聚焦並全選；
輸入資料夾會切到側邊欄「檔案」分頁並換根，而不是把資料夾當 markdown 開；
`Esc` 還原並把焦點交回內容區；支援 `~` 展開、相對路徑與 `file://` URL。
路徑解析抽成 `PathBar::resolveInput()` 這個純函式，可獨立單元測試。

主題選單改成兩個互斥選項（`Alt+Shift+1` / `Alt+Shift+2`）加一個切換動作
（`Alt+Shift+T`），取代原本單一的 checkbox。

### 13.4 順手修掉的清理問題

`MmdcRenderer` 的解構子原本是 `= default`，關閉時若還有渲染在跑會發出
`QProcess: Destroyed while process is still running` 並可能留下孤兒行程。
現在明確 kill 並 `waitForFinished`。

### 13.5 數字

| 情境 | PSS | RSS |
|---|---|---|
| 三行小檔（基準） | 33.4 MB | 66.4 MB |
| `docs/sample.md` | 38.9 MB | 82.1 MB |

路徑列的 `QFileSystemModel` 與 `QCompleter` 約增加 0.7MB，仍在 40MB 目標內。

測試成長為 **7 個套件、95 個測試函式**，全數通過。

## 14. 第三輪：對照 Chrome extension 調整排版（2026-08-31）

### 14.1 取得基準的方法

沒有靠目測。用本機 HTTP 伺服器把同一份 `docs/sample.md` 餵給使用者 Chrome 裡的
「Markdown Reader」extension（它的 content script 也匹配 `*://*/*.md`），
再用 `getComputedStyle` 把實際數值抓出來。

過程中兩個小絆腳石：瀏覽器工具不允許導向 `file://`，所以改用 HTTP；
Python 的 `http.server` 不會為 `.md` 送 charset，Chrome 以 latin-1 解碼變成亂碼，
需要自己覆寫 `guess_type()` 回 `text/markdown; charset=utf-8`。

抓到的關鍵值：行內 `code` 是 `rgb(103,133,224)` 配 `rgba(103,133,224,0.1)` 底、
圓角 6px；`h2` 有 1px `rgb(48,54,61)` 的底線、`margin-top:35px`；
`blockquote` 有 4px 左色條；連結有底線且與行內 code **同色**。

### 14.2 採納與刻意不採納

採納：行內 code 的顏色 + 底色 chip、連結底線、H1/H2 分隔線、引用左色條、
表格內距加大。

**刻意不採納 extension 讓行內 code 與連結同色。** 那樣讀者分不出「這是程式碼」
還是「這是可點的連結」。改成色相差 > 110° 的洋紅系，且區分不只靠顏色 ——
連結有底線、行內 code 有 chip 與等寬字，色覺不同的人也分得出來。

### 14.3 自繪：標題分隔線與引用色條

Qt rich-text 不支援 block 層級的 `border-bottom` / `border-left`，只能自己畫。
`MdTextBrowser::paintEvent()` 在 `QTextBrowser::paintEvent()` 之後補上，
用 `documentLayout()->hitTest()` 找到第一個可見 block 才開始掃，不是每次重繪都
掃全文件。

引用區塊的辨識是個明確契約：`blockFormat().leftMargin()` 等於
`Theme::BlockquoteIndentPx`，該常數同時被 CSS 與繪製程式碼使用。
探測 Qt 實際留下的 block 屬性後確認可行 —— 清單項目有 `textList()`、
標題的 `leftMargin` 是 0，都不會誤判。由 `blockquoteMarkerContractHolds()` 盯住。

連續的引用 block 會先併成一個 union rect 再畫一條，否則多段引用會變成好幾截
斷開的短棒。若第一個可見 block 落在引用段中間，會往回走到該段起點，
避免色條從畫面上緣被切掉。

### 14.4 又一個被寬鬆斷言掩蓋的 bug

改 CSS 時把 `%4` 拿掉了（`codeBackground` 不再由 stylesheet 使用），
於是字串裡的 placeholder 只剩 8 個而我傳了 9 個引數。

**`QString::arg` 的多引數版本是按「字串中出現的 placeholder 由小到大」依序取代，
不是 `%N` 對應第 N 個引數。** 少用掉一個編號會讓其後全部錯位 ——
結果 `code` 的 `background-color` 拿到了前景色。

而測試當時是綠的：`inlineCodeIsVisuallyDistinct()` 只驗「顏色與正文不同」，
而對比修正把前景改成了在該底色上可讀的白色，於是斷言通過。是產生的 CSS
印出來看才發現的。

兩項修正：
1. stylesheet 改用具名 token（`@TEXT@`、`@CODE_FG@`…）逐一 `replace`，
   徹底消除位置引數錯位這一類 bug；`styleSheetHasNoLeftoverPlaceholders()`
   確認取代乾淨、也確認沒人改回位置引數。
2. 測試改成斷言**確切等於** `Theme::colors(m).codeInline` 與
   `codeInlineBackground`。寬鬆的斷言會掩蓋 bug。

### 14.5 「可區分」不能用對比比衡量

第一版把「行內 code 與連結要分得出來」寫成 `contrastRatio(fg, link) > 1.2`，
測試失敗：紫 `#6f42c1` 與藍 `#0b57d0` 的對比比只有 1.13:1。

WCAG 對比比只衡量**亮度**差異，對「兩個顏色是否看得出不同」是錯的工具 ——
用它當判準會逼人去改亮度而不是改色相。改用色相差（門檻 60°），
實際選用的兩色相差 118°（白）與 116°（黑）。

### 14.6 數字

| 情境 | PSS | RSS |
|---|---|---|
| 三行小檔（基準） | 33.5 MB | 66.4 MB |
| `docs/sample.md` | 39.0 MB | 82.0 MB |

樣式改動不影響記憶體。測試成長為 **7 個套件、100 個測試函式**，全數通過。

### 14.7 表格改為只有橫線

原本用 `<table border="1">` 畫完整格線，改成對照 extension 的橫線樣式。
**用 Qt 原生能力而非自繪**：`QTextTableFormat::setBorderCollapse(true)` 之後
Qt 會渲染每個 cell 自己的邊框，所以把整體 `border` 設為 0、只給每列上框線即可；
表頭那一列的下框線加粗到 2px 把標題列分出來。少了 `setBorderCollapse(true)`
Qt 根本不會畫 cell 層級的邊框 —— 這是關鍵那一行。

邊框與內距改由 `MdTextBrowser::applyTableStyling()` 統一處理，
解析層只輸出 `<table cellspacing="0">`。
由 `tableUsesHorizontalRulesOnly()` 盯住整組契約。

## 15. 標題層級（2026-08-31）

新增 `docs/headings.md` 作為視覺語料：H1–H6 各層都有內文，另含「標題緊接標題」
與「標題內含行內 code 與連結」兩種邊角情況。

### 15.1 Qt 的 FontSizeAdjustment 陷阱

原本用 CSS 設標題字級，實際畫出來的階層是壞的 —— **H5 比正文還小，H6 又比 H5 大**。

量測後找到原因：`QTextFormat::FontSizeAdjustment` 這個屬性**只要存在**（即使值是 0），
Qt 就完全忽略 `FontPointSize`，改用「預設字級 × 層級係數」。實測值：

| | 設定的 pointSize | adjust | 實際畫出 |
|---|---|---|---|
| H1 | 23pt | +3 | 18pt |
| H2 | 17pt | +2 | 13.5pt |
| H3 | 14pt | +1 | 10.8pt |
| H4 | 12.5pt | 0 | 9pt |
| H5 | 11.5pt | -1 | **7.2pt** |
| H6 | 11pt | 無 | 11pt ✓ |

只有 H6 沒有那個屬性，所以只有它正確。順帶推翻了中途一個錯誤假設 ——
我一度以為問題是「`h4, h5, h6 { }` 群組選擇器 Qt 套用不完整」，
分開寫之後 H5 依然是小的，所以那不是原因。

修法：在 `applyHeadingScale()` 裡 `clearProperty(QTextFormat::FontSizeAdjustment)`
再設字級。**設成 0 沒有用，必須清掉。** 且要用 `setCharFormat`（從既有格式複製後
修改）而非 `mergeCharFormat` —— merge 無法移除屬性。

字級定義集中在 `Theme::headingPointSize()`：23 / 17 / 14 / 12.5 / 11.5 / 11pt，
正文 11pt，H6 另外用次要色以便與正文區分。
`headingSizesFollowThemeScale()` 驗證實際 `font().pointSizeF()`、粗體、
「六個層級都出現在語料裡」、「階層嚴格遞減」、「H6 不得小於正文」。

### 15.2 截圖工具的可指定文件

`dumpScreenshotsIfRequested()` 新增 `MD_E2E_DOC` 與 `MD_E2E_ANCHORS` 兩個環境變數，
可以針對任意文件與段落產生兩個主題的截圖。標題層級這一輪就是靠它反覆重現檢查的。

## 16. 逐項對照 Chrome extension（2026-08-31）

第二次開 Chrome 抓 computed style，這次把 `sample.md` 與 `headings.md` 都跑過，
逐項列出「已達到／較差／刻意不同」。

### 16.1 抓到的基準

extension（body 16px）：

| 元素 | 值 |
|---|---|
| h1..h6 | 32 / 24 / 20 / 16 / 14 / 13px（= 2.00 / 1.50 / 1.25 / 1.00 / 0.875 / 0.8125 × body）|
| p | line-height 24px（1.5）、margin 25/16px |
| li | line-height 24px、margin-top 0 |
| pre code | line-height 22.5px（1.6）、padding 10.7/16px、radius 6px |
| td | line-height 24px、padding 9.6/16px |
| blockquote p | line-height 24px、margin 0/10px |
| 內容欄寬 | 1200px |

### 16.2 較差、已修

1. **完全沒設行高。** Qt 預設約等於單行，中文在那個行距下很擠 —— 這是兩邊
   可讀性差距最大的一項。改為 155%（`Typography::LineHeightPercent`）。
   實測確認 Qt 的 CSS 確實支援 `line-height`（`lineHeightType` 為
   `ProportionalHeight`），不是所有 CSS 屬性都有這個待遇。
2. **段距太緊**（7/7px）。改為 10/10px。
3. **程式碼區塊沒有行高、內距偏小。** 補上同樣的 155% 與 10px。
4. 補完行高後**清單變太鬆**（相鄰項目 28.9px，extension 是 24px）。
   把 `li` 上下邊距設為 0、清單整體 4px 收回來。

行高數值一開始分別寫在 Theme 與 CodeHighlighter 兩處，結果正文 155%、
程式碼區塊 150%，是測試抓到的。已抽成 `src/core/Typography.h` ——
放在 core 是因為 CodeHighlighter 在 mdcore（只依賴 QtCore）而 Theme 在 mdgui。

### 16.3 已達到或更好

| 項目 | 說明 |
|---|---|
| 行內 code | 兩邊都有色 + 底色；**本專案另外與連結色相分離**（extension 兩者同色，分不出程式碼與連結）|
| 連結底線 | 本專案有；extension 內文的 `a` 實測是 `text-decoration: none`（先前量到的 underline 是側邊欄目錄裡的連結，是我取樣取錯）|
| 標題級距 | 比例相近（本專案 2.09/1.55/1.27/1.14/1.05/1.00 × body）|
| H1 分隔線 | 本專案有，extension 只有 H2 |
| mermaid | 兩邊都能顯示；本專案是預先算好的 PNG 快取，捲動不重排 |

### 16.4 刻意不同或 Qt 限制

| 項目 | 說明 |
|---|---|
| H5/H6 字級 | extension 讓它們小於正文（0.875/0.8125×），本專案維持 ≥ 正文，改用粗體與次要色區分 |
| 內容欄寬 | 980px（沿用 Okular 的 CONTENT_WIDTH）vs 1200px |
| 行內 code 圓角 | Qt rich-text 不支援 `border-radius`，是方角 |
| 待辦核取方塊 | Qt 不渲染 `<input type=checkbox>`，用 ☑/☐ 字元 |
| 清單間距 | 28.9px vs 24px，Qt 對 li 的 leading 處理不同，已收到可接受範圍 |

## 17. 效能：寬表格開檔退化與拖曳開檔（2026-09-01）

### 17.1 2146ms → 23ms

使用者回報開 `edu-confluence-previous-docs/INDEX.md` 很慢。檔案只有 6.9KB、72 行，
所以不是大小問題 —— 是那張 **65 列 × 5 欄 = 325 個 cell** 的表格。

量測：

| 文件 | blocks | cells | 開檔 |
|---|---|---|---|
| `sample.md` | 62 | 12 | 9ms |
| `INDEX.md` | 329 | 325 | **2146ms** |

原因是四個 document tree walk（圖片尺寸、標題字級、表格樣式、對比修正）
逐項修改格式，**每一次 `setCharFormat` / `setFormat` 都會觸發一次重新排版**，
而且每一筆都進 undo stack。在 cell 多的文件上是二次方級的成本。

兩項修正：

1. 每個 walk 的修改包進單一 `beginEditBlock()` / `endEditBlock()`，讓 Qt 合併成
   一次重排。
2. 建構時 `document()->setUndoRedoEnabled(false)` —— 檢視器不需要復原，
   而那幾個 walk 會做上百次修改。

結果 `INDEX.md` 開檔 **23ms**（93 倍）。
`wideTableOpensQuickly()` 以 1505 個 cell 的合成表格守住（實測 113ms，門檻 1500ms；
退化前光 325 個 cell 就要 2146ms，所以這個門檻能穩定抓到退回逐項重排）。

### 17.2 拖曳開檔

`MainWindow` 開啟 `acceptDrops`，並把 `QTextBrowser` 與側邊欄所有子 widget 的
`acceptDrops` 關掉 —— 否則它們會先收下 drop，事件冒泡不到主視窗。
拖入檔案就開、資料夾就換側邊欄的根，與路徑列共用 `onPathSubmitted()`。
一次拖多個時優先開 markdown 檔。

**測試方式的取捨**：合成 `QDropEvent` 送給 widget 在測試裡走不到 ——
`QWidget::event()` 是 protected，而 `QApplication::notify` 對拖放另有一套經過
`QDragManager` 的流程。所以把邏輯抽成公開的 `openFromUrls()`，`dropEvent` 只是
薄轉接；測試直接驗 `openFromUrls()`，另外用 `acceptDrops` 斷言驗「主視窗接受、
子 widget 不攔截」這條接線。

**誠實記錄**：真正的 X11 拖放動作沒有被自動化測試涵蓋（這台沒有 xdotool
之類的工具可以合成拖放序列），需要人工驗。

## 18. 多文件：分頁與比較模式（2026-09-01，branch `feat/multi-document`）

使用者的兩個需求：分頁開多份 markdown、分割畫面並排比較 2–4 份。
決策：**一列分頁 + 比較模式**（非 VS Code 那種編輯器群組）、**不同步捲動**、
**純並排不做 diff**。

### 18.1 前置重構

兩個功能需要同一個前置動作：把「單一文件的狀態」從 MainWindow 抽出來。
原本 `m_path` / `m_markdown` / `m_doc` / `m_backend` / `m_watcher` 都掛在
MainWindow 上，結構上只能開一份。

- `DocumentView`：一份文件的完整狀態與畫面（path、原始碼、Document、
  render backend、FileWatcher）。`MermaidCache` 由外部共用而非每份各持一份 ——
  它的 key 是內容雜湊，不同文件裡相同的圖表本來就該共用。
- `DocumentArea`：分頁列 + 比較模式的容器。

重構完成時既有的 108 個測試全數通過，這是安全網。

### 18.2 DocumentArea 的兩個設計選擇

**所有 DocumentView 常駐同一個 QSplitter，靠 setVisible() 決定顯示哪幾個。**
切換分頁與進出比較模式都不需要 reparent —— 把 widget 在容器之間搬移會丟掉
捲動位置與焦點。

**分頁順序的唯一真實來源是 QTabBar**：每個 tab 的 `tabData` 存對應的
DocumentView 指標，所以使用者拖曳排序後不需要同步任何平行清單。

比較模式顯示「從目前分頁起算連續 N 個」，右邊不夠時視窗往左滑
（`start = clamp(min(current, count - columns))`），所以分頁數 >= N 時一定滿欄。

### 18.3 踩到的坑：隱藏的 widget 不會排版

第一版 `DocumentArea::openFile()` 是「建立 view（隱藏）→ 載入檔案 → 掛上分頁 →
顯示」。結果所有圖片的尺寸都是 0。

原因是隱藏的 widget 不會觸發 QTextDocument 排版，`loadResource()` 就不會被呼叫，
`applyImageSizing()` 拿不到任何邏輯尺寸。

兩處修正：
1. 順序改成「掛上分頁並顯示 → 再載入檔案」。
2. `applyImageSizing()` 加防禦：若某張圖沒有記錄到尺寸，主動呼叫
   `doc->resource()` 逼 `loadResource` 跑一次。

另一個相關的坑：`DocumentView` 必須自己關掉 `acceptDrops` ——
分頁是動態建立的，MainWindow 在建構時遍歷子 widget 抓不到之後才出現的 view。

### 18.4 連結導航留在同一分頁

點連結若開新分頁，`INDEX.md` 那種 64 個連結的索引頁點幾下就爆掉。
所以連結一律在觸發它的分頁內換檔，由 `linkNavigationStaysInSameTab()` 盯住。
要另開分頁請用路徑列、檔案樹或拖曳。

### 18.5 狀態列的錯誤比值

比較模式截圖時發現狀態列出現「mermaid 3/2 產生中」—— 分子是全域佇列長度
（三個分頁共用一個 MermaidCache），分母是當前文件的圖表數，比出來沒有意義。
改成分開顯示：當前文件「mermaid N 張」，全域佇列「產生中 M 張」。

### 18.6 記憶體

| 情境 | PSS | RSS |
|---|---|---|
| 1 個分頁（sample.md） | 41.6 MB | 87.1 MB |
| 3 個分頁（含 325 cell 的 INDEX.md） | 43.7 MB | 92.2 MB |

**多開分頁的邊際成本比預估小得多** —— 我原本估 4 份會到 60–90MB，實際 3 份只比
1 份多 2.1MB。Qt 函式庫本身才是大宗，每份 QTextDocument 反而便宜。
所以「非作用中分頁延遲卸載」這個最佳化目前沒有必要做。

### 18.7 測試

新增 `tests/test_e2e_tabs.cpp`（16 個），總計 **8 個套件、124 個測試函式**。
涵蓋：重複開檔不重複分頁、分頁標題來源、切分頁同步路徑列與 TOC、關閉最後一個
分頁的空狀態、拖曳排序、**各分頁獨立監看自己的檔案**、連結留在同分頁、
比較欄數與滑動視窗、主題與縮放套用到所有分頁。

## 19. 改為 VS Code 式的 editor group（2026-09-01）

### 19.1 為什麼改

第一版做的是「一列全域分頁列 + 比較模式顯示連續 N 個」。使用者實際看到畫面後
指出問題：**分頁列只在最左邊那一格上方，看不出哪個分頁對應哪一格**。
分割成兩格時，第二個分頁的標籤畫在左格上方，但它的內容其實在右格。

改成每個面板有自己的分頁列，這件事就變成自明的。

### 19.2 結構

```
DocumentArea
 └── QSplitter
      ├── PaneGroup（PaneTabBar + QStackedWidget，自己的文件）
      ├── PaneGroup
      └── …（上限 4）
```

`PaneGroup` 持有它那一格的文件所有權，`addView()` 收下、`takeView()` 交出。
`DocumentArea` 仍提供「全域索引」（面板順序 × 面板內分頁順序）給
MainWindow 與設定持久化使用，所以 `openFile` / `nextTab` / `openPaths`
這些介面沒有變。

面板數量改由 `setPaneCount()` 控制：增加時從分頁最多的那一格勻一份出來，
減少時把最後一格併回前一格。**不會做出空面板** —— 面板數不超過文件數。

### 19.3 拖曳分頁自動分割

QTabBar 內建的拖曳只能在同一列內重新排序。要支援拖到別的面板、或拖到邊緣
自動分割，得自己接手：

- `PaneTabBar` 在游標**垂直離開**分頁列時發起 QDrag。用垂直距離判斷是刻意的 ——
  水平移動代表使用者想在同一列裡調順序，那條路徑要留給 QTabBar 自己處理。
- `PaneGroup` 接受放置，左右各 25%（至少 40px）是「在該側分割」，中間是
  「併入這一格」，並用 QRubberBand 顯示放置提示。
- 來源資訊記在 `DocumentArea`（同一時間只有一個拖曳），不把指標塞進 mime data。

**測試取捨**：合成跨 widget 的 QDrag 序列在測試裡跑不起來，所以放置邏輯抽成
公開的 `moveTabToPane(source, index, target, zone)`，`dropEvent` 只是轉接。
測試驗那個方法加上純邏輯的 `PaneGroup::zoneFor()`；真正的滑鼠拖曳動作
仍需人工驗。

### 19.4 作用中面板的標示

`setActive()` 第一版用 `parentWidget()->children().size()` 判斷是否已分割，
時機不對，結果強調線根本沒顯示 —— 是看截圖才發現的（測試當時沒有驗這件事）。
改由 `DocumentArea::refreshPaneIndicators()` 明確告知，並補上
`activePaneIsMarkedOnlyWhenSplit()` 驗證「只有一格時不顯示、分割後只有作用中
那格顯示、切換時跟著移動」。

教訓與 §14.4 同一類：**只在程式碼裡寫了不代表畫面上有**，宣稱之前要有測試或
截圖為證。

### 19.5 測試

`e2e_tabs` 從 16 個增為 21 個，總計 **8 個套件、129 個測試函式**。
新增：每格有自己的分頁列且內容正確、面板數不超過文件數、併回單格不遺失文件、
搬到相鄰面板會新建一格、放置區判定、拖到邊緣分割、拖到中間併入、
空面板自動收掉、作用中標示。
