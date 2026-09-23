// The one command queue, shared by every transport that can drive the pet.
//
// The source travels *with* the line rather than in a side variable: the USB
// reader and the NimBLE host task post concurrently, and the worker can dequeue
// a line long after another producer would have overwritten a "last source"
// global. Attribution has to be atomic with the line.
#pragma once

#include <stdint.h>

// Longest command line accepted from any transport, terminator included.
#define CMD_LINE_MAX 128

typedef enum {
  CMD_SRC_SERIAL = 0,   // USB-Serial-JTAG (/dev/ttyACM0 on the host)
  CMD_SRC_BLE    = 1,   // NimBLE GATT write
  CMD_SRC_HTTP   = 2,   // esp_http_server route
} cmd_src_t;

// "Nothing has driven the pet yet" — for the link indicator, never a queue tag.
#define CMD_SRC_NONE 0xFF

typedef struct {
  uint8_t src;                  // cmd_src_t, or CMD_SRC_NONE
  char    line[CMD_LINE_MAX];
} cmd_item_t;

static inline const char* cmdSrcName(uint8_t src) {
  switch (src) {
    case CMD_SRC_SERIAL: return "usb";
    case CMD_SRC_BLE:    return "ble";
    case CMD_SRC_HTTP:   return "http";
    default:             return "none";
  }
}
