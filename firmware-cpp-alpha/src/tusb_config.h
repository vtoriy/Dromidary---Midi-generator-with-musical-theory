#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUD_MAX_SPEED         OPT_MODE_FULL_SPEED

#define CFG_TUSB_MEM_ALIGN        TU_ATTR_ALIGNED(4)
#define CFG_TUSB_MEM_SECTION      __attribute__((section("usb_ram")))

#define CFG_TUD_CDC               0
#define CFG_TUD_MSC               0
#define CFG_TUD_HID               0
#define CFG_TUD_MIDI              1
#define CFG_TUD_VENDOR            0

#define CFG_TUD_EP_IN_BUFSIZE     64
#define CFG_TUD_EP_OUT_BUFSIZE    64
#define CFG_TUD_MIDI_RX_BUFSIZE   64
#define CFG_TUD_MIDI_TX_BUFSIZE   64

#endif
