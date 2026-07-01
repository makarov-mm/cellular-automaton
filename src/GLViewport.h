#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QMatrix4x4>
#include <vector>

// Renders the living cells as instanced cubes with a simple orbit camera and
// lambert lighting. Cell instance data is 4 floats each: x, y, z, stateNorm.
class GLViewport : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit GLViewport(QWidget* parent = nullptr);
    ~GLViewport() override;

    void setGridSize(int n);
    void setInstances(const std::vector<float>& data, int count);
    void setPalette(int p);   // index into the shader's palette switch

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    GLuint m_program   = 0;
    GLuint m_vao       = 0;
    GLuint m_cubeVbo   = 0;
    GLuint m_instVbo   = 0;

    int    m_instanceCount = 0;
    std::vector<float> m_pending;
    bool   m_pendingDirty  = false;

    int    m_gridN = 48;

    QMatrix4x4 m_proj;
    float  m_yaw   = 30.0f;
    float  m_pitch = 20.0f;
    float  m_dist  = 90.0f;
    QPoint m_lastMouse;

    int    m_palette = 0;

    GLint  m_locMVP = -1, m_locLightDir = -1, m_locCell = -1, m_locCenter = -1;
    GLint  m_locPalette = -1;

    GLuint compileProgram();
};
