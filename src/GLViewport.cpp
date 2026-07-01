#include "GLViewport.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>

// 36-vertex cube (position + normal), spanning [-0.5, 0.5].
static const float kCube[] = {
    // pos                 // normal
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

static const char* kVert = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aInstance;   // xyz = cell coord, w = stateNorm
uniform mat4  uMVP;
uniform float uCell;
uniform vec3  uCenter;
out vec3  vNormal;
out float vState;
out vec3  vPosN;
void main() {
    vec3 world = aPos * uCell * 0.85 + (aInstance.xyz - uCenter) * uCell;
    gl_Position = uMVP * vec4(world, 1.0);
    vNormal = aNormal;
    vState  = aInstance.w;
    vPosN   = aInstance.xyz / (2.0 * uCenter);   // 0..1 position inside the grid
}
)";

static const char* kFrag = R"(#version 330 core
in vec3  vNormal;
in float vState;
in vec3  vPosN;
out vec4 FragColor;
uniform vec3 uLightDir;
uniform int  uPalette;

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
}

// three-stop gradient: c1 at t=0, c2 at t=0.5, c3 at t=1
vec3 grad3(float t, vec3 c1, vec3 c2, vec3 c3) {
    return t < 0.5 ? mix(c1, c2, t * 2.0) : mix(c2, c3, t * 2.0 - 1.0);
}

vec3 shade(float t) {
    if (uPalette == 1)   // Fire: ember red -> orange -> pale yellow
        return grad3(t, vec3(0.25, 0.02, 0.02), vec3(0.95, 0.45, 0.08), vec3(1.00, 0.95, 0.65));
    if (uPalette == 2)   // Viridis-like: purple -> teal -> yellow
        return grad3(t, vec3(0.27, 0.00, 0.33), vec3(0.13, 0.57, 0.55), vec3(0.99, 0.91, 0.14));
    if (uPalette == 3)   // Rainbow: hue sweeps with state, alive = red/bright
        return hsv2rgb(vec3(0.83 * (1.0 - t), 0.85, 0.35 + 0.65 * t));
    if (uPalette == 4) { // Position: hue from grid position, brightness from state
        float hue = fract(vPosN.x * 0.45 + vPosN.y * 0.30 + vPosN.z * 0.25);
        return hsv2rgb(vec3(hue, 0.75, 0.30 + 0.70 * t));
    }
    // 0: Ice (default) — deep violet decaying, cyan alive
    return mix(vec3(0.15, 0.05, 0.40), vec3(0.20, 0.90, 1.00), t);
}

void main() {
    float d   = max(dot(normalize(vNormal), normalize(uLightDir)), 0.0);
    float lit = 0.25 + 0.75 * d;
    FragColor = vec4(shade(vState) * lit, 1.0);
}
)";

GLViewport::GLViewport(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumSize(400, 400);
    setFocusPolicy(Qt::StrongFocus);
}

GLViewport::~GLViewport() {
    makeCurrent();
    if (m_cubeVbo) glDeleteBuffers(1, &m_cubeVbo);
    if (m_instVbo) glDeleteBuffers(1, &m_instVbo);
    if (m_vao)     glDeleteVertexArrays(1, &m_vao);
    if (m_program) glDeleteProgram(m_program);
    doneCurrent();
}

void GLViewport::setGridSize(int n) {
    m_gridN = n;
    m_dist  = n * 1.9f;   // frame the whole grid
    update();
}

void GLViewport::setPalette(int p) {
    m_palette = p;
    update();
}

void GLViewport::setInstances(const std::vector<float>& data, int count) {
    m_pending      = data;
    m_instanceCount = count;
    m_pendingDirty = true;
    update();
}

GLuint GLViewport::compileProgram() {
    auto compile = [&](GLenum type, const char* src) -> GLuint {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        GLint ok = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(sh, 1024, nullptr, log);
            qWarning("Shader compile error: %s", log);
        }
        return sh;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, kVert);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, 1024, nullptr, log);
        qWarning("Program link error: %s", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void GLViewport::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.04f, 0.04f, 0.06f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_program = compileProgram();
    m_locMVP      = glGetUniformLocation(m_program, "uMVP");
    m_locLightDir = glGetUniformLocation(m_program, "uLightDir");
    m_locCell     = glGetUniformLocation(m_program, "uCell");
    m_locCenter   = glGetUniformLocation(m_program, "uCenter");
    m_locPalette  = glGetUniformLocation(m_program, "uPalette");

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_cubeVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCube), kCube, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glGenBuffers(1, &m_instVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_instVbo);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribDivisor(2, 1);   // one instance datum per cube

    glBindVertexArray(0);
}

void GLViewport::resizeGL(int w, int h) {
    m_proj.setToIdentity();
    m_proj.perspective(45.0f, (h > 0) ? (float)w / (float)h : 1.0f, 0.1f, 5000.0f);
}

void GLViewport::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_program) return;

    if (m_pendingDirty) {
        glBindBuffer(GL_ARRAY_BUFFER, m_instVbo);
        glBufferData(GL_ARRAY_BUFFER,
                     (GLsizeiptr)(m_pending.size() * sizeof(float)),
                     m_pending.empty() ? nullptr : m_pending.data(),
                     GL_DYNAMIC_DRAW);
        m_pendingDirty = false;
    }
    if (m_instanceCount <= 0) return;

    QMatrix4x4 view;
    view.translate(0.0f, 0.0f, -m_dist);
    view.rotate(m_pitch, 1.0f, 0.0f, 0.0f);
    view.rotate(m_yaw,   0.0f, 1.0f, 0.0f);
    QMatrix4x4 mvp = m_proj * view;

    float half = m_gridN * 0.5f;

    glUseProgram(m_program);
    glUniformMatrix4fv(m_locMVP, 1, GL_FALSE, mvp.constData());
    glUniform3f(m_locLightDir, 0.5f, 0.8f, 0.6f);
    glUniform1f(m_locCell, 1.0f);
    glUniform3f(m_locCenter, half, half, half);
    glUniform1i(m_locPalette, m_palette);

    glBindVertexArray(m_vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 36, m_instanceCount);
    glBindVertexArray(0);
}

void GLViewport::mousePressEvent(QMouseEvent* e) {
    m_lastMouse = e->pos();
}

void GLViewport::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) {
        QPoint d = e->pos() - m_lastMouse;
        m_lastMouse = e->pos();
        m_yaw   += d.x() * 0.4f;
        m_pitch += d.y() * 0.4f;
        if (m_pitch >  89.0f) m_pitch =  89.0f;
        if (m_pitch < -89.0f) m_pitch = -89.0f;
        update();
    }
}

void GLViewport::wheelEvent(QWheelEvent* e) {
    float steps = e->angleDelta().y() / 120.0f;
    m_dist *= std::pow(0.9f, steps);
    if (m_dist < 5.0f)    m_dist = 5.0f;
    if (m_dist > 2000.0f) m_dist = 2000.0f;
    update();
}
