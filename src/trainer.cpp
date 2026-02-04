#include "trainer.h"
#include <libusb-1.0/libusb.h> // Die Bibliothek für direkten USB-Zugriff
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>
#include <cmath>
#include <ctime>
#include <iomanip>

// ==================================================================================
// HILFSFUNKTIONEN
// ==================================================================================

// Speichert die gefundene Trainer-ID in eine Textdatei, damit wir beim nächsten Mal
// nicht scannen müssen (spart 5 Sekunden Startzeit).
void save_trainer_id(unsigned short id) {
    std::ofstream file("my_trainer.txt");
    if (file.is_open()) { file << id; file.close(); }
}

unsigned short load_trainer_id_from_file() {
    std::ifstream file("my_trainer.txt");
    if (file.is_open()) { unsigned short id; file >> id; return id; }
    return 0; // 0 bedeutet: Keine ID gefunden -> Wir müssen scannen
}

// ==================================================================================
// KLASSE: ActiveScanner
// Aufgabe: Sucht einmalig nach dem Trainer, wenn wir noch keine ID haben.
// ==================================================================================
class ActiveScanner {
    libusb_context* usb = nullptr;
    libusb_device_handle* handle = nullptr;

    // XOR Checksumme: Tacx benötigt am Ende jeder Nachricht eine Prüfsumme.
    // Sie stellt sicher, dass keine Bits bei der Übertragung kippen.
    uint8_t checksum(uint8_t* msg, int len) { 
        uint8_t c = 0; 
        for (int i=0; i<len-1; i++) c ^= msg[i]; 
        return c; 
    }

    // Sendet Bytes an den USB Stick (Endpoint 0x01 = OUT)
    void send_raw(uint8_t* msg, int len) {
        if(!handle) return;
        msg[len-1] = checksum(msg, len); // Letztes Byte ist immer die Checksumme
        int t=0; 
        libusb_bulk_transfer(handle, 0x01, msg, len, &t, 100);
    }

public:
    ~ActiveScanner() { close(); } // Destruktor: Räumt automatisch auf

    void close() { 
        if(handle) { 
            libusb_release_interface(handle,0); 
            libusb_close(handle); 
            handle=nullptr; 
        }
        if(usb) { libusb_exit(usb); usb=nullptr; }
    }

    // Die eigentliche Such-Funktion
    unsigned short perform_scan_step() {
        if (libusb_init(&usb) < 0) return 0;
        
        // Versuche den ANT+ Stick zu öffnen (es gibt zwei gängige IDs)
        handle = libusb_open_device_with_vid_pid(usb, 0x0FCF, 0x1008); // Garmin Stick
        if (!handle) handle = libusb_open_device_with_vid_pid(usb, 0x0FCF, 0x1009); // Tacx Stick
        
        if (!handle) return 0; // Kein Stick eingesteckt?

        // Treiber vom Betriebssystem lösen, damit WIR ihn steuern können
        if (libusb_kernel_driver_active(handle, 0) == 1) libusb_detach_kernel_driver(handle, 0);
        libusb_set_configuration(handle, 1); 
        libusb_claim_interface(handle, 0);
        
        // --- MAGIC BYTES ---
        // Das hier weckt den Tacx Vortex auf. Ohne diese Sequenz bleibt er stumm.
        // m1: Reset, m2: Tacx Handshake
        uint8_t m1[] = {0xA4,0x01,0x4A,0x00,0x00}; send_raw(m1,sizeof(m1)); usleep(200000);
        uint8_t m2[] = {0xA4,0x09,0x46,0x00,0xB9,0xA5,0x21,0xFB,0xBD,0x72,0xC3,0x45,0x00}; send_raw(m2,sizeof(m2)); usleep(100000);
        
        // Setup für "Wildcard Scan" (Suche JEDES Gerät)
        uint8_t m4[] = {0xA4,0x05,0x51,0x00,0x00,0x00,0x00,0x00,0x00}; send_raw(m4,sizeof(m4)); usleep(100000);
        uint8_t m5[] = {0xA4,0x02,0x45,0x00,57,0x00}; send_raw(m5,sizeof(m5)); usleep(100000);

        // Wir lauschen kurz auf Antworten
        for(int k=0; k<10; k++) {
            uint8_t buf[64]; int received=0;
            // Frage nach ID
            uint8_t req[] = {0xA4,0x02,0x4D,0x00,0x51,0x00}; send_raw(req,sizeof(req));
            
            // Lesen von Endpoint 0x81 (IN)
            libusb_bulk_transfer(handle, 0x81, buf, sizeof(buf), &received, 100);
            
            if (received > 5) {
                 for (int i=0; i<received-5; i++) {
                    // Startbyte A4 gefunden? Nachricht ID 0x51 (Device ID)?
                    if (buf[i]==0xA4 && buf[i+2]==0x51) {
                        unsigned short found = buf[i+4] | (buf[i+5]<<8);
                        if (found>0) return found; // TREFFER! ID zurückgeben
                    }
                 }
            }
            usleep(100000);
        }
        return 0; // Nichts gefunden
    }
};

