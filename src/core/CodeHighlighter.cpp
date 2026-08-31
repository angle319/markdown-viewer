#include "core/CodeHighlighter.h"

#include <QHash>
#include <QSet>
#include <QStringList>

namespace {

struct Palette {
    QString keyword;
    QString string;
    QString comment;
    QString number;
    QString preBg;
    QString preFg;
};

const Palette &palette(bool dark)
{
    static const Palette light{ QStringLiteral("#0033b3"), QStringLiteral("#a31515"),
                                QStringLiteral("#3f7f5f"), QStringLiteral("#098658"),
                                QStringLiteral("#f6f8fa"), QStringLiteral("#24292f") };
    static const Palette night{ QStringLiteral("#82aaff"), QStringLiteral("#c3e88d"),
                                QStringLiteral("#7f848e"), QStringLiteral("#f78c6c"),
                                QStringLiteral("#1f2430"), QStringLiteral("#d7dae0") };
    return dark ? night : light;
}

/// 每個語言的詞法規則。
struct LangSpec {
    QSet<QString> keywords;
    QStringList lineComments;   ///< 例如 "//" 或 "#"
    bool blockComments = false; ///< /* … */
    bool backtickString = false;
};

QSet<QString> toSet(const char *words)
{
    QSet<QString> s;
    for (const QString &w : QString::fromLatin1(words).split(QLatin1Char(' '), Qt::SkipEmptyParts))
        s.insert(w);
    return s;
}

const LangSpec *specFor(const QString &langRaw)
{
    const QString lang = langRaw.trimmed().toLower();
    static QHash<QString, LangSpec> table;
    if (table.isEmpty()) {
        LangSpec cpp;
        cpp.keywords = toSet("alignas alignof and asm auto bool break case catch char char8_t char16_t "
                             "char32_t class concept const consteval constexpr constinit const_cast continue "
                             "co_await co_return co_yield decltype default delete do double dynamic_cast else "
                             "enum explicit export extern false float for friend goto if inline int long "
                             "mutable namespace new noexcept not nullptr operator or private protected public "
                             "register reinterpret_cast requires return short signed sizeof static static_assert "
                             "static_cast struct switch template this thread_local throw true try typedef "
                             "typeid typename union unsigned using virtual void volatile wchar_t while xor "
                             "include define ifdef ifndef endif pragma elif undef error");
        cpp.lineComments = { QStringLiteral("//") };
        cpp.blockComments = true;
        table.insert(QStringLiteral("cpp"), cpp);
        table.insert(QStringLiteral("c++"), cpp);
        table.insert(QStringLiteral("cc"), cpp);
        table.insert(QStringLiteral("hpp"), cpp);
        table.insert(QStringLiteral("c"), cpp);
        table.insert(QStringLiteral("h"), cpp);

        LangSpec py;
        py.keywords = toSet("False None True and as assert async await break class continue def del elif "
                            "else except finally for from global if import in is lambda nonlocal not or "
                            "pass raise return try while with yield match case self");
        py.lineComments = { QStringLiteral("#") };
        table.insert(QStringLiteral("python"), py);
        table.insert(QStringLiteral("py"), py);

        LangSpec js;
        js.keywords = toSet("async await break case catch class const continue debugger default delete do "
                            "else enum export extends false finally for function if implements import in "
                            "instanceof interface let new null of package private protected public return "
                            "static super switch this throw true try type typeof var void while with yield "
                            "as any string number boolean unknown never readonly");
        js.lineComments = { QStringLiteral("//") };
        js.blockComments = true;
        js.backtickString = true;
        table.insert(QStringLiteral("js"), js);
        table.insert(QStringLiteral("javascript"), js);
        table.insert(QStringLiteral("ts"), js);
        table.insert(QStringLiteral("typescript"), js);
        table.insert(QStringLiteral("jsx"), js);
        table.insert(QStringLiteral("tsx"), js);

        LangSpec json;
        json.keywords = toSet("true false null");
        table.insert(QStringLiteral("json"), json);

        LangSpec sh;
        sh.keywords = toSet("if then else elif fi for while until do done case esac function return "
                            "in select time coproc local export readonly declare unset shift source "
                            "echo cd set trap exit eval exec test");
        sh.lineComments = { QStringLiteral("#") };
        table.insert(QStringLiteral("bash"), sh);
        table.insert(QStringLiteral("sh"), sh);
        table.insert(QStringLiteral("shell"), sh);
        table.insert(QStringLiteral("zsh"), sh);

        LangSpec cm;
        cm.keywords = toSet("add_executable add_library add_subdirectory cmake_minimum_required else "
                            "elseif endforeach endfunction endif endmacro find_package foreach function "
                            "if include install macro option project return set set_target_properties "
                            "target_compile_definitions target_include_directories target_link_libraries "
                            "enable_testing add_test message REQUIRED COMPONENTS PUBLIC PRIVATE STATIC SHARED");
        cm.lineComments = { QStringLiteral("#") };
        table.insert(QStringLiteral("cmake"), cm);
    }

    const auto it = table.constFind(lang);
    return it == table.constEnd() ? nullptr : &it.value();
}

void appendEscaped(QString &out, QChar c)
{
    switch (c.unicode()) {
    case '&': out += QLatin1String("&amp;"); break;
    case '<': out += QLatin1String("&lt;"); break;
    case '>': out += QLatin1String("&gt;"); break;
    default:  out += c; break;
    }
}

void appendEscaped(QString &out, const QString &s)
{
    for (const QChar c : s)
        appendEscaped(out, c);
}

void appendColored(QString &out, const QString &text, const QString &color, bool italic = false)
{
    out += QLatin1String("<span style=\"color:") + color;
    if (italic)
        out += QLatin1String("; font-style:italic");
    out += QLatin1String("\">");
    appendEscaped(out, text);
    out += QLatin1String("</span>");
}

bool isIdentChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$');
}

} // namespace

