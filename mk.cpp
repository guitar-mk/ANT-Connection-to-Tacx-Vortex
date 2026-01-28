#include <libusb-1.0/libusb.h>
#include <cstdio>
#include <cstdint>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <termios.h>
#include <fcntl.h>
#include <cstdlib> 
#include <cstring>
#include <iostream>
#include <string>
#include <fstream>

// ==========================================
// VORTEX MASTER V42 - COMPLETE PACKAGE
// Feature: Menu 3 "SCAN" integriert v_scan Logik
// Feature: Auto-Save ID
// Feature: Golden Lock Verbindung (Stabil)
// ==========================================

static volatile bool running = true;
void sigint(int) { running = false; }

// --- Helpers ---
int kbhit(void) {
    struct termios oldt, newt;
    int ch, oldf;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    if(ch != EOF) { ungetc(ch, stdin); return 1; }
    return 0;
}

// --- FILE MANAGER ---
uint16_t load_trainer_id() {
    std::ifstream file("my_trainer.txt");
    if (file.is_open()) {
        uint16_t id;
        file >> id;
        file.close();
        return id;
    }
    return 0;
}

void save_trainer_id(uint16_t id) {
    std::ofstream file("my_trainer.txt");
    if (file.is_open()) {
        file << id;
        file.close();
    }
}

// --- SCANNER CLASS (Die Logik von v_scan) ---
class TrainerScanner {
private:
    libusb_context* usb = nullptr;
    libusb_device_handle* handle = nullptr;

    uint8_t checksum(uint8_t* msg, int len) {
        uint8_t c = 0; for (int i = 0; i < len - 1; i++) c ^= msg[i]; return c;
    }
    void send_raw(uint8_t* msg, int len) {
        if(!handle) return;
        msg[len - 1] = checksum(msg, len);
        int t = 0; libusb_bulk_transfer(handle, 0x01, msg, len, &t, 100);
    }

public:
    TrainerScanner() {}
    ~TrainerScanner() { close(); }

    void close() {
        if(handle) { libusb_release_interface(handle, 0); libusb_close(handle); handle=nullptr;}
        if(usb) { libusb_exit(usb); usb=nullptr; }
    }

    // Gibt die gefundene ID zurück, oder 0 wenn Abbruch
    uint16_t scan() {
        if (libusb_init(&usb) < 0) return 0;
        handle = libusb_open_device_with_vid_pid(usb, 0x0FCF, 0x1008);
        if (!handle) handle = libusb_open_device_with_vid_pid(usb, 0x0FCF, 0x1009);
        if (!handle) return 0;

        if (libusb_kernel_driver_active(handle, 0) == 1) libusb_detach_kernel_driver(handle, 0);
        libusb_set_configuration(handle, 1);
        libusb_claim_interface(handle, 0);
        libusb_reset_device(handle);
        usleep(200000);

        // Wildcard Setup (Wie in v_scan)
        uint8_t m1[] = {0xA4, 0x01, 0x4A, 0x00, 0x00}; send_raw(m1, sizeof(m1)); usleep(300000);
        uint8_t m2[] = {0xA4, 0x09, 0x46, 0x00, 0xB9, 0xA5, 0x21, 0xFB, 0xBD, 0x72, 0xC3, 0x45, 0x00}; send_raw(m2, sizeof(m2)); usleep(100000);
        uint8_t m3[] = {0xA4, 0x03, 0x42, 0x00, 0x00, 0x00, 0x00}; send_raw(m3, sizeof(m3)); usleep(100000);
        // ID 0,0,0 (Wildcard)
        uint8_t m4[] = {0xA4, 0x05, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; send_raw(m4, sizeof(m4)); usleep(100000);
        uint8_t m5[] = {0xA4, 0x02, 0x45, 0x00, 57, 0x00}; send_raw(m5, sizeof(m5)); usleep(100000);
        uint8_t m6[] = {0xA4, 0x01, 0x4B, 0x00, 0x00}; send_raw(m6, sizeof(m6)); usleep(200000);

        printf("\nSUCHE LÄUFT... BITTE TRETEN!\n");
        printf("[Drücke 'q' zum Abbrechen]\n\n");

        uint8_t buf[1024]; int received = 0;
        time_t last_req = 0;

        while(running) {
            if(kbhit()) { if(getchar() == 'q') return 0; }

            // Alle 1s aktiv fragen (Das hat in v_scan geholfen!)
            time_t now = time(NULL);
            if(now > last_req) {
                 uint8_t req[] = {0xA4, 0x02, 0x4D, 0x00, 0x51, 0x00}; // Request Channel ID
                 send_raw(req, sizeof(req));
                 last_req = now;
                 printf("."); fflush(stdout);
            }

            libusb_bulk_transfer(handle, 0x81, buf, sizeof(buf), &received, 10);
            if(received > 0) {
                for (int i = 0; i < received - 5; i++) {
                    if (buf[i] == 0xA4) {
                        uint8_t msg_id = buf[i+2];
                        // 0x51 Channel ID Report (Antwort auf Request oder Auto-Discovery)
                        if (msg_id == 0x51) {
                            uint16_t found_id = buf[i+4] | (buf[i+5] << 8);
                            if (found_id > 0) {
                                printf("\n\n>>> TREFFER! ID: %u <<<\n", found_id);
                                usleep(1000000);
                                return found_id;
                            }
                        }
                    }
                }
            }
        }
        return 0;
    }
};

// --- DRIVER CLASS (Der stabile V40 Code) ---
class VortexTrainer {
private:
    libusb_context* usb = nullptr;
    libusb_device_handle* handle = nullptr;
    uint16_t current_power = 0;
    uint8_t current_cadence = 0;
    float current_speed = 0.0f;
    bool connected = false;
    uint16_t trainer_id = 0;
    int pps = 0; int last_pps = 0; time_t last_pps_time = 0;

