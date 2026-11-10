#pragma once

#include "Automaton.h"

#include <QMainWindow>

class GLViewport;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    QWidget* createParametersBox();
    QWidget* createTimingBox();

    void resetAutomaton();  // new grid from the selected rule and grid size
    void advance();         // one timer tick: step the simulation and redraw
    void showCells();       // hand the current grid to the viewport
    void togglePlayback();
    void runBenchmark();

    Backend selectedBackend() const;

    Automaton m_automaton;
    QTimer* m_timer = nullptr;  // runs while the simulation is playing

    GLViewport* m_view = nullptr;
    QComboBox* m_ruleCombo = nullptr;
    QComboBox* m_gridCombo = nullptr;
    QSpinBox* m_stepsSpin = nullptr;
    QComboBox* m_backendCombo = nullptr;
    QComboBox* m_paletteCombo = nullptr;
    QCheckBox* m_wrapCheck = nullptr;
    QPushButton* m_playButton = nullptr;
    QLabel* m_timingLabel = nullptr;
    QLabel* m_cellsLabel = nullptr;
    QLabel* m_benchmarkLabel = nullptr;
};
