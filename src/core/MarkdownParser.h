#pragma once

#include "core/Document.h"

/// Parse options.
///
/// Deliberately declared outside MarkdownParser: a nested class's default
/// member initialisers are ill-formed when used as a default argument inside
/// the enclosing class, and GCC rejects it outright.
struct MarkdownOptions {
    bool mermaidEnabled = true;  ///< When false, mermaid fences render as ordinary code
    bool darkTheme = false;      ///< Selects the syntax-highlighting palette
};

/// md4c to HTML, restricted to Qt's rich-text subset.
///
/// This deliberately implements its own MD_PARSER callbacks rather than calling
/// md4c's bundled md_html(): mermaid interception, heading anchors, syntax
/// highlighting and image path rewriting all have to happen in the same pass.
/// Post-processing an HTML string with regular expressions is too fragile.
class MarkdownParser
{
public:
    using Options = MarkdownOptions;

    /// @param baseDir Base for resolving relative image paths; when empty,
    ///                relative paths are left untouched
    static Document parse(const QString &markdown,
                          const QString &baseDir = QString(),
                          const Options &opt = Options());
};