    uint8_t checksum(uint8_t* msg, int len) {
        uint8_t c = 0; for (int i = 0; i < len - 1; i++) c ^= msg[i]; return c;
    }
    void send_raw(uint8_t* msg, int len) {
        if(!handle) return;
        msg[len - 1] = checksum(msg, len);
        int t = 0; libusb_bulk_transfer(handle, 0x01, msg, len, &t, 50); 
    }

public:
    VortexTrainer() {}
    ~VortexTrainer() { close(); }

    bool init(uint16_t target_id) {
        if (libusb_init(&usb) < 0) return false;
        handle = libusb_open_device_with_vid_pid(usb, 0x0FCF, 0x1008);
        if (!handle) handle = libusb_open_device_with_vid_pid(usb, 0x0FCF, 0x1009);
        if (!handle) return false;
        if (libusb_kernel_driver_active(handle, 0) == 1) libusb_detach_kernel_driver(handle, 0);
        libusb_set_configuration(handle, 1);
        libusb_claim_interface(handle, 0);
        libusb_reset_device(handle);
        usleep(200000);

        trainer_id = target_id;
        // DIRECT CONNECT SETUP (V40 Logic)
        uint8_t m1[] = {0xA4, 0x01, 0x4A, 0x00, 0x00}; send_raw(m1, sizeof(m1)); usleep(300000); 
        uint8_t m2[] = {0xA4, 0x09, 0x46, 0x00, 0xB9, 0xA5, 0x21, 0xFB, 0xBD, 0x72, 0xC3, 0x45, 0x00}; send_raw(m2, sizeof(m2)); usleep(100000);
        uint8_t m3[] = {0xA4, 0x03, 0x42, 0x00, 0x00, 0x00, 0x00}; send_raw(m3, sizeof(m3)); usleep(100000);
        
        // Forced ID & Type 17
        uint8_t id_lo = (uint8_t)(target_id & 0xFF);
        uint8_t id_hi = (uint8_t)((target_id >> 8) & 0xFF);
        uint8_t m4[] = {0xA4, 0x05, 0x51, 0x00, id_lo, id_hi, 17, 0x00, 0x00}; send_raw(m4, sizeof(m4)); usleep(100000);
        
        uint8_t m5[] = {0xA4, 0x02, 0x45, 0x00, 57, 0x00}; send_raw(m5, sizeof(m5)); usleep(100000);
        uint8_t m6[] = {0xA4, 0x03, 0x43, 0x00, 0x00, 0x20, 0x00}; send_raw(m6, sizeof(m6)); usleep(100000);
        uint8_t m7[] = {0xA4, 0x01, 0x4B, 0x00, 0x00}; send_raw(m7, sizeof(m7)); usleep(200000);
        
        return true;
    }

