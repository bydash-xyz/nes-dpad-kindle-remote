/**
 * Amazon Kindle Compatible BLE Remote (ESP32 / ESP32-C3)
 * 
 * Reverse-engineered for Kindle Scribe, Kindle Colorsoft, and modern Kindles (FW 5.20.1+)
 * Emulates official Amazon Kindle Remote:
 *   - Vendor ID:  0x0171 (Amazon.com, Inc.)
 *   - Product ID: 0x0443 (Amazon Kindle Remote)
 *   - Category:   page_turner
 * 
 * Hardware: Designed for Seeed Studio XIAO ESP32-C3 fitting inside a 
 *           50x50x20mm Nintendo D-Pad capsule toy enclosure.
 * 
 * Pinout (XIAO ESP32-C3):
 *   - D0 (GPIO2): D-Pad Right -> Page Forward (KEY_PAGEDOWN / 0x4E)
 *   - D1 (GPIO3): D-Pad Left  -> Page Backward (KEY_PAGEUP / 0x4B)
 *   - D2 (GPIO4): D-Pad Up    -> Brightness Up (Consumer 0x006F)
 *   - D3 (GPIO5): D-Pad Down  -> Brightness Down (Consumer 0x0070)
 *   - Hold UP+DOWN (2s)       -> Kindle Sleep (Consumer Power 0x0030)
 * 
 * Dependencies:
 *   - NimBLE-Arduino by h2zero (Install via Arduino Library Manager or PlatformIO)
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_sleep.h>

// ============================================================================
// CONFIGURATION & PIN DEFINITIONS
// ============================================================================
#define PIN_BTN_RIGHT   2   // D0 on XIAO ESP32-C3 -> Page Forward
#define PIN_BTN_LEFT    3   // D1 on XIAO ESP32-C3 -> Page Back
#define PIN_BTN_UP      4   // D2 on XIAO ESP32-C3 -> Brightness Up
#define PIN_BTN_DOWN    5   // D3 on XIAO ESP32-C3 -> Brightness Down
#define PIN_LED         10  // Built-in User LED (optional status indicator)

// Deep Sleep timeout: 5 minutes of inactivity (300,000 ms)
#define SLEEP_TIMEOUT_MS  300000ULL

// Long-press threshold for sleep trigger
#define LONG_PRESS_SLEEP_MS 1500

// ============================================================================
// HID REPORT DEFINITIONS & KEYCODES
// ============================================================================
#define REPORT_ID_KEYBOARD  1
#define REPORT_ID_CONSUMER  2

// HID Keyboard Usage Codes (Usage Page 0x07)
#define HID_KEY_PAGE_DOWN   0x4E  // Linux KEY_PAGEDOWN (nav.page_forward)
#define HID_KEY_PAGE_UP     0x4B  // Linux KEY_PAGEUP   (nav.page_back)

// HID Consumer Control Usages (Usage Page 0x0C)
#define CONSUMER_BRIGHTNESS_UP    0x006F  // display.brightness_up
#define CONSUMER_BRIGHTNESS_DOWN  0x0070  // display.brightness_down
#define CONSUMER_POWER            0x0030  // system.sleep

// Dual HID Report Descriptor: Keyboard (Report ID 1) + Consumer Control (Report ID 2)
static const uint8_t hidReportDescriptor[] = {
  // --------------------------------------------------------
  // Keyboard Report (Report ID 1)
  // --------------------------------------------------------
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x85, REPORT_ID_KEYBOARD, //   Report ID (1)
  0x05, 0x07,        //   Usage Page (Key Codes)
  0x19, 0xE0,        //   Usage Minimum (224)
  0x29, 0xE7,        //   Usage Maximum (231)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data, Variable, Absolute) ; Modifier Byte
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x01,        //   Input (Constant) ; Reserved Byte
  0x95, 0x06,        //   Report Count (6)
  0x75, 0x08,        //   Report Size (8)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x65,        //   Logical Maximum (101)
  0x05, 0x07,        //   Usage Page (Key Codes)
  0x19, 0x00,        //   Usage Minimum (0)
  0x29, 0x65,        //   Usage Maximum (101)
  0x81, 0x00,        //   Input (Data, Array) ; 6 Key Array
  0xC0,              // End Collection

  // --------------------------------------------------------
  // Consumer Control Report (Report ID 2)
  // --------------------------------------------------------
  0x05, 0x0C,        // Usage Page (Consumer)
  0x09, 0x01,        // Usage (Consumer Control)
  0xA1, 0x01,        // Collection (Application)
  0x85, REPORT_ID_CONSUMER, //   Report ID (2)
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
  0x19, 0x00,        //   Usage Minimum (0)
  0x2A, 0xFF, 0x03,  //   Usage Maximum (1023)
  0x75, 0x10,        //   Report Size (16)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x00,        //   Input (Data, Array, Absolute)
  0xC0               // End Collection
};

// ============================================================================
// STATE VARIABLES
// ============================================================================
static NimBLEServer*     pServer         = nullptr;
static NimBLEHIDDevice*  pHidDev         = nullptr;
static NimBLECharacteristic* pInputKeyboard = nullptr;
static NimBLECharacteristic* pInputConsumer = nullptr;
static NimBLECharacteristic* pBatteryChar   = nullptr;

static bool isConnected   = false;
static uint32_t lastActivityMs = 0;
static uint8_t batteryLevelPercent = 100;

// ============================================================================
// BLE SERVER CALLBACKS
// ============================================================================
class BleServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
    isConnected = true;
    lastActivityMs = millis();
    digitalWrite(PIN_LED, LOW); // LED ON when connected (active low)
    Serial.println("[BLE] Kindle Connected!");
  }

  void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) override {
    isConnected = false;
    digitalWrite(PIN_LED, HIGH); // LED OFF
    Serial.println("[BLE] Kindle Disconnected. Restarting advertising...");
    NimBLEDevice::startAdvertising();
  }
};

// ============================================================================
// INPUT REPORT TRANSMISSION HELPERS
// ============================================================================
void sendKeyboardKey(uint8_t keycode) {
  if (!isConnected || !pInputKeyboard) return;

  uint8_t report[8] = {0};
  report[2] = keycode; // First key in key array
  pInputKeyboard->setValue(report, sizeof(report));
  pInputKeyboard->notify();

  delay(20); // Hold key for 20ms

  // Key Release
  memset(report, 0, sizeof(report));
  pInputKeyboard->setValue(report, sizeof(report));
  pInputKeyboard->notify();

  lastActivityMs = millis();
}

void sendConsumerKey(uint16_t consumerUsage) {
  if (!isConnected || !pInputConsumer) return;

  uint8_t report[2];
  report[0] = (uint8_t)(consumerUsage & 0xFF);
  report[1] = (uint8_t)((consumerUsage >> 8) & 0xFF);
  pInputConsumer->setValue(report, sizeof(report));
  pInputConsumer->notify();

  delay(25);

  // Key Release
  report[0] = 0;
  report[1] = 0;
  pInputConsumer->setValue(report, sizeof(report));
  pInputConsumer->notify();

  lastActivityMs = millis();
}

// ============================================================================
// DEEP SLEEP ENTRY
// ============================================================================
void enterDeepSleep() {
  Serial.println("[POWER] Entering Deep Sleep. Wake up on button press...");
  
  if (isConnected) {
    pServer->disconnect(0);
    delay(100);
  }

  // Configure GPIOs to wake the ESP32 when any button is pressed (pulled to LOW)
  uint64_t wakeupPinMask = (1ULL << PIN_BTN_RIGHT) | 
                           (1ULL << PIN_BTN_LEFT)  | 
                           (1ULL << PIN_BTN_UP)    | 
                           (1ULL << PIN_BTN_DOWN);

  esp_deep_sleep_enable_gpio_wakeup(wakeupPinMask, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

// ============================================================================
// SETUP & INITIALIZATION
// ============================================================================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  // Configure button pins with internal pull-up resistors
  pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT,  INPUT_PULLUP);
  pinMode(PIN_BTN_UP,    INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN,  INPUT_PULLUP);

  lastActivityMs = millis();

  Serial.println("==============================================");
  Serial.println("Amazon Kindle BLE Remote - Initializing...");
  Serial.println("VID: 0x0171 | PID: 0x0443 (Amazon Kindle Remote)");
  Serial.println("==============================================");

  // Initialize NimBLE with exact official device name
  NimBLEDevice::init("Amazon Kindle Remote");
  NimBLEDevice::setSecurityAuth(true, true, true); // Bonding + MITM + Secure Connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT); // Just Works Pairing

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new BleServerCallbacks());

  // --------------------------------------------------------------------------
  // 1. Device Information Service (0x180A) with Amazon VID / PID
  // --------------------------------------------------------------------------
  NimBLEService* pDis = pServer->createService((uint16_t)0x180A);

  // PnP ID characteristic (0x2A50):
  // Byte 0:    Vendor ID Source (0x02 = USB Implementers Forum)
  // Byte 1-2:  Vendor ID (0x0171 = Amazon.com, Inc.) -> little-endian [0x71, 0x01]
  // Byte 3-4:  Product ID (0x0443 = Amazon Kindle Remote) -> little-endian [0x43, 0x04]
  // Byte 5-6:  Product Version (0x0100 = v1.0.0) -> little-endian [0x00, 0x01]
  const uint8_t pnpIdData[] = {0x02, 0x71, 0x01, 0x43, 0x04, 0x00, 0x01};
  NimBLECharacteristic* pPnpChar = pDis->createCharacteristic((uint16_t)0x2A50, NIMBLE_PROPERTY::READ);
  pPnpChar->setValue(pnpIdData, sizeof(pnpIdData));

  // Firmware Revision String (0x2A26) - explicitly checked by Kindle acsbtfd
  NimBLECharacteristic* pFwChar = pDis->createCharacteristic((uint16_t)0x2A26, NIMBLE_PROPERTY::READ);
  pFwChar->setValue("1.0.0");

  // Manufacturer Name (0x2A29)
  NimBLECharacteristic* pMfgChar = pDis->createCharacteristic((uint16_t)0x2A29, NIMBLE_PROPERTY::READ);
  pMfgChar->setValue("Amazon");

  // Model Number String (0x2A24)
  NimBLECharacteristic* pModelChar = pDis->createCharacteristic((uint16_t)0x2A24, NIMBLE_PROPERTY::READ);
  pModelChar->setValue("Kindle Remote");

  pDis->start();

  // --------------------------------------------------------------------------
  // 2. Battery Service (0x180F)
  // --------------------------------------------------------------------------
  NimBLEService* pBas = pServer->createService((uint16_t)0x180F);
  pBatteryChar = pBas->createCharacteristic((uint16_t)0x2A19, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pBatteryChar->setValue(&batteryLevelPercent, 1);
  pBas->start();

  // --------------------------------------------------------------------------
  // 3. Human Interface Device (HID) Service (0x1812)
  // --------------------------------------------------------------------------
  pHidDev = new NimBLEHIDDevice(pServer);
  pInputKeyboard = pHidDev->inputReport(REPORT_ID_KEYBOARD);
  pInputConsumer = pHidDev->inputReport(REPORT_ID_CONSUMER);

  pHidDev->manufacturer()->setValue("Amazon");
  pHidDev->pnp(0x02, 0x0171, 0x0443, 0x0100);
  pHidDev->hidInfo(0x00, 0x02); // Country: 0x00, Flags: Normally Connectable (0x02)
  pHidDev->reportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));
  pHidDev->startServices();

  // --------------------------------------------------------------------------
  // 4. BLE Advertising Setup
  // --------------------------------------------------------------------------
  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->setAppearance(0x03C1); // Appearance: Keyboard (or 0x0180 Remote Control)
  pAdv->addServiceUUID(pHidDev->hidService()->getUUID());
  pAdv->addServiceUUID(pBas->getUUID());
  pAdv->addServiceUUID(pDis->getUUID());
  pAdv->setName("Amazon Kindle Remote");
  pAdv->start();

  Serial.println("[BLE] Advertising started. Ready to pair with Kindle!");
}

// ============================================================================
// MAIN LOOP & INPUT HANDLER
// ============================================================================
void loop() {
  // Read active-low buttons
  bool rightPressed = (digitalRead(PIN_BTN_RIGHT) == LOW);
  bool leftPressed  = (digitalRead(PIN_BTN_LEFT)  == LOW);
  bool upPressed    = (digitalRead(PIN_BTN_UP)    == LOW);
  bool downPressed  = (digitalRead(PIN_BTN_DOWN)  == LOW);

  // Check simultaneous Up+Down press or Left+Right press for Sleep
  if ((upPressed && downPressed) || (leftPressed && rightPressed)) {
    uint32_t pressStart = millis();
    while (((digitalRead(PIN_BTN_UP) == LOW && digitalRead(PIN_BTN_DOWN) == LOW) ||
            (digitalRead(PIN_BTN_LEFT) == LOW && digitalRead(PIN_BTN_RIGHT) == LOW)) &&
           (millis() - pressStart < LONG_PRESS_SLEEP_MS)) {
      delay(20);
    }
    if (millis() - pressStart >= LONG_PRESS_SLEEP_MS) {
      Serial.println("[BTN] Sleep Command Triggered!");
      sendConsumerKey(CONSUMER_POWER);
      // Wait for release
      while (digitalRead(PIN_BTN_UP) == LOW || digitalRead(PIN_BTN_DOWN) == LOW) delay(20);
      enterDeepSleep();
      return;
    }
  }

  // Handle individual directional presses
  if (rightPressed) {
    Serial.println("[BTN] Right -> Page Forward (KEY_PAGEDOWN)");
    sendKeyboardKey(HID_KEY_PAGE_DOWN);
    delay(200); // Debounce / throttle
  } 
  else if (leftPressed) {
    Serial.println("[BTN] Left -> Page Backward (KEY_PAGEUP)");
    sendKeyboardKey(HID_KEY_PAGE_UP);
    delay(200);
  } 
  else if (upPressed) {
    Serial.println("[BTN] Up -> Brightness Up");
    sendConsumerKey(CONSUMER_BRIGHTNESS_UP);
    delay(150);
  } 
  else if (downPressed) {
    Serial.println("[BTN] Down -> Brightness Down");
    sendConsumerKey(CONSUMER_BRIGHTNESS_DOWN);
    delay(150);
  }

  // Inactivity timeout -> Deep Sleep
  if (millis() - lastActivityMs > SLEEP_TIMEOUT_MS) {
    enterDeepSleep();
  }

  delay(15);
}
