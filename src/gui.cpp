#include "gui.h"
#include "imgui.h"
#include <string>
#include <vector>

void BeginCard(const char* name, ImVec4 color) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, color);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::BeginChild(name, ImVec2(0, 150), true);
}
void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void RenderDashboard(SharedData* data) {
    static float watt_history[100] = {};
    static int history_offset = 0;
    static float update_timer = 0.0f;

    float watt = data->current_watt;
    float speed = data->current_speed;
    int cad = data->current_cadence;
    bool conn = data->connected;
    int mode = data->control_mode;
    
    // Tasten
    bool key_plus = ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) || ImGui::IsKeyPressed(ImGuiKey_Equal);
    bool key_minus = ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract) || ImGui::IsKeyPressed(ImGuiKey_Minus);
    
    if (key_plus) {
        if (mode == 1) data->target_watt = data->target_watt + 5;
        if (mode == 2) data->target_slope = data->target_slope + 0.5f;
    }
    if (key_minus) {
        if (mode == 1) data->target_watt = data->target_watt - 5;
        if (mode == 2) data->target_slope = data->target_slope - 0.5f;
    }

    update_timer += ImGui::GetIO().DeltaTime;
    if (update_timer > 0.1f) {
        watt_history[history_offset] = watt;
        history_offset = (history_offset + 1) % 100;
        update_timer = 0.0f;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 10));
    ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    // TOP BAR
    if (conn) ImGui::TextColored(ImVec4(0,1,0,1), "VERBUNDEN");
    else      ImGui::TextColored(ImVec4(1,0,0,1), "SUCHE...");
    
    ImGui::SameLine();
    ImGui::Text("| Modus: %s", (mode == 1) ? "ERG" : "GRADE");

    ImGui::SameLine(ImGui::GetWindowWidth() - 150);
    bool is_rec = data->record_log;
    if (is_rec) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.1f, 0.1f, 1.0f));
    if (ImGui::Button(is_rec ? "STOP REC" : "START REC", ImVec2(130, 40))) data->record_log = !is_rec;
    if (is_rec) ImGui::PopStyleColor();

    ImGui::Separator();
    
    // METRIK KARTEN
    ImGui::Columns(3, "metrics", false);

    BeginCard("WattCard", ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "LEISTUNG");
    ImGui::SetWindowFontScale(2.5f); 
    ImGui::Text("%.0f W", watt);
    ImGui::SetWindowFontScale(1.0f); 
    EndCard();
    ImGui::NextColumn();

    BeginCard("SpeedCard", ImVec4(0.15f, 0.15f, 0.25f, 1.0f));
    ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "TEMPO");
    ImGui::SetWindowFontScale(2.5f);
    ImGui::Text("%.1f", speed);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Text("km/h");
    EndCard();
    ImGui::NextColumn();

    BeginCard("CadCard", ImVec4(0.15f, 0.25f, 0.15f, 1.0f));
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "KADENZ");
    ImGui::SetWindowFontScale(2.5f);
    ImGui::Text("%d", cad);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Text("rpm");
    EndCard();
    
    ImGui::Columns(1);
    ImGui::Spacing(); ImGui::Spacing();

    // STEUERUNG
    ImGui::BeginChild("Controls", ImVec2(0, 180), true);
    ImGui::Columns(2, "ctrl", false);
    
    ImGui::Text("Modus Auswahl:");
    int temp_mode = mode;
    if (ImGui::RadioButton("ERG (Watt)", temp_mode == 1)) temp_mode = 1;
    if (ImGui::RadioButton("Grade (%)", temp_mode == 2)) temp_mode = 2;
    if (temp_mode != mode) data->control_mode = temp_mode;
    
    ImGui::NextColumn();
    
    if (mode == 1) {
        ImGui::Text("Ziel-Leistung (Watt):");
        int w = data->target_watt;
        ImGui::PushItemWidth(-1);
        if(ImGui::SliderInt("##W", &w, 50, 400, "%d W")) data->target_watt = w;
        ImGui::PopItemWidth();
        
        float width = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button("- 5", ImVec2(width/2 - 5, 60))) data->target_watt -= 5;
        ImGui::SameLine();
        if (ImGui::Button("+ 5", ImVec2(width/2 - 5, 60))) data->target_watt += 5;
    } else {
        ImGui::Text("Steigung (Prozent):");
        float s = data->target_slope;
        ImGui::PushItemWidth(-1);
        if(ImGui::SliderFloat("##S", &s, -5.0f, 10.0f, "%.1f %%")) data->target_slope = s;
        ImGui::PopItemWidth();

        float width = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button("- 0.5", ImVec2(width/2 - 5, 60))) {float t = data->target_slope; data->target_slope = t - 0.5f;}
        ImGui::SameLine();
        if (ImGui::Button("+ 0.5", ImVec2(width/2 - 5, 60))) {float t = data->target_slope; data->target_slope = t + 0.5f;}
    }
    ImGui::Columns(1);
    ImGui::EndChild();

    // --- NEU: SYSTEM & KALIBRIERUNG ---
    ImGui::Spacing();
    ImGui::Text("Kalibrierung (Echtzeit):");
    ImGui::Columns(2, "calib", false);
    
    // ERG SCALE SLIDER
    float s_erg = data->scale_erg;
    ImGui::Text("ERG Härte (%.2f)", s_erg);
    // Von 0.5 (sehr weich) bis 1.5 (sehr hart)
    if (ImGui::SliderFloat("##CalWatt", &s_erg, 0.5f, 1.5f, "")) data->scale_erg = s_erg;
    
    ImGui::NextColumn();
    
    // GRADE SCALE SLIDER (falls Steigung zu steil ist)
    float s_grade = data->scale_grade;
    ImGui::Text("Grade Härte (%.2f)", s_grade);
    if (ImGui::SliderFloat("##CalGrade", &s_grade, 0.5f, 2.0f, "")) data->scale_grade = s_grade;
    
    ImGui::Columns(1);
    // ----------------------------------

    ImGui::Spacing();
    ImGui::PlotLines("##G", watt_history, 100, history_offset, "Verlauf", 0.0f, 400.0f, ImVec2(-1, 80));

    ImGui::SetCursorPosY(viewport->Size.y - 60);
    if (ImGui::Button("EXIT", ImVec2(100, 40))) data->app_running = false;

    ImGui::End();
    ImGui::PopStyleVar();
}
