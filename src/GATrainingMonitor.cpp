#include "GATrainingMonitor.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <cstdio>

// ─── История ─────────────────────────────────────────────────────────────────
void GATrainingMonitor::addGeneration(const GenerationStats& s) {
    history_.push_back(s);
    if ((int)history_.size() > kMaxHistory)
        history_.erase(history_.begin());
}

// ─── Вспомогательная: float-серия из поля структуры ─────────────────────────
std::vector<float> GATrainingMonitor::series(float GenerationStats::*fld) const {
    std::vector<float> out;
    out.reserve(history_.size());
    for (const auto& g : history_) out.push_back(g.*fld);
    return out;
}

// ─── Мини-график через ImDrawList ────────────────────────────────────────────
void GATrainingMonitor::sparkline(const char* id,
                                  const std::vector<float>& vals,
                                  float minV, float maxV,
                                  ImVec2 size,
                                  ImVec4 color,
                                  bool   fillArea)
{
    if (vals.empty()) return;
    if (ImGui::GetCurrentContext() == nullptr) return;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Фон
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                      IM_COL32(30, 30, 35, 200), 3.f);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                IM_COL32(80, 80, 90, 180), 3.f);

    // Нулевая линия (если minV < 0 < maxV)
    if (minV < 0.f && maxV > 0.f) {
        float t = 1.f - (0.f - minV) / (maxV - minV + 1e-6f);
        float y0 = pos.y + t * size.y;
        dl->AddLine(ImVec2(pos.x, y0), ImVec2(pos.x + size.x, y0),
                    IM_COL32(120, 120, 120, 100));
    }

    const float range = maxV - minV;
    const int   N     = (int)vals.size();

    auto toScreen = [&](int i, float v) -> ImVec2 {
        float x = pos.x + (float)i / (float)(N - 1) * size.x;
        float t = (range > 1e-6f) ? (v - minV) / range : 0.5f;
        float y = pos.y + (1.f - t) * size.y;
        return ImVec2(x, y);
    };

    ImU32 col = ImGui::ColorConvertFloat4ToU32(color);

    if (fillArea && N >= 2) {
        // Заливка через треугольники (strip)
        float baseY = pos.y + size.y;
        ImU32 fillCol = IM_COL32(
            (int)(color.x * 255), (int)(color.y * 255),
            (int)(color.z * 255), 50);
        for (int i = 0; i < N - 1; ++i) {
            ImVec2 p0 = toScreen(i,   vals[i]);
            ImVec2 p1 = toScreen(i+1, vals[i+1]);
            dl->AddQuadFilled(
                p0, p1,
                ImVec2(p1.x, baseY), ImVec2(p0.x, baseY),
                fillCol);
        }
    }

    // Линия
    for (int i = 0; i < N - 1; ++i) {
        dl->AddLine(toScreen(i, vals[i]), toScreen(i+1, vals[i+1]), col, 1.5f);
    }

    // Крайняя точка
    if (N >= 1) {
        ImVec2 last = toScreen(N-1, vals[N-1]);
        dl->AddCircleFilled(last, 3.f, col);
    }

    // Текущее значение
    if (N >= 1) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", vals.back());
        dl->AddText(ImVec2(pos.x + size.x - 40.f, pos.y + 2.f),
                    IM_COL32(220,220,220,220), buf);
    }

    // Подпись
    dl->AddText(ImVec2(pos.x + 4.f, pos.y + 2.f),
                IM_COL32(160,160,160,180), id);

    ImGui::Dummy(size);
    ImGui::Spacing();
}

