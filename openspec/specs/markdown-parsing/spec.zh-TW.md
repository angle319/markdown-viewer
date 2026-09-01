# Markdown 解析

把 markdown 原始碼轉成 Qt 富文字引擎看得懂的 HTML 子集。實作在
`src/core/MarkdownParser.cpp`，基於 vendored 的 md4c 0.5.2（`third_party/md4c/`）。
解析器**刻意自己實作 `MD_PARSER` callback** 而不是呼叫 md4c 內建的 `md_html()`——
因為 mermaid 攔截、標題錨點、語法高亮、圖片路徑改寫都必須在同一趟完成；
事後用正規表達式刮 HTML 字串太脆弱。

語法高亮在 `src/core/CodeHighlighter.cpp`，產生帶 inline `<span style>` 的著色，無外部依賴。

> 本檔為 `spec.md` 的中文翻譯。兩份必須在同一個 commit 內同步更新；
> 若有出入，以英文版為準。

## 文件結構

### Requirement: 標題抽取
解析器 SHALL 為每個標題產生一筆 `TocEntry`，包含層級（1–6）、去除 markup 後的純文字、
以及 slug 錨點。文件標題 SHALL 取第一個 H1 的文字；文件沒有 H1 時為空。

#### Scenario: 巢狀標題
- GIVEN 一份含有 `# One`、`## Two`、`### Three` 的文件
- WHEN 進行解析
- THEN TOC 有三筆，層級分別為 1、2、3，文字為 `One`、`Two`、`Three`

#### Scenario: 標題退回為空
- GIVEN 一份唯一標題是 `## sub` 的文件
- WHEN 進行解析
- THEN `Document::title` 為空，呼叫端改用檔名

### Requirement: 與 GitHub 相容的錨點
標題 slug SHALL 遵循 GitHub 規則：轉小寫、空白換成 `-`、丟棄標點、保留 `-` 與 `_`、
非 ASCII 字元（含 CJK）原樣保留。重複的 slug SHALL 依文件順序加上 `-1`、`-2`… 後綴。

#### Scenario: 標點與空白
- GIVEN 標題 `## Hello, World! (v2)`
- WHEN 進行解析
- THEN 錨點為 `hello-world-v2`

#### Scenario: 保留 CJK
- GIVEN 標題 `## 安裝說明` 與 `## 使用 方式`
- WHEN 進行解析
- THEN 錨點為 `安裝說明` 與 `使用-方式`

#### Scenario: 重複標題
- GIVEN 三個都叫 `重複` 的標題
- WHEN 進行解析
- THEN 錨點為 `重複`、`重複-1`、`重複-2`

## Qt 富文字相容性

Qt 富文字只支援 HTML 的一個子集。它畫不出來的結構 SHALL 在解析階段就改寫掉，
而不是照樣輸出然後祈禱。

### Requirement: 不支援的標籤要改寫
刪除線 SHALL 輸出為 `<s>`，不得是 `<del>`。待辦清單項目 SHALL 輸出為字元
`☑`（U+2611）與 `☐`（U+2610）；解析器 SHALL NOT 輸出 Qt 不會渲染的
`<input type="checkbox">`。

#### Scenario: 刪除線
- GIVEN 原始碼 `~~gone~~`
- WHEN 進行解析
- THEN HTML 含有 `<s>gone</s>` 且不含 `<del>`

#### Scenario: 待辦清單
- GIVEN 原始碼 `- [x] done` 與 `- [ ] todo`
- WHEN 進行解析
- THEN HTML 含有 `☑` 與 `☐`，且不含 `<input`

### Requirement: 文字必須轉義
文字內容 SHALL 做 HTML 轉義，讓文件裡的標記以字面顯示而不是被當成標籤解讀。

#### Scenario: 角括號與 &
- GIVEN 原始碼 `a < b & c > d`
- WHEN 進行解析
- THEN HTML 含有 `a &lt; b &amp; c &gt; d`

#### Scenario: 程式碼區塊裡的 script 標籤
- GIVEN 程式碼區塊內含 `<script>alert(1)</script>`
- WHEN 解析並渲染
- THEN 該標籤以字面文字顯示，永遠不會被解讀

## 圖片與連結

### Requirement: 相對圖片路徑要解析
相對的圖片來源 SHALL 以 markdown 檔所在目錄為基準解析，並改寫為絕對的 `file://` URL；
改寫前 SHALL 先做 percent-decoding。已帶 scheme 的來源 SHALL 原樣保留。

#### Scenario: 相對路徑
- GIVEN 基準目錄為 `/tmp/base` 的文件含有 `![alt](sub/pic.png)`
- WHEN 進行解析
- THEN HTML 含有 `src="file:///tmp/base/sub/pic.png"`

#### Scenario: 絕對 URL
- GIVEN `![a](https://example.com/y.png)`
- WHEN 進行解析
- THEN `src` 維持不變

## Mermaid 攔截

### Requirement: mermaid 區塊變成圖片佔位
啟用 mermaid 支援時，` ```mermaid ` 區塊 SHALL 從 HTML 中移除，並在原位置換成
`<img src="mermaid://<key>">`，其中 `<key>` 為圖表原始碼的 SHA-1 十六進位摘要。
圖表原始碼 SHALL 記錄在 `Document::mermaid`。

#### Scenario: 區塊被置換
- GIVEN 一份含有一個 mermaid 區塊的文件
- WHEN 啟用 mermaid 並解析
- THEN `Document::mermaid` 有一筆，HTML 含有 `<img src="mermaid://<key>"`，
  且圖表原始碼不再以程式碼區塊出現

#### Scenario: 停用 mermaid
- GIVEN 同一份文件以 `Options::mermaidEnabled == false` 解析
- WHEN 進行解析
- THEN `Document::mermaid` 為空、HTML 不含 `mermaid://`，圖表以一般程式碼區塊顯示

## 語法高亮

### Requirement: 程式碼高亮不依賴外部套件
程式碼區塊 SHALL 包在 `<pre>` 中，內容經 HTML 轉義，並以 inline
`<span style="color:…">` 著色。支援的語言為 C/C++、Python、JavaScript/TypeScript、
JSON、Bash/shell、CMake。未知語言 SHALL 退化為等寬、不著色的文字。

#### Scenario: 已知語言
- GIVEN 內容為 `return nullptr;` 的 `cpp` 區塊
- WHEN 進行高亮
- THEN 輸出含有 `return` 與 `nullptr` 的著色 span

#### Scenario: 未知語言
- GIVEN 標記為 `nosuchlang`、內容為 `some plain <text>` 的區塊
- WHEN 進行高亮
- THEN 輸出轉義了角括號，且不含 `<span`

### Requirement: 壞掉的字串不能吃掉整份檔案
未閉合的單行字串 SHALL 在該行結束處停止，而不是把文件其餘部分都當成字串內容著色。

#### Scenario: 少一個引號
- GIVEN C++ 原始碼 `a = "oops`，下一行是 `return 1;`
- WHEN 進行高亮
- THEN `return` 仍然被當作關鍵字著色
