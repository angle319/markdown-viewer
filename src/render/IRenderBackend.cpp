#include "render/IRenderBackend.h"

// out-of-line 解構子：同時讓 AUTOMOC 認得這個純介面 header（見 IMermaidRenderer.cpp）
IRenderBackend::IRenderBackend(QObject *parent) : QObject(parent) {}
IRenderBackend::~IRenderBackend() = default;