// ─── Строка-сводка вверху окна ───────────────────────────────────────────────
void GATrainingMonitor::renderSummaryBar(const GenerationStats& s) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.2f, 1.f));
    ImGui::Text(" Gen: %d  |  Pop: %d  |  Elite: %d  |  σ: %.4f",
                s.generation, s.populationSize, s.eliteCount, s.mutationSigma);
    ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::Columns(4, "sumcols", false);

    auto statCell = [&](const char* label, float val, ImVec4 col) {
        ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.f), "%s", label);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Text("%.3f", val);
        ImGui::PopStyleColor();
        ImGui::NextColumn();
    };

    statCell("Best fit",   s.bestFitness,  ImVec4(0.3f, 1.f, 0.4f, 1.f));
    statCell("Mean fit",   s.meanFitness,  ImVec4(0.9f, 0.9f, 0.3f, 1.f));
    statCell("Worst fit",  s.worstFitness, ImVec4(1.f,  0.4f, 0.4f, 1.f));
    statCell("Diversity",  s.diversity,    ImVec4(0.5f, 0.8f, 1.f,  1.f));

    ImGui::Columns(1);

    ImGui::Columns(3, "sumcols2", false);
    auto statCell2 = [&](const char* label, float val, const char* fmt = "%.2f") {
        ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.f), "%s", label);
        char buf[32]; std::snprintf(buf, sizeof(buf), fmt, val);
        ImGui::Text("%s", buf);
        ImGui::NextColumn();
    };
    statCell2("Best prog",  s.bestProgress  * 100.f, "%.1f%%");
    statCell2("Mean prog",  s.meanProgress  * 100.f, "%.1f%%");
    statCell2("Mean spd",   s.meanSpeed,              "%.1f m/s");
    ImGui::Columns(1);
    ImGui::Separator();
}

// ─── Таблица особей текущего поколения ───────────────────────────────────────
void GATrainingMonitor::renderPopulationTable(const GenerationStats& s) {
    if (s.individuals.empty()) {
        ImGui::TextDisabled("No individual data recorded yet.");
        return;
    }

    const float tableH = std::min(200.f, (float)s.individuals.size() * 20.f + 30.f);
    ImGui::BeginChild("##pop_table", ImVec2(0.f, tableH), true);

    ImGui::Columns(4, "popcols", true);
    ImGui::SetColumnWidth(0, 36.f);
    ImGui::SetColumnWidth(1, 90.f);
    ImGui::SetColumnWidth(2, 80.f);

    ImGui::TextDisabled("#"); ImGui::NextColumn();
    ImGui::TextDisabled("Fitness");   ImGui::NextColumn();
    ImGui::TextDisabled("Progress"); ImGui::NextColumn();
    ImGui::TextDisabled("Speed");    ImGui::NextColumn();
    ImGui::Separator();

    for (int i = 0; i < (int)s.individuals.size(); ++i) {
        const auto& ind = s.individuals[i];

        // Цвет строки
        ImVec4 rowColor = ind.isElite
            ? ImVec4(0.3f, 1.f,  0.4f, 1.f)     // элита — зелёный
            : ImVec4(0.9f, 0.9f, 0.9f, 0.85f);  // остальные — белый

        ImGui::PushStyleColor(ImGuiCol_Text, rowColor);
        ImGui::Text("%d%s", i, ind.isElite ? "*" : "");
        ImGui::NextColumn();
        ImGui::Text("%.3f", ind.fitness);
        ImGui::NextColumn();
        ImGui::Text("%.1f%%", ind.progress * 100.f);
        ImGui::NextColumn();
        ImGui::Text("%.1f", ind.speed);
        ImGui::NextColumn();
        ImGui::PopStyleColor();

        // Полоска под строкой — визуальный индикатор фитнеса
        float barW = std::clamp(ind.fitness / std::max(0.01f, s.bestFitness), 0.f, 1.f);
        ImVec2 p = ImGui::GetCursorScreenPos();
        // сдвигаем назад на высоту строки
        p.y -= 2.f;
        float lineW = ImGui::GetColumnWidth(0) + ImGui::GetColumnWidth(1);
        ImGui::GetWindowDrawList()->AddRectFilled(
            p, ImVec2(p.x + lineW * barW, p.y + 2.f),
            ind.isElite ? IM_COL32(60,220,80,80) : IM_COL32(80,140,220,60));
    }

    ImGui::Columns(1);
    ImGui::EndChild();
}

