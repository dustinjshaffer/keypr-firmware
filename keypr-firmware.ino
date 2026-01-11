/*
 * Keypr Firmware
 * ESP32-C3-DevKitC-02 v1.1
 *
 * Components:
 * - BLE server for phone communication
 * - WiFi client for API communication
 * - 1.54" E-ink display (Waveshare V2)
 * - SG90 Servo for latch control
 * - Reed switch for lid detection
 * - Button for manual unlock
 *
 * Libraries required:
 * - NimBLE-Arduino
 * - GxEPD2
 * - Adafruit GFX Library
 * - ESP32Servo
 * - ArduinoJson
 *
 * Board: ESP32C3 Dev Module
 */

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <ESP32Servo.h>
#include <Update.h>
#include <esp_ota_ops.h>

// ============================================
// PIN DEFINITIONS (ESP32-C3-DevKitC-02 v1.1)
// ============================================

// E-Ink Display (SPI) - Using native FSPI pins
#define EINK_MOSI   7   // GPIO7  (J1 pin 9) - DIN on display
#define EINK_CLK    6   // GPIO6  (J1 pin 8) - CLK on display
#define EINK_CS     10  // GPIO10 (J3 pin 7) - CS on display
#define EINK_DC     3   // GPIO3  (J3 pin 5) - DC on display
#define EINK_RST    4   // GPIO4  (J1 pin 6) - RST on display
#define EINK_BUSY   5   // GPIO5  (J1 pin 7) - BUSY on display

// Servo Motor
#define SERVO_PIN   8   // GPIO8 (J1 pin 11) - also has onboard RGB LED

// Reed Switch (Lid Detection)
#define REED_SWITCH 1   // GPIO1 (J3 pin 3) - moved from GPIO9 to avoid boot strapping conflict

// Unlock Button
#define UNLOCK_BTN  2   // GPIO2 (J3 pin 4) - INPUT_PULLUP

// ============================================
// SERVO POSITIONS
// ============================================
#define SERVO_LOCKED    90   // Hook engaged
#define SERVO_UNLOCKED  0    // Hook released

// ============================================
// QR CODE BITMAP for "https://getkeypr.com/download-app"
// 32x32 pixels, White = 1, Black = 0
// ============================================
#define QR_SIZE 32
const unsigned char qrcodeBitmap[] PROGMEM = {
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x74, 0x72, 0x07,
  0xef, 0x56, 0x56, 0xf7, 0xe9, 0x7c, 0x66, 0x97, 0xe9, 0x6f, 0xaa, 0x97, 0xef, 0x49, 0xd6, 0xf7,
  0xe0, 0x55, 0xaa, 0x07, 0xff, 0xe3, 0xc3, 0xff, 0xe1, 0x0b, 0x59, 0x6f, 0xf9, 0xd4, 0x71, 0x2f,
  0xe4, 0x36, 0x17, 0x5f, 0xff, 0xbc, 0xb0, 0x37, 0xeb, 0x65, 0x58, 0x3f, 0xf2, 0xef, 0x76, 0x2f,
  0xe6, 0xe3, 0x80, 0x27, 0xfd, 0x2b, 0x1f, 0x7f, 0xe5, 0xd4, 0x63, 0x27, 0xef, 0x26, 0x57, 0xcf,
  0xec, 0x9c, 0x86, 0x27, 0xe9, 0x5d, 0x38, 0x0f, 0xff, 0xdf, 0x2b, 0xaf, 0xe0, 0x41, 0xd2, 0x9f,
  0xef, 0x63, 0xcb, 0x87, 0xe9, 0x5b, 0x58, 0x07, 0xe9, 0x56, 0x57, 0x4f, 0xef, 0x44, 0xaf, 0xd7,
  0xe0, 0x45, 0x2e, 0x9f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

// ============================================
// LOCK ICON BITMAPS (48x48, Bootstrap bi-lock-fill / bi-unlock-fill)
// ============================================
const unsigned char lockBitmap48[] PROGMEM = {
  0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xfc, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
  0x00, 0x00, 0x00, 0x01, 0xfc, 0x3f, 0x80, 0x00, 0x00, 0x03, 0xe0, 0x07, 0xc0, 0x00, 0x00, 0x03,
  0xc0, 0x03, 0xc0, 0x00, 0x00, 0x07, 0x80, 0x01, 0xe0, 0x00, 0x00, 0x07, 0x00, 0x00, 0xe0, 0x00,
  0x00, 0x0f, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x0f, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x0e, 0x00, 0x00,
  0x70, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x70, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x70, 0x00, 0x00, 0x0e,
  0x00, 0x00, 0x70, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x70, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x70, 0x00,
  0x00, 0x0e, 0x00, 0x00, 0x70, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x70, 0x00, 0x00, 0x3f, 0xff, 0xff,
  0xfc, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xfe, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0xff,
  0xff, 0xff, 0xff, 0x80, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0,
  0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff,
  0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff,
  0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0,
  0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff,
  0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff,
  0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0,
  0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff,
  0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x01, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00, 0xff,
  0xff, 0xff, 0xff, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x3f, 0xff, 0xff, 0xfc, 0x00
};

const unsigned char unlockBitmap48[] PROGMEM = {
  0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xfc, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
  0x00, 0x00, 0x00, 0x01, 0xfc, 0x3f, 0x80, 0x00, 0x00, 0x01, 0xe0, 0x07, 0xc0, 0x00, 0x00, 0x01,
  0xc0, 0x03, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x01, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x3f, 0xff, 0xff,
  0xfc, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xfe, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0xff,
  0xff, 0xff, 0xff, 0x80, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0,
  0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff,
  0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff,
  0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0,
  0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff,
  0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff,
  0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0,
  0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x03, 0xff, 0xff, 0xff,
  0xff, 0xc0, 0x03, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x01, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00, 0xff,
  0xff, 0xff, 0xff, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x3f, 0xff, 0xff, 0xfc, 0x00
};

// ============================================
// BLE UUIDs (from SHARED_API_CONTRACT.md)
// ============================================
#define SERVICE_UUID           "12345678-1234-5678-1234-56789abcdef0"
#define CHAR_LOCK_CMD_UUID     "12345678-1234-5678-1234-56789abcdef1"  // Write - commands
#define CHAR_STATUS_UUID       "12345678-1234-5678-1234-56789abcdef2"  // Read/Notify - JSON status
#define CHAR_DISPLAY_TEXT_UUID "12345678-1234-5678-1234-56789abcdef3"  // Write - keyholder message
#define CHAR_DEVICE_INFO_UUID  "12345678-1234-5678-1234-56789abcdef4"  // Read - MAC, serial, firmware
#define CHAR_OTA_CONTROL_UUID  "12345678-1234-5678-1234-56789abcdef5"  // Write/Notify - OTA commands & status
#define CHAR_DEVICE_KEY_UUID   "12345678-1234-5678-1234-56789abcdef6"  // Write - API key provisioning
#define CHAR_OTA_DATA_UUID     "12345678-1234-5678-1234-56789abcdef7"  // Write - OTA firmware chunks
#define CHAR_TIME_SYNC_UUID    "12345678-1234-5678-1234-56789abcdef8"  // Write - UTC timestamp from app
#define CHAR_BUFFERED_EVENTS_UUID "12345678-1234-5678-1234-56789abcdef9" // Read/Write - buffered events

// ============================================
// EVENT BUFFERING CONFIGURATION
// ============================================
#define MAX_BUFFERED_EVENTS 50
#define MAX_CRITICAL_EVENTS 10

// Event type codes (per SHARED_API_CONTRACT.md)
#define EVT_BUTTON_PRESS_DENIED   0x01
#define EVT_BUTTON_PRESS_ACCEPTED 0x02
#define EVT_LID_OPENED            0x03
#define EVT_LID_CLOSED            0x04
#define EVT_LOCKED                0x05
#define EVT_UNLOCKED              0x06
#define EVT_EMERGENCY_UNLOCK      0x07
#define EVT_TAMPER_DETECTED       0x08
#define EVT_BATTERY_WARNING       0x09
#define EVT_TIMER_EXPIRED         0x0A
#define EVT_OFFLINE_FALLBACK_STARTED 0x0B
#define EVT_OFFLINE_FALLBACK_UNLOCK  0x0C

// ============================================
// OTA CONFIGURATION (per SHARED_API_CONTRACT.md)
// ============================================
#define OTA_CHUNK_SIZE 512           // Bytes per chunk
#define OTA_TIMEOUT_MS 30000         // Timeout for chunk reception
#define OTA_RESUME_TIMEOUT_MS 300000 // 5 minutes to resume after disconnect
#define OTA_MIN_BATTERY 20           // Minimum battery % to start OTA

// OTA error codes (per SHARED_API_CONTRACT.md)
#define OTA_ERR_FLASH_SPACE   "E01"  // Not enough flash space
#define OTA_ERR_FLASH_WRITE   "E02"  // Flash write failed
#define OTA_ERR_CRC_MISMATCH  "E03"  // CRC mismatch
#define OTA_ERR_INVALID_FW    "E04"  // Invalid firmware format
#define OTA_ERR_LOW_BATTERY   "E05"  // Battery too low
#define OTA_ERR_DEVICE_LOCKED "E06"  // Device is locked
#define OTA_ERR_CHUNK_ORDER   "E07"  // Chunk out of order
#define OTA_ERR_TIMEOUT       "E08"  // Timeout waiting for chunks

// OTA states (per SHARED_API_CONTRACT.md)
enum OTAState {
  OTA_IDLE,       // No OTA in progress
  OTA_RECEIVING,  // Receiving firmware chunks
  OTA_VERIFYING,  // Calculating CRC32
  OTA_APPLYING,   // Writing to boot partition
  OTA_ERROR       // OTA failed
};

// ============================================
// GENERAL CONFIGURATION
// ============================================
#define EMERGENCY_PRESS_COUNT 5
#define EMERGENCY_PRESS_WINDOW_MS 3000

// ============================================
// STATE MACHINE
// ============================================
enum BoxState {
  STATE_READY,            // Servo retracted (0), lid free to open/close
  STATE_LOCKED,           // Servo engaged (90), lid secured
  STATE_LOW_BATTERY       // Critical battery, cannot lock
};

BoxState currentState = STATE_READY;
BoxState previousState = STATE_READY;

// ============================================
// GLOBAL VARIABLES
// ============================================

// Persistent storage
Preferences preferences;

// Timing
unsigned long lockEndTime = 0;          // When the lock should open (millis)
uint32_t lockEndUnixTime = 0;           // When lock expires (Unix timestamp, for persistence)
bool isIndefiniteLock = false;          // True if LOCK:0 (indefinite mode)
unsigned long lastDisplayUpdate = 0;
unsigned long lastFullRefresh = 0;
unsigned long lastBLENotify = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 60000;  // Update display every minute
const unsigned long FULL_REFRESH_INTERVAL = 300000;   // Full display refresh every 5 minutes
const unsigned long BLE_NOTIFY_INTERVAL = 5000;       // BLE notify every 5 seconds

// Hardware state
bool lidClosed = true;
bool deviceConnected = false;
int batteryPercent = 100;  // TODO: Implement actual battery reading
int pendingLockMinutes = 0;  // Queued lock time (0 = no pending lock, -1 = indefinite)

// Device provisioning
char deviceKey[128] = "";  // Base64-encoded HMAC key

// OTA state (per SHARED_API_CONTRACT.md)
OTAState otaState = OTA_IDLE;
uint32_t otaTotalSize = 0;           // Expected firmware size
uint32_t otaExpectedCRC = 0;         // Expected CRC32
uint32_t otaBytesReceived = 0;       // Bytes received so far
uint32_t otaResumeOffset = 0;        // Resume offset (if resuming)
uint32_t otaLastChunk = 0;           // Last received chunk number
uint32_t otaCalculatedCRC = 0;       // Running CRC32 calculation
unsigned long otaStartTime = 0;      // When OTA started (for timeout)
unsigned long otaLastChunkTime = 0;  // When last chunk received
char otaErrorCode[8] = "";           // Error code if OTA_ERROR
char otaErrorMsg[64] = "";           // Error message if OTA_ERROR
bool otaPendingValidation = false;   // True if new firmware needs OTA_CONFIRM

// Tamper detection
bool tamperAlert = false;
bool tamperAlertDisplayed = false;

// Emergency unlock tracking (5 quick presses)
unsigned long emergencyPresses[EMERGENCY_PRESS_COUNT] = {0};
int emergencyPressIndex = 0;

// Custom display text (set via BLE)
char displayText[65] = "Keypr";  // Default text, max 64 chars + null

// Button debouncing
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_MS = 200;

// Last event for BLE status (per SHARED_API_CONTRACT.md)
// Values: null, button_press_denied, button_press_accepted, lid_opened, lid_closed, locked, unlocked, emergency_unlock
char lastEvent[32] = "";

// ============================================
// EVENT BUFFERING (per SHARED_API_CONTRACT.md)
// ============================================

// Event structure for buffering
struct BufferedEvent {
  uint16_t seq;           // Sequence number for deduplication/ACK
  uint8_t type;           // Event type code (EVT_*)
  uint32_t timestamp;     // Unix timestamp or millis (if needs_correction)
  bool needsCorrection;   // True if timestamp is millis, not Unix time
  bool persisted;         // True if saved to NVS flash
  uint16_t details;       // Optional details (e.g., duration_minutes for lock)
};

// RAM event buffer
BufferedEvent eventBuffer[MAX_BUFFERED_EVENTS];
uint8_t eventCount = 0;
uint16_t nextSeq = 1;
uint16_t droppedCount = 0;

// Time synchronization
bool timeSynced = false;
uint32_t syncUnixTime = 0;      // Unix timestamp received from app
unsigned long syncMillis = 0;   // millis() at sync point

// E-Ink Display - Waveshare 1.54" V2 (SSD1681 controller)
// Trying GDEY0154D67 driver for newer panel revision
GxEPD2_BW<GxEPD2_154_GDEY0154D67, GxEPD2_154_GDEY0154D67::HEIGHT> display(
  GxEPD2_154_GDEY0154D67(EINK_CS, EINK_DC, EINK_RST, EINK_BUSY)
);

// Servo
Servo lockServo;

// BLE
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pLockCmdChar = nullptr;
NimBLECharacteristic* pStatusChar = nullptr;
NimBLECharacteristic* pDisplayTextChar = nullptr;
NimBLECharacteristic* pDeviceInfoChar = nullptr;
NimBLECharacteristic* pOTAControlChar = nullptr;
NimBLECharacteristic* pDeviceKeyChar = nullptr;
NimBLECharacteristic* pOTADataChar = nullptr;
NimBLECharacteristic* pTimeSyncChar = nullptr;
NimBLECharacteristic* pBufferedEventsChar = nullptr;

// ============================================
// DISPLAY STATE MACHINE (Simplified)
// ============================================
// Screen is 200x200 pixels
// 6 screen types: Setup, Splash, Default, Message, Error, OTA

enum DisplayScreen {
  SCREEN_SETUP,      // QR code + download instructions (unconfigured device)
  SCREEN_SPLASH,     // "Keypr" logo (startup)
  SCREEN_DEFAULT,    // Big lock/unlock icon + battery (normal operation)
  SCREEN_MESSAGE,    // Status text + keyholder message (when displayText set)
  SCREEN_ERROR,      // Error icon + text (errors/alerts)
  SCREEN_OTA         // OTA update progress
};

DisplayScreen currentScreen = SCREEN_SPLASH;

// Error queue for cycling through multiple errors
#define MAX_ERRORS 5
#define ERROR_DISPLAY_DURATION 3000  // 3 seconds per error
#define ERROR_DISMISS_TIMEOUT  3000  // Auto-dismiss after 3 seconds
#define CANNOT_UNLOCK_THROTTLE 10000 // Only show "Cannot Unlock" once per 10 sec

struct ErrorInfo {
  char text[32];
  bool active;
};

ErrorInfo errorQueue[MAX_ERRORS];
int errorCount = 0;
int currentErrorIndex = 0;
unsigned long errorDisplayStart = 0;
unsigned long lastCannotUnlockTime = 0;

// Factory reset detection
unsigned long factoryResetStart = 0;    // When button+lid started
bool factoryResetInProgress = false;
const unsigned long FACTORY_RESET_HOLD_TIME = 10000;  // 10 seconds

// ============================================
// FORWARD DECLARATIONS
// ============================================
void lockBox(int minutes);
void lockBoxIndefinite();
void unlockBox();
void showPendingLockMessage();
void showCannotUnlockMessage();
void notifyBLEStatus();
void setLastEvent(const char* event);
void setLastEventWithDetails(const char* event, uint16_t details);
uint8_t stringToEventType(const char* event);

// New simplified display system
void updateDisplay();
void updateDisplayFull();
void showScreen(DisplayScreen screen);
void drawSetupScreen();
void drawSplashScreen();
void drawDefaultScreen();
void drawMessageScreen();
void drawErrorScreen();
void drawBatteryIndicator(int x, int y, int percent);
void drawBigLockIcon(bool locked);
void drawErrorIcon(int x, int y, int size);
void queueError(const char* errorText);
void clearError(int index);
void clearAllErrors();
void checkErrorCycle();
bool isDeviceConfigured();
void checkFactoryReset();
void performFactoryReset();
void showResetCountdown(int secondsRemaining);

// Lock icons
void setServoPosition(int angle);
void drawLockedLock(int x, int y, int size);
void drawUnlockedLock(int x, int y, int size);
void checkLidState();
void checkButton();

// OTA functions
void startOTA(uint32_t size, uint32_t crc, uint32_t resumeOffset);
void abortOTA(const char* errorCode, const char* errorMsg);
void processOTAChunk(const uint8_t* data, size_t len);
void verifyOTA();
void applyOTA();
void confirmOTA();
void notifyOTAStatus(const char* status);
uint32_t calculateCRC32(const uint8_t* data, size_t len);
void drawOTAScreen();
void checkOTAPendingValidation();

// Storage functions
void loadSettings();
void saveDeviceKey();

// Emergency unlock
void checkEmergencyUnlock();
void triggerEmergencyUnlock(uint8_t method);

// Tamper detection
void checkTamper();
void showTamperAlert();
void clearTamperAlert();

// Lock state persistence
void saveLockState();

// Event buffering
void bufferEvent(uint8_t type, uint16_t details);
bool isCriticalEvent(uint8_t type);
void persistCriticalEvent(BufferedEvent* event);
void loadCriticalEventsFromNVS();
void clearPersistedEvents(uint16_t upToSeq);
uint32_t getCurrentTimestamp();
const char* eventTypeToString(uint8_t type);
void recordButtonPress();

// ============================================
// BLE CALLBACKS
// ============================================

class ServerCallbacks : public NimBLEServerCallbacks {
public:
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    deviceConnected = true;
    Serial.print("BLE: Client connected, MTU: ");
    Serial.println(connInfo.getMTU());
    // BLE status no longer displayed in simplified UI
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    deviceConnected = false;
    Serial.print("BLE: Client disconnected, reason: ");
    Serial.println(reason);
    // Restart advertising
    NimBLEDevice::startAdvertising();
  }
};