// ==================================================================================
// KLASSE: VortexDriver
// Aufgabe: Verwaltet die ständige Verbindung während des Trainings.
// ==================================================================================
class VortexDriver {
    libusb_context* usb = nullptr;
    libusb_device_handle* handle = nullptr;
    
    // Checksumme berechnen (Intern)
    uint8_t checksum(uint8_t* msg, int len) { 
        uint8_t c=0; for(int i=0; i<len-1; i++) c^=msg[i]; return c; 
    }
    
    // Daten senden (Intern)
    void send_raw(uint8_t* msg, int len) {
        if(!handle) return;
        msg[len-1] = checksum(msg, len);
        int t=0; libusb_bulk_transfer(handle, 0x01, msg, len, &t, 50);
    }

public:
    ~VortexDriver() { close(); }
    
    void close() { 
        if(handle) { 
            libusb_release_interface(handle,0); 
            libusb_close(handle); 
            handle=nullptr; 
        }
        if(usb) { libusb_exit(usb); usb=nullptr; }
    }

    // Verbindung aufbauen mit bekannter ID
    bool init(unsigned short target_id) {
        if (libusb_init(&usb) < 0) return false;
        handle = libusb_open_device_with_vid_pid(usb, 0x0FCF, 0x1008);
        if (!handle) handle = libusb_open_device_with_vid_pid(usb, 0x0FCF, 0x1009);
        if (!handle) return false;
        
        if (libusb_kernel_driver_active(handle, 0) == 1) libusb_detach_kernel_driver(handle, 0);
        libusb_set_configuration(handle, 1); 
        libusb_claim_interface(handle, 0);
        
        // Startsequenz (Wake Up + Setup)
        uint8_t m1[] = {0xA4,0x01,0x4A,0x00,0x00}; send_raw(m1,sizeof(m1)); usleep(200000);
        uint8_t m2[] = {0xA4,0x09,0x46,0x00,0xB9,0xA5,0x21,0xFB,0xBD,0x72,0xC3,0x45,0x00}; send_raw(m2,sizeof(m2)); usleep(100000);
        uint8_t m3[] = {0xA4,0x03,0x42,0x00,0x00,0x00,0x00}; send_raw(m3, sizeof(m3)); usleep(100000);
        
        // Hier setzen wir die Ziel-ID fest, damit wir nicht auf fremde Trainer hören
        uint8_t id_lo = (target_id & 0xFF); 
        uint8_t id_hi = ((target_id >> 8) & 0xFF);
        uint8_t m4[] = {0xA4,0x05,0x51,0x00,id_lo,id_hi,17,0x00,0x00}; send_raw(m4,sizeof(m4)); usleep(100000);
        
        // Kanal Parameter setzen
        uint8_t m5[] = {0xA4,0x02,0x45,0x00,57,0x00}; send_raw(m5,sizeof(m5)); usleep(100000);
        uint8_t m6[] = {0xA4,0x03,0x43,0x00,0x00,0x20,0x00}; send_raw(m6,sizeof(m6)); usleep(100000);
        
        // WICHTIG: Start Datastream (sonst kommen keine Watt-Werte!)
        uint8_t m7[] = {0xA4,0x01,0x4B,0x00,0x00}; send_raw(m7,sizeof(m7)); usleep(200000);
        return true;
    }

