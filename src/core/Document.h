#pragma once

#include <QString>
#include <QVector>

/// 目錄（TOC）中的一個標題項。
struct TocEntry {
    int level = 1;      ///< 1..6
    QString text;       ///< 顯示文字，已去除 markdown markup
    QString anchor;     ///< GitHub 風格 slug，不含前導 '#'
    int htmlPos = -1;   ///< 在 html 字串中的位置，供捲動反查用
};

/// 一個 ```mermaid 區塊。
struct MermaidBlock {
    QString source;     ///< 圖表原始碼
    QString key;        ///< sha1(source)，作為快取檔名與 mermaid:// URL 的 host
};

/// 解析結果：可直接餵給 render backend。
struct Document {
    QString html;                   ///< Qt rich-text 子集的 HTML
    QVector<TocEntry> toc;
    QVector<MermaidBlock> mermaid;
    QString baseDir;                ///< 相對路徑（圖片、連結）解析基準
    QString title;                  ///< 第一個 H1；沒有則留空由呼叫端用檔名
};