class LockCommandCallback : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      String cmd = String(value.c_str());
      cmd.trim();

      Serial.print("BLE: Received command: [");
      Serial.print(cmd);
      Serial.println("]");

      if (cmd.startsWith("LOCK:")) {
        // LOCK:<minutes> - Start lock timer (0 = indefinite)
        String minutesStr = cmd.substring(5);
        minutesStr.trim();
        long minutes = minutesStr.toInt();

        if (minutes == 0) {
          // Indefinite lock mode
          Serial.println("LOCK indefinitely (no timer)");
          lockBoxIndefinite();
        } else if (minutes > 0 && minutes <= 525600) {
          Serial.print("LOCK for ");
          Serial.print(minutes);
          Serial.println(" minutes");
          lockBox((int)minutes);
        } else {
          Serial.println("Invalid minutes value!");
        }
      } else if (cmd == "UNLOCK") {
        // UNLOCK - App has verified with API, device trusts and unlocks
        // BLE-proxy model: app handles all API communication, device just executes
        if (currentState == STATE_LOCKED) {
          // App says unlock - we trust it (API has authorized)
          Serial.println("UNLOCK command received - unlocking (API authorized)");
          unlockBox();
        } else if (currentState == STATE_READY) {
          Serial.println("Already in ready state");
        } else {
          Serial.print("UNLOCK received in unexpected state: ");
          Serial.println(currentState);
        }
      } else if (cmd == "FORCE_UNLOCK") {
        // FORCE_UNLOCK - Emergency unlock via app (bypasses timer)
        Serial.println("FORCE_UNLOCK - Emergency unlock triggered via BLE");
        triggerEmergencyUnlock(1);  // method=1 (app)
      } else if (cmd == "STATUS") {
        Serial.println("STATUS request");
        notifyBLEStatus();
      } else if (cmd == "SYNC") {
        // SYNC kept for backwards compatibility, but no-op with BLE-proxy model
        Serial.println("SYNC request (no-op in BLE-proxy mode)");
        notifyBLEStatus();
      } else if (cmd == "CLEAR_TAMPER") {
        Serial.println("CLEAR_TAMPER request");
        clearTamperAlert();
      } else {
        Serial.println("Unknown command: " + cmd);
      }
    }
  }
};

class DisplayTextCallback : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() <= 64) {
      // Handle empty message (clears display text)
      if (value.length() == 0) {
        strcpy(displayText, "Keypr");  // Reset to default
        Serial.println("Display text cleared");
      } else {
        strncpy(displayText, value.c_str(), 64);
        displayText[64] = '\0';
        Serial.print("Display text set to: ");
        Serial.println(displayText);
      }
      updateDisplay();  // Update to show message screen or default screen
    }
  }
};

