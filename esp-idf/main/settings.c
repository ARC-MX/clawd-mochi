#include "settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char* TAG = "settings";

#define NVS_NS       "device"
#define DEFAULT_NAME "ClaWD-Mochi"
#define DEFAULT_PASS "clawd1234"

// Both buffers are one longer than their limits: the strings here are always
// NUL-terminated, unlike the lengths the WiFi driver is handed.
static char s_name[DEV_NAME_MAX + 1];
static char s_pass[DEV_PASS_MAX + 1];

// Printable ASCII only, and no quote or backslash: the name and password ride in
// the /state JSON and in a URL query string, and refusing these two characters is
// far cheaper than escaping them in two places. A device name is not free-form
// text — the on-panel font is ASCII 5x7 and could not draw anything else anyway.
static bool shellSafe(const char* s) {
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7e || *p == '"' || *p == '\\') return false;
  }
  return true;
}

bool settingsNameValid(const char* name) {
  const size_t n = name ? strlen(name) : 0;
  return n >= 1 && n <= DEV_NAME_MAX && shellSafe(name);
}

bool settingsPassValid(const char* pass) {
  const size_t n = pass ? strlen(pass) : 0;
  return n >= DEV_PASS_MIN && n <= DEV_PASS_MAX && shellSafe(pass);
}

static void useDefaults(void) {
  memcpy(s_name, DEFAULT_NAME, sizeof(DEFAULT_NAME));
  memcpy(s_pass, DEFAULT_PASS, sizeof(DEFAULT_PASS));
}

void settingsLoad(void) {
  useDefaults();

  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
    ESP_LOGI(TAG, "no stored identity; using the defaults");
    return;
  }

  // Two calls: the first asks for the length, so nothing has to be allocated and
  // an oversized stored value is rejected rather than truncated.
  size_t len = sizeof(s_name);
  if (nvs_get_str(h, "name", s_name, &len) != ESP_OK || !settingsNameValid(s_name)) {
    memcpy(s_name, DEFAULT_NAME, sizeof(DEFAULT_NAME));
  }
  len = sizeof(s_pass);
  if (nvs_get_str(h, "pass", s_pass, &len) != ESP_OK || !settingsPassValid(s_pass)) {
    // A password that WPA2 would reject must not reach esp_wifi_set_config: that
    // call is ESP_ERROR_CHECKed, so a bad stored value would abort the boot and
    // reboot on every attempt, with no way back in to fix it.
    memcpy(s_pass, DEFAULT_PASS, sizeof(DEFAULT_PASS));
  }
  nvs_close(h);

  ESP_LOGI(TAG, "device name '%s', password %u chars", s_name,
           (unsigned)strlen(s_pass));
}

const char* settingsName(void) { return s_name; }
const char* settingsPass(void) { return s_pass; }

static bool store(const char* name, const char* pass, const char** err) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
    if (err) *err = "could not open storage";
    return false;
  }
  esp_err_t e = nvs_set_str(h, "name", name);
  if (e == ESP_OK) e = nvs_set_str(h, "pass", pass);
  if (e == ESP_OK) e = nvs_commit(h);
  nvs_close(h);
  if (e != ESP_OK) {
    if (err) *err = "could not write storage";
    return false;
  }

  strncpy(s_name, name, sizeof(s_name) - 1);
  s_name[sizeof(s_name) - 1] = 0;
  strncpy(s_pass, pass, sizeof(s_pass) - 1);
  s_pass[sizeof(s_pass) - 1] = 0;
  // Deliberately does not log the password.
  ESP_LOGI(TAG, "stored name '%s'", s_name);
  return true;
}

bool settingsGetBg(uint16_t* colour) {
  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
  uint16_t v = 0;
  const bool found = (nvs_get_u16(h, "bg", &v) == ESP_OK);
  nvs_close(h);
  if (found && colour) *colour = v;
  return found;
}

bool settingsSetBg(uint16_t colour) {
  uint16_t stored = 0;
  if (settingsGetBg(&stored) && stored == colour) return true;   // nothing to do

  nvs_handle_t h;
  if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t e = nvs_set_u16(h, "bg", colour);
  if (e == ESP_OK) e = nvs_commit(h);
  nvs_close(h);
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "could not store the background");
    return false;
  }
  ESP_LOGI(TAG, "stored background 0x%04x", (unsigned)colour);
  return true;
}

bool settingsSave(const char* name, const char* pass, const char** err) {
  const char* n = (name && name[0]) ? name : s_name;
  const char* p = (pass && pass[0]) ? pass : s_pass;

  if (!settingsNameValid(n)) {
    if (err) *err = "name: 1-22 characters, no quotes";
    return false;
  }
  if (!settingsPassValid(p)) {
    if (err) *err = "password: 8-63 characters, no quotes";
    return false;
  }
  return store(n, p, err);
}

bool settingsReset(const char** err) {
  char name[sizeof(s_name)];
  char pass[sizeof(s_pass)];
  memcpy(name, DEFAULT_NAME, sizeof(DEFAULT_NAME));
  memcpy(pass, DEFAULT_PASS, sizeof(DEFAULT_PASS));
  return store(name, pass, err);
}
