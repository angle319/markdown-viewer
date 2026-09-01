#pragma once

#include <QObject>
#include <QString>

/// Turns mermaid source into an image file.
///
/// The interface is asynchronous because QProcess is already event driven; no
/// extra thread is needed. Implementations may shell out (MmdcRenderer) or, in
/// future, translate to another diagram engine such as graphviz.
class IMermaidRenderer : public QObject
{
    Q_OBJECT

public:
    explicit IMermaidRenderer(QObject *parent = nullptr) : QObject(parent) {}
    ~IMermaidRenderer() override;

    /// Whether the external tool is present. When it is not, MermaidCache
    /// degrades to showing the diagram source as a code block.
    virtual bool isAvailable() const = 0;

    /// Identifies anything that changes the output; folded into the cache key so
    /// that upgrading the tool invalidates old images.
    virtual QString rendererId() const = 0;

    /// Output extension, "svg" or "png".
    virtual QString outputExtension() const = 0;

    /// Ratio of output pixels to logical display size. PNG is rasterised by the
    /// external tool at N×, so the display size divides it back down; SVG is
    /// rasterised in-process and therefore stays at 1.
    virtual qreal outputScale() const { return 1.0; }

    /// Starts one render. MermaidCache guarantees only one runs at a time.
    virtual void start(const QString &source, bool dark, const QString &outPath) = 0;

Q_SIGNALS:
    /// When ok is false, error explains why.
    void finished(bool ok, const QString &error);
};