// OTA Control Callback - handles OTA_START, OTA_ABORT, OTA_VERIFY, OTA_APPLY, OTA_CONFIRM
class OTAControlCallback : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() == 0) return;

    String cmd = String(value.c_str());
    Serial.print("OTA Control received: ");
    Serial.println(cmd);

    if (cmd.startsWith("OTA_START:")) {
      // Format: OTA_START:<size>:<crc32> or OTA_START:<size>:<crc32>:<resume_offset>
      String params = cmd.substring(10);
      int firstColon = params.indexOf(':');
      int secondColon = params.indexOf(':', firstColon + 1);

      if (firstColon > 0) {
        uint32_t size = params.substring(0, firstColon).toInt();
        uint32_t crc = strtoul(params.substring(firstColon + 1, secondColon > 0 ? secondColon : params.length()).c_str(), NULL, 16);
        uint32_t resumeOffset = 0;

        if (secondColon > 0) {
          resumeOffset = params.substring(secondColon + 1).toInt();
        }

        startOTA(size, crc, resumeOffset);
      }
    } else if (cmd == "OTA_ABORT") {
      abortOTA("", "Aborted by user");
    } else if (cmd == "OTA_VERIFY") {
      verifyOTA();
    } else if (cmd == "OTA_APPLY") {
      applyOTA();
    } else if (cmd == "OTA_CONFIRM") {
      confirmOTA();
    }
  }
};

class DeviceKeyCallback : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      // Format: KEY:<base64-device-key>
      String cmd = String(value.c_str());
      if (cmd.startsWith("KEY:")) {
        String key = cmd.substring(4);
        strncpy(deviceKey, key.c_str(), 127);
        deviceKey[127] = '\0';

        Serial.println("Device key provisioned via BLE");
        Serial.print("Key length: ");
        Serial.println(strlen(deviceKey));
        saveDeviceKey();

        // Update display - transition from setup screen to default screen
        Serial.println("Updating display after device key provisioned...");
        updateDisplay();
        Serial.print("isDeviceConfigured() = ");
        Serial.println(isDeviceConfigured() ? "true" : "false");
      }
    }
  }
};

// OTA Data Callback - receives raw firmware binary chunks
class OTADataCallback : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    if (otaState != OTA_RECEIVING) {
      Serial.println("OTA: Received data but not in RECEIVING state");
      return;
    }

    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      processOTAChunk((const uint8_t*)value.data(), value.length());
    }
  }
};

class TimeSyncCallback : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      // Format: TIME:<unix_timestamp_seconds>
      String cmd = String(value.c_str());
      if (cmd.startsWith("TIME:")) {
        String timestampStr = cmd.substring(5);
        uint32_t unixTime = timestampStr.toInt();

        if (unixTime > 1700000000) {  // Sanity check: after 2023
          syncUnixTime = unixTime;
          syncMillis = millis();
          timeSynced = true;

          Serial.print("Time synced: ");
          Serial.print(unixTime);
          Serial.print(" at millis ");
          Serial.println(syncMillis);

          // If locked and we have a stored end time, recalculate lockEndTime
          if (currentState == STATE_LOCKED && lockEndUnixTime > 0) {
            if (lockEndUnixTime > 1700000000) {
              // It's a Unix timestamp - calculate remaining time
              if (unixTime >= lockEndUnixTime) {
                // Lock has expired
                Serial.println("Time sync: Lock has expired");
                lockEndTime = 0;  // Will trigger unlock flow on next loop
              } else {
                // Lock still active - calculate remaining millis
                uint32_t remainingSecs = lockEndUnixTime - unixTime;
                lockEndTime = millis() + ((unsigned long)remainingSecs * 1000);
                Serial.print("Time sync: Lock has ");
                Serial.print(remainingSecs);
                Serial.println(" seconds remaining");
              }
            } else {
              // It's a duration (lock was set before time sync) - calculate new end time
              uint32_t durationSecs = lockEndUnixTime;
              lockEndUnixTime = unixTime + durationSecs;
              lockEndTime = millis() + ((unsigned long)durationSecs * 1000);
              saveLockState();  // Update with proper Unix timestamp
              Serial.print("Time sync: Lock duration ");
              Serial.print(durationSecs);
              Serial.println(" secs converted to Unix timestamp");
            }
            notifyBLEStatus();  // Update app with accurate remaining time
          }
        } else {
          Serial.println("Invalid timestamp received");
        }
      }
    }
  }
};

class BufferedEventsCallback : public NimBLECharacteristicCallbacks {
public:
  void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    Serial.println("Buffered events requested via BLE...");

    // Build JSON response - need enough space for up to 50 events (~120 bytes each)
    DynamicJsonDocument doc(8192);
    doc["count"] = eventCount;
    doc["time_synced"] = timeSynced;
    doc["dropped"] = droppedCount;

    JsonArray events = doc.createNestedArray("events");

    for (int i = 0; i < eventCount; i++) {
      JsonObject evt = events.createNestedObject();
      evt["seq"] = eventBuffer[i].seq;
      evt["type"] = eventTypeToString(eventBuffer[i].type);
      evt["timestamp"] = eventBuffer[i].timestamp;
      evt["needs_correction"] = eventBuffer[i].needsCorrection;
      evt["persisted"] = eventBuffer[i].persisted;

      // Add details based on event type
      JsonObject details = evt.createNestedObject("details");
      if (eventBuffer[i].type == EVT_LOCKED) {
        details["duration_minutes"] = eventBuffer[i].details;
      } else if (eventBuffer[i].type == EVT_EMERGENCY_UNLOCK) {
        details["method"] = eventBuffer[i].details;  // 0=button, 1=app
      } else if (eventBuffer[i].type == EVT_BATTERY_WARNING) {
        details["percent"] = eventBuffer[i].details;
      } else if (eventBuffer[i].type == EVT_OFFLINE_FALLBACK_UNLOCK) {
        details["offline_minutes"] = eventBuffer[i].details;
      }
    }

    String jsonStr;
    serializeJson(doc, jsonStr);
    pCharacteristic->setValue(jsonStr.c_str());

    Serial.print("Buffered events: ");
    Serial.print(eventCount);
    Serial.print(" events, ");
    Serial.print(jsonStr.length());
    Serial.println(" bytes");
    Serial.println(jsonStr);  // Print full JSON for debugging
  }

  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      String cmd = String(value.c_str());
      cmd.trim();

      if (cmd == "CLEAR") {
        // Clear all buffered events
        Serial.println("Clearing all buffered events");
        clearPersistedEvents(65535);  // Clear all
        eventCount = 0;
        droppedCount = 0;
      } else if (cmd.startsWith("ACK:")) {
        // Acknowledge events up to sequence number
        String seqStr = cmd.substring(4);
        uint16_t ackSeq = seqStr.toInt();
        Serial.print("ACK events up to seq ");
        Serial.println(ackSeq);

        // Remove acknowledged events from buffer
        int removeCount = 0;
        for (int i = 0; i < eventCount; i++) {
          if (eventBuffer[i].seq <= ackSeq) {
            removeCount++;
          } else {
            break;  // Events are in order
          }
        }

        if (removeCount > 0) {
          // Clear persisted events
          clearPersistedEvents(ackSeq);

          // Shift remaining events
          memmove(&eventBuffer[0], &eventBuffer[removeCount],
                  (eventCount - removeCount) * sizeof(BufferedEvent));
          eventCount -= removeCount;
          Serial.print("Removed ");
          Serial.print(removeCount);
          Serial.println(" events from buffer");
        }
      }
    }
  }
};

// ============================================
// BLE SETUP
// ============================================

