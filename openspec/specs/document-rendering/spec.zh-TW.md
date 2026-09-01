# 文件渲染

顯示解析完成的 `Document`。實作在 `src/render/TextBrowserBackend.cpp`，
基於 `QTextBrowser`（Qt 的 `QTextDocument` 富文字引擎）——**沒有瀏覽器引擎、沒有 JavaScript**。
`IRenderBackend` 是刻意留的接縫：若哪天 Qt 的 CSS 子集不夠用，可以加一個 litehtml 後端，
而不必動到解析器、側邊欄或視窗。

Qt 富文字既不支援 `max-width` 也不支援 block 層級的框線，而且會**默默忽略**若干 CSS 屬性。
本規格中所有稱為「document tree walk」的動作，都是在 `setHtml()` 之後執行，用來補這些缺口。

> 本檔為 `spec.md` 的中文翻譯。兩份必須在同一個 commit 內同步更新；
> 若有出入，以英文版為準。

## 版面

### Requirement: 內容欄要限寬並置中
閱讀欄寬 SHALL 限制在 980 邏輯像素（`Theme::ContentWidth`，沿用 Okular markdown generator
的數值）並在視埠中置中。由於 Qt 富文字沒有 `max-width`，這件事 SHALL 用 viewport margin 達成。

#### Scenario: 寬視窗
- GIVEN 一個 1400px 寬、顯示單一文件的視窗
- WHEN 版面穩定後
- THEN 文字視埠不超過 990px 寬

### Requirement: 舒適的行高
內文、清單項目與表格儲存格 SHALL 使用 155% 的行高（`Typography::LineHeightPercent`）。
Qt 預設約等於單行間距，對中文而言太擠。

#### Scenario: 段落行高
- GIVEN 一份已渲染的文件
- WHEN 檢查其段落 block
- THEN 每個都是 `lineHeightType == ProportionalHeight` 且 `lineHeight == 155`

### Requirement: 字級要明確設定，不透過 CSS
正文字級 SHALL 用 `QTextDocument::setDefaultFont()` 設定，標題字級 SHALL 用明確的
tree walk 設定。兩者 SHALL NOT 依賴 stylesheet 的 `font-size`。

有兩個經實測確認的 Qt 行為使得 CSS 在這裡不可用：`body { font-size }` 對
`QTextDocument` **完全沒有效果**；而標題方面，只要 `QTextFormat::FontSizeAdjustment`
屬性存在（**即使值是 0**），它就會蓋過 `FontPointSize`。因此 walk SHALL 用
`clearProperty()` 清掉該屬性（設成 0 沒有用），並使用 `setCharFormat` ——
因為 `mergeCharFormat` 無法移除屬性。

#### Scenario: 標題級距
- GIVEN 一份含有 H1 到 H6 的文件
- WHEN 進行渲染
- THEN 實際字級為 23、17、14、12.5、11.5、11pt，都沒有 `FontSizeAdjustment` 屬性、
  都是粗體、級距嚴格遞減，且 H6 不小於 11pt 的正文

## 圖片

### Requirement: 圖片依固有尺寸決定顯示大小
`setHtml()` 之後，後端 SHALL 走一遍文件，為每張圖片明確設定由固有尺寸推得的顯示尺寸；
超過內容欄寬時等比縮小。若版面尚未執行過（例如載入文件時該 view 是隱藏的），
walk SHALL 主動請求該資源以取得尺寸。

#### Scenario: 每張圖都有正尺寸
- GIVEN 已渲染的 `docs/sample.md`
- WHEN 檢查其圖片片段
- THEN 每個的寬高皆大於 0，且寬度不超過 980

### Requirement: 缺少的圖片要退化為文字
無法載入的圖片 SHALL 換成標示檔名的斜體文字，而不是留下一個空白框。

#### Scenario: 壞掉的圖片參照
- GIVEN 文件含有 `![替代文字](nope.png)` 而該檔不存在
- WHEN 進行渲染
- THEN 純文字內容含有 `[缺少圖片: nope.png]`

### Requirement: 低對比圖片要墊底
帶 alpha 通道、可見像素平均亮度與頁面底色太接近的圖片 SHALL 合成到一塊中性底色卡片上
（含 padding），以確保看得見。mermaid 圖表為例外：它的主題由程式指定，本來就與頁面相符。

#### Scenario: 黑底頁面上的深色線稿
- GIVEN 一張可見像素偏暗的透明 PNG
- WHEN 在黑色主題下渲染
- THEN 它被畫在淺色卡片上，而不是消失