    void set_erg_watt(uint16_t watts) {
        static uint8_t ev = 0;
        uint16_t w4 = watts * 4;
        uint8_t msg[] = {0xA4, 0x09, 0x4F, 0x00, 0x31, 0xFF, 0xFF, 0xFF, 0xFF, ev++, 
                         (uint8_t)(w4 & 0xFF), (uint8_t)((w4 >> 8) & 0x0F), 0x00};
        send_raw(msg, sizeof(msg));
    }

    void set_slope(float slope_percent) {
        static uint8_t ev = 0;
        uint16_t slope_val = (uint16_t)((slope_percent + 200.0f) * 100.0f);
        uint8_t msg[] = {0xA4, 0x09, 0x4F, 0x00, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, 
                         (uint8_t)(slope_val & 0xFF), (uint8_t)((slope_val >> 8) & 0xFF), 
                         0xFF, 0x00};
        send_raw(msg, sizeof(msg));
    }

    void update_flush() {
        if (!handle) return;
        uint8_t buf[1024]; int received = 0; int loops = 0;
        do {
            int r = libusb_bulk_transfer(handle, 0x81, buf, sizeof(buf), &received, 1);
            if (received > 0) {
                parse(buf, received);
                pps++;
            }
            loops++;
        } while (received > 0 && loops < 10); 
        time_t now = time(NULL);
        if (now != last_pps_time) { last_pps = pps; pps = 0; last_pps_time = now; }
    }

    void parse(uint8_t* b, int len) {
        for (int i = 0; i < len - 12; i++) {
            if (b[i] == 0xA4 && checksum(&b[i], b[i+1]+4) == b[i+b[i+1]+3]) {
                uint8_t msg_id = b[i+2];
                uint8_t* p = &b[i+4]; 
                if (msg_id == 0x4E || msg_id == 0x4F) {
                    connected = true;
                    if (p[0] == 0x10) current_speed = (p[4] | (p[5] << 8)) * 0.0036f;
                    if (p[0] == 0x19) {
                        current_power = (p[5] | ((p[6] & 0x0F) << 8));
                        if (p[2] != 0xFF) current_cadence = p[2];
                    }
                }
            }
        }
    }

    void close() {
        if (handle) {
            set_erg_watt(0); usleep(200000);
            libusb_release_interface(handle, 0); libusb_close(handle); handle = nullptr;
        }
        if (usb) { libusb_exit(usb); usb = nullptr; }
    }
    
