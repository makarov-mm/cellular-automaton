#include "GLViewport.h"

#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace {

constexpr int kFloatsPerVertex = 6;  // position + normal

// Unit cube centered on the origin: 6 faces x 2 triangles, counter-clockwise
// when seen from outside.
// clang-format off
constexpr float kCube[] = {
    // position            normal
    // -Z
    -0.5f,-0.5f,-0.5f,  0,0,-1,   0.5f, 0.5f,-0.5f,  0,0,-1,   0.5f,-0.5f,-0.5f,  0,0,-1,
    -0.5f,-0.5f,-0.5f,  0,0,-1,  -0.5f, 0.5f,-0.5f,  0,0,-1,   0.5f, 0.5f,-0.5f,  0,0,-1,
    // +Z
    -0.5f,-0.5f, 0.5f,  0,0, 1,   0.5f,-0.5f, 0.5f,  0,0, 1,   0.5f, 0.5f, 0.5f,  0,0, 1,
    -0.5f,-0.5f, 0.5f,  0,0, 1,   0.5f, 0.5f, 0.5f,  0,0, 1,  -0.5f, 0.5f, 0.5f,  0,0, 1,
    // -X
    -0.5f, 0.5f, 0.5f, -1,0,0,   -0.5f, 0.5f,-0.5f, -1,0,0,   -0.5f,-0.5f,-0.5f, -1,0,0,
    -0.5f, 0.5f, 0.5f, -1,0,0,   -0.5f,-0.5f,-0.5f, -1,0,0,   -0.5f,-0.5f, 0.5f, -1,0,0,
    // +X
     0.5f, 0.5f, 0.5f,  1,0,0,    0.5f,-0.5f,-0.5f,  1,0,0,    0.5f, 0.5f,-0.5f,  1,0,0,
     0.5f, 0.5f, 0.5f,  1,0,0,    0.5f,-0.5f, 0.5f,  1,0,0,    0.5f,-0.5f,-0.5f,  1,0,0,
    // -Y
    -0.5f,-0.5f,-0.5f,  0,-1,0,   0.5f,-0.5f,-0.5f,  0,-1,0,   0.5f,-0.5f, 0.5f,  0,-1,0,
    -0.5f,-0.5f,-0.5f,  0,-1,0,   0.5f,-0.5f, 0.5f,  0,-1,0,  -0.5f,-0.5f, 0.5f,  0,-1,0,
    // +Y
    -0.5f, 0.5f,-0.5f,  0,1,0,    0.5f, 0.5f, 0.5f,  0,1,0,    0.5f, 0.5f,-0.5f,  0,1,0,
    -0.5f, 0.5f,-0.5f,  0,1,0,   -0.5f, 0.5f, 0.5f,  0,1,0,    0.5f, 0.5f, 0.5f,  0,1,0,
};
// clang-format on
constexpr int kCubeVertexCount = static_cast<int>(std::size(kCube)) / kFloatsPerVertex;

constexpr char kVertexShader[] = R"(#version 330 core
layout(location = 0) in vec3  aPosition;  // cube corner, -0.5..0.5
layout(location = 1) in vec3  aNormal;
layout(location = 2) in uvec4 aCell;      // xyz = grid coordinates, w = state

uniform mat4  uViewProjection;
uniform float uGridSize;
uniform float uAliveState;

out      vec3  vNormal;
flat out float vState;         // 0..1, 1 = fully alive
flat out vec3  vGridPosition;  // 0..1 across the grid

const float kCubeScale = 0.85;  // leaves a gap between neighboring cubes

void main() {
    vec3 cell  = vec3(aCell.xyz);
    vec3 world = aPosition * kCubeScale + cell - 0.5 * (uGridSize - 1.0);
    gl_Position   = uViewProjection * vec4(world, 1.0);
    vNormal       = aNormal;
    vState        = float(aCell.w) / uAliveState;
    vGridPosition = cell / uGridSize;
}
)";

constexpr char kFragmentShader[] = R"(#version 330 core
in      vec3  vNormal;
flat in float vState;
flat in vec3  vGridPosition;

uniform int uPalette;

out vec4 fragColor;

const vec3 kLightDirection = normalize(vec3(0.5, 0.8, 0.6));

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

// Three-stop gradient: c1 at t = 0, c2 at t = 0.5, c3 at t = 1.
vec3 gradient3(float t, vec3 c1, vec3 c2, vec3 c3) {
    return t < 0.5 ? mix(c1, c2, t * 2.0) : mix(c2, c3, t * 2.0 - 1.0);
}

vec3 paletteColor(float t) {
    if (uPalette == 1)  // Fire: ember red -> orange -> pale yellow
        return gradient3(t, vec3(0.25, 0.02, 0.02), vec3(0.95, 0.45, 0.08), vec3(1.00, 0.95, 0.65));
    if (uPalette == 2)  // Viridis-like: purple -> teal -> yellow
        return gradient3(t, vec3(0.27, 0.00, 0.33), vec3(0.13, 0.57, 0.55), vec3(0.99, 0.91, 0.14));
    if (uPalette == 3)  // Rainbow: hue sweeps with the state, alive = bright red
        return hsv2rgb(vec3(0.83 * (1.0 - t), 0.85, 0.35 + 0.65 * t));
    if (uPalette == 4) {  // Position: hue from the place in the grid, brightness from the state
        float hue = fract(dot(vGridPosition, vec3(0.45, 0.30, 0.25)));
        return hsv2rgb(vec3(hue, 0.75, 0.30 + 0.70 * t));
    }
    // 0, Ice: deep violet while decaying, cyan when alive
    return mix(vec3(0.15, 0.05, 0.40), vec3(0.20, 0.90, 1.00), t);
}