## 可讀性強制

### Requirement: 不得有看不見的文字
對每個文字片段，後端 SHALL 計算其前景色與**實際背景**（片段自身背景 → block 背景 →
頁面底色）的 WCAG 對比比，低於 4.5:1 時 SHALL 換成該背景上讀得到的顏色。
置換 SHALL 使用 `mergeCharFormat`，讓字型、粗細等其餘格式保留。

這條存在的理由是 markdown 可以內嵌寫死顏色的原始 HTML，那不是任何配色選擇擋得住的。

#### Scenario: 黑色主題下寫死的黑色文字
- GIVEN 文件含有 `<span style="color:#000000">隱形候選</span>`
- WHEN 在黑色主題下渲染
- THEN 該片段實際的前景色與黑底的對比比至少 4.5:1

#### Scenario: 全文件不變式
- GIVEN 刻意含有寫死顏色的 `docs/sample.md`
- WHEN 在任一主題下渲染
- THEN 沒有任何文字片段與其實際背景的對比比低於 4.5:1

## Qt 畫不出來的結構

### Requirement: 標題分隔線與引用色條要自繪
Qt 富文字沒有 block 層級的 `border-bottom` 或 `border-left`。因此 H1/H2 的底線與
引用區塊的左色條 SHALL 在 `paintEvent()` 中於文字之後自行繪製，且 SHALL 只掃可見範圍的
block，而非整份文件。

引用區塊 SHALL 以 `blockFormat().leftMargin()` 等於 `Theme::BlockquoteIndentPx` 辨識，
該常數由 stylesheet 與繪製程式碼共用。這是一份契約：清單項目因為有 `textList()` 而被排除，
標題則因為左邊距為 0 而被排除。連續的引用 block SHALL 合併成一條色條。

#### Scenario: 引用辨識契約
- GIVEN 已渲染、含有兩段引用區塊的文件
- WHEN 檢查 block 格式
- THEN 恰好那兩個引用 block 的 `leftMargin == Theme::BlockquoteIndentPx` 且沒有
  `textList()`，而每個標題 block 的 `leftMargin == 0`

### Requirement: 表格只畫橫線
表格 SHALL 只畫橫線，不畫直線與外框；表頭下方的線較粗，儲存格內距 8px。
這 SHALL 使用 Qt 原生的儲存格框線，而該功能需要
`QTextTableFormat::setBorderCollapse(true)`——**沒有這一行 Qt 根本不會畫**儲存格層級的框線。

#### Scenario: 表格框線
- GIVEN 一個已渲染的表格
- WHEN 檢查其格式
- THEN `border() == 0`、`borderCollapse()` 為真、`cellPadding() == 8`、
  每個儲存格的左右框線為 0、表頭列的下框線至少 2、其餘每列的上框線為 1

## 縮放

### Requirement: 縮放時標題與內文要等比
縮放 SHALL 實作為後端持有的明確倍率——每階 1.1 倍、限制在 0.5×–3.0×——同時套用到
文件預設字型與明確設定的標題字級，且 SHALL NOT 使用 `QTextEdit::zoomIn()`：
後者只改 widget 字型，因此動不到明確設過字級的標題。

#### Scenario: 等比縮放
- GIVEN 一份已渲染的文件
- WHEN 觸發放大兩次
- THEN 內文與 H1 都變大，且兩者的放大倍率差距小於 2%

#### Scenario: 上下限與復原
- GIVEN 連續觸發放大 40 次後再縮小 80 次
- WHEN 檢查字級
- THEN 從未超過基準的 3 倍、也未低於 0.5 倍，且「原始大小」能回到初始值

## 效能

### Requirement: 文件修改必須批次化
每個 document tree walk SHALL 把它的修改包在單一
`beginEditBlock()`/`endEditBlock()` 內，且文件 SHALL 關閉 undo
（`setUndoRedoEnabled(false)`）。少了任一項，每次 `setCharFormat` 或 `setFormat`
都會觸發一次完整重排並推入一筆 undo 指令，成本對儲存格數或片段數呈二次方成長。

實測：一份 6.9KB、72 行但含 65 列表格（325 個儲存格）的文件，修正前開檔 2146ms，
修正後 23ms。

#### Scenario: 寬表格
- GIVEN 一份合成的、表格至少 1500 個儲存格的文件
- WHEN 開啟它
- THEN 在 1500ms 內完成
