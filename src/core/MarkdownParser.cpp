#include "core/MarkdownParser.h"
#include "core/CodeHighlighter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QUrl>

extern "C" {
#include "md4c.h"
}

namespace {

QString escapeText(const QString &s)
{
    QString out;
    out.reserve(s.size() + 16);
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"': out += QLatin1String("&quot;"); break;
        default:  out += c; break;
        }
    }
    return out;
}

/// 屬性值用：刻意不動 '&'，避免把 md4c 已產生的 entity 二次轉義。
QString escapeAttr(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    out.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    out.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    return out;
}

/// GitHub 風格 slug：小寫、空白轉 '-'、丟棄標點、保留 '-' '_' 與非 ASCII（含 CJK）。
QString githubSlug(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text.toLower()) {
        if (c.isSpace())
            out += QLatin1Char('-');
        else if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))
            out += c;
    }
    return out;
}

QString attrToString(const MD_ATTRIBUTE &attr)
{
    if (!attr.text || attr.size == 0)
        return QString();
    return QString::fromUtf8(attr.text, int(attr.size));
}

/// 圖片 / 連結目標的相對路徑解析。Okular 的 converter.cpp 同樣做 percent-decoding。
QString resolveLocalTarget(const QString &raw, const QString &baseDir)
{
    if (raw.isEmpty() || raw.startsWith(QLatin1Char('#')))
        return raw;
    if (!QUrl(raw).scheme().isEmpty())
        return raw;                       // 已是絕對 URL
    if (baseDir.isEmpty())
        return raw;

    const QString decoded = QUrl::fromPercentEncoding(raw.toUtf8());
    const QString abs = QDir::isAbsolutePath(decoded)
                            ? decoded
                            : QDir(baseDir).absoluteFilePath(decoded);
    return QUrl::fromLocalFile(abs).toString();
}

const char *alignAttr(MD_ALIGN a)
{
    switch (a) {
    case MD_ALIGN_LEFT:   return " align=\"left\"";
    case MD_ALIGN_CENTER: return " align=\"center\"";
    case MD_ALIGN_RIGHT:  return " align=\"right\"";
    default:              return "";
    }
}

struct Renderer {
    QString html;
    Document doc;
    MarkdownParser::Options opt;

    // heading 狀態：anchor 的 slug 只有在讀完整個 heading 後才知道,
    // 所以記下插入點, 於 leave_block 時回頭 insert。
    int headingLevel = 0;
    QString headingText;
    int headingAnchorPos = -1;
    int headingStartPos = -1;
    QHash<QString, int> slugSeen;

    // fenced code 狀態
    bool inCodeBlock = false;
    QString codeLang;
    QString codeText;

    // 圖片 alt 累積（alt 內不能有 markup）
    int imageNesting = 0;
    QString imageAlt;
    QString imageSrc;
    QString imageTitle;

    bool capturingPlainText() const { return headingLevel > 0 && imageNesting == 0; }
};

int enterBlock(MD_BLOCKTYPE type, void *detail, void *ud)
{
    auto *r = static_cast<Renderer *>(ud);

    switch (type) {
    case MD_BLOCK_DOC:
        break;
    case MD_BLOCK_P:
        r->html += QLatin1String("<p>");
        break;
    case MD_BLOCK_H: {
        const auto *d = static_cast<MD_BLOCK_H_DETAIL *>(detail);
        r->headingLevel = int(d->level);
        r->headingText.clear();
        r->headingStartPos = r->html.size();
        r->html += QStringLiteral("<h%1>").arg(d->level);
        r->headingAnchorPos = r->html.size();
        break;
    }
    case MD_BLOCK_CODE: {
        const auto *d = static_cast<MD_BLOCK_CODE_DETAIL *>(detail);
        r->inCodeBlock = true;
        r->codeLang = attrToString(d->lang);
        r->codeText.clear();
        break;
    }
    case MD_BLOCK_QUOTE:
        r->html += QLatin1String("<blockquote>");
        break;
    case MD_BLOCK_UL:
        r->html += QLatin1String("<ul>");
        break;
    case MD_BLOCK_OL: {
        const auto *d = static_cast<MD_BLOCK_OL_DETAIL *>(detail);
        if (d->start != 1)
            r->html += QStringLiteral("<ol start=\"%1\">").arg(d->start);
        else
            r->html += QLatin1String("<ol>");
        break;
    }
    case MD_BLOCK_LI: {
        const auto *d = static_cast<MD_BLOCK_LI_DETAIL *>(detail);
        r->html += QLatin1String("<li>");
        if (d->is_task) {
            // Qt rich-text 不渲染 <input type=checkbox>, 改用字元
            r->html += (d->task_mark == 'x' || d->task_mark == 'X')
                           ? QString::fromUtf8("\xE2\x98\x91 ")   // U+2611 BALLOT BOX WITH CHECK
                           : QString::fromUtf8("\xE2\x98\x90 ");  // U+2610 BALLOT BOX
        }
        break;
    }
    case MD_BLOCK_HR:
        r->html += QLatin1String("<hr>");
        break;
    case MD_BLOCK_TABLE:
        // 邊框與內距由 render backend 的 applyTableStyling() 統一處理（只有橫線）
        r->html += QLatin1String("<table cellspacing=\"0\">");
        break;
    case MD_BLOCK_THEAD:
        r->html += QLatin1String("<thead>");
        break;
    case MD_BLOCK_TBODY:
        r->html += QLatin1String("<tbody>");
        break;
    case MD_BLOCK_TR:
        r->html += QLatin1String("<tr>");
        break;
    case MD_BLOCK_TH: {
        const auto *d = static_cast<MD_BLOCK_TD_DETAIL *>(detail);
        r->html += QStringLiteral("<th%1>").arg(QLatin1String(alignAttr(d->align)));
        break;
    }
    case MD_BLOCK_TD: {
        const auto *d = static_cast<MD_BLOCK_TD_DETAIL *>(detail);
        r->html += QStringLiteral("<td%1>").arg(QLatin1String(alignAttr(d->align)));
        break;
    }
    case MD_BLOCK_HTML:
        break; // 內容由 MD_TEXT_HTML 原樣輸出
    }
    return 0;
}

