#pragma once

/// 排版基準值。
///
/// 放在 core 而不是 Theme：CodeHighlighter（mdcore，只依賴 QtCore）與
/// Theme（mdgui，需要 QtGui 的 QPalette）都要用到，數值必須是同一份。
/// 一開始行高分別寫在兩處，結果正文 155% 而程式碼區塊 150%，測試才抓到。
namespace Typography {

/// 正文字級（pt）。明確給值，否則各標題層級與正文的相對大小會隨系統字型漂移。
inline constexpr double BodyPointSize = 11.0;

/// 行高（百分比）。Qt 預設約等於單行，中文在那個行距下很擠。
/// 對照 Chrome extension 是 1.5（16px 字對 24px 行高）。
inline constexpr double LineHeightPercent = 155.0;

} // namespace Typography
