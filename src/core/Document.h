#pragma once

#include <QString>
#include <QVector>

/// One heading entry in the table of contents.
struct TocEntry {
    int level = 1;      ///< 1..6
    QString text;       ///< Display text, with markdown markup stripped
    QString anchor;     ///< GitHub-style slug, without the leading '#'
    int htmlPos = -1;   ///< Offset into `html`, used for scroll-position lookup
};

/// One ```mermaid fenced block.
struct MermaidBlock {
    QString source;     ///< Diagram source
    QString key;        ///< sha1(source); the mermaid:// URL host and cache file name
};

/// Parse result, ready to hand to a render backend.
struct Document {
    QString html;                   ///< HTML restricted to Qt's rich-text subset
    QVector<TocEntry> toc;
    QVector<MermaidBlock> mermaid;
    QString baseDir;                ///< Base for resolving relative image and link paths
    QString title;                  ///< First H1; empty means the caller should use the file name
};
