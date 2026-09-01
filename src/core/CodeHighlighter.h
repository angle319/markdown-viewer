#pragma once

#include <QString>

/// Minimal syntax highlighting: emits <pre>…</pre> with inline styles.
///
/// No external dependency. An unknown language falls back to monospaced text on
/// the code background, with no colouring.
class CodeHighlighter
{
public:
    /// @param code Raw source (not yet escaped)
    /// @param lang Language tag from the fence; may be empty
    /// @return A complete <pre>…</pre> with HTML-escaped content
    static QString highlight(const QString &code, const QString &lang, bool dark = false);

    /// Whether the language has a keyword table (otherwise the fallback applies)
    static bool supports(const QString &lang);
};
