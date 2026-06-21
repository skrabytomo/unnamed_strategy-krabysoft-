#pragma once
#include "../sim/SimTypes.h"
#include "../sim/Simulator.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

// ImGui window for the Combat Simulator.
// Opened from the map editor or from Game::renderEditor().
// Runs simulation in a background thread so the UI stays responsive.
class SimulatorWindow
{
public:
    ~SimulatorWindow();

    // Call once per frame inside an ImGui frame
    void render();

    bool isOpen() const { return m_open; }
    void setOpen(bool v) { m_open = v; }

private:
    void drawConfigPanel();
    void drawResultsPanel();
    void drawMatchupGrid();
    void drawBalanceReport();
    void launchSimulation();
    void stopSimulation();

    static const char* factionLabel(int idx);

    // Window state
    bool m_open = false;

    // Config
    int  m_faction1    = 0;
    int  m_faction2    = 1;
    bool m_allVsAll    = false;
    int  m_weeks       = 4;
    int  m_numBattles  = 1000;
    int  m_seed        = 42;
    int  m_ai1         = 1;   // AIDifficulty index: 0=Passive,1=Standard,2=Tactical
    int  m_ai2         = 1;

    // Simulation thread
    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<int>  m_progressDone{0};
    std::atomic<int>  m_progressTotal{1};
    std::mutex        m_resultMutex;
    SimResult         m_result;
    bool              m_hasResult = false;

    // Drilldown selection
    int m_selectedRow = -1;
    int m_selectedCol = -1;
};
