// BLE peripheral exposing one writable GATT characteristic for commands.
// See CLAUDE-CODE-BRIDGE.md §方案 D.
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Size of one queued command line, including the NUL terminator. The queue
// passed to bleCliInit must have items of exactly this size.
#define BLE_CLI_LINE_MAX 128

// Starts a BLE peripheral advertising as "clawd-mochi" with a single writable
// characteristic. Each write is trimmed and posted as one NUL-terminated
// command line onto cmdQueue — the same queue the USB-serial reader feeds.
void bleCliInit(QueueHandle_t cmdQueue);

#ifdef __cplusplus
}
#endif