    // --- ERG MODE (Watt halten) ---
    void set_erg_watt(int watts, float scale) {
        static uint8_t ev = 0; // Event Counter (muss hochzählen)
        
        // Anwenden des Kalibrierungsfaktors aus der GUI
        int send_watts = (int)(watts * scale);
        
        // Sicherheitsgrenzen des Trainers
        if (send_watts < 10) send_watts = 10; 
        if (send_watts > 990) send_watts = 990;
        
        // Tacx erwartet: Watt * 4
        uint16_t w4 = send_watts * 4;
        
        // Befehl 0x31: Set Target Power
        uint8_t msg[] = {0xA4, 0x09, 0x4F, 0x00, 0x31, 0xFF, 0xFF, 0xFF, 0xFF, ev++, 
                         (uint8_t)(w4 & 0xFF), (uint8_t)((w4 >> 8) & 0x0F), 0x00};
        send_raw(msg, sizeof(msg));
    }

    // --- GRADE MODE (Steigung) ---
    void set_slope(float slope_percent, float scale) {
        static uint8_t ev = 0;
        
        // Anwenden des Kalibrierungsfaktors
        slope_percent = slope_percent * scale;
        
        // Limitierung auf physikalisch Mögliches (+/- 20%)
        if (slope_percent < -20.0f) slope_percent = -20.0f;
        if (slope_percent > 20.0f)  slope_percent = 20.0f;
        
        // Formel für Tacx Legacy Protocol:
        // Wert = (Prozent + 200) * 50
        // Beispiel 0%: (0 + 200) * 50 = 10000
        long val = (long)((slope_percent + 200.0f) * 50.0f);
        
        // Aufteilen in zwei Bytes
        uint8_t l_byte = (uint8_t)(val & 0xFF);
        uint8_t h_byte = (uint8_t)((val >> 8) & 0xFF);
        
        // Befehl 0x33: Set Slope / Resistance
        uint8_t msg[] = {0xA4, 0x09, 0x4F, 0x00, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, ev++, 
                         l_byte, h_byte, 0x00};
        send_raw(msg, sizeof(msg));
    }

    // --- DATEN EMPFANGEN ---
    // Diese Funktion wird in der Endlosschleife aufgerufen
    void update(SharedData* data) {
        if (!handle) return;
        uint8_t buf[128]; int received = 0;
        
        // Timeout 10ms (damit wir nicht blockieren)
        libusb_bulk_transfer(handle, 0x81, buf, sizeof(buf), &received, 10);
        
        if (received > 12) {
             // Suche nach gültigen Paketen im Datenstrom
             for (int i=0; i<received-12; i++) {
                // A4 ist immer der Start eines ANT+ Pakets
                if (buf[i]==0xA4 && (buf[i+2]==0x4E || buf[i+2]==0x4F)) {
                    data->connected = true; // Lebenszeichen!
                    
                    uint8_t* p = &buf[i+4]; // Zeiger auf die Nutzdaten
                    
                    // Page 0x10: Geschwindigkeit
                    if (p[0]==0x10) {
                        // Zeit zwischen Radumdrehungen -> km/h
                        data->current_speed = (p[4] | (p[5]<<8)) * 0.0036f;
                    }
                    
                    // Page 0x19: Leistung (Watt) & Kadenz
                    if (p[0]==0x19) {
                        // Watt Wert zusammensetzen
                        data->current_watt = (float)(p[5] | ((p[6] & 0x0F)<<8));
                        // Kadenz ist direkt in Byte 2, wenn nicht ungültig (0xFF)
                        if(p[2]!=0xFF) data->current_cadence = p[2];
                    }
                }
             }
        }
    }
};