void main() {
    float diffuse = max(dot(normalize(vNormal), kLightDirection), 0.0);
    fragColor = vec4(paletteColor(vState) * (0.25 + 0.75 * diffuse), 1.0);
}
)";

constexpr float kOrbitDegreesPerPixel = 0.4f;
constexpr float kMaxPitch = 89.0f;  // stops short of looking along the vertical axis
constexpr float kZoomPerWheelNotch = 0.9f;
constexpr float kMinDistance = 5.0f;
constexpr float kMaxDistance = 2000.0f;
constexpr float kFramingDistance = 1.9f;  // in grid sides: far enough to see the whole grid

}  // namespace

GLViewport::GLViewport(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumSize(400, 400);
    setFocusPolicy(Qt::StrongFocus);
}

GLViewport::~GLViewport() {
    releaseResources();
}

void GLViewport::releaseResources() {
    if (!m_program) return;  // nothing has been created yet

    // The GL objects have to be released while the widget's context is current.
    makeCurrent();
    m_instanceBuffer.destroy();
    m_cubeBuffer.destroy();
    m_vertexArray.destroy();
    m_program.reset();
    doneCurrent();
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &GLViewport::releaseResources);
}

void GLViewport::setCells(std::span<const CellInstance> cells, int gridSize, int aliveState) {
    if (gridSize != m_gridSize) m_distance = kFramingDistance * static_cast<float>(gridSize);
    m_gridSize = gridSize;
    m_aliveState = aliveState;
    m_cells.assign(cells.begin(), cells.end());
    m_cellsChanged = true;  // uploaded in paintGL(), where the context is current
    update();
}

void GLViewport::setPalette(Palette palette) {
    m_palette = palette;
    update();
}

void GLViewport::initializeGL() {
    initializeOpenGLFunctions();

    // Qt replaces the context if the widget moves to another top-level
    // window, and then calls initializeGL() again.
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &GLViewport::releaseResources);

    glClearColor(0.04f, 0.04f, 0.06f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    // Qt logs compile and link errors itself.
    m_program = std::make_unique<QOpenGLShaderProgram>();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_program->link();

    m_vertexArray.create();
    const QOpenGLVertexArrayObject::Binder vertexArrayBinder(&m_vertexArray);

    constexpr GLsizei stride = kFloatsPerVertex * sizeof(float);
    const auto* const normalOffset = reinterpret_cast<const void*>(3 * sizeof(float));
    m_cubeBuffer.create();
    m_cubeBuffer.bind();
    m_cubeBuffer.allocate(kCube, sizeof(kCube));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, normalOffset);

    // One CellInstance per cube, passed to the shader as four unsigned bytes.
    m_instanceBuffer.create();
    m_instanceBuffer.setUsagePattern(QOpenGLBuffer::StreamDraw);
    m_instanceBuffer.bind();
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(2, 4, GL_UNSIGNED_BYTE, sizeof(CellInstance), nullptr);
    glVertexAttribDivisor(2, 1);
    m_cellsChanged = true;  // the new buffer is still empty
}

void GLViewport::resizeGL(int width, int height) {
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    m_projection.setToIdentity();
    m_projection.perspective(45.0f, aspect, 0.1f, 5000.0f);
}

void GLViewport::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_cellsChanged) {
        m_instanceBuffer.bind();
        m_instanceBuffer.allocate(m_cells.data(),
                                  static_cast<int>(std::span(m_cells).size_bytes()));
        m_cellsChanged = false;
    }
    if (m_cells.empty()) return;

    QMatrix4x4 view;
    view.translate(0.0f, 0.0f, -m_distance);
    view.rotate(m_pitch, 1.0f, 0.0f, 0.0f);
    view.rotate(m_yaw, 0.0f, 1.0f, 0.0f);

    m_program->bind();
    m_program->setUniformValue("uViewProjection", m_projection * view);
    m_program->setUniformValue("uGridSize", static_cast<float>(m_gridSize));
    m_program->setUniformValue("uAliveState", static_cast<float>(m_aliveState));
    m_program->setUniformValue("uPalette", std::to_underlying(m_palette));

    const QOpenGLVertexArrayObject::Binder vertexArrayBinder(&m_vertexArray);
    glDrawArraysInstanced(GL_TRIANGLES, 0, kCubeVertexCount, static_cast<GLsizei>(m_cells.size()));
}

void GLViewport::mousePressEvent(QMouseEvent* event) {
    m_lastMousePosition = event->position();
}

void GLViewport::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;

    const QPointF delta = event->position() - m_lastMousePosition;
    m_lastMousePosition = event->position();
    m_yaw += kOrbitDegreesPerPixel * static_cast<float>(delta.x());
    m_pitch += kOrbitDegreesPerPixel * static_cast<float>(delta.y());
    m_pitch = std::clamp(m_pitch, -kMaxPitch, kMaxPitch);
    update();
}

void GLViewport::wheelEvent(QWheelEvent* event) {
    const float notches = static_cast<float>(event->angleDelta().y()) / 120.0f;
    const float zoomed = m_distance * std::pow(kZoomPerWheelNotch, notches);
    m_distance = std::clamp(zoomed, kMinDistance, kMaxDistance);
    update();
}
