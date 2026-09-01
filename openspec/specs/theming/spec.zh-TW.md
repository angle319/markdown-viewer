# 主題

兩套主題——白色（`#ffffff`）與黑色（`#000000`）——定義在 `src/Theme.cpp`。
對比被視為**硬性要求**而非美感偏好：每一組顏色都以 WCAG 2.1 對比比表示，
並由 `tests/test_theme.cpp` 釘住。改動顏色而打破門檻會讓建置失敗。

門檻：正文 ≥ 7:1（AAA）、次要文字與連結 ≥ 4.5:1（AA）、框線等非文字元素 ≥ 3:1。

> 本檔為 `spec.md` 的中文翻譯。兩份必須在同一個 commit 內同步更新；
> 若有出入，以英文版為準。

## 調色盤

### Requirement: 主題是純白與純黑
白色主題的底色 SHALL 為 `#ffffff`，黑色主題 SHALL 為 `#000000`。黑色主題 SHALL 為預設。

#### Scenario: 預設值
- GIVEN 沒有既有設定的首次啟動
- WHEN 視窗開啟
- THEN 使用黑色主題

### Requirement: 啟動時無條件套用主題
即使解析出來的模式等於初始值，應用程式的 palette SHALL 在啟動時套用一次。
跳過這一步會讓整個 app 停留在系統（GTK）的 palette，與文件內容的配色對不起來——
實際觀察到的是一個既不是白色也不是黑色的深藍灰視窗。

#### Scenario: 以預設模式啟動
- GIVEN 既有設定所選的模式恰好等於程式的初始值
- WHEN 建構視窗
- THEN `qApp` 仍然會收到主題 palette

### Requirement: 每個 palette role 都要設
`Theme::palette()` SHALL 設定完整的顏色 role，而不只是 `Window`、`Base`、`Text`。
`QTabBar` 與 `QMenuBar` 是用 `Button`/`ButtonText` 繪製的；把那些留在預設值，
在黑色主題下會得到看不見的分頁標籤與看不見的選單列。

#### Scenario: role 配對可讀
- GIVEN 任一主題的 palette
- WHEN 量測每一組配對
- THEN `WindowText`/`Window` ≥ 4.5:1、`Text`/`Base` ≥ 7:1、`Text`/`AlternateBase` ≥ 7:1、
  `ButtonText`/`Button` ≥ 4.5:1、`ToolTipText`/`ToolTipBase` ≥ 4.5:1、
  `HighlightedText`/`Highlight` ≥ 4.5:1、`Link`/`Base` ≥ 4.5:1、
  `PlaceholderText`/`Base` ≥ 3:1、`Mid`/`Window` ≥ 3:1，停用狀態的 `WindowText` ≥ 3:1

#### Scenario: 黑色主題不得漏進預設的淺色 role
- GIVEN 黑色主題的 palette
- WHEN 量測背景類 role（`Window`、`Base`、`AlternateBase`、`Button`、`ToolTipBase`）
- THEN 每個的相對亮度低於 0.2，而每個文字類 role 高於 0.3

## 文件顏色

### Requirement: 行內 code 要與連結區分得出來
行內 `code` SHALL 有自己的前景色與帶色底的 chip，且其**色相**與連結色 SHALL 相差至少 60°。

可區分性 SHALL NOT 用對比比判斷：對比比只衡量亮度，所以紫色 `#6f42c1` 與藍色 `#0b57d0`
只有 1.13:1，儘管它們明顯是不同顏色。用對比比當判準會把修正方向推向改亮度而不是改色相。

顏色也不是唯一線索——連結有底線，行內 code 是等寬字加底色 chip——
所以對色覺不同的讀者這個區分依然成立。

#### Scenario: 行內 code 配色
- GIVEN 任一主題
- WHEN 量測行內 code 的顏色
- THEN 前景對其 chip ≥ 4.5:1、chip 對頁面底色介於 1.05:1 與 2:1 之間、
  色相與連結色相差 ≥ 60°、且前景與正文色不同

### Requirement: 語法高亮必須保持可讀
高亮器產生的每一個顏色 SHALL 與程式碼區塊底色至少有 4.5:1 的對比，
兩個主題、每個支援的語言皆然。

#### Scenario: 所有語言、兩個主題
- GIVEN 兩個主題下每個支援語言的高亮輸出
- WHEN 把輸出中每個 `color:` 對照同一段輸出所宣告的 `<pre>` 底色量測
- THEN 每一組都至少 4.5:1

### Requirement: stylesheet 要用具名 token
產生的 stylesheet SHALL 以具名 token（`@TEXT@`、`@CODE_FG@`…）取代來組成，
SHALL NOT 使用位置引數 `%1`。

`QString::arg` 的多引數版本是把**字串中出現的 placeholder 由小到大**依序取代，
而不是把 `%N` 對應到第 N 個引數，所以少用掉一個編號會讓其後全部錯位。
這件事真的發生過：行內 code 的 `background-color` 拿到了前景色，
而測試竟然還是綠的——因為對比修正隨後把前景換成了可讀的顏色。

#### Scenario: 沒有殘留
- GIVEN 任一主題的文件 stylesheet
- WHEN 產生它
- THEN 內容不含 `@`、不含 `%<數字>`，且每個主題顏色都出現在裡面

## 分頁狀態

### Requirement: 選取中的分頁必須一眼認得出
選取中的分頁 SHALL 使用頁面底色、正文色、粗體與頂端強調線；
未選取的分頁 SHALL 使用比頁面沉一階的底色與次要文字色。
`QTabBar` 預設的選取狀態只差一點點底色深淺，使用者回報「不知道 focus 在哪」。

#### Scenario: 分頁對比
- GIVEN 任一主題
- WHEN 量測分頁顏色
- THEN 選取中的文字對其底色 ≥ 7:1、未選取的文字對其底色 ≥ 4.5:1、
  兩種底色相差 ≥ 1.2:1、選取中的文字比未選取更顯眼、強調線對頁面 ≥ 3:1

## 退路

### Requirement: 任何背景都要有可讀的前景
`Theme::readableOn(bg, mode)` 在主題文字色對 `bg` 達到 4.5:1 時 SHALL 回傳該色，
否則 SHALL 回傳純黑或純白中對比較高的那一個。

#### Scenario: 主題文字色會隱形
- GIVEN 黑色主題與白色背景
- WHEN 呼叫 `readableOn`
- THEN 回傳一個至少達 4.5:1 的深色

#### Scenario: 任意背景
- GIVEN 從 0 到 255、間隔 5 的所有灰階，外加數個高彩度顏色
- WHEN 在兩個主題下各自呼叫 `readableOn`
- THEN 回傳的顏色總是至少達到 4.5:1
