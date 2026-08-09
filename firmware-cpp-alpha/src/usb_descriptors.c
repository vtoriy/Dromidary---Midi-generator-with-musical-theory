// USB device descriptors for the MIDI class (required by TinyUSB).
#include "tusb.h"

/* Device descriptor. Class 0 + IAD-less MIDI: the MIDI descriptor macro
   provides the interface association descriptor internally. */
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x1209,
    .idProduct          = 0x0001,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

#define EPNUM_MIDI_OUT 0x01
#define EPNUM_MIDI_IN  0x81

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)

uint8_t const desc_configuration[CONFIG_TOTAL_LEN] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_SELF_POWERED, 100),
    TUD_MIDI_DESCRIPTOR(0, 0, EPNUM_MIDI_OUT, EPNUM_MIDI_IN, 64),
};

static char const* string_descrs[] = {
    (char const[]){0x09, 0x04}, /* 0: Language IDs */
    "dromidary",               /* 1: Manufacturer */
    "dromidary MIDI",          /* 2: Product */
    "000000",                  /* 3: Serial */
};

uint8_t const* tud_descriptor_device_cb(void) {
    return (uint8_t const*)&desc_device;
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index; /* only one configuration */
    return desc_configuration;
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t /*langid*/) {
    static uint16_t desc_str[32];
    unsigned int chr_count;

    if (index == 0) {
        desc_str[0] = 0x0409; /* language ID */
        chr_count = 1;
    } else {
        if (index >= sizeof(string_descrs) / sizeof(string_descrs[0])) {
            return NULL;
        }
        const char* content = string_descrs[index];
        chr_count = 0;
        while (content[chr_count] != '\0' && chr_count < 31) {
            desc_str[1 + chr_count] = (uint16_t)(uint8_t)content[chr_count];
            ++chr_count;
        }
    }

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return desc_str;
}