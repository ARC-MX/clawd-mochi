// BLE peripheral exposing one writable GATT characteristic for commands.
// See CLAUDE-CODE-BRIDGE.md §方案 D.
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "cmd_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts a BLE peripheral advertising as "clawd-mochi" with a single writable
// characteristic. Each write is trimmed and posted as one cmd_item_t tagged
// CMD_SRC_BLE onto cmdQueue — the same queue the USB-serial reader feeds, whose
// items are cmd_item_t (see cmd_queue.h).
void bleCliInit(QueueHandle_t cmdQueue);

#ifdef __cplusplus
}
#endif
