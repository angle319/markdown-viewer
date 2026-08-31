#pragma once

#include "core/Document.h"

/// 解析選項。
///
/// 刻意定義在 MarkdownParser 之外：巢狀類別的 default member initializer
/// 在同一個 class 內被當作預設引數使用時是 ill-formed（GCC 會直接拒絕）。
struct MarkdownOptions {
    bool mermaidEnabled = true;  ///< false 時 mermaid 區塊當普通程式碼顯示
    bool darkTheme = false;      ///< 影響語法高亮配色
};

/// md4c → Qt rich-text 子集 HTML。
///
/// 刻意不使用 md4c 內建的 md_html()，而是自己實作 MD_PARSER callback，
/// 因為需要在同一趟解析中攔截 mermaid fence、產生 heading anchor、
/// 注入語法高亮，並改寫圖片相對路徑。事後用 regex 刮 HTML 太脆弱。
class MarkdownParser
{
public:
    using Options = MarkdownOptions;

    /// @param baseDir 圖片相對路徑的解析基準；空字串則原樣保留相對路徑
    static Document parse(const QString &markdown,
                          const QString &baseDir = QString(),
                          const Options &opt = Options());
};
