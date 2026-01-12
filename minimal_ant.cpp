#include <libusb-1.0/libusb.h>
#include <cstdio>
#include <cstdint>
#include <unistd.h>
#include <signal.h>
#include <time.h>

/* ========= CONSTANT TARGET ========= */
#define TARGET_DEVNUM   25445
#define TARGET_DEVTYPE  0x11   // FE-C
#define TARGET_TRANSTYP 0x05

/* ========= COLORS ========= */
#define C_RESET "\033[0m"
#define C_STATE "\033[33m"
#define C_ERR   "\033[31m"

/* ========= GLOBAL ========= */
static volatile bool running = true;

/* ========= CONTEXT ========= */
struct AntContext {
    libusb_context* usb = nullptr;
    libusb_device_handle* handle = nullptr;
    uint8_t ep_in  = 0;
    uint8_t ep_out = 0;
    bool startup = false;
};

/* ========= SIGNAL ========= */
void sigint(int) { running = false; }

/* ========= CHECKSUM ========= */
uint8_t ant_checksum(const uint8_t* b, int len)
{
    uint8_t c = 0;
    for (int i = 0; i < len - 1; i++) c ^= b[i];
    return c;
}

/* ========= SEND ========= */
void ant_send(AntContext& c, uint8_t* m, int l)
{
    m[l - 1] = ant_checksum(m, l);
    int t = 0;
    libusb_bulk_transfer(c.handle, c.ep_out, m, l, &t, 1000);
}

/* ========= USB OPEN ========= */
bool ant_usb_open(AntContext& c)
{
    libusb_init(&c.usb);

    libusb_device** list;
    ssize_t cnt = libusb_get_device_list(c.usb, &list);

    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device_descriptor d;
        libusb_get_device_descriptor(list[i], &d);
        if (d.idVendor == 0x0FCF &&
            libusb_open(list[i], &c.handle) == 0)
            break;
    }
    libusb_free_device_list(list, 1);
    if (!c.handle) return false;

    if (libusb_kernel_driver_active(c.handle, 0))
        libusb_detach_kernel_driver(c.handle, 0);

    libusb_claim_interface(c.handle, 0);

    libusb_config_descriptor* cfg;
    libusb_get_active_config_descriptor(
        libusb_get_device(c.handle), &cfg);

    for (int i = 0; i < cfg->interface[0].altsetting[0].bNumEndpoints; i++) {
        auto& ep = cfg->interface[0].altsetting[0].endpoint[i];
        if ((ep.bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_BULK) {
            if (ep.bEndpointAddress & 0x80)
                c.ep_in = ep.bEndpointAddress;
            else
                c.ep_out = ep.bEndpointAddress;
        }
    }
    libusb_free_config_descriptor(cfg);

    printf(C_STATE "[STATE] USB ready IN=0x%02X OUT=0x%02X\n" C_RESET,
           c.ep_in, c.ep_out);
    return true;
}

/* ========= RX (NON BLOCKING) ========= */
void ant_rx(AntContext& c)
{
    uint8_t buf[64];
    int len = 0;

    if (libusb_bulk_transfer(
            c.handle,
            c.ep_in,
            buf,
            sizeof(buf),
            &len,
            20   // <<< KURZER TIMEOUT !!!
        ) == 0)
    {
        if (buf[0] == 0xA4 && buf[2] == 0x6F) {
            if (!c.startup) {
                c.startup = true;
                printf(C_STATE "[STATE] ANT STARTUP\n" C_RESET);
            }
        }
    }
}

/* ========= FE-C REQUEST CONTROL ========= */
void fec_request_control(AntContext& c)
{
    uint8_t msg[] = {
        0xA4,0x09,0x4F,0x01,
        0x46,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0
    };
    ant_send(c, msg, sizeof(msg));
}

