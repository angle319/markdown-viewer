#pragma once

/// Typography baselines.
///
/// These live in core rather than Theme because both CodeHighlighter (mdcore,
/// QtCore only) and Theme (mdgui, needs QtGui for QPalette) use them and the
/// values must be identical. They were originally duplicated, which left body
/// text at 155% and code blocks at 150% until a test caught it.
namespace Typography {

/// Body point size. Set explicitly, otherwise the ratio between heading levels
/// and body text drifts with whatever the system font happens to be.
inline constexpr double BodyPointSize = 11.0;

/// Line height, as a percentage. Qt's default is roughly single spacing, which
/// is cramped for CJK text. The Chrome extension this was compared against uses
/// 1.5 (24px line height on a 16px font).
inline constexpr double LineHeightPercent = 155.0;

} // namespace Typography
