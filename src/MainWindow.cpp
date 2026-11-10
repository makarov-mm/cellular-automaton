#include "MainWindow.h"

#include "GLViewport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>
#include <string_view>
#include <utility>

using namespace std::chrono_literals;
using namespace Qt::StringLiterals;

namespace {

constexpr auto kFrameInterval = 33ms;  // ~30 fps; every frame advances "Steps/frame" steps
constexpr int kDefaultGridSize = 48;
constexpr int kMaxStepsPerFrame = 20;
constexpr int kBenchmarkSteps = 20;
constexpr int kControlPanelWidth = 300;

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(Automaton::cudaAvailable() ? u"Cellular Automaton 3D — CPU vs CUDA"_s
                                              : u"Cellular Automaton 3D — CPU only"_s);
    resize(1200, 820);

    m_playButton = new QPushButton(u"Pause"_s);
    auto* resetButton = new QPushButton(u"Reset / reseed"_s);
    auto* benchmarkButton = new QPushButton(u"Benchmark all backends"_s);
    auto* hint = new QLabel(u"Drag to orbit · scroll to zoom"_s);
    hint->setStyleSheet(u"color: #888;"_s);

    auto* panel = new QWidget;
    panel->setFixedWidth(kControlPanelWidth);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->addWidget(createParametersBox());
    panelLayout->addWidget(m_playButton);
    panelLayout->addWidget(resetButton);
    panelLayout->addWidget(benchmarkButton);
    panelLayout->addWidget(createTimingBox());
    panelLayout->addStretch();
    panelLayout->addWidget(hint);

    m_view = new GLViewport;
    auto* central = new QWidget;
    auto* centralLayout = new QHBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->addWidget(panel);
    centralLayout->addWidget(m_view, 1);
    setCentralWidget(central);

    m_timer = new QTimer(this);
    m_timer->setInterval(kFrameInterval);

    connect(m_ruleCombo, &QComboBox::currentIndexChanged, this, &MainWindow::resetAutomaton);
    connect(m_gridCombo, &QComboBox::currentIndexChanged, this, &MainWindow::resetAutomaton);
    connect(m_wrapCheck, &QCheckBox::toggled, this,
            [this](bool wrap) { m_automaton.setWrap(wrap); });
    connect(m_paletteCombo, &QComboBox::currentIndexChanged, this, [this] {
        m_view->setPalette(GLViewport::Palette{m_paletteCombo->currentData().toInt()});
    });
    connect(m_playButton, &QPushButton::clicked, this, &MainWindow::togglePlayback);
    connect(resetButton, &QPushButton::clicked, this, &MainWindow::resetAutomaton);
    connect(benchmarkButton, &QPushButton::clicked, this, &MainWindow::runBenchmark);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::advance);

    m_automaton.setWrap(m_wrapCheck->isChecked());
    resetAutomaton();
    m_timer->start();
}

QWidget* MainWindow::createParametersBox() {
    m_ruleCombo = new QComboBox;
    for (const RulePreset& preset : Automaton::presets())
        m_ruleCombo->addItem(toQString(preset.name));

    m_gridCombo = new QComboBox;
    for (const int size : {32, 48, 64, 96})
        m_gridCombo->addItem(u"%1 x %1 x %1"_s.arg(size), size);
    m_gridCombo->setCurrentIndex(m_gridCombo->findData(kDefaultGridSize));

    m_stepsSpin = new QSpinBox;
    m_stepsSpin->setRange(1, kMaxStepsPerFrame);

    // Starts on the last back-end of the list: CUDA when there is a device,
    // otherwise the multi-threaded CPU.
    m_backendCombo = new QComboBox;
    for (const Backend backend : {Backend::CpuSingle, Backend::CpuMulti, Backend::Cuda})
        if (backend != Backend::Cuda || Automaton::cudaAvailable())
            m_backendCombo->addItem(toQString(Automaton::backendName(backend)),
                                    std::to_underlying(backend));
    m_backendCombo->setCurrentIndex(m_backendCombo->count() - 1);

    m_paletteCombo = new QComboBox;
    const auto addPalette = [this](const QString& name, GLViewport::Palette palette) {
        m_paletteCombo->addItem(name, std::to_underlying(palette));
    };
    addPalette(u"Ice (blue)"_s, GLViewport::Palette::Ice);
    addPalette(u"Fire"_s, GLViewport::Palette::Fire);
    addPalette(u"Viridis"_s, GLViewport::Palette::Viridis);
    addPalette(u"Rainbow"_s, GLViewport::Palette::Rainbow);
    addPalette(u"Position"_s, GLViewport::Palette::Position);

    // Several rules only stay alive on a torus: with dead edges everything
    // that drifts out of the grid is lost.
    m_wrapCheck = new QCheckBox(u"Wrap edges (toroidal)"_s);
    m_wrapCheck->setChecked(true);

    auto* box = new QGroupBox(u"Parameters"_s);
    auto* form = new QFormLayout(box);
    form->addRow(u"Rule"_s, m_ruleCombo);
    form->addRow(u"Grid"_s, m_gridCombo);
    form->addRow(u"Steps/frame"_s, m_stepsSpin);
    form->addRow(u"Backend"_s, m_backendCombo);
    form->addRow(u"Palette"_s, m_paletteCombo);
    form->addRow(QString(), m_wrapCheck);
    return box;
}