int leaveBlock(MD_BLOCKTYPE type, void *detail, void *ud)
{
    Q_UNUSED(detail);
    auto *r = static_cast<Renderer *>(ud);

    switch (type) {
    case MD_BLOCK_DOC:
        break;
    case MD_BLOCK_P:
        r->html += QLatin1String("</p>");
        break;
    case MD_BLOCK_H: {
        const QString text = r->headingText.trimmed();
        QString slug = githubSlug(text);
        if (slug.isEmpty())
            slug = QStringLiteral("section");

        const int seen = r->slugSeen.value(slug, 0);
        r->slugSeen.insert(slug, seen + 1);
        if (seen > 0)
            slug += QStringLiteral("-%1").arg(seen);

        r->html.insert(r->headingAnchorPos,
                       QStringLiteral("<a name=\"%1\"></a>").arg(slug));
        r->html += QStringLiteral("</h%1>").arg(r->headingLevel);

        TocEntry e;
        e.level = r->headingLevel;
        e.text = text;
        e.anchor = slug;
        e.htmlPos = r->headingStartPos;
        r->doc.toc.append(e);

        if (r->headingLevel == 1 && r->doc.title.isEmpty())
            r->doc.title = text;

        r->headingLevel = 0;
        r->headingAnchorPos = -1;
        break;
    }
    case MD_BLOCK_CODE: {
        r->inCodeBlock = false;
        const bool isMermaid = r->codeLang.trimmed().compare(QLatin1String("mermaid"),
                                                             Qt::CaseInsensitive) == 0;
        if (isMermaid && r->opt.mermaidEnabled) {
            MermaidBlock b;
            b.source = r->codeText;
            b.key = QString::fromLatin1(
                QCryptographicHash::hash(b.source.toUtf8(), QCryptographicHash::Sha1).toHex());
            r->doc.mermaid.append(b);
            r->html += QStringLiteral("<p><img src=\"mermaid://%1\" alt=\"mermaid diagram\"></p>")
                           .arg(b.key);
        } else {
            r->html += CodeHighlighter::highlight(r->codeText, r->codeLang, r->opt.darkTheme);
        }
        r->codeText.clear();
        r->codeLang.clear();
        break;
    }
    case MD_BLOCK_QUOTE:
        r->html += QLatin1String("</blockquote>");
        break;
    case MD_BLOCK_UL:
        r->html += QLatin1String("</ul>");
        break;
    case MD_BLOCK_OL:
        r->html += QLatin1String("</ol>");
        break;
    case MD_BLOCK_LI:
        r->html += QLatin1String("</li>");
        break;
    case MD_BLOCK_HR:
        break;
    case MD_BLOCK_TABLE:
        r->html += QLatin1String("</table>");
        break;
    case MD_BLOCK_THEAD:
        r->html += QLatin1String("</thead>");
        break;
    case MD_BLOCK_TBODY:
        r->html += QLatin1String("</tbody>");
        break;
    case MD_BLOCK_TR:
        r->html += QLatin1String("</tr>");
        break;
    case MD_BLOCK_TH:
        r->html += QLatin1String("</th>");
        break;
    case MD_BLOCK_TD:
        r->html += QLatin1String("</td>");
        break;
    case MD_BLOCK_HTML:
        break;
    }
    return 0;
}

