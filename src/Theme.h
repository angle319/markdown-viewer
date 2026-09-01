#pragma once

#include "core/Typography.h"

#include <QColor>
#include <QPalette>
#include <QString>

/// Two themes: pure white and pure black.
///
/// The enumerators keep the internal names Light/Dark (they feed the `dark`
/// flag through to mermaid and syntax highlighting), but the actual backgrounds
/// are #ffffff and #000000, presented in the UI as 白色主題 / 黑色主題.
///
/// Contrast is a hard requirement, not an aesthetic preference: every colour
/// pair is computed as a WCAG 2.1 ratio and verified by tests/test_theme.cpp —
/// body text >= 7:1, secondary text and links >= 4.5:1, non-text elements such
/// as borders >= 3:1. Changing a colour that breaks a threshold fails the test.
///
/// The content column cap follows Okular's markdown generator CONTENT_WIDTH.
class Theme
{
public:
    enum Mode { Light, Dark };

    static constexpr int ContentWidth = 980;

    /// Blockquote left margin, in pixels.
    ///
    /// This constant is a **contract** between the stylesheet and the painting
    /// code: Qt rich text has no border-left, so the bar is drawn by hand in
    /// paintEvent(), and "which blocks are blockquotes" is decided by this
    /// leftMargin value. List items are excluded because they have a textList()
    /// and headings because their left margin is 0.
    /// Pinned by blockquoteMarkerContractHolds() in test_e2e_regression.cpp.
    ///
    /// 中：CSS 與自繪共用的契約值，改動要兩邊一起改。
    static constexpr int BlockquoteIndentPx = 22;

    /// Width of the blockquote bar, in pixels
    static constexpr int BlockquoteBarPx = 4;

    /// WCAG thresholds
    static constexpr double MinTextContrast = 4.5;
    static constexpr double MinBodyTextContrast = 7.0;
    static constexpr double MinNonTextContrast = 3.0;

    struct Colors {
        QString background;
        QString text;
        QString muted;
        QString link;
        QString codeBackground;
        /// Foreground for inline `code`. Deliberately a magenta whose **hue
        /// differs from the link colour by more than 110 degrees**.
        ///
        /// Distinguishability cannot be judged by WCAG contrast: that measures
        /// luminance only, so purple and blue score 1.13:1 despite being
        /// obviously different colours. Hue difference is the right metric.
        /// Colour is also not the only cue — links are underlined and inline
        /// code is monospaced on a chip — so the distinction survives for
        /// readers with colour vision deficiencies.
        QString codeInline;
        /// Background chip for inline `code`. The accent is pre-blended into
        /// the page colour at low opacity, because Qt rich text's rgba()
        /// support is unreliable; this is a solid colour computed in advance.
        QString codeInlineBackground;
        /// Background for unselected tabs. The selected tab uses the page
        /// colour so it reads as continuous with the content below; unselected
        /// tabs sit one step back, which is what makes focus obvious.
        QString tabInactive;
        QString border;
        // Note: there is deliberately no quoteBar colour here. Qt rich text has
        // no border-left, so the bar is painted by hand rather than styled.
        QString tableHeader;
    };

    static const Colors &colors(Mode m);
    static QString documentStyleSheet(Mode m);
    /// Stylesheet for the tab bar. The palette alone cannot separate selected
    /// from unselected enough: QTabBar's default selected state differs by only
    /// a slight background shade.
    static QString tabBarStyleSheet(Mode m);
    static QPalette palette(Mode m);

    static QString name(Mode m);

    /// See Typography — both values are needed by CodeHighlighter too, which is
    /// why they live in core.
    static constexpr qreal BodyPointSize = Typography::BodyPointSize;
    static constexpr qreal LineHeightPercent = Typography::LineHeightPercent;

    /// Heading point size for level 1..6.
    ///
    /// **Deliberately not set through CSS.** Qt's HTML parser applies its own
    /// fontSizeAdjustment to h5/h6, which overrides the CSS font-size: measured,
    /// H5 came out smaller than body text and H6 larger than H5, so the whole
    /// hierarchy was broken. The render backend sets these explicitly in a
    /// document tree walk; this function is the single definition.
    ///
    /// 中：CSS 的 font-size 對 h5/h6 沒用，字級一律在這裡定義。
    static qreal headingPointSize(int level);

    // ---- Contrast maths (WCAG 2.1) ----

    /// Relative luminance, 0 (black) to 1 (white).
    static double relativeLuminance(const QColor &c);

    /// Contrast ratio, 1.0 (identical) to 21.0 (pure black on pure white).
    static double contrastRatio(const QColor &a, const QColor &b);

    /// Returns a foreground guaranteed to be readable on `bg`.
    /// Prefers the theme's own text colour, falling back to whichever of black
    /// or white contrasts more when that is not enough. This is what rescues
    /// hard-coded colours embedded in the markdown as raw HTML.
    static QColor readableOn(const QColor &bg, Mode mode);
};