QWidget* MainWindow::createTimingBox() {
    m_timingLabel = new QLabel(u"—"_s);
    m_timingLabel->setWordWrap(true);
    m_cellsLabel = new QLabel(u"—"_s);
    m_benchmarkLabel = new QLabel;
    m_benchmarkLabel->setWordWrap(true);
    m_benchmarkLabel->setTextFormat(Qt::RichText);

    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);

    auto* box = new QGroupBox(u"Timing"_s);
    auto* layout = new QVBoxLayout(box);
    layout->addWidget(m_timingLabel);
    layout->addWidget(m_cellsLabel);
    layout->addWidget(separator);
    layout->addWidget(m_benchmarkLabel);
    return box;
}

Backend MainWindow::selectedBackend() const {
    return Backend{m_backendCombo->currentData().toInt()};
}

void MainWindow::resetAutomaton() {
    m_automaton.reset(m_gridCombo->currentData().toInt(), m_ruleCombo->currentIndex());
    showCells();
}

void MainWindow::showCells() {
    const auto cells = m_automaton.collectInstances();
    m_view->setCells(cells, m_automaton.gridSize(), m_automaton.aliveState());
    m_cellsLabel->setText(u"%1 live/decaying cells"_s.arg(cells.size()));
}

void MainWindow::advance() {
    const int steps = m_stepsSpin->value();
    const StepResult result = m_automaton.step(selectedBackend(), steps);
    showCells();

    QString text = u"%1\n%2 ms/step"_s.arg(toQString(Automaton::backendName(result.backend)))
                       .arg(result.milliseconds / steps, 0, 'f', 3);
    if (!result.error.empty()) text += u"\nCUDA failed: %1"_s.arg(toQString(result.error));
    m_timingLabel->setText(text);
}

void MainWindow::togglePlayback() {
    const bool play = !m_timer->isActive();
    if (play)
        m_timer->start();
    else
        m_timer->stop();
    m_playButton->setText(play ? u"Pause"_s : u"Play"_s);
}

void MainWindow::runBenchmark() {
    // Runs on the GUI thread and can take a moment on large grids.
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const std::vector<StepResult> results = m_automaton.benchmark(kBenchmarkSteps);
    QGuiApplication::restoreOverrideCursor();

    const int n = m_automaton.gridSize();
    const double baseline = results.front().milliseconds;  // single-threaded CPU
    QString text =
        u"<b>Benchmark</b> (%1<sup>3</sup> cells, %2 steps)<br/>"_s.arg(n).arg(kBenchmarkSteps);
    for (const StepResult& result : results) {
        if (!result.error.empty()) {
            text += u"CUDA failed: %1<br/>"_s.arg(toQString(result.error));
            continue;
        }
        text += u"%1: <b>%2 ms/step</b> (%3x)<br/>"_s
                    .arg(toQString(Automaton::backendName(result.backend)))
                    .arg(result.milliseconds / kBenchmarkSteps, 0, 'f', 3)
                    .arg(baseline / result.milliseconds, 0, 'f', 1);
    }
    m_benchmarkLabel->setText(text);
}
