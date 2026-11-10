#pragma once

#include "CellInstance.h"

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPointF>

#include <memory>
#include <span>
#include <vector>

class QOpenGLShaderProgram;

// Draws the cells as instanced cubes, with an orbit camera and Lambert shading.
class GLViewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    // The values match the palette switch in the fragment shader.
    enum class Palette {
        Ice,
        Fire,
        Viridis,
        Rainbow,
        Position,
    };

    explicit GLViewport(QWidget* parent = nullptr);
    ~GLViewport() override;

    // Replaces the cells to draw. `gridSize` is the side of the grid they
    // belong to and `aliveState` the state of a fully alive cell; the camera
    // re-frames the grid whenever its size changes.
    void setCells(std::span<const CellInstance> cells, int gridSize, int aliveState);
    void setPalette(Palette palette);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void releaseResources();

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    QOpenGLVertexArrayObject m_vertexArray;
    QOpenGLBuffer m_cubeBuffer;
    QOpenGLBuffer m_instanceBuffer;

    std::vector<CellInstance> m_cells;
    bool m_cellsChanged = false;  // m_cells is newer than m_instanceBuffer
    int m_gridSize = 0;
    int m_aliveState = 1;
    Palette m_palette = Palette::Ice;

    QMatrix4x4 m_projection;
    float m_yaw = 30.0f;       // degrees
    float m_pitch = 20.0f;     // degrees
    float m_distance = 90.0f;  // camera distance from the grid center, in cells
    QPointF m_lastMousePosition;
};
