#pragma once

#include <QString>

/// 極簡語法高亮：產生帶 inline style 的 <pre>…</pre>。
///
/// 不使用外部依賴。未知語言只上等寬字與底色，不著色。
class CodeHighlighter
{
public:
    /// @param code 原始程式碼（未 escape）
    /// @param lang fence 上的語言標記，可為空
    /// @return 完整的 <pre>…</pre>，內容已 HTML escape
    static QString highlight(const QString &code, const QString &lang, bool dark = false);

    /// 該語言是否有關鍵字表（否則走 fallback）
    static bool supports(const QString &lang);
};
