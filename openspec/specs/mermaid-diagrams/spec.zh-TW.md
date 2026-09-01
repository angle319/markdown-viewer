# Mermaid 圖表

在不內嵌 JavaScript 引擎的前提下支援 mermaid。程式對每一張獨特的圖表呼叫一次外部的
`mmdc`（mermaid-cli），把結果快取到磁碟，之後就當成一般圖片顯示。
實作在 `src/core/MermaidCache.cpp` 與 `src/core/MmdcRenderer.cpp`。

一張已渲染圖表的穩態成本就是一個快取檔；mmdc 用到的瀏覽器引擎活在另一個短命的行程裡，
從不進駐檢視器本體。

> 本檔為 `spec.md` 的中文翻譯。兩份必須在同一個 commit 內同步更新；
> 若有出入，以英文版為準。

## 輸出格式

### Requirement: 圖表輸出 PNG 而非 SVG
`MmdcRenderer` SHALL 預設輸出由 mmdc 自己光柵化的 PNG。SVG 輸出仍可選擇，
但已知與 Qt 不相容。

Qt 的 `QSvgRenderer` 實作的是 SVG Tiny 1.2，沒有 `<marker>`。用 Qt 渲染 mermaid 的 SVG
會失去**所有連線與箭頭**、節點文字被畫在方框上緣、邊標籤旁出現灰色方塊、
原點還留下一個黑色三角形。

以 `flowchart LR  A[AAAAAAAA] --> B[BBBBBBBB]` 實測，計算兩個節點之間中央水平帶的
深色像素數——那個區域只可能被連線佔用：

| 路徑 | 中央帶墨水 |
|---|---|
| SVG 經 `QSvgRenderer` | **0** |
| PNG 經 Chromium | **249** |

#### Scenario: PNG 畫得出連線
- GIVEN 以預設設定渲染的雙節點流程圖
- WHEN 量測結果影像的中央帶
- THEN 其中含有連線的像素

#### Scenario: SVG 路徑仍然是壞的
- GIVEN 同一張圖以 SVG 輸出渲染
- WHEN 經 `QSvgRenderer` 光柵化後量測中央帶
- THEN 其墨水量至少比 PNG 路徑少三倍，但整張影像並非空白——
  也就是形狀畫得出來、連線畫不出來

這個 scenario 是一條絆線：若未來的 Qt 支援了 `<marker>`，它會失敗，屆時可重新評估 SVG。

### Requirement: 必須關閉 HTML 標籤
渲染器 SHALL 傳入 `htmlLabels: false` 的 mermaid 設定。mermaid 預設把節點文字放進
`<foreignObject>`，那是 Qt 完全不支援的，結果是形狀畫得出來但**文字全部空白**。

#### Scenario: 沒有 foreignObject
- GIVEN 一張含 CJK 標籤、以 SVG 模式渲染的圖
- WHEN 檢查輸出
- THEN 不含 `foreignObject`、含有真正的 `<text>` 元素、且含有該 CJK 標籤文字

### Requirement: 光柵化倍率跟隨顯示器
PNG 輸出 SHALL 以螢幕的 device pixel ratio 光柵化。在 1× 螢幕上用 2× 渲染是白花記憶體
而沒有可見效益（實測：兩張圖約 10MB PSS）。

#### Scenario: 倍率影響像素尺寸
- GIVEN 同一張圖分別以倍率 1 與 2 渲染
- WHEN 載入兩張影像
- THEN 後者的寬度是前者的兩倍

## 快取

### Requirement: 快取 key 要涵蓋內容、主題與渲染器
快取 key SHALL 為 `sha1(原始碼 + 主題 + rendererId)`，其中 `rendererId` 包含 mmdc 版本
與光柵化倍率。主題進 key 是因為明暗兩版的圖不同；版本與倍率進 key 是因為它們的產出不同。

#### Scenario: key 的敏感度
- GIVEN 一份圖表原始碼
- WHEN 計算 key
- THEN 不同原始碼得到不同 key、明暗主題的 key 不同、相同輸入重複計算得到相同 key

#### Scenario: 命中快取不重新渲染
- GIVEN 一張已在快取中的圖
- WHEN 再次請求
- THEN 不會啟動任何渲染行程，且快取檔的修改時間不變

### Requirement: 渲染必須序列化
快取 SHALL 在同一時間最多執行一次渲染。每次呼叫 mmdc 都會拉起一個 headless Chromium
（實測峰值約 106MB）；一份文件有多張圖時若平行執行，會把整個設計存在的理由——
記憶體優勢——抵銷掉。

#### Scenario: 三張圖
- GIVEN 連續請求三張未快取的圖
- WHEN 進行渲染
- THEN 三張都完成，且同時進行中的渲染數從未超過一

### Requirement: 渲染失敗不得留下快取
失敗時 SHALL 移除半成品輸出檔，讓下次請求會重試，而不是把截斷的檔案當成命中。

#### Scenario: 渲染器失敗
- GIVEN 一個會失敗的渲染器
- WHEN 請求一張圖
- THEN 回報失敗，且快取回報該圖為未快取

## 退化

### Requirement: 找不到 mmdc 不是錯誤
找不到 `mmdc` 時，mermaid 區塊 SHALL 以一般程式碼區塊顯示，狀態列 SHALL 提示一次安裝指令。
不跳錯誤對話框、不發出失敗訊號。

#### Scenario: mmdc 不存在
- GIVEN 一個回報自己不可用的渲染器
- WHEN 請求一張圖
- THEN 不會排入佇列，且不會發出 `rendered` 或 `failed` 訊號

### Requirement: PATH 之外也要找得到 mmdc
尋找順序 SHALL 為 `MARKDOWN_TOOL_MMDC`、`PATH`、再來是
`~/.nvm/versions/node/*/bin` 底下的 nvm 安裝位置。從桌面啟動器啟動時，
環境變數裡通常沒有 nvm。

#### Scenario: 從桌面捷徑啟動
- GIVEN 一個 PATH 不含 nvm bin 目錄的 session
- WHEN 渲染器尋找 mmdc
- THEN 仍能找到 nvm 安裝的執行檔

### Requirement: 進行中的渲染不得活得比視窗久
解構時渲染器 SHALL 終止仍在執行的行程並等待它結束，而不是任由 `QProcess`
在執行中被解構。

#### Scenario: 渲染中關閉程式
- GIVEN 一個正在進行的渲染
- WHEN 應用程式關閉
- THEN 不會出現 "QProcess: Destroyed while process is still running" 警告

## 顯示

### Requirement: 圖表在完成前先顯示佔位
未快取的圖 SHALL 立即顯示佔位圖，並在渲染完成後 SHALL 換成真正的影像，
過程中不遺失捲動位置。

#### Scenario: 第一次看到某張圖
- GIVEN 一份含有一張未快取 mermaid 圖的文件
- WHEN 開啟它
- THEN 先出現固定高度的佔位圖，稍後被換成高度不同、且寬度不超過內容欄的影像
