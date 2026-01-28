#include <libusb-1.0/libusb.h>
#include <cstdio>
#include <cstdint>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <iostream>

// ==========================================
// V_SCAN - THE TRUTH SEEKER
// Ziel: Zeigt ROHE ID-Pakete an.
// ==========================================

static volatile bool running = true;
void sigint(int) { running = false; }

class IdScanner {
private:
    libusb_context* usb = nullptr;
    libusb_device_handle* handle = nullptr;

    uint8_t checksum(uint8_t* msg, int len) {
        uint8_t c = 0;
        for (int i = 0; i < len - 1; i++) c ^= msg[i];
        return c;
    }

    void send_raw(uint8_t* msg, int len) {
        if(!handle) return;
        msg[len - 1] = checksum(msg, len);
        int t = 0;
        libusb_bulk_transfer(handle, 0x01, msg, len, &t, 100);
    }

public:
    IdScanner() {}
    ~IdScanner() { 
        if(handle) {
            libusb_release_interface(handle, 0);
            libusb_close(handle);
        }
        if(usb) libusb_exit(usb);
    }

    bool init() {
        libusb_init(&usb);
        handle = libusb_open_device_with_vid_pid(usb, 0x0FCF, 0x1008);
        if (!handle) handle = libusb_open_device_with_vid_pid(usb, 0x0FCF, 0x1009);
        if (!handle) return false;

        if (libusb_kernel_driver_active(handle, 0) == 1) libusb_detach_kernel_driver(handle, 0);
        libusb_set_configuration(handle, 1);
        libusb_claim_interface(handle, 0);
        
        // Reset
        uint8_t m1[] = {0xA4, 0x01, 0x4A, 0x00, 0x00}; send_raw(m1, sizeof(m1)); usleep(500000);
        // Key
        uint8_t m2[] = {0xA4, 0x09, 0x46, 0x00, 0xB9, 0xA5, 0x21, 0xFB, 0xBD, 0x72, 0xC3, 0x45, 0x00}; send_raw(m2, sizeof(m2)); usleep(100000);
        // Assign
        uint8_t m3[] = {0xA4, 0x03, 0x42, 0x00, 0x00, 0x00, 0x00}; send_raw(m3, sizeof(m3)); usleep(100000);
        // ID (WILDCARD 0, 0, 0) -> Suche ALLES, auch Type 11, Type 17, egal!
        uint8_t m4[] = {0xA4, 0x05, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; send_raw(m4, sizeof(m4)); usleep(100000);
        // Freq
        uint8_t m5[] = {0xA4, 0x02, 0x45, 0x00, 57, 0x00}; send_raw(m5, sizeof(m5)); usleep(100000);
        // Open
        uint8_t m6[] = {0xA4, 0x01, 0x4B, 0x00, 0x00}; send_raw(m6, sizeof(m6)); usleep(100000);
        
        return true;
    }

    void request_id() {
        // MSG: Request (0x4D), Channel 0, MsgID (0x51 Channel ID)
        uint8_t msg[] = {0xA4, 0x02, 0x4D, 0x00, 0x51, 0x00};
        send_raw(msg, sizeof(msg));
    }

    void loop() {
        uint8_t buf[1024]; int received = 0;
        time_t last_req = 0;

        printf("SCANNER LÄUFT... BITTE TRETEN!\n");
        printf("(Drücke Ctrl+C wenn du IDs siehst)\n\n");

        while(running) {
            // Alle 1 Sekunde ID abfragen
            time_t now = time(NULL);
            if(now > last_req) {
                request_id(); 
                last_req = now;
            }

            int r = libusb_bulk_transfer(handle, 0x81, buf, sizeof(buf), &received, 10);
            if(received > 0) parse(buf, received);
        }
    }

    void parse(uint8_t* b, int len) {
        for (int i = 0; i < len - 5; i++) {
            if (b[i] == 0xA4) {
                uint8_t msg_len = b[i+1];
                uint8_t msg_id = b[i+2];
                uint8_t* p = &b[i+4]; // Payload

                // 0x51 = CHANNEL ID REPORT (Das ist der Jackpot!)
                if (msg_id == 0x51) {
                    uint16_t dev_id = p[0] | (p[1] << 8);
                    uint8_t dev_type = p[2];
                    uint8_t trans_type = p[3];
                    
                    printf(">>> ID GEFUNDEN: %u  (Type: %d)\n", dev_id, dev_type);
                    
                    if (dev_type == 17) {
                        printf("\033[1;32m    !!! DAS IST ER! TACX TRAINER ID: %u !!!\033[0m\n", dev_id);
                    }
                }
                
                // 0x4E/4F = Daten
                else if (msg_id == 0x4E || msg_id == 0x4F) {
                   // Nur zur Info, dass was kommt
                   // printf("."); 
                   // fflush(stdout);
                }
            }
        }
    }
};

int main() {
    signal(SIGINT, sigint);
    IdScanner scan;
    if(!scan.init()) { printf("USB Fehler\n"); return 1; }
    scan.loop();
    return 0;
}
