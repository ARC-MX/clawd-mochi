// Minimal NimBLE peripheral: one service, one writable characteristic.
//
// Writes are handed to the shared command queue rather than executed here, so
// the NimBLE host task is never blocked by a slow screen animation.

#include "ble_cli.h"

#include <string.h>

#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_cli";

// 16-bit UUIDs, so the host side can use the short form:
//   service        0000abf0-0000-1000-8000-00805f9b34fb
//   characteristic 0000abf1-0000-1000-8000-00805f9b34fb
static const ble_uuid16_t s_svc_uuid = BLE_UUID16_INIT(0xABF0);
static const ble_uuid16_t s_chr_uuid = BLE_UUID16_INIT(0xABF1);

static QueueHandle_t s_cmdQueue;
static uint8_t       s_ownAddrType;
static const char    s_devName[] = "clawd-mochi";
// Written only by the NimBLE host task — the GAP callback runs there, and that
// is where NimBLE serialises connection state. A count rather than a flag:
// CONFIG_BT_NIMBLE_MAX_CONNECTIONS is 3, and a second central disconnecting must
// not clear the first one's.
static uint8_t       s_connCount = 0;

uint8_t bleCliConnCount(void) { return s_connCount; }

static int chrAccess(uint16_t connHandle, uint16_t attrHandle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  cmd_item_t item = {};
  item.src = CMD_SRC_BLE;
  uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  uint16_t n = len < sizeof(item.line) - 1 ? len : (uint16_t)(sizeof(item.line) - 1);
  if (os_mbuf_copydata(ctxt->om, 0, n, item.line) != 0) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  item.line[n] = 0;

  // Trim trailing CR/LF/space; a write may or may not carry a terminator.
  while (n > 0 && (item.line[n - 1] == '\r' || item.line[n - 1] == '\n' ||
                   item.line[n - 1] == ' ')) {
    item.line[--n] = 0;
  }
  if (n == 0) return 0;

  // The tag rides with the line: the worker can run it long after another
  // producer would have overwritten any "last source" side variable.
  ESP_LOGD(TAG, "write: %s", item.line);
  if (xQueueSend(s_cmdQueue, &item, 0) != pdTRUE) {
    ESP_LOGW(TAG, "command queue full, dropped: %s", item.line);
  }
  return 0;
}

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &s_chr_uuid.u,
                .access_cb = chrAccess,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {0}   // terminator
        },
    },
    {0}           // terminator
};

static void advertise(void);

static int gapEvent(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        if (s_connCount < 0xFF) s_connCount++;
        ESP_LOGI(TAG, "central connected (handle %d), %u up",
                 event->connect.conn_handle, s_connCount);
      } else {
        ESP_LOGW(TAG, "connect failed (%d); advertising again", event->connect.status);
        advertise();
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      if (s_connCount) s_connCount--;
      ESP_LOGI(TAG, "central disconnected (%d), %u up; advertising again",
               event->disconnect.reason, s_connCount);
      advertise();
      break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      advertise();
      break;

    default:
      break;
  }
  return 0;
}

static void advertise(void) {
  struct ble_hs_adv_fields fields = {0};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.name = (uint8_t *)s_devName;
  fields.name_len = strlen(s_devName);
  fields.name_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
    return;
  }

  struct ble_gap_adv_params params = {0};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  rc = ble_gap_adv_start(s_ownAddrType, NULL, BLE_HS_FOREVER, &params,
                         gapEvent, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "adv_start failed: %d", rc);
  } else {
    ESP_LOGI(TAG, "advertising as '%s'", s_devName);
  }
}

static void onSync(void) {
  int rc = ble_hs_util_ensure_addr(0);
  if (rc != 0) {
    ESP_LOGE(TAG, "ensure_addr failed: %d", rc);
    return;
  }
  rc = ble_hs_id_infer_auto(0, &s_ownAddrType);
  if (rc != 0) {
    ESP_LOGE(TAG, "id_infer_auto failed: %d", rc);
    return;
  }
  advertise();
}

static void onReset(int reason) {
  ESP_LOGW(TAG, "host reset, reason %d", reason);
}

static void hostTask(void *param) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void bleCliInit(QueueHandle_t cmdQueue) {
  s_cmdQueue = cmdQueue;

  ESP_ERROR_CHECK(nimble_port_init());

  ble_hs_cfg.sync_cb = onSync;
  ble_hs_cfg.reset_cb = onReset;

  ble_svc_gap_init();
  ble_svc_gatt_init();

  int rc = ble_gatts_count_cfg(s_svcs);
  ESP_ERROR_CHECK(rc == 0 ? ESP_OK : ESP_FAIL);
  rc = ble_gatts_add_svcs(s_svcs);
  ESP_ERROR_CHECK(rc == 0 ? ESP_OK : ESP_FAIL);

  ble_svc_gap_device_name_set(s_devName);

  nimble_port_freertos_init(hostTask);
}
