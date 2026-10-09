/**
 * Kindle page-turner protocol test harness (ESP32-S3, NimBLE-Arduino 1.4.x)
 *
 * Presents as "Amazon Kindle Remote" (VID 0x0171 / PID 0x0443) and lets you
 * fire every page-turner action from the serial monitor, so the Kindle side
 * can be validated without any buttons wired.
 *
 * Serial commands (115200 baud, no line ending needed):
 *   f  page forward      (keyboard 0x4E, KEY_PAGEDOWN)
 *   b  page back         (keyboard 0x4B, KEY_PAGEUP)
 *   u  brightness up     (consumer 0x006F)
 *   d  brightness down   (consumer 0x0070)
 *   s  sleep             (consumer 0x0030, KEY_POWER)
 *   0-9  battery level 0%..90%, B = 100% (notifies if subscribed)
 *   x  toggle PnP vendor-ID source 0x01 <-> 0x02 (unpair on the Kindle first)
 *   c  clear all bonds on the ESP32
 *   i  request a long connection interval (power test)
 *   ?  print status
 *
 * Optional buttons (active low, internal pull-up), same mapping as the final remote:
 *   GPIO4 Right -> page forward, GPIO5 Left -> page back,
 *   GPIO6 Up -> brightness up, GPIO7 Down -> tap: brightness down, hold 1.5 s: sleep
 *
 * Stays connected; no deep sleep (deep sleep drops the BLE link).
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#define DEVICE_NAME         "Amazon Kindle Remote"
#define PNP_VID_SOURCE_DEFAULT 0x01   // 0x01 = Bluetooth SIG (likely), 0x02 = USB-IF
#define PNP_VID             0x0171
#define PNP_PID             0x0443
#define PNP_VER             0x0100

#define PIN_BTN_RIGHT  4
#define PIN_BTN_LEFT   5
#define PIN_BTN_UP     6
#define PIN_BTN_DOWN   7

#define DEBOUNCE_MS        15
#define HOLD_SLEEP_MS      1500
#define HOLD_TEST_MS       2000   // U/D/F: 2 s hold = 1 press + ~5 repeats if the Kindle repeats

#define REPORT_ID_KEYBOARD 1
#define REPORT_ID_CONSUMER 2

#define HID_KEY_PAGE_DOWN  0x4E
#define HID_KEY_PAGE_UP    0x4B
#define CONSUMER_BRIGHTNESS_UP   0x006F
#define CONSUMER_BRIGHTNESS_DOWN 0x0070
#define CONSUMER_POWER           0x0030

static const uint8_t hidReportDescriptor[] = {
  // Keyboard, Report ID 1 (8 bytes: modifiers, reserved, 6 keys)
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
  0x85, REPORT_ID_KEYBOARD,
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
  0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
  0xC0,
  // Consumer control, Report ID 2 (one 16-bit usage)
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01,
  0x85, REPORT_ID_CONSUMER,
  0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
  0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
  0xC0
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static NimBLEServer*         pServer        = nullptr;
static NimBLEHIDDevice*      pHid           = nullptr;
static NimBLECharacteristic* pInKeyboard    = nullptr;
static NimBLECharacteristic* pInConsumer    = nullptr;
static NimBLECharacteristic* pPnpChar       = nullptr;

static bool     connected     = false;
static uint16_t connHandle    = 0;
static uint8_t  pnpSource     = PNP_VID_SOURCE_DEFAULT;
static uint8_t  batteryLevel  = 100;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void writePnp() {
  // Written manually, little-endian per the Bluetooth DIS spec, to avoid any
  // byte-order differences in the library's pnp() helper.
  const uint8_t v[7] = {
    pnpSource,
    (uint8_t)(PNP_VID & 0xFF), (uint8_t)(PNP_VID >> 8),
    (uint8_t)(PNP_PID & 0xFF), (uint8_t)(PNP_PID >> 8),
    (uint8_t)(PNP_VER & 0xFF), (uint8_t)(PNP_VER >> 8)
  };
  pPnpChar->setValue(v, sizeof(v));
  Serial.printf("[DIS] PnP ID = %02X %02X %02X %02X %02X %02X %02X (source 0x%02X)\n",
                v[0], v[1], v[2], v[3], v[4], v[5], v[6], pnpSource);
}

static void printStatus() {
  Serial.printf("[STATUS] connected=%d bonds=%d pnpSource=0x%02X battery=%u%%\n",
                connected, NimBLEDevice::getNumBonds(), pnpSource, batteryLevel);
  if (connected) {
    NimBLEConnInfo ci = pServer->getPeerIDInfo(connHandle);
    Serial.printf("[STATUS] interval=%.2f ms latency=%u timeout=%u ms\n",
                  ci.getConnInterval() * 1.25f, ci.getConnLatency(), ci.getConnTimeout() * 10);
  }
}

// NimBLE 1.4 has no conn-update callback; poll once a second and log changes.
static void watchConnParams() {
  static uint32_t last = 0;
  static uint16_t itvl = 0, lat = 0, to = 0;
  if (!connected) { itvl = lat = to = 0; return; }
  if (millis() - last < 1000) return;
  last = millis();
  NimBLEConnInfo ci = pServer->getPeerIDInfo(connHandle);
  if (ci.getConnInterval() == itvl && ci.getConnLatency() == lat && ci.getConnTimeout() == to) return;
  itvl = ci.getConnInterval(); lat = ci.getConnLatency(); to = ci.getConnTimeout();
  Serial.printf("[BLE] params now: interval=%.2f ms latency=%u timeout=%u ms (effective idle %.0f ms)\n",
                itvl * 1.25f, lat, to * 10, itvl * 1.25f * (lat + 1));
}

// Peripheral-requested parameter presets (units: 1.25 ms interval, 10 ms timeout).
static void requestParams(uint16_t minI, uint16_t maxI, uint16_t lat, uint16_t to) {
  if (!connected) { Serial.println("[BLE] not connected"); return; }
  pServer->updateConnParams(connHandle, minI, maxI, lat, to);
  Serial.printf("[BLE] requested interval %.2f-%.2f ms latency %u timeout %u ms\n",
                minI * 1.25f, maxI * 1.25f, lat, to * 10);
}

// Single report: key/usage != 0 is a press, 0 is the release.
static void keyboardReport(uint8_t key) {
  uint8_t r[8] = {0};
  r[2] = key;
  pInKeyboard->setValue(r, sizeof(r));
  pInKeyboard->notify();
}

static void consumerReport(uint16_t usage) {
  uint8_t r[2] = { (uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8) };
  pInConsumer->setValue(r, sizeof(r));
  pInConsumer->notify();
}

// Tap = press, 30 ms, release. holdMs > 0 keeps the key down that long to
// exercise the Kindle's kernel_repeat (500 ms delay, 333 ms period).
static void sendKeyboard(uint8_t key, const char* label, uint32_t holdMs = 30) {
  if (!connected) { Serial.println("[TX] not connected"); return; }
  keyboardReport(key);
  delay(holdMs);
  keyboardReport(0);
  Serial.printf("[TX] keyboard 0x%02X (%s) held %lu ms\n", key, label, (unsigned long)holdMs);
}

static void sendConsumer(uint16_t usage, const char* label, uint32_t holdMs = 30) {
  if (!connected) { Serial.println("[TX] not connected"); return; }
  consumerReport(usage);
  delay(holdMs);
  consumerReport(0);
  Serial.printf("[TX] consumer 0x%04X (%s) held %lu ms\n", usage, label, (unsigned long)holdMs);
}

// Reconnect-limit test: drop the link and stop advertising so the Kindle's
// reconnect cycles (acsbtfd "Reconnect:OnCycleTimer ... max cycles") run out.
static bool offline = false;
static uint32_t offlineSince = 0, onlineSince = 0;

static void goOffline() {
  offline = true;
  offlineSince = millis();
  NimBLEDevice::getAdvertising()->stop();
  if (connected) pServer->disconnect(connHandle);
  Serial.printf("[RC] offline at t=%lu s: link dropped, advertising stopped\n", offlineSince / 1000);
}

static void goOnline() {
  offline = false;
  onlineSince = millis();
  bool ok = NimBLEDevice::startAdvertising();
  Serial.printf("[RC] online at t=%lu s after %lu s offline, adv.start=%s\n",
                onlineSince / 1000, (onlineSince - offlineSince) / 1000, ok ? "ok" : "FAILED");
}

static void setBattery(uint8_t pct) {
  batteryLevel = pct;
  pHid->setBatteryLevel(pct);          // sets the value only (NimBLE 1.4)
  if (connected) pHid->batteryLevel()->notify();
  Serial.printf("[BAS] battery level -> %u%%\n", pct);
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* desc) override {
    connected  = true;
    connHandle = desc->conn_handle;
    Serial.printf("[BLE] connected: peer=%s interval=%.2f ms latency=%u timeout=%u ms\n",
                  NimBLEAddress(desc->peer_ota_addr).toString().c_str(),
                  desc->conn_itvl * 1.25f, desc->conn_latency,
                  desc->supervision_timeout * 10);
    if (offline) Serial.println("[RC] UNEXPECTED connect while offline");
    if (onlineSince) {
      Serial.printf("[RC] reconnected %lu ms after advertising resumed\n", millis() - onlineSince);
      onlineSince = 0;
    }
  }
  void onDisconnect(NimBLEServer* s, ble_gap_conn_desc* desc) override {
    connected = false;
    if (offline) { Serial.println("[BLE] disconnected (offline, not advertising)"); return; }
    Serial.println("[BLE] disconnected, advertising again");
    NimBLEDevice::startAdvertising();
  }
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    Serial.printf("[SEC] encrypted=%d bonded=%d authenticated=%d\n",
                  desc->sec_state.encrypted, desc->sec_state.bonded,
                  desc->sec_state.authenticated);
  }
};

class SubscribeLogger : public NimBLECharacteristicCallbacks {
 public:
  explicit SubscribeLogger(const char* n) : name(n) {}
  void onSubscribe(NimBLECharacteristic* c, ble_gap_conn_desc* desc, uint16_t subValue) override {
    Serial.printf("[GATT] host %s %s\n", subValue ? "subscribed to" : "unsubscribed from", name);
  }
  void onRead(NimBLECharacteristic* c, ble_gap_conn_desc* desc) override {
    Serial.printf("[GATT] host read %s\n", name);
  }
 private:
  const char* name;
};

// ---------------------------------------------------------------------------
// Optional buttons
// ---------------------------------------------------------------------------
struct Btn { uint8_t pin; bool pressed; uint32_t changedAt; };
static Btn btnRight{PIN_BTN_RIGHT, false, 0}, btnLeft{PIN_BTN_LEFT, false, 0},
           btnUp{PIN_BTN_UP, false, 0}, btnDown{PIN_BTN_DOWN, false, 0};
static bool downHoldFired = false;

// Returns +1 on press edge, -1 on release edge, 0 otherwise (debounced).
static int readBtn(Btn& b) {
  bool raw = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  if (raw != b.pressed && now - b.changedAt > DEBOUNCE_MS) {
    b.pressed = raw;
    b.changedAt = now;
    return raw ? 1 : -1;
  }
  return 0;
}

static void pollButtons() {
  if (readBtn(btnRight) == 1) sendKeyboard(HID_KEY_PAGE_DOWN, "page forward");
  if (readBtn(btnLeft)  == 1) sendKeyboard(HID_KEY_PAGE_UP,   "page back");
  // Up: press/release follow the button so holding it ramps brightness
  int u = readBtn(btnUp);
  if (connected && u == 1)  { consumerReport(CONSUMER_BRIGHTNESS_UP); Serial.println("[TX] brightness up pressed"); }
  if (connected && u == -1) { consumerReport(0); Serial.println("[TX] brightness up released"); }

  int e = readBtn(btnDown);
  if (e == 1) downHoldFired = false;
  if (btnDown.pressed && !downHoldFired && millis() - btnDown.changedAt >= HOLD_SLEEP_MS) {
    downHoldFired = true;
    sendConsumer(CONSUMER_POWER, "sleep (hold Down)");
  }
  if (e == -1 && !downHoldFired) sendConsumer(CONSUMER_BRIGHTNESS_DOWN, "brightness down");
}

// ---------------------------------------------------------------------------
// Serial commands
// ---------------------------------------------------------------------------
static void handleSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    switch (ch) {
      case 'f': sendKeyboard(HID_KEY_PAGE_DOWN, "page forward"); break;
      case 'b': sendKeyboard(HID_KEY_PAGE_UP,   "page back"); break;
      case 'u': sendConsumer(CONSUMER_BRIGHTNESS_UP,   "brightness up"); break;
      case 'd': sendConsumer(CONSUMER_BRIGHTNESS_DOWN, "brightness down"); break;
      case 's': sendConsumer(CONSUMER_POWER, "sleep"); break;
      case 'U': sendConsumer(CONSUMER_BRIGHTNESS_UP,   "brightness up",   HOLD_TEST_MS); break;
      case 'D': sendConsumer(CONSUMER_BRIGHTNESS_DOWN, "brightness down", HOLD_TEST_MS); break;
      case 'F': sendKeyboard(HID_KEY_PAGE_DOWN, "page forward", HOLD_TEST_MS); break;
      case 'o': goOffline(); break;
      case 'a': goOnline(); break;
      case 'B': setBattery(100); break;
      case 'x':
        pnpSource = (pnpSource == 0x01) ? 0x02 : 0x01;
        writePnp();
        Serial.println("[DIS] unpair on the Kindle and press 'c' here before re-pairing");
        break;
      case 'c':
        if (connected) pServer->disconnect(connHandle);
        NimBLEDevice::deleteAllBonds();
        Serial.println("[SEC] all bonds cleared");
        break;
      case 'i': requestParams(24, 40, 30, 600); break;   // 30-50 ms, latency 30, 6 s
      case 'j': requestParams(12, 24, 30, 400); break;   // 15-30 ms, latency 30, 4 s
      case 'k': requestParams(80, 100, 4, 600); break;   // 100-125 ms, latency 4, 6 s
      case 'l': requestParams(6, 12, 0, 400); break;     // 7.5-15 ms, latency 0 (fast baseline)
      case '?': printStatus(); break;
      default:
        if (ch >= '0' && ch <= '9') setBattery((ch - '0') * 10);
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Kindle page-turner protocol test (ESP32-S3) ===");

  for (uint8_t p : {PIN_BTN_RIGHT, PIN_BTN_LEFT, PIN_BTN_UP, PIN_BTN_DOWN}) pinMode(p, INPUT_PULLUP);

  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  // Bonding + Secure Connections, Just Works (no MITM: the remote has no display/keypad)
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  pServer->advertiseOnDisconnect(false);  // onDisconnect restarts it unless offline

  // NimBLEHIDDevice creates the HID (0x1812), Device Information (0x180A) and
  // Battery (0x180F) services itself. Add characteristics to those instead of
  // creating duplicate services.
  pHid = new NimBLEHIDDevice(pServer);

  NimBLEService* dis = pHid->deviceInfo();
  pHid->manufacturer()->setValue(std::string("Amazon"));
  pHid->pnp(PNP_VID_SOURCE_DEFAULT, PNP_VID, PNP_PID, PNP_VER);
  pPnpChar = dis->getCharacteristic((uint16_t)0x2A50);
  writePnp();
  pPnpChar->setCallbacks(new SubscribeLogger("PnP ID"));
  NimBLECharacteristic* fw = dis->createCharacteristic((uint16_t)0x2A26, NIMBLE_PROPERTY::READ);
  fw->setValue(std::string("1.0.0"));
  fw->setCallbacks(new SubscribeLogger("firmware revision"));
  dis->createCharacteristic((uint16_t)0x2A24, NIMBLE_PROPERTY::READ)->setValue(std::string("Kindle Remote"));

  pHid->hidInfo(0x00, 0x02);
  pHid->reportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));
  pInKeyboard = pHid->inputReport(REPORT_ID_KEYBOARD);
  pInConsumer = pHid->inputReport(REPORT_ID_CONSUMER);
  pInKeyboard->setCallbacks(new SubscribeLogger("keyboard report"));
  pInConsumer->setCallbacks(new SubscribeLogger("consumer report"));

  pHid->batteryLevel()->setCallbacks(new SubscribeLogger("battery level"));
  pHid->startServices();
  pHid->setBatteryLevel(batteryLevel);

  // Advertising. The Kindle (acsbtfd BtDiscoveryFilter::Classify) only matches a
  // page turner by VID/PID taken from manufacturer data; its UUID table has only
  // A2DP 0x110B. libbluetooth.so parses AD 0xFF as company LE16 + PID BE16 and
  // checks btif_in_is_amazon_ble_remote(0x0171, 0x0443).
  // Primary: flags (3) + mfr 71 01 04 43 (6) + HID UUID (4) + appearance (4) = 17.
  // Scan response: full name (22).
  static const uint8_t kMfr[] = {0x71, 0x01, (uint8_t)(PNP_PID >> 8), (uint8_t)(PNP_PID & 0xFF)};
  NimBLEAdvertisementData adv;
  adv.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  adv.setManufacturerData(std::string((const char*)kMfr, sizeof(kMfr)));
  adv.setCompleteServices(NimBLEUUID((uint16_t)0x1812));
  adv.setAppearance(0x03C1);  // HID keyboard
  NimBLEAdvertisementData scanResp;
  scanResp.setName(DEVICE_NAME);

  NimBLEAdvertising* a = NimBLEDevice::getAdvertising();
  a->setAdvertisementData(adv);
  a->setScanResponseData(scanResp);
  bool advOk = a->start();
  Serial.printf("[BLE] addr=%s adv.start=%s isAdvertising=%d\n",
                NimBLEDevice::getAddress().toString().c_str(), advOk ? "ok" : "FAILED",
                a->isAdvertising());

  Serial.println("[BLE] advertising as \"" DEVICE_NAME "\"");
  Serial.println("Commands: f b u d s | U D F hold 2 s | o offline a online | 0-9 B battery | x toggle PnP source | c clear bonds | i/j/k/l conn param presets | ? status");
  printStatus();
}

void loop() {
  handleSerial();
  pollButtons();
  watchConnParams();
  delay(5);
}
