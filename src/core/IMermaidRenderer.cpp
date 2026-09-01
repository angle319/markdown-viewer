#include "core/IMermaidRenderer.h"

// This .cpp exists for two reasons:
//  1. An out-of-line destructor for a polymorphic base keeps the vtable out of
//     every translation unit.
//  2. CMake's AUTOMOC only runs moc on headers that share a name with a listed
//     source, or that are listed as sources themselves. A header-only Q_OBJECT
//     class ends up without a vtable or staticMetaObject and fails to link.
IMermaidRenderer::~IMermaidRenderer() = default;
