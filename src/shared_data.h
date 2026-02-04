#pragma once
#include <atomic>

struct SharedData {
    // VOM TRAINER
    std::atomic<bool> connected{false};
    std::atomic<float> current_watt{0.0f};
    std::atomic<float> current_speed{0.0f};
    std::atomic<int>   current_cadence{0};
    
    // ZUM TRAINER
    std::atomic<bool> app_running{true};
    std::atomic<int>  target_watt{100};
    std::atomic<float> target_slope{0.0f};
    std::atomic<int> control_mode{1};       
    std::atomic<bool> record_log{false};
    
    // NEU: KALIBRIERUNG
    // 1.0 = Normal. 
    // 0.8 = Trainer bremst 20% weniger.
    std::atomic<float> scale_erg{0.75f};   // Startwert 0.75 (da du sagtest es ist zu hart)
    std::atomic<float> scale_grade{1.0f};  // Faktor für Steigung
};
