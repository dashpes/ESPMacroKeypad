// BLE HID for SpaceDeck — keyboard (report 1) + consumer control (report 2)
// Uses the Bluedroid BLE stack that ships with arduino-esp32 2.x; no extra library.
#include "hid.h"
#include "config.h"

#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>

HidOutput hid;

namespace {

constexpr uint8_t KEYBOARD_ID = 1;
constexpr uint8_t MEDIA_ID    = 2;

const uint8_t REPORT_MAP[] = {
    // ---- Keyboard ----
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, KEYBOARD_ID, //   Report ID
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0, 0x29, 0xE7,  // Usage Min/Max (modifiers)
    0x15, 0x00, 0x25, 0x01,  // Logical 0..1
    0x75, 0x01, 0x95, 0x08,  // 8 x 1 bit
    0x81, 0x02,        //   Input (Data, Var, Abs) - modifier byte
    0x95, 0x01, 0x75, 0x08,
    0x81, 0x01,        //   Input (Const) - reserved byte
    0x95, 0x05, 0x75, 0x01,
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01, 0x29, 0x05,
    0x91, 0x02,        //   Output (Data, Var, Abs) - LED report
    0x95, 0x01, 0x75, 0x03,
    0x91, 0x01,        //   Output (Const) - LED padding
    0x95, 0x06, 0x75, 0x08,
    0x15, 0x00, 0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00, 0x29, 0x65,
    0x81, 0x00,        //   Input (Data, Array) - 6 key slots
    0xC0,              // End Collection

    // ---- Consumer control (media keys) ----
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, MEDIA_ID,    //   Report ID
    0x15, 0x00, 0x26, 0xFF, 0x03,  // Logical 0..0x3FF
    0x19, 0x00, 0x2A, 0xFF, 0x03,  // Usage 0..0x3FF
    0x75, 0x10, 0x95, 0x01,        // 1 x 16 bits
    0x81, 0x00,        //   Input (Data, Array, Abs)
    0xC0,              // End Collection
};

BLEServer* g_server = nullptr;
BLEHIDDevice* g_hid = nullptr;
BLECharacteristic* g_kbIn = nullptr;
BLECharacteristic* g_mediaIn = nullptr;

volatile bool g_connected = false;
volatile bool g_linkChanged = false;
volatile bool g_needAdvertise = false;
esp_bd_addr_t g_peer = {0};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*, esp_ble_gatts_cb_param_t* param) override {
    memcpy(g_peer, param->connect.remote_bda, sizeof(esp_bd_addr_t));
    g_connected = true;
    g_linkChanged = true;
  }
  void onDisconnect(BLEServer*) override {
    g_connected = false;
    g_linkChanged = true;
    g_needAdvertise = true;  // restart from loop(), not from the BLE task
  }
};

// Pairing: "Just Works" (no PIN), bonded. If encryption fails - usually because
// one side forgot the pairing and the other didn't - drop our stale bond so the
// Mac's next attempt starts fresh instead of spinning forever.
volatile bool g_authEvent = false;
volatile bool g_authOk = false;
volatile uint8_t g_authReason = 0;

class SecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void onPassKeyNotify(uint32_t) override {}
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t) override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t r) override {
    g_authOk = r.success;
    g_authReason = r.fail_reason;
    if (!r.success) esp_ble_remove_bond_device(r.bd_addr);
    g_authEvent = true;
  }
};

// ASCII -> HID usage (+ whether Shift is needed), US layout
bool asciiToHid(char c, uint8_t& code, bool& shift) {
  shift = false;
  if (c >= 'a' && c <= 'z') { code = 0x04 + (c - 'a'); return true; }
  if (c >= 'A' && c <= 'Z') { code = 0x04 + (c - 'A'); shift = true; return true; }
  if (c >= '1' && c <= '9') { code = 0x1E + (c - '1'); return true; }
  switch (c) {
    case '0':  code = 0x27; return true;
    case '\n': code = 0x28; return true;  // Enter
    case 0x1B: code = 0x29; return true;  // Esc
    case '\b': code = 0x2A; return true;  // Backspace
    case '\t': code = 0x2B; return true;
    case ' ':  code = 0x2C; return true;
    case '-':  code = 0x2D; return true;
    case '=':  code = 0x2E; return true;
    case '[':  code = 0x2F; return true;
    case ']':  code = 0x30; return true;
    case '\\': code = 0x31; return true;
    case ';':  code = 0x33; return true;
    case '\'': code = 0x34; return true;
    case '`':  code = 0x35; return true;
    case ',':  code = 0x36; return true;
    case '.':  code = 0x37; return true;
    case '/':  code = 0x38; return true;
  }
  shift = true;
  switch (c) {
    case '!': code = 0x1E; return true;
    case '@': code = 0x1F; return true;
    case '#': code = 0x20; return true;
    case '$': code = 0x21; return true;
    case '%': code = 0x22; return true;
    case '^': code = 0x23; return true;
    case '&': code = 0x24; return true;
    case '*': code = 0x25; return true;
    case '(': code = 0x26; return true;
    case ')': code = 0x27; return true;
    case '_': code = 0x2D; return true;
    case '+': code = 0x2E; return true;
    case '{': code = 0x2F; return true;
    case '}': code = 0x30; return true;
    case '|': code = 0x31; return true;
    case ':': code = 0x33; return true;
    case '"': code = 0x34; return true;
    case '~': code = 0x35; return true;
    case '<': code = 0x36; return true;
    case '>': code = 0x37; return true;
    case '?': code = 0x38; return true;
  }
  return false;
}