    uint16_t getPower() { return current_power; }
    uint8_t getCadence() { return current_cadence; }
    float getSpeed() { return current_speed; }
    bool isConnected() { return connected; }
    int getPPS() { return last_pps; } 
};

// --- MAIN ---
int main() {
    signal(SIGINT, sigint);
    
    while(running) {
        printf("\033[?25h"); 
        uint16_t my_id = load_trainer_id();
        
        system("clear");
        printf("==========================================\n");
        printf("   VORTEX V42 - COMPLETE\n");
        if (my_id > 0) printf("   Trainer: %u\n", my_id);
        else           printf("   Trainer: NICHT GEKOPPELT\n");
        printf("==========================================\n\n");
        
        printf(" [1] ERG Modus\n");
        printf(" [2] GRADE Modus\n");
        printf(" [3] NEU KOPPELN (Scan)\n");
        printf(" [q] Beenden\n");
        printf("\n Wahl: ");
        
        char choice_c = 0;
        std::cin >> choice_c;

        if (choice_c == 'q') break;

        // --- SCANNER MODUS ---
        if (choice_c == '3') {
            TrainerScanner scanner;
            uint16_t new_id = scanner.scan();
            if (new_id > 0) {
                save_trainer_id(new_id);
                printf("\nErfolgreich gespeichert! Starte neu...\n");
                sleep(2);
                continue; // Zurück zum Hauptmenü (Loop)
            } else {
                printf("\nAbbruch oder nichts gefunden.\n");
                sleep(2);
                continue;
            }
        }

        // --- FAHR MODUS (1 oder 2) ---
        int mode = 0;
        if (choice_c == '1') mode = 1;
        if (choice_c == '2') mode = 2;

        if (mode == 0) continue; // Ungültig

        if (my_id == 0) {
            printf("\nFEHLER: Bitte erst koppeln (Menü 3)!\n");
            sleep(2);
            continue;
        }

        VortexTrainer trainer;
        printf("\nVerbinde mit ID %u...\n", my_id);
        if (!trainer.init(my_id)) {
            printf("USB Fehler. Retry...\n");
            sleep(1);
            continue;
        }

        std::string filename = (mode == 1) ? "v42_erg.csv" : "v42_grade.csv";
        FILE* log = fopen(filename.c_str(), "w");
        if(log) fprintf(log, "Zeit;Ist_Watt;RPM;Speed;Distanz;Ziel;Modus\n");

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t start_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        uint64_t last_tx = 0;
        
        uint16_t target_watt = 100;    
        float target_slope = 0.0f;     
        double total_km = 0;
        double session_time = 0;
        char hb[] = {'|', '/', '-', '\\'}; int hb_idx = 0;

        printf("\033[?25l"); 
        system("clear");

        bool session_running = true;
        while (session_running && running) {
            if (kbhit()) {
                char c = getchar();
                if (c == 'q') session_running = false; // Nur Session beenden, nicht Programm
                
                if (mode == 1) { 
                    if (c == '+') target_watt += 10;
                    if (c == '-' && target_watt > 20) target_watt -= 10;
                } 
                else if (mode == 2) { 
                    if (c == '+') target_slope += 0.5f;
                    if (c == '-') target_slope -= 0.5f;
                }
            }

            trainer.update_flush(); 
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            session_time = (double)(now - start_ms) / 1000.0;

            if (now - last_tx > 250) {
                if (mode == 1) trainer.set_erg_watt(target_watt);
                else trainer.set_slope(target_slope);
                
                if (trainer.getPower() > 0) total_km += (trainer.getSpeed() / 3600.0) * 0.25;
                
                if (log && trainer.getPower() > 0) {
                    fprintf(log, "%.2f;%u;%u;%.1f;%.3f;%.1f;%d\n", 
                        session_time, trainer.getPower(), trainer.getCadence(), trainer.getSpeed(), 
                        total_km, (mode==1 ? (float)target_watt : target_slope), mode);
                }
                last_tx = now;
                hb_idx = (hb_idx + 1) % 4;
            }

            printf("\033[H"); 
            printf("\033[1;34m==========================================\033[0m\n");
            printf(" VORTEX V42 (PPS: %d) %c  \n", trainer.getPPS(), hb[hb_idx]);
            printf("\033[1;34m==========================================\033[0m\n\n");
            
            if (trainer.isConnected()) printf(" STATUS:  \033[1;32mLOCKED TO %u\033[0m\n", my_id);
            else                       printf(" STATUS:  \033[1;31mCONNECTING...\033[0m\n");

            printf("\n");
            printf("  ZEIT:     \033[1;37m%6.2f s\033[0m\n", session_time);
            printf("  DISTANZ:  \033[1;37m%6.3f km\033[0m\n", total_km);
            
            printf("\n");
            if (mode == 1) printf("  ZIEL:     \033[1;32m%3u Watt\033[0m   ( +/- )\n", target_watt);
            else           printf("  ZIEL:     \033[1;32m%3.1f %%\033[0m      ( +/- )\n", target_slope);
            
            printf("  IST:      \033[1;33m%3u Watt\033[0m\n", trainer.getPower());
            printf("  CADENCE:  \033[1;36m%3u rpm\033[0m\n", trainer.getCadence());
            
            printf("\n\033[1;34m------------------------------------------\033[0m\n");
            printf(" [q] Zurück zum Menü\n");
            
            usleep(10000); 
        }

        if(log) fclose(log);
        trainer.close();
        // Loop restartet und zeigt Menü wieder
    }

    printf("\033[?25h"); 
    return 0;
}
