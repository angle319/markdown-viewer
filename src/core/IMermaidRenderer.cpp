#include "core/IMermaidRenderer.h"

// 這個 .cpp 存在的理由有兩個：
//  1. 多型基底類別的解構子放 out-of-line，避免每個 TU 都產生一份 vtable。
//  2. CMake 的 AUTOMOC 只會替「與某個 source 同名」或明列在 sources 裡的 header
//     跑 moc。純 header 的 Q_OBJECT 類別會缺 vtable 與 staticMetaObject，
//     連結時報 undefined reference。
IMermaidRenderer::~IMermaidRenderer() = default;
