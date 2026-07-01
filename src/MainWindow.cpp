#include "MainWindow.h"
#include "GLViewport.h"

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QTimer>
#include <QList>
#include <QCheckBox>
#include <QApplication>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    bool cuda = Automaton::cudaSupported();
    setWindowTitle(cuda ? "Cellular Automaton 3D — CPU vs CUDA"
                        : "Cellular Automaton 3D — CPU only");

    m_suppress = true;

    m_view = new GLViewport(this);

    m_ruleCombo = new QComboBox;
    for (int i = 0; i < Automaton::presetCount(); ++i)
        m_ruleCombo->addItem(Automaton::presetName(i));

    m_gridCombo = new QComboBox;
    m_gridCombo->addItem("32 x 32 x 32", 32);
    m_gridCombo->addItem("48 x 48 x 48", 48);
    m_gridCombo->addItem("64 x 64 x 64", 64);
    m_gridCombo->addItem("96 x 96 x 96", 96);
    m_gridCombo->setCurrentIndex(1);

    m_substepsSpin = new QSpinBox;
    m_substepsSpin->setRange(1, 20);
    m_substepsSpin->setValue(1);

    m_paletteCombo = new QComboBox;
    m_paletteCombo->addItem("Ice (blue)");
    m_paletteCombo->addItem("Fire");
    m_paletteCombo->addItem("Viridis");
    m_paletteCombo->addItem("Rainbow");
    m_paletteCombo->addItem("Position");

    m_wrapCheck = new QCheckBox("Wrap edges (toroidal)");

    m_backendCombo = new QComboBox;
    m_backendCombo->addItem("CPU (single-threaded)", (int)Backend::CpuSingle);
    m_backendCombo->addItem("CPU (multi-threaded)",  (int)Backend::CpuMulti);
    if (cuda) m_backendCombo->addItem("CUDA (GPU)", (int)Backend::Cuda);
    int cudaIdx = m_backendCombo->findData((int)Backend::Cuda);
    if (cudaIdx >= 0) m_backendCombo->setCurrentIndex(cudaIdx);

    auto* form = new QFormLayout;
    form->addRow("Rule",        m_ruleCombo);
    form->addRow("Grid",        m_gridCombo);
    form->addRow("Steps/frame", m_substepsSpin);
    form->addRow("Backend",     m_backendCombo);
    form->addRow("Palette",     m_paletteCombo);
    form->addRow("",            m_wrapCheck);
    auto* paramsBox = new QGroupBox("Parameters");
    paramsBox->setLayout(form);

    m_playButton      = new QPushButton("Pause");
    auto* resetButton = new QPushButton("Reset / reseed");
    auto* benchButton = new QPushButton("Benchmark all backends");

    m_timingLabel = new QLabel("—");
    m_timingLabel->setWordWrap(true);
    m_cellsLabel  = new QLabel("—");
    m_benchLabel  = new QLabel();
    m_benchLabel->setWordWrap(true);
    m_benchLabel->setTextFormat(Qt::RichText);

    auto* statusBox    = new QGroupBox("Timing");
    auto* statusLayout = new QVBoxLayout;
    statusLayout->addWidget(m_timingLabel);
    statusLayout->addWidget(m_cellsLabel);
    auto* line = new QFrame; line->setFrameShape(QFrame::HLine);
    statusLayout->addWidget(line);
    statusLayout->addWidget(m_benchLabel);
    statusBox->setLayout(statusLayout);

    auto* hint = new QLabel("Drag to orbit · scroll to zoom");
    hint->setStyleSheet("color: #888;");

    auto* panelLayout = new QVBoxLayout;
    panelLayout->addWidget(paramsBox);
    panelLayout->addWidget(m_playButton);
    panelLayout->addWidget(resetButton);
    panelLayout->addWidget(benchButton);
    panelLayout->addWidget(statusBox);
    panelLayout->addStretch();
    panelLayout->addWidget(hint);

    auto* panel = new QWidget;
    panel->setLayout(panelLayout);
    panel->setFixedWidth(300);

    auto* central       = new QWidget;
    auto* centralLayout = new QHBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->addWidget(panel);
    centralLayout->addWidget(m_view, 1);
    setCentralWidget(central);
    resize(1200, 820);

    connect(m_ruleCombo,    &QComboBox::currentIndexChanged, this, &MainWindow::onRuleChanged);
    connect(m_gridCombo,    &QComboBox::currentIndexChanged, this, &MainWindow::onGridChanged);
    connect(m_backendCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onControlsChanged);
    connect(m_substepsSpin, &QSpinBox::valueChanged,         this, &MainWindow::onControlsChanged);
    connect(m_wrapCheck,    &QCheckBox::toggled,             this, &MainWindow::onControlsChanged);
    connect(m_paletteCombo, &QComboBox::currentIndexChanged,
            this, [this](int i) { m_view->setPalette(i); });
    connect(m_playButton,   &QPushButton::clicked,           this, &MainWindow::onPlayPause);
    connect(resetButton,    &QPushButton::clicked,           this, &MainWindow::onReset);
    connect(benchButton,    &QPushButton::clicked,           this, &MainWindow::onBenchmark);

    m_suppress = false;

    m_auto.reset(m_params);
    m_view->setGridSize(m_params.gridN);
    pushInstances();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::onTick);
    m_timer->start(33);   // ~30 fps; each tick advances substeps
}