uint16_t mediaUsage(Media m) {
  switch (m) {
    case Media::PlayPause: return 0x00CD;
    case Media::Next:      return 0x00B5;
    case Media::Prev:      return 0x00B6;
    case Media::VolUp:     return 0x00E9;
    case Media::VolDown:   return 0x00EA;
    case Media::Mute:      return 0x00E2;
  }
  return 0;
}

void sendKeyboard(uint8_t mods, uint8_t code) {
  uint8_t r[8] = {mods, 0, code, 0, 0, 0, 0, 0};
  g_kbIn->setValue(r, sizeof r);
  g_kbIn->notify();
  delay(BLE_SEND_DELAY_MS);
}

void sendMedia(uint16_t usage) {
  uint8_t r[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  g_mediaIn->setValue(r, sizeof r);
  g_mediaIn->notify();
  delay(BLE_SEND_DELAY_MS);
}

void printAddr(const uint8_t* a) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

bool ready(const char* what) {
  if (g_connected) return true;
  Serial.printf("[hid] not connected - dropped %s\n", what);
  return false;
}

}  // namespace

void HidOutput::begin() {
  BLEDevice::init(BLE_DEVICE_NAME);
  g_server = BLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks());

  g_hid = new BLEHIDDevice(g_server);
  g_kbIn = g_hid->inputReport(KEYBOARD_ID);
  g_hid->outputReport(KEYBOARD_ID);  // caps-lock LED etc. (ignored)
  g_mediaIn = g_hid->inputReport(MEDIA_ID);

  g_hid->manufacturer()->setValue(BLE_MANUFACTURER);
  g_hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  g_hid->hidInfo(0x00, 0x01);

  BLEDevice::setSecurityCallbacks(new SecurityCallbacks());
  BLESecurity* security = new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);  // bond so the Mac reconnects automatically
  security->setCapability(ESP_IO_CAP_NONE);                 // no screen/keypad -> "Just Works"
  security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  g_hid->reportMap((uint8_t*)REPORT_MAP, sizeof REPORT_MAP);
  g_hid->startServices();
  g_hid->setBatteryLevel(100);  // USB powered

  BLEAdvertising* adv = g_server->getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(g_hid->hidService()->getUUID());
  adv->setScanResponse(false);
  adv->start();

  Serial.printf("[hid] advertising as \"%s\" - pair it from Mac Bluetooth settings\n", BLE_DEVICE_NAME);
  printInfo();
}

void HidOutput::printInfo() {
  Serial.print("[hid] this board's BT address: ");
  printAddr(BLEDevice::getAddress().getNative()[0]);
  Serial.println();
  int n = esp_ble_get_bond_device_num();
  Serial.printf("[hid] remembered hosts (bonds): %d\n", n);
  if (n > 0) {
    esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * n);
    if (list && esp_ble_get_bond_device_list(&n, list) == ESP_OK)
      for (int i = 0; i < n; i++) { Serial.print("        "); printAddr(list[i].bd_addr); Serial.println(); }
    free(list);
  }
  Serial.printf("[hid] link: %s\n", g_connected ? "connected" : "advertising");
}

void HidOutput::forgetAll() {
  int n = esp_ble_get_bond_device_num();
  if (n > 0) {
    esp_ble_bond_dev_t* list = (esp_ble_bond_dev_t*)malloc(sizeof(esp_ble_bond_dev_t) * n);
    if (list && esp_ble_get_bond_device_list(&n, list) == ESP_OK)
      for (int i = 0; i < n; i++) esp_ble_remove_bond_device(list[i].bd_addr);
    free(list);
  }
  Serial.printf("[hid] forgot %d host(s)\n", n);
  if (g_connected) g_server->disconnect(g_server->getConnId());  // triggers re-advertising
}

LinkEvent HidOutput::poll() {
  if (g_authEvent) {
    g_authEvent = false;
    if (g_authOk) Serial.println("[hid] paired / encrypted OK - keys will work now");
    else Serial.printf("[hid] pairing FAILED (reason 0x%02X) - cleared stale bond; forget SpaceDeck on the Mac and connect again\n", g_authReason);
  }
  if (g_needAdvertise) {
    g_needAdvertise = false;
    delay(200);  // let the stack finish tearing down the old link
    g_server->getAdvertising()->start();
    Serial.println("[hid] advertising again");
  }
  if (!g_linkChanged) return LinkEvent::None;
  g_linkChanged = false;
  if (g_connected) { Serial.print("[hid] connected to "); printAddr(g_peer); Serial.println(" - waiting for pairing/encryption..."); }
  else Serial.println("[hid] disconnected");
  return g_connected ? LinkEvent::Connected : LinkEvent::Disconnected;
}

bool HidOutput::connected() const { return g_connected; }

void HidOutput::tapKey(uint8_t mods, uint8_t key) {
  if (!ready("key")) return;
  uint8_t code;
  bool shift;
  if (!asciiToHid((char)key, code, shift)) {
    Serial.printf("[hid] no HID code for 0x%02X\n", key);
    return;
  }
  sendKeyboard(mods | (shift ? MOD_SHIFT : 0), code);
  sendKeyboard(0, 0);
}

void HidOutput::tapMedia(Media m) {
  if (!ready("media")) return;
  sendMedia(mediaUsage(m));
  sendMedia(0);
}

void HidOutput::typeText(const char* text) {
  if (!ready("text")) return;
  for (const char* p = text; *p; p++) {
    uint8_t code;
    bool shift;
    if (!asciiToHid(*p, code, shift)) continue;
    sendKeyboard(shift ? MOD_SHIFT : 0, code);
    sendKeyboard(0, 0);
  }
}