// ─── Графики истории ─────────────────────────────────────────────────────────
void GATrainingMonitor::renderCharts() {
    if (history_.empty()) {
        ImGui::TextDisabled("No generation history yet. Run training to see charts.");
        return;
    }

    float panelW = ImGui::GetContentRegionAvail().x;
    float chartW = (panelW - 12.f) * 0.5f;
    float chartH = 80.f;

    // ── Ряд 1: Fitness ────────────────────────────────────────────────────────
    {
        auto best  = series(&GenerationStats::bestFitness);
        auto mean  = series(&GenerationStats::meanFitness);
        auto worst = series(&GenerationStats::worstFitness);

        float fmin = *std::min_element(worst.begin(), worst.end());
        float fmax = *std::max_element(best.begin(), best.end());
        float margin = (fmax - fmin) * 0.1f + 0.01f;

        sparkline("Best fit",  best,  fmin-margin, fmax+margin,
                  ImVec2(chartW, chartH), ImVec4(0.3f,1.f,0.4f,1.f));
        ImGui::SameLine(0.f, 6.f);
        sparkline("Mean fit",  mean,  fmin-margin, fmax+margin,
                  ImVec2(chartW, chartH), ImVec4(0.9f,0.9f,0.3f,1.f));

        sparkline("Worst fit", worst, fmin-margin, fmax+margin,
                  ImVec2(chartW, chartH), ImVec4(1.f,0.4f,0.4f,1.f));
        ImGui::SameLine(0.f, 6.f);

        // Diversity
        auto div = series(&GenerationStats::diversity);
        float dmin = *std::min_element(div.begin(), div.end());
        float dmax = *std::max_element(div.begin(), div.end());
        sparkline("Diversity", div, dmin*0.9f, dmax*1.1f+0.01f,
                  ImVec2(chartW, chartH), ImVec4(0.5f,0.8f,1.f,1.f));
    }

    ImGui::Spacing();

    // ── Ряд 2: Progress / Speed / Sigma ──────────────────────────────────────
    {
        auto prog  = series(&GenerationStats::bestProgress);
        auto speed = series(&GenerationStats::meanSpeed);
        auto sigma = series(&GenerationStats::mutationSigma);

        sparkline("Best progress", prog, 0.f, 1.f,
                  ImVec2(chartW, chartH), ImVec4(0.6f,0.9f,1.f,1.f));
        ImGui::SameLine(0.f, 6.f);
        sparkline("Mean speed m/s", speed,
                  0.f, *std::max_element(speed.begin(), speed.end())*1.15f + 0.1f,
                  ImVec2(chartW, chartH), ImVec4(1.f,0.7f,0.3f,1.f));

        float smin = *std::min_element(sigma.begin(), sigma.end());
        float smax = *std::max_element(sigma.begin(), sigma.end());
        sparkline("Mutation σ", sigma, smin*0.9f, smax*1.1f+1e-4f,
                  ImVec2(chartW, chartH), ImVec4(0.9f,0.5f,1.f,1.f));

        ImGui::SameLine(0.f, 6.f);

        // Fitness range bar (best - worst) — показывает расхождение в популяции
        std::vector<float> spread;
        spread.reserve(history_.size());
        for (const auto& g : history_)
            spread.push_back(g.bestFitness - g.worstFitness);
        float spmax = *std::max_element(spread.begin(), spread.end());
        sparkline("Fit spread", spread, 0.f, spmax*1.1f+0.01f,
                  ImVec2(chartW, chartH), ImVec4(1.f,0.6f,0.6f,1.f), false);
    }
}

