# 工作區

文件本身以外的一切：分頁、分割面板、路徑列、側邊欄、檔案監看、拖放與設定持久化。
實作在 `src/DocumentView.cpp`（單一文件）、`src/PaneGroup.cpp`（一格與它的分頁）、
`src/DocumentArea.cpp`（面板集合）與 `src/MainWindow.cpp`（外殼）。

分割模型採 VS Code 的 editor group：**一份文件只屬於一格，每一格有自己的分頁列**。
先前用單一全域分頁列的設計在實際使用後被推翻——分頁列只在最左那一格上方，
根本看不出哪個分頁對應哪一格。

> 本檔為 `spec.md` 的中文翻譯。兩份必須在同一個 commit 內同步更新；
> 若有出入，以英文版為準。

## 文件與分頁

### Requirement: 一個開啟的檔案對應一個分頁
開檔 SHALL 在作用中的面板建立一個分頁。開啟一個**已經開著**的檔案（不論在哪一格）
SHALL 切到既有分頁，而不是再開一個。路徑比對 SHALL 使用絕對路徑。

#### Scenario: 兩個檔案
- GIVEN 開啟兩個不同的檔案
- WHEN 檢查分頁
- THEN 有兩個分頁，各自持有不同的文件

#### Scenario: 重複開啟
- GIVEN 第一個分頁已開著某檔，目前作用中的是第二個分頁
- WHEN 再次開啟同一個檔（包含 `./a.md` 這種非正規化路徑）
- THEN 不會新增分頁，且第一個分頁變成作用中

### Requirement: 分頁標題取自文件
分頁標題 SHALL 為文件的第一個 H1，沒有時退回檔名，且 SHALL 隨文件變動而更新。

#### Scenario: 標題與退回
- GIVEN 一個檔含 `# 我的標題`、另一個沒有標題
- WHEN 兩者都開啟
- THEN 分頁分別顯示 `我的標題` 與第二個檔的檔名，且兩者的 tooltip 都帶完整路徑

### Requirement: 關掉最後一個分頁要留下可用的空狀態
關掉所有分頁 SHALL 清空視窗標題、路徑列與目錄，並 SHALL 顯示提示說明開檔的方式。
之後再開檔 SHALL 能正常運作。

#### Scenario: 關掉唯一的分頁
- GIVEN 一份開啟中的文件
- WHEN 關閉它的分頁
- THEN 沒有作用中的 view、視窗標題為 `markdown-tool`、目錄為空，
  且有可見的提示提到如何開檔

### Requirement: 每份文件監看自己的檔案
每份文件 SHALL 監看自己的檔案，變更時以 150ms 去彈跳重新載入並保留捲動位置，
**不論它是不是作用中的分頁**。監看 SHALL 能撐過編輯器的 atomic save
（寫暫存檔再 rename 覆蓋）。

#### Scenario: 背景分頁的檔案被編輯
- GIVEN 兩份文件開啟中、第二個為作用中
- WHEN 第一份文件的檔案在磁碟上增加了一個標題
- THEN 第一份文件重新載入且其目錄變長，作用中的分頁不受影響

### Requirement: 連結導航留在同一個分頁
點擊連往另一個 markdown 檔的連結 SHALL 置換**點擊發生的那個分頁**的內容，
而不是開新分頁。否則一個有幾十個連結的索引頁會生出幾十個分頁。
要開新分頁請用路徑列、檔案樹或拖放。

#### Scenario: 相對連結
- GIVEN 一份含有連往另一個 markdown 檔之相對連結的文件
- WHEN 觸發該連結
- THEN 分頁數不變，該分頁的標題與路徑列更新為目標檔

## 分割面板

### Requirement: 每一格擁有自己的分頁列
每一格 SHALL 顯示一個分頁列，內容恰好是該格裡的文件。作用中的那一格 SHALL 在分頁列
下方以強調線標示，且僅在面板超過一格時顯示。

#### Scenario: 兩格
- GIVEN 三份文件分割成兩格
- WHEN 檢查各格
- THEN 每格有自己的分頁列且項目與該格的文件相符、沒有文件遺失、
  恰好一格顯示作用中標示

