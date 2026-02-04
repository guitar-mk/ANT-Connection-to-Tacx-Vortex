#pragma once
#include <atomic>

// Diese Struktur ist das Herzstück der Kommunikation.
// Da GUI und Hardware-Logik in zwei verschiedenen Threads laufen,
// müssen wir sicherstellen, dass sie nicht gleichzeitig schreiben und lesen,
// ohne dass die Daten korrupt werden. 'std::atomic' regelt das automatisch.

struct SharedData {
    // --- VOM TRAINER ZUR GUI (Hardware schreibt, GUI liest) ---
    std::atomic<bool> connected{false};     // Status der USB Verbindung
    std::atomic<float> current_watt{0.0f};  // Aktuelle Leistung
    std::atomic<float> current_speed{0.0f}; // Tempo
    std::atomic<int>   current_cadence{0};  // Trittfrequenz
    
    // --- VON GUI ZUM TRAINER (GUI schreibt, Hardware liest) ---
    std::atomic<bool> app_running{true};    // Hauptschalter: false = Programm beenden
    
    // Zielwerte
    std::atomic<int>  target_watt{100};     // Sollwert für ERG Modus
    std::atomic<float> target_slope{0.0f};  // Sollwert für Grade Modus (in %)
    
    // Modus: 1 = ERG (Watt halten), 2 = GRADE (Steigung simulieren)
    std::atomic<int> control_mode{1};       
    
    // Logging Schalter
    std::atomic<bool> record_log{false};
    
    // --- KALIBRIERUNG (Experten-Einstellungen) ---
    // Diese Faktoren werden mit dem Zielwert multipliziert, bevor er an den Trainer geht.
    // Beispiel: Ziel 100 Watt * scale 0.8 = Trainer bremst nur mit 80 Watt.
    std::atomic<float> scale_erg{0.75f};   
    std::atomic<float> scale_grade{1.0f};  
};
