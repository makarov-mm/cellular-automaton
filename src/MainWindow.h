#pragma once
#include <QMainWindow>
#include "Types.h"
#include "Automaton.h"

class GLViewport;
class QComboBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QTimer;
class QCheckBox;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onTick();
    void onRuleChanged();
    void onGridChanged();
    void onControlsChanged();
    void onPlayPause();
    void onReset();
    void onBenchmark();

private:
    Backend currentBackend() const;
    void    pushInstances();       // collect + upload current grid to the viewport

    AutomatonParams m_params;
    Automaton       m_auto;
    QTimer*         m_timer   = nullptr;
    bool            m_playing = true;

    GLViewport*  m_view         = nullptr;
    QComboBox*   m_ruleCombo    = nullptr;
    QComboBox*   m_gridCombo    = nullptr;
    QComboBox*   m_backendCombo = nullptr;
    QComboBox*   m_paletteCombo = nullptr;
    QCheckBox*   m_wrapCheck    = nullptr;
    QSpinBox*    m_substepsSpin = nullptr;
    QPushButton* m_playButton   = nullptr;
    QLabel*      m_timingLabel  = nullptr;
    QLabel*      m_benchLabel   = nullptr;
    QLabel*      m_cellsLabel   = nullptr;

    std::vector<float> m_instanceScratch;
    bool m_suppress = false;
};