### Requirement: 面板數受內容限制
面板 SHALL 最多 4 格，且 SHALL 不超過文件數，因此不會出現空的面板。
減少面板數 SHALL 把文件併回，而不是關閉它們。

#### Scenario: 面板數超過文件數
- GIVEN 兩份文件
- WHEN 要求 4 格
- THEN 得到 2 格

#### Scenario: 併回
- GIVEN 兩格各一份文件
- WHEN 要求 1 格
- THEN 得到一格，內含兩份文件

### Requirement: 拖曳分頁可分割或併入
把分頁拖出它的分頁列 SHALL 啟動跨面板拖曳。放在某一格左側或右側四分之一區域
（至少 40px）SHALL 在該側新建一格；放在中間 SHALL 把分頁移入該格。
拖曳過程中放置目標 SHALL 顯示提示。

Qt 的 `QTabBar` 只能在自己內部重新排序，所以當游標**垂直**離開分頁列時
SHALL 由我們自己發起拖曳——水平移動留給 Qt 自己的重排。

#### Scenario: 放在邊緣
- GIVEN 一格內有兩份文件
- WHEN 把一個分頁放到該格的右側四分之一
- THEN 右側新建一格，內含該文件

#### Scenario: 放在另一格中間
- GIVEN 兩格各一份文件
- WHEN 把右格的分頁放到左格中間
- THEN 兩份文件都在左格，空掉的右格被移除

> 真正的 X11 拖曳手勢**沒有**被自動化測試涵蓋；放置邏輯是透過
> `DocumentArea::moveTabToPane()` 驗證，接線則另外斷言。

### Requirement: 面板要維持可用的寬度
面板 SHALL 有 240px 的最小寬度，且在任何結構變動後 SHALL 取得相近的寬度。
`QSplitter` 只給新插入的 widget 它的 sizeHint，這讓拖曳新建的面板只有約 150px——
窄到表格與行內 code 會被逼到逐字換行。

#### Scenario: 三格
- GIVEN 1400px 視窗中三份文件分割成三格
- WHEN 量測寬度
- THEN 沒有一格低於 200px，且最寬的不超過最窄的兩倍

### Requirement: 結構變動後不得留下殘影
任何結構變動後，該區域 SHALL 明確重繪各面板。X11 在放置之後不保證送出 expose 事件，
先前導致上一影格的內容明顯疊在分頁列上。

#### Scenario: 幾何不變式
- GIVEN 任何一連串的分割、併回與分頁搬移
- WHEN 檢查各面板
- THEN 每份文件都在某一格的堆疊 widget 內、只有各格的當前文件可見、
  且每個可見文件的上緣都在該格分頁列的下方

### Requirement: 分頁要有右鍵選單
在分頁上按右鍵 SHALL 提供：關閉、關閉其他、關閉右側全部、關閉這一格、
移到右邊面板、移到左邊面板。不適用的項目 SHALL 停用。

#### Scenario: 停用狀態
- GIVEN 單一面板內三份文件，在中間的分頁按右鍵
- WHEN 建立選單
- THEN 「關閉其他」與「關閉右側全部」為啟用、「關閉這一格」為停用；
  而在最後一個分頁上，「關閉右側全部」為停用

## 導航

### Requirement: 路徑列的行為比照網址列
路徑列 SHALL 顯示作用中文件的路徑、SHALL 由 `Ctrl+L` 聚焦並全選、
SHALL 在 Enter 時開啟輸入的路徑、SHALL 在 Escape 時還原目前路徑並把焦點交回文件。
它 SHALL 接受 `~`、相對於目前文件所在目錄的路徑、以及 `file://` URL。
SHALL 提供來自檔案系統的自動完成。

#### Scenario: 路徑解析
- GIVEN 路徑列
- WHEN 解析 `~`、`~/x.md`、`other.md`（相對 `/tmp/base`）、`../up.md`
  （相對 `/tmp/base/sub`）、前後有空白的絕對路徑、以及 `file:///tmp/x.md`
- THEN 分別得到家目錄、`<home>/x.md`、`/tmp/base/other.md`、`/tmp/base/up.md`、
  去除空白的絕對路徑、以及 `/tmp/x.md`