void setupBLE() {
  Serial.println("Initializing BLE...");

  NimBLEDevice::init("Keypr");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // Max power for range
  NimBLEDevice::setMTU(517);  // Max BLE MTU to prevent status JSON truncation (~260 bytes)

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  // Lock Command Characteristic (Write)
  pLockCmdChar = pService->createCharacteristic(
    CHAR_LOCK_CMD_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  pLockCmdChar->setCallbacks(new LockCommandCallback());

  // Status Characteristic (Read/Notify) - JSON format
  pStatusChar = pService->createCharacteristic(
    CHAR_STATUS_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  notifyBLEStatus();  // Set initial value

  // Display Text Characteristic (Write)
  pDisplayTextChar = pService->createCharacteristic(
    CHAR_DISPLAY_TEXT_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ
  );
  pDisplayTextChar->setValue(displayText);
  pDisplayTextChar->setCallbacks(new DisplayTextCallback());

  // Device Info Characteristic (Read) - MAC, serial, firmware
  pDeviceInfoChar = pService->createCharacteristic(
    CHAR_DEVICE_INFO_UUID,
    NIMBLE_PROPERTY::READ
  );
  // Set device info JSON
  String macAddr = WiFi.macAddress();
  StaticJsonDocument<256> infoDoc;
  infoDoc["mac"] = macAddr;
  infoDoc["firmware"] = "1.0.0";
  infoDoc["serial"] = "KPR-0001";  // TODO: Generate unique serial
  String infoJson;
  serializeJson(infoDoc, infoJson);
  pDeviceInfoChar->setValue(infoJson.c_str());

  // OTA Control Characteristic (Write/Notify) - OTA commands and status
  pOTAControlChar = pService->createCharacteristic(
    CHAR_OTA_CONTROL_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY
  );
  pOTAControlChar->setCallbacks(new OTAControlCallback());

  // Device Key Characteristic (Write)
  pDeviceKeyChar = pService->createCharacteristic(
    CHAR_DEVICE_KEY_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  pDeviceKeyChar->setCallbacks(new DeviceKeyCallback());

  // OTA Data Characteristic (Write) - firmware binary chunks
  pOTADataChar = pService->createCharacteristic(
    CHAR_OTA_DATA_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  pOTADataChar->setCallbacks(new OTADataCallback());

  // Time Sync Characteristic (Write) - receives UTC timestamp from app
  pTimeSyncChar = pService->createCharacteristic(
    CHAR_TIME_SYNC_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  pTimeSyncChar->setCallbacks(new TimeSyncCallback());

  // Buffered Events Characteristic (Read/Write) - retrieve/clear buffered events
  pBufferedEventsChar = pService->createCharacteristic(
    CHAR_BUFFERED_EVENTS_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
  );
  pBufferedEventsChar->setCallbacks(new BufferedEventsCallback());
  pBufferedEventsChar->setValue("{\"count\":0,\"time_synced\":false,\"dropped\":0,\"events\":[]}");

  pService->start();

  // Start advertising
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);

  // Set up advertising data with device name
  NimBLEAdvertisementData advData;
  advData.setName("Keypr");
  advData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
  pAdvertising->setAdvertisementData(advData);

  // Set up scan response with full name
  NimBLEAdvertisementData scanData;
  scanData.setName("Keypr");
  pAdvertising->setScanResponseData(scanData);

  pAdvertising->start();

  Serial.println("BLE: Advertising started as 'Keypr'");
}

// Convert string event name to event type code
uint8_t stringToEventType(const char* event) {
  if (strcmp(event, "button_press_denied") == 0) return EVT_BUTTON_PRESS_DENIED;
  if (strcmp(event, "button_press_accepted") == 0) return EVT_BUTTON_PRESS_ACCEPTED;
  if (strcmp(event, "lid_opened") == 0) return EVT_LID_OPENED;
  if (strcmp(event, "lid_closed") == 0) return EVT_LID_CLOSED;
  if (strcmp(event, "locked") == 0) return EVT_LOCKED;
  if (strcmp(event, "unlocked") == 0) return EVT_UNLOCKED;
  if (strcmp(event, "emergency_unlock") == 0) return EVT_EMERGENCY_UNLOCK;
  if (strcmp(event, "tamper_detected") == 0) return EVT_TAMPER_DETECTED;
  if (strcmp(event, "battery_warning") == 0) return EVT_BATTERY_WARNING;
  if (strcmp(event, "timer_expired") == 0) return EVT_TIMER_EXPIRED;
  if (strcmp(event, "offline_fallback_started") == 0) return EVT_OFFLINE_FALLBACK_STARTED;
  if (strcmp(event, "offline_fallback_unlock") == 0) return EVT_OFFLINE_FALLBACK_UNLOCK;
  return 0;  // Unknown
}

// Helper to set last event, buffer it, and notify BLE
void setLastEvent(const char* event) {
  setLastEventWithDetails(event, 0);
}

// Set last event with optional details (e.g., duration for lock)
void setLastEventWithDetails(const char* event, uint16_t details) {
  strncpy(lastEvent, event, 31);
  lastEvent[31] = '\0';

  // Buffer the event
  uint8_t eventType = stringToEventType(event);
  if (eventType != 0) {
    bufferEvent(eventType, details);
  }

  notifyBLEStatus();
}

void notifyBLEStatus() {
  // Build JSON status per SHARED_API_CONTRACT.md
  StaticJsonDocument<512> doc;  // Increased for event buffering fields

  // State string
  switch (currentState) {
    case STATE_READY:
      doc["state"] = "ready";
      break;
    case STATE_LOCKED:
      doc["state"] = "locked";
      break;
    case STATE_LOW_BATTERY:
      doc["state"] = "low_battery";
      break;
  }

  // Lock mode and remaining seconds
  if (currentState == STATE_LOCKED) {
    doc["lock_mode"] = isIndefiniteLock ? "indefinite" : "timed";
    if (isIndefiniteLock) {
      doc["remaining_seconds"] = -1;  // -1 indicates indefinite lock
    } else if (millis() < lockEndTime) {
      doc["remaining_seconds"] = (lockEndTime - millis()) / 1000;
    } else {
      doc["remaining_seconds"] = 0;
    }
  } else {
    doc["lock_mode"] = (char*)nullptr;  // JSON null when not locked
    doc["remaining_seconds"] = 0;
  }

  doc["lid_closed"] = lidClosed;
  doc["battery_percent"] = batteryPercent;
  doc["tamper_alert"] = tamperAlert;

  // Last event (null if empty string)
  if (strlen(lastEvent) > 0) {
    doc["last_event"] = lastEvent;
  } else {
    doc["last_event"] = (char*)nullptr;  // JSON null
  }

  // Event buffering fields (per SHARED_API_CONTRACT.md)
  doc["uptime_millis"] = millis();
  doc["buffered_event_count"] = eventCount;

  // OTA state fields (per SHARED_API_CONTRACT.md)
  switch (otaState) {
    case OTA_IDLE: doc["ota_state"] = "idle"; break;
    case OTA_RECEIVING: doc["ota_state"] = "receiving"; break;
    case OTA_VERIFYING: doc["ota_state"] = "verifying"; break;
    case OTA_APPLYING: doc["ota_state"] = "applying"; break;
    case OTA_ERROR: doc["ota_state"] = "error"; break;
  }
  if (otaState != OTA_IDLE) {
    doc["ota_progress"] = otaBytesReceived;
    doc["ota_total"] = otaTotalSize;
  }

  String jsonStr;
  serializeJson(doc, jsonStr);

  if (pStatusChar != nullptr) {
    pStatusChar->setValue(jsonStr.c_str());
    if (deviceConnected) {
      pStatusChar->notify();
    }
  }
}

// ============================================
// DISPLAY FUNCTIONS
// ============================================

void setupDisplay() {
  Serial.println("Initializing display...");
  Serial.print("  Pins: MOSI="); Serial.print(EINK_MOSI);
  Serial.print(" CLK="); Serial.print(EINK_CLK);
  Serial.print(" CS="); Serial.print(EINK_CS);
  Serial.print(" DC="); Serial.print(EINK_DC);
  Serial.print(" RST="); Serial.print(EINK_RST);
  Serial.print(" BUSY="); Serial.println(EINK_BUSY);

  // Check BUSY pin before anything
  pinMode(EINK_BUSY, INPUT);
  Serial.print("BUSY before reset: ");
  Serial.println(digitalRead(EINK_BUSY) ? "HIGH" : "LOW");

  // Manual reset first
  pinMode(EINK_RST, OUTPUT);
  digitalWrite(EINK_RST, HIGH);
  delay(100);
  digitalWrite(EINK_RST, LOW);
  delay(100);
  digitalWrite(EINK_RST, HIGH);
  delay(100);

  Serial.print("BUSY after reset: ");
  Serial.println(digitalRead(EINK_BUSY) ? "HIGH" : "LOW");

  // Initialize SPI explicitly
  pinMode(EINK_CS, OUTPUT);
  digitalWrite(EINK_CS, HIGH);
  pinMode(EINK_DC, OUTPUT);

  // Use HSPI with explicit pins
  SPI.begin(EINK_CLK, -1, EINK_MOSI, EINK_CS);

  // Initialize GxEPD2 - let it do its own reset too
  display.init(115200, true, 50, false);
  display.setRotation(2);  // 180° rotation - pins at bottom
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();

  Serial.print("BUSY after init: ");
  Serial.println(digitalRead(EINK_BUSY) ? "HIGH" : "LOW");

  Serial.println("Display initialized");
}

// ============================================
// SIMPLIFIED DISPLAY SYSTEM
// ============================================
// 5 screen types: Setup, Splash, Default, Message, Error
// All use full refresh for clean display on e-ink

// Check if device has been configured (has device key)
bool isDeviceConfigured() {
  return strlen(deviceKey) > 0;
}

// Update display based on current state
void updateDisplay() {
  // Determine which screen to show
  if (!isDeviceConfigured()) {
    showScreen(SCREEN_SETUP);
  } else if (otaState != OTA_IDLE) {
    showScreen(SCREEN_OTA);
  } else if (errorCount > 0) {
    showScreen(SCREEN_ERROR);
  } else if (strlen(displayText) > 0 && strcmp(displayText, "Keypr") != 0) {
    showScreen(SCREEN_MESSAGE);
  } else {
    showScreen(SCREEN_DEFAULT);
  }
}

// Full refresh version (same as updateDisplay for simplified system)
void updateDisplayFull() {
  Serial.println("Full display refresh...");
  updateDisplay();
}

// Show a specific screen
void showScreen(DisplayScreen screen) {
  currentScreen = screen;

  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    switch (screen) {
      case SCREEN_SETUP:
        drawSetupScreen();
        break;
      case SCREEN_SPLASH:
        drawSplashScreen();
        break;
      case SCREEN_DEFAULT:
        drawDefaultScreen();
        break;
      case SCREEN_MESSAGE:
        drawMessageScreen();
        break;
      case SCREEN_ERROR:
        drawErrorScreen();
        break;
      case SCREEN_OTA:
        drawOTAScreen();
        break;
    }
  } while (display.nextPage());

  display.hibernate();
  Serial.print("Screen displayed: ");
  Serial.println(screen);
}

// ============================================
// SCREEN DRAWING FUNCTIONS
// ============================================

// Setup Screen: Logo + QR code
void drawSetupScreen() {
  // Title at top - "Keypr"
  display.setFont(&FreeMonoBold12pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds("Keypr", 0, 0, &x1, &y1, &w, &h);
  display.setCursor((200 - w) / 2, 22);
  display.print("Keypr");

  // Draw QR code (32x32 scaled 4x = 128x128 pixels)
  // Center horizontally: (200 - 128) / 2 = 36
  int qrX = 36;
  int qrY = 35;
  int scale = 4;

  // Draw each module of the QR code
  // Note: bitmap is white=1, black=0, so we draw when bit is 0
  for (int row = 0; row < QR_SIZE; row++) {
    for (int col = 0; col < QR_SIZE; col++) {
      int byteIndex = row * 4 + (col / 8);
      int bitIndex = 7 - (col % 8);
      uint8_t b = pgm_read_byte(&qrcodeBitmap[byteIndex]);
      bool isWhite = (b >> bitIndex) & 1;

      if (!isWhite) {
        display.fillRect(qrX + col * scale, qrY + row * scale, scale, scale, GxEPD_BLACK);
      }
    }
  }
}

// Splash Screen: "Keypr" logo centered
void drawSplashScreen() {
  display.setFont(&FreeMonoBold18pt7b);

  // Center "Keypr" text
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds("Keypr", 0, 0, &x1, &y1, &w, &h);
  int x = (200 - w) / 2;
  int y = 100 + (h / 2);  // Center vertically

  display.setCursor(x, y);
  display.print("Keypr");
}

// Default Screen: Big lock/unlock icon centered, battery top right
void drawDefaultScreen() {
  // Battery indicator in top right (larger)
  drawBatteryIndicator(140, 5, batteryPercent);

  // Big lock icon centered (use 48x48 bitmap scaled by drawing twice)
  drawBigLockIcon(currentState == STATE_LOCKED);

  // Optional: Show timer below icon for timed locks
  if (currentState == STATE_LOCKED && !isIndefiniteLock) {
    unsigned long remaining = 0;
    if (millis() < lockEndTime) {
      remaining = (lockEndTime - millis()) / 1000;
    }

    unsigned long days = remaining / 86400;
    unsigned long hours = (remaining % 86400) / 3600;
    unsigned long mins = (remaining % 3600) / 60;
    unsigned long secs = remaining % 60;

    char timeStr[24];
    if (days > 0) {
      sprintf(timeStr, "%lud %luh", days, hours);
    } else if (hours > 0) {
      sprintf(timeStr, "%luh %lum", hours, mins);
    } else if (mins > 0) {
      sprintf(timeStr, "%lum %lus", mins, secs);
    } else {
      sprintf(timeStr, "%lus", secs);
    }

    display.setFont(&FreeMonoBold9pt7b);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((200 - w) / 2, 170);
    display.print(timeStr);
  }
}

// Message Screen: Status text top left, battery top right, message centered
void drawMessageScreen() {
  // Status in top left (LOCKED/UNLOCKED)
  display.setFont(&FreeMonoBold9pt7b);
  display.setCursor(5, 18);
  if (currentState == STATE_LOCKED) {
    display.print("LOCKED");
  } else {
    display.print("UNLOCKED");
  }

  // Battery indicator in top right
  drawBatteryIndicator(140, 5, batteryPercent);

  // Divider line
  display.drawLine(0, 28, 200, 28, GxEPD_BLACK);

  // Message centered in remaining space with word wrap
  display.setFont(&FreeMonoBold9pt7b);

  int textY = 60;
  int lineHeight = 22;
  int maxCharsPerLine = 16;
  int maxLines = 6;

  String text = String(displayText);
  int currentLine = 0;
  int startIdx = 0;

  while (startIdx < (int)text.length() && currentLine < maxLines) {
    int endIdx = startIdx + maxCharsPerLine;
    if (endIdx >= (int)text.length()) {
      // Center this line
      String line = text.substring(startIdx);
      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
      display.setCursor((200 - w) / 2, textY + (currentLine * lineHeight));
      display.print(line);
      break;
    }

    // Word wrap
    int breakIdx = endIdx;
    while (breakIdx > startIdx && text.charAt(breakIdx) != ' ') {
      breakIdx--;
    }
    if (breakIdx == startIdx) {
      breakIdx = endIdx;
    }

    String line = text.substring(startIdx, breakIdx);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((200 - w) / 2, textY + (currentLine * lineHeight));
    display.print(line);

    startIdx = breakIdx;
    if (startIdx < (int)text.length() && text.charAt(startIdx) == ' ') {
      startIdx++;
    }
    currentLine++;
  }
}

// Error Screen: Error icon centered, error text below, NO battery
void drawErrorScreen() {
  if (errorCount == 0) return;

  // Draw error icon (circle with X) centered
  int iconX = 100;
  int iconY = 70;
  int iconR = 35;

  drawErrorIcon(iconX, iconY, iconR);

  // Error text below icon
  display.setFont(&FreeMonoBold9pt7b);
  const char* errText = errorQueue[currentErrorIndex].text;

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(errText, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((200 - w) / 2, 140);
  display.print(errText);

  // If multiple errors, show indicator
  if (errorCount > 1) {
    display.setFont();
    char indicator[16];
    sprintf(indicator, "(%d/%d)", currentErrorIndex + 1, errorCount);
    display.setCursor(80, 165);
    display.print(indicator);
  }
}

// OTA Screen: Progress bar and status
void drawOTAScreen() {
  // Title
  display.setFont(&FreeMonoBold12pt7b);
  const char* title = "Updating...";

  // Show different title based on OTA state
  switch (otaState) {
    case OTA_RECEIVING: title = "Updating..."; break;
    case OTA_VERIFYING: title = "Verifying..."; break;
    case OTA_APPLYING: title = "Applying..."; break;
    case OTA_ERROR: title = "Update Failed"; break;
    default: title = "Update"; break;
  }

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((200 - w) / 2, 40);
  display.print(title);

  // Progress bar (if receiving or verifying)
  if (otaState == OTA_RECEIVING || otaState == OTA_VERIFYING) {
    // Bar outline
    int barX = 20;
    int barY = 80;
    int barW = 160;
    int barH = 20;
    display.drawRect(barX, barY, barW, barH, GxEPD_BLACK);

    // Fill bar based on progress
    int progress = 0;
    if (otaTotalSize > 0) {
      progress = (otaBytesReceived * (barW - 4)) / otaTotalSize;
    }
    display.fillRect(barX + 2, barY + 2, progress, barH - 4, GxEPD_BLACK);

    // Percentage text
    display.setFont(&FreeMonoBold9pt7b);
    int percent = 0;
    if (otaTotalSize > 0) {
      percent = (otaBytesReceived * 100) / otaTotalSize;
    }
    char percentStr[8];
    snprintf(percentStr, sizeof(percentStr), "%d%%", percent);
    display.getTextBounds(percentStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((200 - w) / 2, barY + barH + 25);
    display.print(percentStr);

    // Size text
    display.setFont();
    char sizeStr[32];
    snprintf(sizeStr, sizeof(sizeStr), "%lu / %lu KB", otaBytesReceived / 1024, otaTotalSize / 1024);
    int textW = strlen(sizeStr) * 6;
    display.setCursor((200 - textW) / 2, barY + barH + 45);
    display.print(sizeStr);
  }

  // Error message (if error state)
  if (otaState == OTA_ERROR && strlen(otaErrorMsg) > 0) {
    display.setFont(&FreeMonoBold9pt7b);
    display.getTextBounds(otaErrorMsg, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((200 - w) / 2, 100);
    display.print(otaErrorMsg);

    // Error code
    if (strlen(otaErrorCode) > 0) {
      display.setFont();
      char codeStr[16];
      snprintf(codeStr, sizeof(codeStr), "Error: %s", otaErrorCode);
      int textW = strlen(codeStr) * 6;
      display.setCursor((200 - textW) / 2, 130);
      display.print(codeStr);
    }
  }

  // "Do not disconnect" warning
  if (otaState == OTA_RECEIVING || otaState == OTA_VERIFYING || otaState == OTA_APPLYING) {
    display.setFont();
    const char* warning = "Do not disconnect";
    int textW = strlen(warning) * 6;
    display.setCursor((200 - textW) / 2, 180);
    display.print(warning);
  }
}

// ============================================
// DISPLAY HELPER FUNCTIONS
// ============================================

// Draw battery indicator (larger, with percentage)
void drawBatteryIndicator(int x, int y, int percent) {
  // Battery outline (larger)
  int batW = 40;
  int batH = 18;
  display.drawRect(x, y, batW, batH, GxEPD_BLACK);
  display.fillRect(x + batW, y + 5, 4, 8, GxEPD_BLACK);  // Terminal

  // Fill based on percentage
  int fillW = ((batW - 4) * percent) / 100;
  display.fillRect(x + 2, y + 2, fillW, batH - 4, GxEPD_BLACK);

  // Percentage text below
  display.setFont();
  char percStr[8];
  sprintf(percStr, "%d%%", percent);
  display.setCursor(x + 10, y + batH + 3);
  display.print(percStr);
}

// Draw big lock icon centered
void drawBigLockIcon(bool locked) {
  // Center the 48x48 bitmap
  int x = (200 - 48) / 2;
  int y = 60;

  if (locked) {
    display.drawBitmap(x, y, lockBitmap48, 48, 48, GxEPD_BLACK);
  } else {
    display.drawBitmap(x, y, unlockBitmap48, 48, 48, GxEPD_BLACK);
  }
}

// Draw error icon (circle with X)
void drawErrorIcon(int cx, int cy, int r) {
  // Outer circle (thick)
  display.drawCircle(cx, cy, r, GxEPD_BLACK);
  display.drawCircle(cx, cy, r - 1, GxEPD_BLACK);
  display.drawCircle(cx, cy, r - 2, GxEPD_BLACK);

  // X inside
  int xOffset = r * 0.5;  // X extends 50% of radius from center
  // Draw thick X lines
  for (int t = -2; t <= 2; t++) {
    display.drawLine(cx - xOffset + t, cy - xOffset, cx + xOffset + t, cy + xOffset, GxEPD_BLACK);
    display.drawLine(cx + xOffset + t, cy - xOffset, cx - xOffset + t, cy + xOffset, GxEPD_BLACK);
  }
}

// Draw an unlocked padlock icon using 48x48 bitmap
void drawUnlockedLock(int x, int y, int size) {
  (void)size;
  display.drawBitmap(x, y, unlockBitmap48, 48, 48, GxEPD_BLACK);
}

// Draw a locked padlock icon using 48x48 bitmap
void drawLockedLock(int x, int y, int size) {
  (void)size;
  display.drawBitmap(x, y, lockBitmap48, 48, 48, GxEPD_BLACK);
}

// ============================================
// ERROR QUEUE MANAGEMENT
// ============================================

void queueError(const char* errorText) {
  // Check if this error already exists
  for (int i = 0; i < errorCount; i++) {
    if (strcmp(errorQueue[i].text, errorText) == 0) {
      return;  // Already in queue
    }
  }

  // Add to queue if space
  if (errorCount < MAX_ERRORS) {
    strncpy(errorQueue[errorCount].text, errorText, 31);
    errorQueue[errorCount].text[31] = '\0';
    errorQueue[errorCount].active = true;
    errorCount++;

    Serial.print("Error queued: ");
    Serial.println(errorText);

    // Start display timer if first error
    if (errorCount == 1) {
      errorDisplayStart = millis();
      currentErrorIndex = 0;
      showScreen(SCREEN_ERROR);
    }
  }
}

void clearError(int index) {
  if (index < 0 || index >= errorCount) return;

  // Shift remaining errors down
  for (int i = index; i < errorCount - 1; i++) {
    errorQueue[i] = errorQueue[i + 1];
  }
  errorCount--;

  // Adjust current index if needed
  if (currentErrorIndex >= errorCount) {
    currentErrorIndex = 0;
  }

  // Update display
  if (errorCount == 0) {
    updateDisplay();  // Will show default or message screen
  } else {
    showScreen(SCREEN_ERROR);
  }
}

void clearAllErrors() {
  errorCount = 0;
  currentErrorIndex = 0;
  updateDisplay();
}

void checkErrorCycle() {
  if (errorCount == 0) return;

  unsigned long now = millis();

  // Check if current error should cycle or dismiss
  if (now - errorDisplayStart >= ERROR_DISPLAY_DURATION) {
    if (errorCount > 1) {
      // Cycle to next error
      currentErrorIndex = (currentErrorIndex + 1) % errorCount;
      errorDisplayStart = now;
      showScreen(SCREEN_ERROR);
    } else {
      // Single error - auto dismiss after timeout
      clearError(0);
    }
  }
}

// ============================================
// FACTORY RESET
// ============================================

void checkFactoryReset() {
  // Factory reset requires: button held + lid open + device unlocked
  bool buttonPressed = (digitalRead(UNLOCK_BTN) == LOW);
  bool resetConditions = buttonPressed && !lidClosed && (currentState == STATE_READY);

  if (resetConditions) {
    if (!factoryResetInProgress) {
      // Start tracking
      factoryResetInProgress = true;
      factoryResetStart = millis();
      Serial.println("Factory reset started - hold for 10 seconds");
    } else {
      // Check if held long enough
      unsigned long elapsed = millis() - factoryResetStart;
      int remaining = (FACTORY_RESET_HOLD_TIME - elapsed) / 1000;

      if (remaining != (int)((FACTORY_RESET_HOLD_TIME - (millis() - 50 - factoryResetStart)) / 1000)) {
        // Show countdown every second
        showResetCountdown(remaining + 1);
      }

      if (elapsed >= FACTORY_RESET_HOLD_TIME) {
        performFactoryReset();
      }
    }
  } else {
    // Conditions no longer met
    if (factoryResetInProgress) {
      factoryResetInProgress = false;
      Serial.println("Factory reset cancelled");
      updateDisplay();  // Return to normal display
    }
  }
}

void showResetCountdown(int secondsRemaining) {
  Serial.print("Factory reset in ");
  Serial.print(secondsRemaining);
  Serial.println(" seconds...");

  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(20, 40);
    display.print("FACTORY");
    display.setCursor(40, 70);
    display.print("RESET");

    // Countdown number (big)
    display.setFont(&FreeMonoBold18pt7b);
    char numStr[4];
    sprintf(numStr, "%d", secondsRemaining);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(numStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((200 - w) / 2, 130);
    display.print(numStr);

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(20, 170);
    display.print("Release to");
    display.setCursor(45, 190);
    display.print("cancel");

  } while (display.nextPage());

  display.hibernate();
}

void performFactoryReset() {
  Serial.println("!!! PERFORMING FACTORY RESET !!!");

  // Show reset message
  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(25, 80);
    display.print("Resetting");
    display.setCursor(60, 110);
    display.print("...");
  } while (display.nextPage());

  display.hibernate();

  // Clear all NVS data
  preferences.begin("keypr", false);
  preferences.clear();
  preferences.end();

  preferences.begin("keypr_evt", false);
  preferences.clear();
  preferences.end();

  // Clear runtime variables
  deviceKey[0] = '\0';
  displayText[0] = '\0';
  strcpy(displayText, "Keypr");
  tamperAlert = false;
  otaState = OTA_IDLE;

  Serial.println("Factory reset complete - rebooting");
  delay(1000);

  // Reboot
  ESP.restart();
}

// ============================================
// SERVO FUNCTIONS
// ============================================

void setupServo() {
  Serial.println("Initializing servo...");

  ESP32PWM::allocateTimer(0);
  lockServo.setPeriodHertz(50);

  // Move to unlocked position, then detach to stop buzzing
  setServoPosition(SERVO_UNLOCKED);

  Serial.println("Servo initialized at UNLOCKED position");
}

void setServoPosition(int angle) {
  Serial.print("Servo moving to: ");
  Serial.println(angle);

  lockServo.attach(SERVO_PIN, 500, 2400);  // Attach before moving
  lockServo.write(angle);
  delay(500);  // Wait for servo to reach position
  lockServo.detach();  // Detach to stop buzzing and save power
}

// ============================================
// LOCK/UNLOCK FUNCTIONS
// ============================================

void lockBox(int minutes) {
  if (currentState == STATE_LOW_BATTERY) {
    Serial.println("Cannot lock: Low battery");
    return;
  }

  // Check if lid is closed before locking
  if (!lidClosed) {
    Serial.print("Lid open - queueing lock for ");
    Serial.print(minutes);
    Serial.println(" minutes");
    pendingLockMinutes = minutes;
    showPendingLockMessage();
    return;
  }

  // Clear any pending lock
  pendingLockMinutes = 0;

  Serial.print("Locking box for ");
  Serial.print(minutes);
  Serial.println(" minutes");

  // Timed lock mode
  isIndefiniteLock = false;

  // Calculate unlock time
  lockEndTime = millis() + (unsigned long)minutes * 60 * 1000;

  // Calculate Unix end time for persistence (if time synced)
  if (timeSynced) {
    lockEndUnixTime = getCurrentTimestamp() + ((uint32_t)minutes * 60);
  } else {
    // Store duration as fallback - will need time sync to validate
    lockEndUnixTime = (uint32_t)minutes * 60;  // Store just the duration
  }

  // Persist lock state to NVS
  saveLockState();

  // Move servo to locked position
  setServoPosition(SERVO_LOCKED);

  // Update state
  currentState = STATE_LOCKED;

  // Update display
  updateDisplay();

  // Set event and notify connected device (with duration in details)
  setLastEventWithDetails("locked", (uint16_t)minutes);
}

// Lock the box indefinitely (LOCK:0 command)
// Only unlocks when app sends UNLOCK or FORCE_UNLOCK
void lockBoxIndefinite() {
  if (currentState == STATE_LOW_BATTERY) {
    Serial.println("Cannot lock: Low battery");
    return;
  }

  // Check if lid is closed before locking
  if (!lidClosed) {
    Serial.println("Lid open - queueing indefinite lock");
    pendingLockMinutes = -1;  // Use -1 to indicate indefinite
    showPendingLockMessage();
    return;
  }

  // Clear any pending lock
  pendingLockMinutes = 0;

  Serial.println("Locking box indefinitely (no timer)");

  // Indefinite lock mode
  isIndefiniteLock = true;

  // Set lockEndTime to max value (prevents auto-unlock)
  lockEndTime = ULONG_MAX;
  lockEndUnixTime = 0;  // 0 indicates indefinite lock

  // Persist lock state to NVS
  saveLockState();

  // Move servo to locked position
  setServoPosition(SERVO_LOCKED);

  // Update state
  currentState = STATE_LOCKED;

  // Update display
  updateDisplay();

  // Set event and notify connected device (details=0 for indefinite)
  setLastEventWithDetails("locked", 0);
}

// Show message when lock is queued waiting for lid close
void showPendingLockMessage() {
  Serial.println("Showing pending lock message");

  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Battery indicator
    drawBatteryIndicator(140, 5, batteryPercent);

    // Unlocked icon centered
    int iconX = (200 - 48) / 2;
    drawUnlockedLock(iconX, 50, 48);

    // Message below
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(30, 130);
    display.print("Close lid to");
    display.setCursor(25, 155);
    display.print("start session");
  } while (display.nextPage());

  display.hibernate();
}

void unlockBox() {
  Serial.println("Unlocking box - entering READY state");

  // Move servo to unlocked position (lid free in READY state)
  setServoPosition(SERVO_UNLOCKED);

  // Reset timing and lock mode
  lockEndTime = 0;
  isIndefiniteLock = false;

  // Clear persisted lock state
  clearLockState();

  // Clear keyholder message on unlock (goes back to default screen)
  strcpy(displayText, "Keypr");

  // Update state
  currentState = STATE_READY;

  // Update display
  updateDisplay();

  // Set event and notify connected device
  setLastEvent("unlocked");
}

// Show "Cannot Unlock" error using the error queue system
// Throttled to show only once per 10 seconds
void showCannotUnlockMessage() {
  Serial.println("Cannot unlock - device is locked");

  // Throttle: only show once per CANNOT_UNLOCK_THROTTLE ms
  unsigned long now = millis();
  if (now - lastCannotUnlockTime < CANNOT_UNLOCK_THROTTLE) {
    Serial.println("Cannot unlock message throttled");
    return;
  }
  lastCannotUnlockTime = now;

  // Queue the error - it will auto-dismiss after 3 seconds
  queueError("Cannot Unlock");
}

// ============================================
// REED SWITCH / LID DETECTION
// ============================================

void setupReedSwitch() {
  Serial.println("Initializing reed switch...");
  pinMode(REED_SWITCH, INPUT_PULLUP);

  // Read initial state
  lidClosed = (digitalRead(REED_SWITCH) == LOW);
  Serial.print("Initial lid state: ");
  Serial.println(lidClosed ? "CLOSED" : "OPEN");
}

void checkLidState() {
  bool currentLidState = (digitalRead(REED_SWITCH) == LOW);

  // Detect lid state change
  if (currentLidState != lidClosed) {
    if (currentLidState) {
      Serial.println("LID: CLOSED (magnet detected)");
      setLastEvent("lid_closed");
    } else {
      Serial.println("LID: OPEN (magnet not detected)");
      setLastEvent("lid_opened");
    }

    // Update lid state first for tamper detection
    lidClosed = currentLidState;

    // Check for tamper (lid opened while locked)
    checkTamper();

    // Check for pending lock when lid closes
    if (currentLidState && pendingLockMinutes != 0) {
      if (pendingLockMinutes == -1) {
        Serial.println("Lid closed - activating pending indefinite lock");
        pendingLockMinutes = 0;
        lockBoxIndefinite();
      } else {
        Serial.println("Lid closed - activating pending timed lock");
        int minutes = pendingLockMinutes;
        pendingLockMinutes = 0;
        lockBox(minutes);
      }
      return;
    }

    // Lid state changed - no need to refresh in simplified display
    // (lid status not shown on screens, will update on periodic refresh)
    return;
  }

  lidClosed = currentLidState;
}

// ============================================
// BUTTON HANDLING
// ============================================

void setupButton() {
  Serial.println("Initializing unlock button...");
  pinMode(UNLOCK_BTN, INPUT_PULLUP);
  Serial.println("Button ready on GPIO2");
}

void checkButton() {
  // Button is active LOW (pressed = LOW)
  if (digitalRead(UNLOCK_BTN) == LOW) {
    // Debounce
    if (millis() - lastButtonPress < DEBOUNCE_MS) {
      return;
    }
    lastButtonPress = millis();

    Serial.println("Button pressed!");

    // Record press for emergency unlock detection
    recordButtonPress();
    checkEmergencyUnlock();

    if (currentState == STATE_READY) {
      // In READY state, lid is free - button does nothing
      Serial.println("Button ignored in READY state (lid already free)");
    } else if (currentState == STATE_LOCKED) {
      // BLE-proxy model: button press handling depends on lock mode
      if (isIndefiniteLock) {
        // Indefinite lock - always deny local button, app must send UNLOCK
        Serial.println("Button denied - indefinite lock");
        setLastEvent("button_press_denied");
        showCannotUnlockMessage();
      } else if (millis() >= lockEndTime) {
        // Timed lock with timer expired - unlock
        Serial.println("Timer expired - unlocking");
        unlockBox();
      } else {
        // Timed lock with timer not expired - deny and notify
        Serial.println("Button denied - time remaining");
        setLastEvent("button_press_denied");
        showCannotUnlockMessage();
      }
    }
  }
}

// ============================================
// BATTERY MONITORING
// ============================================

void checkBattery() {
  // TODO: Implement actual battery voltage reading
  // For now, just simulate
  // The ESP32C3 ADC can read battery voltage through a voltage divider

  // Placeholder - you'll need to add a voltage divider circuit
  // to read battery level. For now, assume full battery.
  batteryPercent = 100;

  // Check for low battery condition
  if (batteryPercent < 10 && currentState == STATE_READY) {
    currentState = STATE_LOW_BATTERY;
    updateDisplay();
  }
}

// ============================================
// PERSISTENT STORAGE (NVS)
// ============================================

void loadSettings() {
  Serial.println("Loading settings from NVS...");
  preferences.begin("keypr", true);  // Read-only mode

  // Load device key
  String key = preferences.getString("device_key", "");
  if (key.length() > 0) {
    strncpy(deviceKey, key.c_str(), 127);
    Serial.println("Device key loaded");
  }

  // Load tamper alert state
  tamperAlert = preferences.getBool("tamper", false);

  // Load lock state
  bool lockActive = preferences.getBool("lock_active", false);
  if (lockActive) {
    lockEndUnixTime = preferences.getULong("lock_end_unix", 0);
    isIndefiniteLock = preferences.getBool("lock_indefinite", false);
    currentState = STATE_LOCKED;
    // Set lockEndTime to max until time syncs (prevents premature unlock)
    lockEndTime = ULONG_MAX;
    if (isIndefiniteLock) {
      Serial.println("Lock state restored (indefinite)");
    } else {
      Serial.print("Lock state restored, end Unix: ");
      Serial.println(lockEndUnixTime);
    }
  }

  preferences.end();
}

void saveDeviceKey() {
  preferences.begin("keypr", false);
  preferences.putString("device_key", deviceKey);
  preferences.end();
  Serial.println("Device key saved");
}

void saveTamperState() {
  preferences.begin("keypr", false);
  preferences.putBool("tamper", tamperAlert);
  preferences.end();
}

void saveLockState() {
  preferences.begin("keypr", false);
  preferences.putBool("lock_active", true);
  preferences.putULong("lock_end_unix", lockEndUnixTime);
  preferences.putBool("lock_indefinite", isIndefiniteLock);
  preferences.end();
  if (isIndefiniteLock) {
    Serial.println("Lock state saved (indefinite)");
  } else {
    Serial.print("Lock state saved, expires at Unix: ");
    Serial.println(lockEndUnixTime);
  }
}

void clearLockState() {
  preferences.begin("keypr", false);
  preferences.putBool("lock_active", false);
  preferences.putULong("lock_end_unix", 0);
  preferences.putBool("lock_indefinite", false);
  preferences.end();
  lockEndUnixTime = 0;
  isIndefiniteLock = false;
  Serial.println("Lock state cleared");
}

// ============================================
// EVENT BUFFERING FUNCTIONS
// ============================================

// Check if event type is critical (persisted to NVS)
bool isCriticalEvent(uint8_t type) {
  return (type == EVT_EMERGENCY_UNLOCK || type == EVT_TAMPER_DETECTED);
}

// Get current timestamp (Unix time if synced, millis if not)
uint32_t getCurrentTimestamp() {
  if (timeSynced) {
    // Calculate Unix timestamp from sync point
    unsigned long elapsed = millis() - syncMillis;
    return syncUnixTime + (elapsed / 1000);
  } else {
    // Return millis (needs correction by app)
    return millis();
  }
}

// Convert event type code to string
const char* eventTypeToString(uint8_t type) {
  switch (type) {
    case EVT_BUTTON_PRESS_DENIED:   return "button_press_denied";
    case EVT_BUTTON_PRESS_ACCEPTED: return "button_press_accepted";
    case EVT_LID_OPENED:            return "lid_opened";
    case EVT_LID_CLOSED:            return "lid_closed";
    case EVT_LOCKED:                return "locked";
    case EVT_UNLOCKED:              return "unlocked";
    case EVT_EMERGENCY_UNLOCK:      return "emergency_unlock";
    case EVT_TAMPER_DETECTED:       return "tamper_detected";
    case EVT_BATTERY_WARNING:       return "battery_warning";
    case EVT_TIMER_EXPIRED:         return "timer_expired";
    case EVT_OFFLINE_FALLBACK_STARTED: return "offline_fallback_started";
    case EVT_OFFLINE_FALLBACK_UNLOCK:  return "offline_fallback_unlock";
    default: return "unknown";
  }
}

// Buffer an event (and persist to NVS if critical)
void bufferEvent(uint8_t type, uint16_t details) {
  // Drop oldest if buffer full
  if (eventCount >= MAX_BUFFERED_EVENTS) {
    memmove(&eventBuffer[0], &eventBuffer[1],
            (MAX_BUFFERED_EVENTS - 1) * sizeof(BufferedEvent));
    eventCount--;
    droppedCount++;
    Serial.println("Event buffer full, dropped oldest event");
  }

  // Create new event
  BufferedEvent* evt = &eventBuffer[eventCount];
  evt->seq = nextSeq++;
  evt->type = type;
  evt->timestamp = getCurrentTimestamp();
  evt->needsCorrection = !timeSynced;
  evt->persisted = false;
  evt->details = details;

  eventCount++;

  Serial.print("Buffered event: ");
  Serial.print(eventTypeToString(type));
  Serial.print(" seq=");
  Serial.print(evt->seq);
  Serial.print(" ts=");
  Serial.println(evt->timestamp);

  // Persist critical events to NVS
  if (isCriticalEvent(type)) {
    persistCriticalEvent(evt);
  }
}

// Persist critical event to NVS flash
void persistCriticalEvent(BufferedEvent* event) {
  preferences.begin("keypr_evt", false);

  // Get current write index (circular buffer 0-9)
  uint8_t idx = preferences.getUChar("evt_idx", 0);

  // Build JSON for this event
  StaticJsonDocument<128> doc;
  doc["seq"] = event->seq;
  doc["type"] = event->type;
  doc["ts"] = event->timestamp;
  doc["nc"] = event->needsCorrection;
  doc["det"] = event->details;

  String jsonStr;
  serializeJson(doc, jsonStr);

  // Store to NVS
  char key[16];
  sprintf(key, "evt_%d", idx);
  preferences.putString(key, jsonStr);

  // Advance circular buffer index
  preferences.putUChar("evt_idx", (idx + 1) % MAX_CRITICAL_EVENTS);

  preferences.end();

  event->persisted = true;

  Serial.print("Persisted critical event to NVS slot ");
  Serial.println(idx);
}

// Load critical events from NVS on boot
void loadCriticalEventsFromNVS() {
  preferences.begin("keypr_evt", true);  // Read-only

  int loadedCount = 0;
  for (int i = 0; i < MAX_CRITICAL_EVENTS; i++) {
    char key[16];
    sprintf(key, "evt_%d", i);
    String jsonStr = preferences.getString(key, "");

    if (jsonStr.length() > 0) {
      StaticJsonDocument<128> doc;
      DeserializationError err = deserializeJson(doc, jsonStr);

      if (!err) {
        // Add to buffer if not full
        if (eventCount < MAX_BUFFERED_EVENTS) {
          BufferedEvent* evt = &eventBuffer[eventCount];
          evt->seq = doc["seq"];
          evt->type = doc["type"];
          evt->timestamp = doc["ts"];
          evt->needsCorrection = doc["nc"];
          evt->persisted = true;
          evt->details = doc["det"];

          // Update nextSeq to be higher than any loaded event
          if (evt->seq >= nextSeq) {
            nextSeq = evt->seq + 1;
          }

          eventCount++;
          loadedCount++;
        }
      }
    }
  }

  preferences.end();

  if (loadedCount > 0) {
    Serial.print("Loaded ");
    Serial.print(loadedCount);
    Serial.println(" critical events from NVS");
  }
}

// Clear persisted events up to sequence number
void clearPersistedEvents(uint16_t upToSeq) {
  preferences.begin("keypr_evt", false);

  for (int i = 0; i < MAX_CRITICAL_EVENTS; i++) {
    char key[16];
    sprintf(key, "evt_%d", i);
    String jsonStr = preferences.getString(key, "");

    if (jsonStr.length() > 0) {
      StaticJsonDocument<128> doc;
      DeserializationError err = deserializeJson(doc, jsonStr);

      if (!err) {
        uint16_t seq = doc["seq"];
        if (seq <= upToSeq) {
          preferences.remove(key);
          Serial.print("Cleared persisted event seq ");
          Serial.println(seq);
        }
      }
    }
  }

  preferences.end();
}

// ============================================
// OTA UPDATE FUNCTIONS (per SHARED_API_CONTRACT.md)
// ============================================

// CRC32 lookup table
static const uint32_t crc32_table[256] PROGMEM = {
  0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
  0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
  0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
  0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
  0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
  0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
  0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
  0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
  0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
  0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
  0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
  0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
  0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
  0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
  0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
  0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
  0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
  0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
  0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
  0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
  0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
  0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
  0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
  0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
  0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
  0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
  0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
  0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
  0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
  0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
  0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD706B3, 0x54DE5729, 0x23D967BF,
  0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

// Update running CRC32 with new data
uint32_t updateCRC32(uint32_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    crc = pgm_read_dword(&crc32_table[(crc ^ data[i]) & 0xFF]) ^ (crc >> 8);
  }
  return crc;
}

// Notify OTA status to app via BLE
void notifyOTAStatus(const char* status) {
  if (pOTAControlChar) {
    pOTAControlChar->setValue(status);
    pOTAControlChar->notify();
    Serial.print("OTA notify: ");
    Serial.println(status);
  }
}

// Start OTA update
void startOTA(uint32_t size, uint32_t crc, uint32_t resumeOffset) {
  Serial.print("OTA: Starting, size=");
  Serial.print(size);
  Serial.print(", CRC=");
  Serial.print(crc, HEX);
  Serial.print(", resume=");
  Serial.println(resumeOffset);

  // Check preconditions
  if (currentState == STATE_LOCKED) {
    abortOTA(OTA_ERR_DEVICE_LOCKED, "Device is locked");
    return;
  }

  if (batteryPercent < OTA_MIN_BATTERY) {
    abortOTA(OTA_ERR_LOW_BATTERY, "Battery too low");
    return;
  }

  // Check available flash space
  if (!Update.begin(size)) {
    abortOTA(OTA_ERR_FLASH_SPACE, "Not enough flash space");
    return;
  }

  // Initialize OTA state
  otaState = OTA_RECEIVING;
  otaTotalSize = size;
  otaExpectedCRC = crc;
  otaBytesReceived = resumeOffset;
  otaResumeOffset = resumeOffset;
  otaLastChunk = resumeOffset / OTA_CHUNK_SIZE;
  otaCalculatedCRC = 0xFFFFFFFF;  // CRC32 initial value
  otaStartTime = millis();
  otaLastChunkTime = millis();
  otaErrorCode[0] = '\0';
  otaErrorMsg[0] = '\0';

  // Update display
  updateDisplay();

  // Notify ready
  notifyOTAStatus("OTA_READY");
}

// Abort OTA with error
void abortOTA(const char* errorCode, const char* errorMsg) {
  Serial.print("OTA: Abort - ");
  Serial.print(errorCode);
  Serial.print(": ");
  Serial.println(errorMsg);

  // End update if in progress
  if (otaState == OTA_RECEIVING || otaState == OTA_VERIFYING) {
    Update.abort();
  }

  // Set error state
  otaState = OTA_ERROR;
  strncpy(otaErrorCode, errorCode, 7);
  otaErrorCode[7] = '\0';
  strncpy(otaErrorMsg, errorMsg, 63);
  otaErrorMsg[63] = '\0';

  // Notify error
  char errorNotify[80];
  snprintf(errorNotify, sizeof(errorNotify), "OTA_ERROR:%s:%s", errorCode, errorMsg);
  notifyOTAStatus(errorNotify);

  // Update display
  updateDisplay();

  // Reset to idle after a delay (done in main loop)
}

// Process received OTA chunk
void processOTAChunk(const uint8_t* data, size_t len) {
  if (otaState != OTA_RECEIVING) {
    return;
  }

  // Calculate expected chunk number
  uint32_t expectedChunk = otaBytesReceived / OTA_CHUNK_SIZE;

  // Write chunk to flash
  size_t written = Update.write((uint8_t*)data, len);
  if (written != len) {
    abortOTA(OTA_ERR_FLASH_WRITE, "Flash write failed");
    return;
  }

  // Update CRC
  otaCalculatedCRC = updateCRC32(otaCalculatedCRC, data, len);

  // Update progress
  otaBytesReceived += len;
  otaLastChunk = expectedChunk;
  otaLastChunkTime = millis();

  // Send ACK
  char ackMsg[32];
  snprintf(ackMsg, sizeof(ackMsg), "OTA_ACK:%lu", expectedChunk);
  notifyOTAStatus(ackMsg);

  // Send progress periodically (every 10 chunks)
  if (expectedChunk % 10 == 0 || otaBytesReceived >= otaTotalSize) {
    char progressMsg[32];
    snprintf(progressMsg, sizeof(progressMsg), "OTA_PROGRESS:%lu", otaBytesReceived);
    notifyOTAStatus(progressMsg);
    updateDisplay();  // Update progress bar
  }

  Serial.print("OTA: Chunk ");
  Serial.print(expectedChunk);
  Serial.print(", ");
  Serial.print(otaBytesReceived);
  Serial.print("/");
  Serial.println(otaTotalSize);
}

// Verify OTA CRC
void verifyOTA() {
  if (otaState != OTA_RECEIVING) {
    Serial.println("OTA: Cannot verify - not in receiving state");
    return;
  }

  if (otaBytesReceived != otaTotalSize) {
    char errMsg[48];
    snprintf(errMsg, sizeof(errMsg), "Incomplete: %lu/%lu bytes", otaBytesReceived, otaTotalSize);
    abortOTA(OTA_ERR_INVALID_FW, errMsg);
    return;
  }

  otaState = OTA_VERIFYING;
  updateDisplay();

  // Finalize CRC
  uint32_t finalCRC = otaCalculatedCRC ^ 0xFFFFFFFF;

  Serial.print("OTA: CRC expected=");
  Serial.print(otaExpectedCRC, HEX);
  Serial.print(", calculated=");
  Serial.println(finalCRC, HEX);

  if (finalCRC != otaExpectedCRC) {
    char errMsg[48];
    snprintf(errMsg, sizeof(errMsg), "%08lX:%08lX", otaExpectedCRC, finalCRC);
    notifyOTAStatus((String("OTA_VERIFY_FAIL:") + errMsg).c_str());
    abortOTA(OTA_ERR_CRC_MISMATCH, "CRC mismatch");
    return;
  }

  notifyOTAStatus("OTA_VERIFY_OK");
  Serial.println("OTA: Verification passed");
}

// Apply OTA update
void applyOTA() {
  if (otaState != OTA_VERIFYING) {
    Serial.println("OTA: Cannot apply - not verified");
    return;
  }

  otaState = OTA_APPLYING;
  updateDisplay();

  // End update (mark partition as bootable)
  if (!Update.end(true)) {
    abortOTA(OTA_ERR_FLASH_WRITE, "Failed to finalize update");
    return;
  }

  // Save flag that we need OTA confirmation after reboot
  preferences.begin("keypr", false);
  preferences.putBool("ota_pending", true);
  preferences.end();

  Serial.println("OTA: Update applied, rebooting...");
  notifyOTAStatus("OTA_COMPLETE");

  delay(500);  // Give time for notification
  ESP.restart();
}

// Confirm OTA (called after successful boot)
void confirmOTA() {
  Serial.println("OTA: Confirming firmware");

  // Mark current partition as valid
  esp_ota_mark_app_valid_cancel_rollback();

  // Clear pending flag
  preferences.begin("keypr", false);
  preferences.putBool("ota_pending", false);
  preferences.end();

  otaPendingValidation = false;
  otaState = OTA_IDLE;

  notifyOTAStatus("OTA_CONFIRMED");
  Serial.println("OTA: Firmware confirmed valid");
}

// Check if OTA confirmation is pending (called on boot)
void checkOTAPendingValidation() {
  preferences.begin("keypr", true);  // Read-only
  otaPendingValidation = preferences.getBool("ota_pending", false);
  preferences.end();

  if (otaPendingValidation) {
    Serial.println("OTA: Pending validation - waiting for OTA_CONFIRM");
  }
}

// Check OTA timeout (called in main loop)
void checkOTATimeout() {
  if (otaState == OTA_RECEIVING) {
    unsigned long now = millis();
    if (now - otaLastChunkTime > OTA_TIMEOUT_MS) {
      abortOTA(OTA_ERR_TIMEOUT, "Chunk timeout");
    }
  } else if (otaState == OTA_ERROR) {
    // Reset to idle after error display
    static unsigned long errorDisplayStart = 0;
    if (errorDisplayStart == 0) {
      errorDisplayStart = millis();
    } else if (millis() - errorDisplayStart > 5000) {
      otaState = OTA_IDLE;
      otaErrorCode[0] = '\0';
      otaErrorMsg[0] = '\0';
      errorDisplayStart = 0;
      updateDisplay();
    }
  }
}

// ============================================
// EMERGENCY UNLOCK
// ============================================

void checkEmergencyUnlock() {
  // Check if we have 5 presses within 3 seconds
  unsigned long now = millis();
  unsigned long oldestPress = emergencyPresses[emergencyPressIndex];

  if (oldestPress > 0 && (now - oldestPress) < EMERGENCY_PRESS_WINDOW_MS) {
    // All 5 presses within window - trigger emergency unlock
    triggerEmergencyUnlock(0);  // method=0 (button)
  }
}

void recordButtonPress() {
  emergencyPresses[emergencyPressIndex] = millis();
  emergencyPressIndex = (emergencyPressIndex + 1) % EMERGENCY_PRESS_COUNT;
}

// method: 0=button, 1=app (FORCE_UNLOCK command)
void triggerEmergencyUnlock(uint8_t method) {
  Serial.print("!!! EMERGENCY UNLOCK TRIGGERED (method=");
  Serial.print(method);
  Serial.println(") !!!");

  // Reset emergency press tracking
  for (int i = 0; i < EMERGENCY_PRESS_COUNT; i++) {
    emergencyPresses[i] = 0;
  }
  emergencyPressIndex = 0;

  // Immediately unlock - go to READY state (lid free)
  setServoPosition(SERVO_UNLOCKED);
  currentState = STATE_READY;
  lockEndTime = 0;
  isIndefiniteLock = false;
  clearLockState();

  // Show emergency message on display using error queue
  queueError("Emergency Unlock");

  // Set event and notify BLE (app will report to API via proxy)
  // method in details: 0=button, 1=app
  setLastEventWithDetails("emergency_unlock", method);
}

// ============================================
// TAMPER DETECTION
// ============================================

void checkTamper() {
  // Tamper = lid opened while in LOCKED state
  if (currentState == STATE_LOCKED && !lidClosed && !tamperAlert) {
    Serial.println("!!! TAMPER DETECTED - Lid opened while locked !!!");
    tamperAlert = true;
    tamperAlertDisplayed = false;
    saveTamperState();
    showTamperAlert();
    // Set event and buffer it (critical event - persisted to NVS)
    // App receives via BLE notification and proxies to API
    setLastEvent("tamper_detected");
  }
}

void showTamperAlert() {
  if (tamperAlertDisplayed) return;

  Serial.println("Showing tamper alert on display");

  // Use error queue for tamper alert
  queueError("Tamper Detected");

  tamperAlertDisplayed = true;
}

void clearTamperAlert() {
  if (!tamperAlert) return;

  Serial.println("Clearing tamper alert");
  tamperAlert = false;
  tamperAlertDisplayed = false;
  saveTamperState();

  // Remove tamper error from queue if present
  for (int i = 0; i < errorCount; i++) {
    if (strcmp(errorQueue[i].text, "Tamper Detected") == 0) {
      clearError(i);
      break;
    }
  }

  updateDisplay();
  notifyBLEStatus();
}

// ============================================
// MAIN SETUP
// ============================================

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for serial

  Serial.println();
  Serial.println("================================");
  Serial.println("  Keypr Firmware v1.0.0");
  Serial.println("  ESP32-C3-DevKitC-02");
  Serial.println("================================");
  Serial.println();

  // Load saved settings first
  Serial.println("Loading settings...");
  loadSettings();
  Serial.println("Settings done.");

  // Load critical events from NVS (tamper, emergency unlock)
  Serial.println("Loading critical events...");
  loadCriticalEventsFromNVS();
  Serial.println("Critical events done.");

  // Check for pending OTA validation (after firmware update)
  Serial.println("Checking OTA validation...");
  checkOTAPendingValidation();
  Serial.println("OTA check done.");

  // Initialize components
  Serial.println("Starting BLE...");
  setupBLE();
  Serial.println("BLE done.");

  Serial.println("Starting Servo...");
  setupServo();
  // If lock state was restored, engage servo in locked position
  if (currentState == STATE_LOCKED) {
    Serial.println("Lock restored - engaging servo");
    setServoPosition(SERVO_LOCKED);
  }
  Serial.println("Servo done.");

  Serial.println("Starting Reed Switch...");
  setupReedSwitch();
  // In new model: READY = servo unlocked (lid free), LOCKED = servo locked
  // Servo is already set correctly based on state from setupServo and lock restore
  Serial.println("Reed Switch done.");

  Serial.println("Starting Button...");
  setupButton();
  Serial.println("Button done.");

  Serial.println("Starting Display...");
  setupDisplay();
  Serial.println("Display done.");

  // Show splash screen first
  showScreen(SCREEN_SPLASH);
  delay(1500);  // Show splash for 1.5 seconds

  // Then show appropriate screen based on configuration state
  if (tamperAlert) {
    showTamperAlert();
  } else {
    updateDisplay();  // Will show setup screen if unconfigured, or default/message screen
  }

  Serial.println();
  Serial.println("Setup complete!");
  Serial.print("BLE Name: Keypr | MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Device Key: ");
  Serial.println(strlen(deviceKey) > 0 ? "Configured" : "Not configured");
  if (otaPendingValidation) {
    Serial.println("OTA: Pending validation - waiting for OTA_CONFIRM");
  }
  Serial.println("Waiting for connections...");
  Serial.println();
}

// ============================================
// MAIN LOOP
// ============================================

void loop() {
  unsigned long now = millis();

  // Check for factory reset (button + lid open while unlocked)
  checkFactoryReset();

  // Check button presses (skip if factory reset in progress)
  if (!factoryResetInProgress) {
    checkButton();
  }

  // Check lid state changes
  checkLidState();

  // Check if lock time has expired (timed locks only, not indefinite)
  if (currentState == STATE_LOCKED && !isIndefiniteLock && now >= lockEndTime) {
    // Timer expired - auto-unlock
    Serial.println("Lock time expired - unlocking");
    unlockBox();
  }

  // Check error queue cycling (auto-dismiss/cycle errors)
  checkErrorCycle();

  // Periodic display update (for countdown) - full refresh on simplified system
  if (currentState == STATE_LOCKED && !isIndefiniteLock &&
      (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL)) {
    updateDisplay();  // Update countdown display
    lastDisplayUpdate = now;
  }

  // Periodic full display refresh to prevent e-ink ghosting (every 5 minutes)
  if (now - lastFullRefresh >= FULL_REFRESH_INTERVAL) {
    Serial.println("Periodic full display refresh");
    updateDisplayFull();
    lastFullRefresh = now;
  }

  // Periodic BLE notifications
  if (deviceConnected && (now - lastBLENotify >= BLE_NOTIFY_INTERVAL)) {
    notifyBLEStatus();
    lastBLENotify = now;
  }

  // Check battery periodically
  static unsigned long lastBatteryCheck = 0;
  if (now - lastBatteryCheck >= 60000) {  // Every minute
    checkBattery();
    lastBatteryCheck = now;
  }

  // Check OTA timeout (if update in progress)
  checkOTATimeout();

  // State change detection - update display
  if (currentState != previousState) {
    // Don't overwrite error display (errors take priority)
    if (errorCount == 0) {
      updateDisplay();
    }
    previousState = currentState;
  }

  // Small delay to prevent tight loop
  delay(50);
}
