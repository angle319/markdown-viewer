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
| `Ctrl+L` | **聚焦路徑列**（全選，可直接覆寫） |
| `Ctrl+W` | 關閉分頁 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 下一個／上一個分頁 |
| `Alt+1`…`Alt+9` | 跳到第 N 個分頁 |
| `Ctrl+Shift+1`…`4` | 面板數（1 = 不分割）|
| `Ctrl+Shift+←` / `→` | 把分頁搬到左／右邊的面板 |
| `Ctrl+O` | 用檔案對話框開啟 |
| `F5` | 重新載入 |
| `F9` | 側邊欄顯示／隱藏 |
| `Alt+Shift+1` | 白色主題 |
| `Alt+Shift+2` | 黑色主題 |
| `Alt+Shift+T` | 切換主題 |
| `Ctrl++` / `Ctrl+-` / `Ctrl+0` | 縮放 |

### 路徑列

視窗最上方是可編輯的路徑列，行為比照瀏覽器的網址列：

- `Ctrl+L` 聚焦並全選，直接打新路徑後 Enter 開啟
- 輸入**資料夾**會切到側邊欄的「檔案」分頁並換根，而不是把資料夾當 markdown 開
- 支援 `~` 展開、相對路徑（相對目前檔案所在目錄）、以及貼上 `file://` URL
- 自動完成由 `QFileSystemModel` 提供
- `Esc` 還原成目前檔案的路徑並把焦點交回內容區
- 路徑不存在時只在狀態列說明，不會關掉目前開著的文件

### 分頁與分割面板

VS Code 的 editor group 模型：**每個分割面板有自己的分頁列**，一份文件屬於其中
一格。這樣「哪個分頁對應哪一格」在畫面上是自明的。作用中的那一格會在分頁列
下方顯示一條強調色細線（只有一格時不顯示）。

分割的產生方式有三種：

1. 選單／`Ctrl+Shift+1..4` 選面板數
2. `Ctrl+Shift+←/→` 把目前分頁搬到相鄰面板（沒有就新建一格）
3. **把分頁拖到某一格的左右邊緣** —— 自動在該側開一格；拖到中間則是併入該格

空掉的面板會自動收掉，面板上限 4 格。並排**不同步捲動、也不標示差異**。

連結導航**在同一個分頁內換檔**，不開新分頁：像 `INDEX.md` 那種有幾十個連結的
索引頁，每點一次新增一個分頁很快就爆掉。要另開分頁請用路徑列、檔案樹或拖曳。

### 拖曳

把 markdown 檔或資料夾拖進視窗即可開啟／換根，與路徑列走同一條路徑。
一次拖多個時**優先開 markdown 檔**，沒有檔案才收資料夾。

### 側邊欄

兩個分頁：**段落**（標題樹，隨捲動高亮）與**檔案**（只顯示資料夾與
markdown 類檔案）。檔案變更會自動重新載入並保留捲動位置。

## 排版

樣式基準是拿 Chrome extension「Markdown Reader」的實際 computed style 比對出來的
（用本機 HTTP 伺服器餵同一份 `docs/sample.md` 給它，再抓 `getComputedStyle`）：

| 元素 | 處理 |
|---|---|
| 行內 `` `code` `` | 專屬洋紅色 + 底色 chip + 等寬字 |
| 連結 | 連結色 + **底線**（不只靠顏色區分） |
| H1 / H2 | 底部 1px 分隔線（自繪，見下） |
| 引用區塊 | 左側 4px 色條（自繪），連續段落合併成一條 |
| 表格 | 只有橫線（`borderCollapse` + 每列上框線）、表頭下方加粗、`cellPadding=8` |
| 巢狀清單 | `setIndentWidth(20)` |
| 標題字級 | H1→H6 為 23/17/14/12.5/11.5/11pt，正文 11pt；H6 用次要色 |
| 行高 | 155%（Qt 預設約單行，中文太擠）；程式碼區塊同值 |
| 段距 | 段落上下各 10px；清單項目 0、清單整體上下 4px |

行內 code 的顏色刻意與連結**色相差 > 110°**。extension 那邊兩者同色，實際上分不出
「這是程式碼」還是「這是可點的連結」。另外區分也不只靠顏色：連結有底線、
行內 code 有 chip 與等寬字。

**標題字級不是用 CSS 設的。** `QTextFormat::FontSizeAdjustment` 這個屬性只要存在
（即使值是 0），Qt 就完全忽略 `FontPointSize`，改用「預設字級 × 層級係數」——
實測 H1 設 23pt 卻畫成 18pt、**H5 設 11.5pt 卻畫成 7.2pt，比正文還小**。
修法是在 `applyHeadingScale()` 裡 `clearProperty()` 掉它（設成 0 沒有用），
再明確指定字級。`Theme::headingPointSize()` 是唯一定義處，由
`headingSizesFollowThemeScale()` 盯住（含「階層必須嚴格遞減」與
「H6 不得小於正文」）。

**標題分隔線與引用色條是自繪的**，因為 Qt rich-text 不支援 block 層級的
`border-bottom` / `border-left`。`MdTextBrowser::paintEvent()` 在文字畫完後補上，
只掃可見範圍的 block。引用區塊的辨識靠 `blockFormat().leftMargin()` 等於
`Theme::BlockquoteIndentPx` —— 那個常數同時被 CSS 與繪製程式碼使用，是明確的契約，
由 `blockquoteMarkerContractHolds()` 盯住（清單項目有 `textList()`、
標題的 `leftMargin` 是 0，所以不會誤判）。

## 主題與對比

兩套主題：**白色**（`#ffffff`）與**黑色**（`#000000`），**預設黑色**。