#### Scenario: 資料夾不會被當成 markdown 開啟
- GIVEN 在路徑列輸入一個資料夾
- WHEN 送出
- THEN 側邊欄切到「檔案」分頁並以該資料夾為根，目前的文件維持開啟

#### Scenario: 不存在的路徑只回報，不破壞
- GIVEN 一個不存在的路徑
- WHEN 送出
- THEN 狀態列說明，目前的文件維持開啟

### Requirement: 側邊欄有目錄與檔案樹
側邊欄 SHALL 有兩個分頁。「段落」SHALL 顯示作用中文件依層級巢狀的標題、
點擊時捲到該標題、並高亮目前位於視埠頂端的標題。
「檔案」SHALL 只顯示資料夾與 markdown 類檔案
（`.md`、`.markdown`、`.mdx`、`.mdc`、`.mkd`、`.txt`）。

#### Scenario: 標題樹
- GIVEN 一份含一個 H1、三個 H2，且第一個 H2 下有一個 H3 的文件
- WHEN 建立目錄
- THEN 有一個頂層項目、其下三個子項目，第一個子項目再有一個子項目

#### Scenario: 檔案過濾
- GIVEN 一個含有 `main.md`、`other.md`、`notes.txt`、`ignore.cpp` 的資料夾
- WHEN 「檔案」分頁以該資料夾為根
- THEN 列出那三個 markdown 類檔案，不含 `ignore.cpp`

### Requirement: 檔案可以拖進視窗
拖入 markdown 檔 SHALL 開啟它；拖入資料夾 SHALL 換「檔案」分頁的根。
一次拖入多個項目時，markdown 檔 SHALL 優先於資料夾。
拖入其他東西 SHALL 只在狀態列回報，不關閉目前的文件。

子 widget SHALL NOT 接受拖放，否則事件永遠到不了視窗。

#### Scenario: 混合拖放
- GIVEN 同時包含一個資料夾與一個 markdown 檔的拖放
- WHEN 進行處理
- THEN 開啟該 markdown 檔

> 與分頁拖曳相同，真正的 X11 拖放手勢沒有自動化涵蓋；邏輯透過
> `MainWindow::openFromUrls()` 驗證，子 widget 的 `acceptDrops` 旗標另外斷言。

## 設定持久化

### Requirement: 工作階段要還原
視窗幾何、分隔比例、主題、側邊欄顯示與分頁、檔案樹根目錄、面板數、
開啟中的文件清單與作用中者，SHALL 在關閉時儲存並於啟動時還原。
命令列參數 SHALL 優先，且 MAY 指定多個檔案，每個一個分頁。

#### Scenario: 重新啟動
- GIVEN 關閉時有三份文件開啟中
- WHEN 不帶參數啟動
- THEN 同樣的三份文件被重新開啟

## 快捷鍵

### Requirement: 文件所載的快捷鍵必須有綁定
程式 SHALL 綁定：`Ctrl+L` 路徑列、`Ctrl+O` 開檔對話框、`F5` 重新載入、
`Ctrl+W` 關閉分頁、`Ctrl+Tab`/`Ctrl+Shift+Tab` 下／上一個分頁、
`Alt+1`–`Alt+9` 跳至分頁、`Ctrl+Shift+1`–`4` 面板數、
`Ctrl+Shift+←`/`→` 在面板間搬移分頁、`F9` 側邊欄、
`Alt+Shift+1`/`2` 白／黑主題、`Alt+Shift+T` 切換主題、
`Ctrl+=`/`Ctrl++` 放大、`Ctrl+-` 縮小、`Ctrl+0` 原始大小。

### Requirement: 放大要在一般鍵盤上按得到
放大 SHALL 除了 `QKeySequence::ZoomIn` 之外，另外綁定 `Ctrl+=`。
在 Linux 上那個標準序列解析為 `Ctrl++`，而多數鍵盤配置需要按 `Ctrl+Shift+=`——
只綁它的話，最自然的那個按鍵組合什麼都不會發生。

#### Scenario: Ctrl+= 有綁定
- GIVEN 放大這個動作
- WHEN 檢查它的快捷鍵
- THEN 其中同時包含 `Ctrl+=` 與 `QKeySequence::ZoomIn`