// ==================================================================================
// HAUPT-THREAD FUNKTION
// Das hier läuft parallel zur GUI im Hintergrund
// ==================================================================================
void run_trainer_thread(SharedData* data) {
    unsigned short my_id = load_trainer_id_from_file();
    
    // --- SCHRITT 1: SCAN ---
    // Falls keine ID bekannt ist, suchen wir den Trainer.
    if (my_id == 0) {
        std::cout << "[SCAN] Starte Scan... Treten!" << std::endl;
        while (data->app_running && my_id == 0) {
            ActiveScanner scanner;
            if ((my_id = scanner.perform_scan_step()) > 0) {
                save_trainer_id(my_id); // Gefunden! Speichern.
            } else {
                // Kurz warten, um USB Bus nicht zu fluten
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
    }
    // Falls App beendet wurde (EXIT gedrückt), Thread stoppen
    if (!data->app_running) return;

    // --- SCHRITT 2: VERBINDEN ---
    VortexDriver driver; 
    
    std::cout << "[TRAINER] Verbinde ID " << my_id << "..." << std::endl;
    // Versuche solange zu verbinden, bis es klappt (z.B. wenn Stick kurz raus war)
    while(data->app_running) {
        if (driver.init(my_id)) break;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    std::cout << "[TRAINER] Bereit!" << std::endl;

    // --- SCHRITT 3: LOOP (Arbeitsschleife) ---
    std::ofstream csv_file;
    float session_time = 0.0f;

    while (data->app_running) {
        // 1. Daten lesen (Empfang)
        driver.update(data);
        
        // 2. Daten senden (Steuerung) - ca. 4x pro Sekunde (alle 250ms)
        // Wir nutzen einen Timer, weil USB senden teuer ist.
        static int timer = 0;
        if (++timer > 25) { 
            int mode = data->control_mode;
            
            // Befehl senden (mit Kalibrierung)
            if (mode == 1) driver.set_erg_watt(data->target_watt, data->scale_erg);
            else if (mode == 2) driver.set_slope(data->target_slope, data->scale_grade);
            
            // 3. Logging (CSV schreiben)
            bool should_record = data->record_log;
            
            // Datei öffnen beim Start der Aufnahme
            if (should_record && !csv_file.is_open()) {
                auto t = std::time(nullptr);
                auto tm = *std::localtime(&t);
                std::ostringstream oss;
                // Dateiname: Training_2026-02-04_18-30-00.csv
                oss << "Training_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".csv";
                csv_file.open(oss.str());
                if (csv_file.is_open()) {
                    csv_file << "Time_s,Watt,Speed_kmh,Cadence_rpm,Target,Mode\n";
                    session_time = 0.0f;
                    std::cout << "[LOG] Start: " << oss.str() << std::endl;
                }
            }
            // Datei schließen beim Stoppen
            if (!should_record && csv_file.is_open()) {
                csv_file.close();
                std::cout << "[LOG] Stopped." << std::endl;
            }
            // Zeile schreiben
            if (csv_file.is_open()) {
                csv_file << std::fixed << std::setprecision(1) << session_time << ","
                         << data->current_watt << ","
                         << data->current_speed << ","
                         << data->current_cadence << ","
                         << (mode==1 ? (float)data->target_watt : (float)data->target_slope) << ","
                         << (mode==1 ? "ERG" : "GRADE") << "\n";
            }
            session_time += 0.25f;
            timer = 0;
        }
        
        // CPU Schlafen legen (10ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    if (csv_file.is_open()) csv_file.close();
}