// ─── Управление параметрами (передаём ссылки на поля CarGame) ────────────────
void GATrainingMonitor::renderControls(
    int&   popSize,
    int&   eliteCount,
    int&   hiddenSize,
    float& sigma,
    float& mutProb,
    float& episodeTime,
    bool&  running,
    bool&  autoReset,
    bool   hasGenomes,
    bool&  doSetup,
    bool&  doSave,
    bool&  doReset)
{
    (void)hasGenomes; // зарезервировано для будущего использования
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f,0.7f,1.f,1.f), "  Controls");
    ImGui::Spacing();

    float bw = (ImGui::GetContentRegionAvail().x - 8.f) / 3.f;

    // ── Кнопки Run/Pause/Reset/Next gen ──────────────────────────────────────
    {
        if (running) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f,0.2f,0.2f,1.f));
            if (ImGui::Button("Pause", ImVec2(bw, 0))) running = false;
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f,0.6f,0.2f,1.f));
            if (ImGui::Button("Run  ", ImVec2(bw, 0))) running = true;
            ImGui::PopStyleColor();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset ep.", ImVec2(bw, 0))) doReset = true;
        ImGui::SameLine();
        if (ImGui::Button("Save best", ImVec2(bw, 0))) doSave = true;
    }

    ImGui::Spacing();

    // ── Population params ─────────────────────────────────────────────────────
    ImGui::PushItemWidth(-1.f);

    ImGui::SliderInt("Population",  &popSize,    2,  64);
    ImGui::SliderInt("Elite",       &eliteCount, 1,  std::max(1, popSize / 2));
    ImGui::SliderInt("Hidden size", &hiddenSize, 8,  128);
    ImGui::SliderFloat("Mut. sigma",  &sigma,   0.005f, 0.6f, "%.4f");
    ImGui::SliderFloat("Mut. prob",   &mutProb, 0.01f,  0.5f, "%.3f");
    ImGui::SliderFloat("Episode s.",  &episodeTime, 5.f, 240.f, "%.0f s");
    ImGui::Checkbox("Auto-reset episodes", &autoReset);

    ImGui::PopItemWidth();
    ImGui::Spacing();

    // ── Setup / Clear ─────────────────────────────────────────────────────────
    float hw = (ImGui::GetContentRegionAvail().x - 8.f) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f,0.4f,0.7f,1.f));
    if (ImGui::Button("Setup GA population", ImVec2(hw, 0))) doSetup = true;
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Clear history", ImVec2(hw, 0))) clear();
}

// ─── Главный метод — рисует всё окно ─────────────────────────────────────────
void GATrainingMonitor::renderWindow(bool* p_open)
{
    if (!showWindow) return;

    ImGui::SetNextWindowSizeConstraints(ImVec2(480, 320), ImVec2(1200, 900));
    ImGui::SetNextWindowSize(ImVec2(680, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(20, 60), ImGuiCond_FirstUseEver);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar;
    if (!ImGui::Begin("GA Training Monitor", p_open ? p_open : &showWindow, flags)) {
        ImGui::End();
        return;
    }

    // Вкладки
    if (ImGui::BeginTabBar("##ga_tabs")) {

        // ── Вкладка Overview ─────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Overview")) {
            if (!history_.empty()) {
                renderSummaryBar(history_.back());
            } else {
                ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.f),
                    "No data yet — click 'Setup GA population' and 'Run'.");
            }
            ImGui::EndTabItem();
        }

        // ── Вкладка Charts ───────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Charts")) {
            ImGui::BeginChild("##charts_scroll", ImVec2(0, 0), false,
                              ImGuiWindowFlags_HorizontalScrollbar);
            renderCharts();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        // ── Вкладка Population ───────────────────────────────────────────────
        if (ImGui::BeginTabItem("Population")) {
            if (!history_.empty()) {
                renderPopulationTable(history_.back());
            } else {
                ImGui::TextDisabled("Run at least one generation first.");
            }
            ImGui::EndTabItem();
        }

        // ── Вкладка Log ──────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Log")) {
            ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false);
            for (int i = (int)history_.size() - 1; i >= 0; --i) {
                const auto& g = history_[i];
                ImGui::Text("Gen %3d  best=%-8.3f  mean=%-8.3f  prog=%.1f%%  σ=%.4f  div=%.3f",
                    g.generation, g.bestFitness, g.meanFitness,
                    g.bestProgress * 100.f, g.mutationSigma, g.diversity);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}