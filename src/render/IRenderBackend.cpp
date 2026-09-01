#include "render/IRenderBackend.h"

// Out-of-line destructor, which also makes AUTOMOC pick up this header-only
// interface (see IMermaidRenderer.cpp)
IRenderBackend::IRenderBackend(QObject *parent) : QObject(parent) {}
IRenderBackend::~IRenderBackend() = default;
