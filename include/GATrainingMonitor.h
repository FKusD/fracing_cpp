#pragma once
#include <vector>
#include <string>
#include <imgui.h>

// ─── Статистика одного поколения ─────────────────────────────────────────────
struct GenerationStats {
    int   generation     = 0;
    float bestFitness    = 0.f;
    float meanFitness    = 0.f;
    float worstFitness   = 0.f;
    float bestProgress   = 0.f;  // [0..1]
    float meanProgress   = 0.f;
    float meanSpeed      = 0.f;  // m/s
    float mutationSigma  = 0.f;
    float diversity      = 0.f;  // std(L2-norm of each genome)
    int   populationSize = 0;
    int   eliteCount     = 0;
    float episodeTime    = 0.f;

    // Per-individual snapshot (sorted by fitness desc)
    struct Individual {
        float fitness  = 0.f;
        float progress = 0.f;
        float speed    = 0.f;
        bool  isElite  = false;
    };
    std::vector<Individual> individuals;
};

// ─── Монитор обучения GA — отдельное ImGui-окно ──────────────────────────────
class GATrainingMonitor {
public:
    // Добавить запись в историю (вызывать в advanceGAGeneration)
    void addGeneration(const GenerationStats& s);

    // Нарисовать окно. Вызывать между ImGui::NewFrame() и ImGui::Render().
    // *p_open — флаг закрытия окна (nullptr = без крестика).
    void renderWindow(bool* p_open = nullptr);

    // Показывать ли окно
    bool showWindow = true;

    // Сброс истории
    void clear() { history_.clear(); }

    const std::vector<GenerationStats>& history() const { return history_; }

private:
    static constexpr int kMaxHistory = 500;
    std::vector<GenerationStats> history_;

    // ── Внутренние хелперы ────────────────────────────────────────────────────
    void renderSummaryBar(const GenerationStats& s);
    void renderPopulationTable(const GenerationStats& s);
    void renderCharts();
    void renderControls(
        int&   popSize,
        int&   eliteCount,
        int&   hiddenSize,
        float& sigma,
        float& mutProb,
        float& episodeTime,
        bool&  running,
        bool&  autoReset,
        bool   hasGenomes,        // есть ли активные геном-машины (зарезервировано)
        bool& doSetup,
        bool& doSave,
        bool& doReset
    );

    // Рисует простой мини-график через DrawList
    void sparkline(const char* id,
                   const std::vector<float>& vals,
                   float minV, float maxV,
                   ImVec2 size,
                   ImVec4 color,
                   bool   fillArea = true);

    // Вытащить float-серию из истории
    std::vector<float> series(float GenerationStats::*fld) const;
};