/* ========= FE-C BASIC RESISTANCE ========= */
void fec_basic_resistance(AntContext& c, uint8_t resistance)
{
    uint8_t msg[] = {
        0xA4,0x09,0x4F,0x01,
        0x30,
        0xFF,
        resistance,
        0xFF,0xFF,0xFF,0xFF,0xFF,
        0
    };
    ant_send(c, msg, sizeof(msg));
}

/* ========= FE-C TARGET POWER ========= */
void fec_target_power(AntContext& c, uint16_t watts)
{
    uint8_t msg[] = {
        0xA4,0x09,0x4F,0x01,
        0x31,0xFF,
        (uint8_t)(watts & 0xFF),
        (uint8_t)(watts >> 8),
        0x00,
        0xFF,0xFF,0xFF,
        0
    };
    ant_send(c, msg, sizeof(msg));
}

/* ========= MAIN ========= */
int main()
{
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGINT, sigint);

    AntContext c{};

    if (!ant_usb_open(c)) return 1;

    /* RESET */
    uint8_t reset[] = {0xA4,0x01,0x4A,0x00,0};
    ant_send(c, reset, sizeof(reset));

    while (running && !c.startup)
        ant_rx(c);

    /* NETWORK KEY */
    uint8_t key[] = {
        0xA4,0x09,0x46,0x00,
        0xB9,0xA5,0x21,0xFB,
        0xBD,0x72,0xC3,0x45,
        0
    };
    ant_send(c, key, sizeof(key));

    /* RX FE CHANNEL (wildcard) */
    uint8_t rx_assign[] = {0xA4,0x03,0x42,0x00,0x00,0x00,0};
    uint8_t rx_chid[]   = {0xA4,0x05,0x51,0x00,0x00,0x00,0x0B,0x00,0};
    uint8_t rx_per[]    = {0xA4,0x03,0x43,0x00,0x00,0x20,0};
    uint8_t rx_rf[]     = {0xA4,0x02,0x45,0x00,57,0};
    uint8_t rx_open[]   = {0xA4,0x01,0x4B,0x00,0};

    ant_send(c, rx_assign, sizeof(rx_assign));
    ant_send(c, rx_chid,   sizeof(rx_chid));
    ant_send(c, rx_per,    sizeof(rx_per));
    ant_send(c, rx_rf,     sizeof(rx_rf));
    ant_send(c, rx_open,   sizeof(rx_open));

    /* TX FE-C CONTROL CHANNEL – BLIND */
    uint8_t tx_assign[] = {0xA4,0x03,0x42,0x01,0x10,0x00,0};
    uint8_t tx_chid[] = {
        0xA4,0x05,0x51,0x01,
        (uint8_t)(TARGET_DEVNUM & 0xFF),
        (uint8_t)(TARGET_DEVNUM >> 8),
        TARGET_DEVTYPE,
        TARGET_TRANSTYP,
        0
    };
    uint8_t tx_open[] = {0xA4,0x01,0x4B,0x01,0};

    ant_send(c, tx_assign, sizeof(tx_assign));
    ant_send(c, tx_chid,   sizeof(tx_chid));
    ant_send(c, tx_open,   sizeof(tx_open));

    printf(C_STATE "[STATE] Blind FE-C control channel opened (dev=%u)\n"
           C_RESET, TARGET_DEVNUM);

    fec_request_control(c);
    printf(C_STATE "[STATE] FE-C Request Control sent\n" C_RESET);
    sleep(1);

    bool basic_sent = false;
    time_t last = 0;

    while (running) {

        ant_rx(c);

        if (!basic_sent) {
            fec_basic_resistance(c, 200);   // deutlich spürbar
            printf(C_STATE "[STATE] FE-C Basic Resistance sent\n" C_RESET);
            basic_sent = true;
            sleep(1);
            continue;
        }

        time_t now = time(NULL);
        if (now != last) {
            fec_target_power(c, 250);   // TEST: deutlich schwer
            printf(C_STATE "[TX] Target Power 250W\n" C_RESET);
            last = now;
        }
    }

    return 0;
}