bool CodeHighlighter::supports(const QString &lang)
{
    return specFor(lang) != nullptr;
}

QString CodeHighlighter::highlight(const QString &code, const QString &lang, bool dark)
{
    const Palette &pal = palette(dark);
    const LangSpec *spec = specFor(lang);

    QString body;
    body.reserve(code.size() * 2);

    if (!spec) {
        appendEscaped(body, code);
    } else {
        const int n = code.size();
        int i = 0;
        while (i < n) {
            const QChar c = code.at(i);

            // 行註解
            bool handled = false;
            for (const QString &lc : spec->lineComments) {
                if (code.mid(i, lc.size()) == lc) {
                    int end = code.indexOf(QLatin1Char('\n'), i);
                    if (end < 0)
                        end = n;
                    appendColored(body, code.mid(i, end - i), pal.comment, true);
                    i = end;
                    handled = true;
                    break;
                }
            }
            if (handled)
                continue;

            // 區塊註解
            if (spec->blockComments && c == QLatin1Char('/') && i + 1 < n
                && code.at(i + 1) == QLatin1Char('*')) {
                int end = code.indexOf(QLatin1String("*/"), i + 2);
                end = (end < 0) ? n : end + 2;
                appendColored(body, code.mid(i, end - i), pal.comment, true);
                i = end;
                continue;
            }

            // 字串 / 字元
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')
                || (spec->backtickString && c == QLatin1Char('`'))) {
                const QChar quote = c;
                int j = i + 1;
                while (j < n) {
                    if (code.at(j) == QLatin1Char('\\')) {
                        j += 2;
                        continue;
                    }
                    if (code.at(j) == quote) {
                        ++j;
                        break;
                    }
                    if (code.at(j) == QLatin1Char('\n') && quote != QLatin1Char('`'))
                        break; // 單行字串不跨行，避免整份檔案被吃掉
                    ++j;
                }
                appendColored(body, code.mid(i, qMin(j, n) - i), pal.string);
                i = qMin(j, n);
                continue;
            }

            // 數字
            if (c.isDigit()) {
                int j = i;
                while (j < n && (code.at(j).isLetterOrNumber() || code.at(j) == QLatin1Char('.')
                                 || code.at(j) == QLatin1Char('x')))
                    ++j;
                appendColored(body, code.mid(i, j - i), pal.number);
                i = j;
                continue;
            }

            // 識別字 / 關鍵字
            if (c.isLetter() || c == QLatin1Char('_')) {
                int j = i;
                while (j < n && isIdentChar(code.at(j)))
                    ++j;
                const QString word = code.mid(i, j - i);
                if (spec->keywords.contains(word))
                    appendColored(body, word, pal.keyword);
                else
                    appendEscaped(body, word);
                i = j;
                continue;
            }

            appendEscaped(body, c);
            ++i;
        }
    }

    return QStringLiteral("<pre style=\"background-color:%1; color:%2; padding:8px; "
                          "font-family:monospace;\">%3</pre>")
        .arg(pal.preBg, pal.preFg, body);
}