主題在啟動時就無條件套用一次（不是等使用者切換才套）。否則整個 app 會沿用系統
GTK 主題的底色，跟文件內容的主題對不起來 —— 實際踩過：視窗是深藍灰色，
既不是白色也不是黑色。

對比是硬性要求而非美感偏好，全部用 WCAG 2.1 相對亮度計算並由測試釘住門檻：
正文 ≥ 7:1、次要文字與連結 ≥ 4.5:1、框線等非文字元素 ≥ 3:1。
`tests/test_theme.cpp` 會檢查主題配色、`QPalette` 的每一組 role、
以及所有語言的語法高亮顏色（實際產出 HTML 後逐一抽出來算，共 208 組）。

除了「配色本身合格」，還有兩道執行期的保護，因為 markdown 可以內嵌任意 HTML：

1. **文字對比修正**：算每個文字片段與其**實際背景**（片段背景 → block 背景 →
   頁面底色）的對比，不足 4.5:1 就換成該背景上讀得到的顏色。這救的是
   `<span style="color:#000000">` 這類寫死顏色在黑色主題下隱形的情況。
2. **低對比圖片墊底**：透明背景的圖片若內容亮度與頁面底色太接近，會墊一層
   中性底色。mermaid 不走這條 —— 它的主題由我們指定，本來就與頁面相符。

`docs/sample.md` 的「對比保護」章節就是這幾種情況的語料，
`everyTextFragmentIsReadableInBothThemes()` 會在兩個主題下掃全文件驗證。

踩過的坑：`QPalette` 只設 `Window`/`Base`/`Text` 是不夠的。`QTabBar` 與
`QMenuBar` 會用預設的 `Button`/`ButtonText`（淺色系）去畫，黑色主題下就變成
隱形的分頁標籤與隱形選單列。現在 role 設滿，並由
`paletteHasNoDefaultLightRolesInBlackTheme()` 盯住。

`docs/sample.md` 涵蓋所有 v0.1 該處理的語法，`docs/headings.md` 專門涵蓋
H1–H6 的層級（含標題緊接標題、標題內含行內樣式），兩份都用來手動驗收。

## 效能

document tree walk（圖片尺寸、標題字級、表格樣式、對比修正）的修改**必須包在
單一 `beginEditBlock`/`endEditBlock` 裡**，而且文件要關掉 undo
（`setUndoRedoEnabled(false)`）。否則每一次 `setCharFormat` / `setFormat` 都會
觸發一次重新排版，在 cell 或片段多的文件上是二次方級的成本。

實測：一份 6.9KB、只有 72 行但含 65 列表格（325 個 cell）的文件，
修復前開檔 **2146ms**，修復後 **23ms**（93 倍）。
`wideTableOpensQuickly()` 用 1505 個 cell 的合成表格守住這件事。

## 記憶體

量整個行程樹的 PSS（`/proc/<pid>/smaps_rollup`）。RSS 會把共享函式庫頁面
重複計算，不作為結論。

| 情境 | PSS | RSS |
|---|---|---|
| 三行小檔（基準） | 33.5 MB | 66.4 MB |
| `docs/sample.md`（含 2 張 mermaid） | 39.0 MB | 82.0 MB |

其中一個關鍵設定是 `QT_XCB_GL_INTEGRATION=none`（已寫在 `src/main.cpp`）：
xcb QPA 的 GL 整合會把 Mesa 的 llvmpipe 連帶 `libLLVM` 拉進行程，光那一顆就
**13.2 MB PSS**，而這個 app 全程 raster 繪製、完全不用 OpenGL。設掉之後
基準從 49.1MB 降到 32.7MB（加入路徑列後為 33.4MB）。

## 測試

```
ctest --test-dir build --output-on-failure
```

131 個測試函式、8 個套件：

| 套件 | 函式數 | 內容 |
|---|---|---|
| markdownparser | 15 | 錨點規則、mermaid 抽取、轉義、圖片路徑 |
| codehighlighter | 8 | 各語言著色、退化、未閉合字串 |
| mermaidcache | 8 | key 敏感度、佇列序列化、degrade 路徑 |
| mmdc_integration | 9 | 真的跑 mmdc；SVG-vs-PNG 的連線墨水差分 |
| theme | 16 | WCAG 對比門檻：配色、palette role、語法高亮、行內 code 色相 |
| e2e_viewer | 26 | 驅動真正的 MainWindow；路徑列、主題、拖曳流程 |
| e2e_regression | 26 | 以 sample.md / headings.md 為語料庫釘住 pipeline 不變式與樣式 |
| e2e_tabs | 23 | 分頁、分割面板、拖曳分割、面板幾何不變式、各分頁獨立監看 |

e2e 用 `QT_QPA_PLATFORM=offscreen` 跑，不需要 X／Wayland。`mmdc` 不在時
整合測試與 mermaid e2e 會自己 skip，不算失敗。

視覺檢查（自動化斷言驗結構，驗不了「看起來對不對」）：

```
MD_E2E_DUMP=/tmp/shots QT_QPA_PLATFORM=offscreen ./build/test_e2e_regression
```

也可以指定別的文件與段落，方便針對特定樣式重現檢查：

```
MD_E2E_DUMP=/tmp/shots MD_E2E_DOC=docs/headings.md \
  MD_E2E_ANCHORS=h1-文件標題,h2-混合內容 \
  QT_QPA_PLATFORM=offscreen ./build/test_e2e_regression
```

會輸出白／黑兩個主題下的頂端、程式碼、mermaid、表格、對比保護章節共 10 張 PNG。

## 狀態

v0.1 功能完成，已在真實 X11 與 offscreen 下驗證。

`docs/superpowers/specs/2026-08-31-markdown-tool-design.md` 第 12 節記錄了
所有實測結果，包含 SVG 路線失敗的完整證據。