int enterSpan(MD_SPANTYPE type, void *detail, void *ud)
{
    auto *r = static_cast<Renderer *>(ud);

    switch (type) {
    case MD_SPAN_EM:
        r->html += QLatin1String("<em>");
        break;
    case MD_SPAN_STRONG:
        r->html += QLatin1String("<strong>");
        break;
    case MD_SPAN_DEL:
        // Qt rich-text 不認 <del>；Okular 的 converter 也做同樣替換
        r->html += QLatin1String("<s>");
        break;
    case MD_SPAN_U:
        r->html += QLatin1String("<u>");
        break;
    case MD_SPAN_CODE:
        r->html += QLatin1String("<code>");
        break;
    case MD_SPAN_A: {
        const auto *d = static_cast<MD_SPAN_A_DETAIL *>(detail);
        const QString href = attrToString(d->href);
        const QString title = attrToString(d->title);
        r->html += QStringLiteral("<a href=\"%1\"").arg(escapeAttr(href));
        if (!title.isEmpty())
            r->html += QStringLiteral(" title=\"%1\"").arg(escapeAttr(title));
        r->html += QLatin1Char('>');
        break;
    }
    case MD_SPAN_IMG: {
        const auto *d = static_cast<MD_SPAN_IMG_DETAIL *>(detail);
        r->imageNesting++;
        r->imageAlt.clear();
        r->imageSrc = attrToString(d->src);
        r->imageTitle = attrToString(d->title);
        break;
    }
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        r->html += QLatin1String("<code>");
        break;
    case MD_SPAN_WIKILINK:
        break;
    }
    return 0;
}

int leaveSpan(MD_SPANTYPE type, void *detail, void *ud)
{
    Q_UNUSED(detail);
    auto *r = static_cast<Renderer *>(ud);

    switch (type) {
    case MD_SPAN_EM:     r->html += QLatin1String("</em>"); break;
    case MD_SPAN_STRONG: r->html += QLatin1String("</strong>"); break;
    case MD_SPAN_DEL:    r->html += QLatin1String("</s>"); break;
    case MD_SPAN_U:      r->html += QLatin1String("</u>"); break;
    case MD_SPAN_CODE:   r->html += QLatin1String("</code>"); break;
    case MD_SPAN_A:      r->html += QLatin1String("</a>"); break;
    case MD_SPAN_IMG: {
        r->imageNesting--;
        const QString src = resolveLocalTarget(r->imageSrc, r->doc.baseDir);
        r->html += QStringLiteral("<img src=\"%1\" alt=\"%2\"")
                       .arg(escapeAttr(src), escapeAttr(r->imageAlt));
        if (!r->imageTitle.isEmpty())
            r->html += QStringLiteral(" title=\"%1\"").arg(escapeAttr(r->imageTitle));
        r->html += QLatin1Char('>');
        r->imageAlt.clear();
        r->imageSrc.clear();
        r->imageTitle.clear();
        break;
    }
    case MD_SPAN_LATEXMATH:
    case MD_SPAN_LATEXMATH_DISPLAY:
        r->html += QLatin1String("</code>");
        break;
    case MD_SPAN_WIKILINK:
        break;
    }
    return 0;
}

int onText(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *ud)
{
    auto *r = static_cast<Renderer *>(ud);
    const QString s = QString::fromUtf8(text, int(size));

    if (r->inCodeBlock) {
        r->codeText += s;   // 高亮器自己負責 escape
        return 0;
    }
    if (r->imageNesting > 0) {
        r->imageAlt += s;
        return 0;
    }
    if (r->capturingPlainText() && (type == MD_TEXT_NORMAL || type == MD_TEXT_CODE
                                    || type == MD_TEXT_ENTITY))
        r->headingText += s;

    switch (type) {
    case MD_TEXT_NORMAL:
    case MD_TEXT_CODE:
    case MD_TEXT_LATEXMATH:
        r->html += escapeText(s);
        break;
    case MD_TEXT_ENTITY:
        r->html += s;   // 已是 entity，原樣輸出
        break;
    case MD_TEXT_BR:
        r->html += QLatin1String("<br>");
        break;
    case MD_TEXT_SOFTBR:
        r->html += QLatin1Char('\n');
        break;
    case MD_TEXT_HTML:
        r->html += s;   // 原始 HTML 直接放行
        break;
    case MD_TEXT_NULLCHAR:
        r->html += QChar(0xFFFD);
        break;
    }
    return 0;
}

} // namespace

Document MarkdownParser::parse(const QString &markdown, const QString &baseDir,
                               const Options &opt)
{
    Renderer r;
    r.opt = opt;
    r.doc.baseDir = baseDir;

    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS
                   | MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_PERMISSIVEATXHEADERS;
    parser.enter_block = enterBlock;
    parser.leave_block = leaveBlock;
    parser.enter_span = enterSpan;
    parser.leave_span = leaveSpan;
    parser.text = onText;
    parser.debug_log = nullptr;
    parser.syntax = nullptr;

    const QByteArray utf8 = markdown.toUtf8();
    md_parse(utf8.constData(), MD_SIZE(utf8.size()), &parser, &r);

    r.doc.html = r.html;
    return r.doc;
}