MainWindow::~MainWindow() {
    Automaton::shutdown();   // free CUDA ping-pong buffers
}

Backend MainWindow::currentBackend() const {
    return static_cast<Backend>(m_backendCombo->currentData().toInt());
}

void MainWindow::pushInstances() {
    int count = 0;
    m_auto.collectInstances(m_instanceScratch, count);
    m_view->setInstances(m_instanceScratch, count);
    m_cellsLabel->setText(QString("%1 live/decaying cells").arg(count));
}

void MainWindow::onTick() {
    if (!m_playing) return;
    StepResult r = m_auto.step(m_params, currentBackend());
    pushInstances();
    double perStep = r.milliseconds / std::max(1, m_params.substeps);
    m_timingLabel->setText(QString("%1\n%2 ms/step")
                               .arg(r.backendName)
                               .arg(QString::number(perStep, 'f', 3)));
}

void MainWindow::onControlsChanged() {
    if (m_suppress) return;
    m_params.substeps = m_substepsSpin->value();
    m_params.wrap     = m_wrapCheck->isChecked();   // applied on the next step
}

void MainWindow::onRuleChanged() {
    m_params.rulePreset = m_ruleCombo->currentIndex();
    m_auto.reset(m_params);
    pushInstances();
}

void MainWindow::onGridChanged() {
    m_params.gridN = m_gridCombo->currentData().toInt();
    m_auto.reset(m_params);
    m_view->setGridSize(m_params.gridN);
    pushInstances();
}

void MainWindow::onPlayPause() {
    m_playing = !m_playing;
    m_playButton->setText(m_playing ? "Pause" : "Play");
}

void MainWindow::onReset() {
    m_auto.reset(m_params);
    pushInstances();
}

void MainWindow::onBenchmark() {
    // The benchmark runs synchronously on the GUI thread and can take a few
    // seconds on large grids; show a busy cursor while it does.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QList<Backend> backends { Backend::CpuSingle, Backend::CpuMulti };
    if (Automaton::cudaSupported())
        backends << Backend::Cuda;

    const int benchSteps = 20;
    int savedSub = m_params.substeps;
    m_params.substeps = benchSteps;

    QString text = QString("<b>Benchmark</b> (%1^3, %2 steps)<br/>")
                       .arg(m_params.gridN).arg(benchSteps);

    double ref = -1.0;
    m_auto.saveState();
    for (Backend b : backends) {
        m_auto.restoreState();
        StepResult r = m_auto.step(m_params, b);
        double perStep = r.milliseconds / benchSteps;
        if (b == Backend::CpuSingle) ref = perStep;
        double sp = (ref > 0.0) ? ref / perStep : 1.0;
        text += QString("%1: <b>%2 ms/step</b> (%3x)<br/>")
                    .arg(r.backendName)
                    .arg(QString::number(perStep, 'f', 3))
                    .arg(QString::number(sp, 'f', 1));
    }
    m_auto.restoreState();
    m_params.substeps = savedSub;

    m_benchLabel->setText(text);
    pushInstances();
    QApplication::restoreOverrideCursor();
}
