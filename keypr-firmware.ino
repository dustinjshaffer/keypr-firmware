/*
 * Keypr Firmware
 * ESP32-C3-DevKitC-02 v1.1
 *
 * Components:
 * - BLE server for phone communication
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
 *
 * Board: ESP32C3 Dev Module
 */

#include <NimBLEDevice.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <ESP32Servo.h>

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
// BLE UUIDs
// ============================================
#define SERVICE_UUID           "12345678-1234-1234-1234-123456789abc"
#define CHAR_LOCK_STATUS_UUID  "12345678-1234-1234-1234-123456789ab1"  // Read/Notify
#define CHAR_LOCK_CMD_UUID     "12345678-1234-1234-1234-123456789ab2"  // Write
#define CHAR_TIME_LEFT_UUID    "12345678-1234-1234-1234-123456789ab3"  // Read/Notify
#define CHAR_BATTERY_UUID      "12345678-1234-1234-1234-123456789ab4"  // Read
#define CHAR_DISPLAY_TEXT_UUID "12345678-1234-1234-1234-123456789ab5"  // Write - custom display text

// ============================================
// STATE MACHINE
// ============================================
enum BoxState {
  STATE_UNLOCKABLE,   // Can be unlocked by button press
  STATE_LOCKED,       // Locked until time expires, then becomes UNLOCKABLE
  STATE_UNLOCKING,    // Servo unlocked, waiting for lid open then close
  STATE_LOW_BATTERY
};

BoxState currentState = STATE_UNLOCKABLE;
BoxState previousState = STATE_UNLOCKABLE;

// ============================================
// GLOBAL VARIABLES
// ============================================

// Timing
unsigned long lockEndTime = 0;          // When the lock should open (millis)
unsigned long lastDisplayUpdate = 0;
unsigned long lastBLENotify = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 60000;  // Update display every minute
const unsigned long BLE_NOTIFY_INTERVAL = 5000;       // BLE notify every 5 seconds

// Hardware state
bool lidClosed = true;
bool deviceConnected = false;
int batteryPercent = 100;  // TODO: Implement actual battery reading
bool lidOpenedDuringUnlock = false;  // Track if lid was opened during unlock cycle
int pendingLockMinutes = 0;  // Queued lock time (0 = no pending lock)

// Custom display text (set via BLE)
char displayText[65] = "Keypr";  // Default text, max 64 chars + null

// Button debouncing
unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_MS = 200;

// E-Ink Display - Waveshare 1.54" V2 (SSD1681 controller)
// Trying GDEY0154D67 driver for newer panel revision
GxEPD2_BW<GxEPD2_154_GDEY0154D67, GxEPD2_154_GDEY0154D67::HEIGHT> display(
  GxEPD2_154_GDEY0154D67(EINK_CS, EINK_DC, EINK_RST, EINK_BUSY)
);

// Servo
Servo lockServo;

// BLE
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pLockStatusChar = nullptr;
NimBLECharacteristic* pLockCmdChar = nullptr;
NimBLECharacteristic* pTimeLeftChar = nullptr;
NimBLECharacteristic* pBatteryChar = nullptr;
NimBLECharacteristic* pDisplayTextChar = nullptr;

// ============================================
// DISPLAY LAYOUT CONSTANTS
// ============================================
// Screen is 200x200 pixels

// Header section (BLE, lid, battery)
#define HEADER_Y      0
#define HEADER_H      25

// Middle section (icon + system message)
#define MIDDLE_Y      25
#define MIDDLE_H      105
#define MIDDLE_DIVIDER_X  80  // Left column width for icon

// Bottom section (keyholder message)
#define BOTTOM_Y      130
#define BOTTOM_H      70

// ============================================
// FORWARD DECLARATIONS
// ============================================
void lockBox(int minutes);
void unlockBox();
void showPendingLockMessage();
void startUnlockCycle();
void completeUnlockCycle();
void showCannotUnlockMessage();
void notifyBLEStatus();
void updateDisplay();
void updateDisplayFull();
void refreshHeader();
void refreshMiddle();
void refreshBottom();
void drawHeader(int yOffset);
void drawMiddleSection(int yOffset);
void drawBottomSection(int yOffset);
void drawKeyIcon(int x, int y, int size);
void setServoPosition(int angle);
void drawLockedLock(int x, int y, int size);
void drawUnlockedLock(int x, int y, int size);
void drawLidIcon(int x, int y, bool closed);
void checkLidState();
void checkButton();

// ============================================
// BLE CALLBACKS
// ============================================

class ServerCallbacks : public NimBLEServerCallbacks {
public:
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    deviceConnected = true;
    Serial.println("BLE: Client connected");
    refreshHeader();  // Update BLE status icon
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    deviceConnected = false;
    Serial.print("BLE: Client disconnected, reason: ");
    Serial.println(reason);
    refreshHeader();  // Update BLE status icon
    // Restart advertising
    NimBLEDevice::startAdvertising();
  }
};

class LockCommandCallback : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      // Command format: "LOCK:minutes" or "UNLOCK"
      String cmd = String(value.c_str());
      cmd.trim();

      Serial.print("BLE: Received command: [");
      Serial.print(cmd);
      Serial.print("] len=");
      Serial.println(cmd.length());

      if (cmd.startsWith("LOCK:")) {
        String minutesStr = cmd.substring(5);
        minutesStr.trim();
        Serial.print("  Parsing minutes from: [");
        Serial.print(minutesStr);
        Serial.println("]");

        long minutes = minutesStr.toInt();
        Serial.print("  Parsed value: ");
        Serial.println(minutes);

        // Validate minutes (reasonable range: 1 to 525600 = 1 year)
        if (minutes > 0 && minutes <= 525600) {
          Serial.print("LOCK for ");
          Serial.print(minutes);
          Serial.println(" minutes");
          lockBox((int)minutes);
        } else {
          Serial.println("  Invalid minutes value!");
        }
      } else if (cmd == "UNLOCK") {
        // Only unlock if time has expired
        if (currentState == STATE_LOCKED && millis() >= lockEndTime) {
          unlockBox();
          Serial.println("UNLOCK (time expired)");
        } else if (currentState == STATE_UNLOCKABLE) {
          Serial.println("Already unlockable");
        } else {
          Serial.println("UNLOCK denied - time remaining");
        }
      } else if (cmd == "FORCE_UNLOCK") {
        // Emergency override - immediately go to UNLOCKABLE state
        unlockBox();
        Serial.println("FORCE_UNLOCK - now unlockable");
      } else if (cmd == "STATUS") {
        Serial.println("STATUS request");
        notifyBLEStatus();
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
    if (value.length() > 0 && value.length() <= 64) {
      strncpy(displayText, value.c_str(), 64);
      displayText[64] = '\0';
      Serial.print("Display text set to: ");
      Serial.println(displayText);
      refreshBottom();  // Only refresh the keyholder message section
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

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  // Lock Status Characteristic (Read/Notify)
  pLockStatusChar = pService->createCharacteristic(
    CHAR_LOCK_STATUS_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  pLockStatusChar->setValue("UNLOCKABLE");

  // Lock Command Characteristic (Write)
  pLockCmdChar = pService->createCharacteristic(
    CHAR_LOCK_CMD_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  pLockCmdChar->setCallbacks(new LockCommandCallback());

  // Time Remaining Characteristic (Read/Notify)
  pTimeLeftChar = pService->createCharacteristic(
    CHAR_TIME_LEFT_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  pTimeLeftChar->setValue("0");

  // Battery Level Characteristic (Read)
  pBatteryChar = pService->createCharacteristic(
    CHAR_BATTERY_UUID,
    NIMBLE_PROPERTY::READ
  );
  pBatteryChar->setValue(String(batteryPercent).c_str());

  // Display Text Characteristic (Write)
  pDisplayTextChar = pService->createCharacteristic(
    CHAR_DISPLAY_TEXT_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ
  );
  pDisplayTextChar->setValue(displayText);
  pDisplayTextChar->setCallbacks(new DisplayTextCallback());

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

void notifyBLEStatus() {
  if (!deviceConnected) return;

  // Update lock status
  switch (currentState) {
    case STATE_UNLOCKABLE:
      pLockStatusChar->setValue("UNLOCKABLE");
      break;
    case STATE_LOCKED:
      pLockStatusChar->setValue("LOCKED");
      break;
    case STATE_UNLOCKING:
      pLockStatusChar->setValue("UNLOCKING");
      break;
    case STATE_LOW_BATTERY:
      pLockStatusChar->setValue("LOW_BATTERY");
      break;
  }
  pLockStatusChar->notify();

  // Update time remaining
  if (currentState == STATE_LOCKED) {
    unsigned long remaining = (lockEndTime - millis()) / 1000;  // seconds
    pTimeLeftChar->setValue(String(remaining).c_str());
  } else {
    pTimeLeftChar->setValue("0");
  }
  pTimeLeftChar->notify();

  // Update battery
  pBatteryChar->setValue(String(batteryPercent).c_str());
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
  display.setRotation(0);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();

  Serial.print("BUSY after init: ");
  Serial.println(digitalRead(EINK_BUSY) ? "HIGH" : "LOW");

  Serial.println("Display initialized");
}

// ============================================
// SECTION-BASED DISPLAY SYSTEM
// ============================================

// Default update - refreshes only what changed (called by state machine)
void updateDisplay() {
  // For state changes, refresh middle section (icon + message changes)
  refreshMiddle();
}

// Full screen refresh - use sparingly (startup, major state changes)
void updateDisplayFull() {
  Serial.println("Full display refresh...");

  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);

    // Draw dividing lines only (no outer borders)
    display.drawLine(0, MIDDLE_Y, 200, MIDDLE_Y, GxEPD_BLACK);           // Header/Middle divider
    display.drawLine(MIDDLE_DIVIDER_X, MIDDLE_Y, MIDDLE_DIVIDER_X, BOTTOM_Y, GxEPD_BLACK);  // Middle column divider
    display.drawLine(0, BOTTOM_Y, 200, BOTTOM_Y, GxEPD_BLACK);           // Middle/Bottom divider

    // Draw all sections with proper Y offsets
    drawHeader(0);
    drawMiddleSection(MIDDLE_Y);
    drawBottomSection(BOTTOM_Y);

  } while (display.nextPage());

  display.hibernate();
  Serial.println("Full display complete");
}

// Partial refresh: Header only (BLE, lid, battery)
void refreshHeader() {
  Serial.println("Refresh header...");

  display.setPartialWindow(0, HEADER_Y, 200, HEADER_H + 1);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawLine(0, MIDDLE_Y, 200, MIDDLE_Y, GxEPD_BLACK);  // Bottom divider (absolute coords)
    drawHeader(HEADER_Y);  // Use absolute coordinates
  } while (display.nextPage());

  display.hibernate();
}

// Partial refresh: Middle section (icon + system message)
void refreshMiddle() {
  Serial.println("Refresh middle...");

  display.setPartialWindow(0, MIDDLE_Y, 200, MIDDLE_H + 1);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    // Draw dividers (absolute coordinates)
    display.drawLine(0, MIDDLE_Y, 200, MIDDLE_Y, GxEPD_BLACK);  // Top line
    display.drawLine(MIDDLE_DIVIDER_X, MIDDLE_Y, MIDDLE_DIVIDER_X, BOTTOM_Y, GxEPD_BLACK);  // Column divider
    display.drawLine(0, BOTTOM_Y, 200, BOTTOM_Y, GxEPD_BLACK);  // Bottom line
    drawMiddleSection(MIDDLE_Y);  // Use absolute coordinates
  } while (display.nextPage());

  display.hibernate();
}

// Partial refresh: Bottom section (keyholder message)
void refreshBottom() {
  Serial.println("Refresh bottom...");

  display.setPartialWindow(0, BOTTOM_Y, 200, BOTTOM_H);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawLine(0, BOTTOM_Y, 200, BOTTOM_Y, GxEPD_BLACK);  // Top divider (absolute coords)
    drawBottomSection(BOTTOM_Y);  // Use absolute coordinates
  } while (display.nextPage());

  display.hibernate();
}

// ============================================
// SECTION DRAWING FUNCTIONS
// ============================================

void drawHeader(int yOffset) {
  // BLE status (left)
  display.setFont();
  display.setCursor(5, yOffset + 8);
  display.print("BLE");
  if (deviceConnected) {
    display.fillCircle(30, yOffset + 12, 4, GxEPD_BLACK);
  } else {
    display.drawCircle(30, yOffset + 12, 4, GxEPD_BLACK);
  }

  // Lid icon (center-left)
  drawLidIcon(45, yOffset + 6, lidClosed);

  // Battery (right side)
  int batX = 145;
  int batY = yOffset + 5;
  display.drawRect(batX, batY, 25, 12, GxEPD_BLACK);
  display.fillRect(batX + 25, batY + 3, 3, 6, GxEPD_BLACK);
  int fillW = (21 * batteryPercent) / 100;
  display.fillRect(batX + 2, batY + 2, fillW, 8, GxEPD_BLACK);
  display.setCursor(batX - 25, yOffset + 8);
  display.print(String(batteryPercent) + "%");
}

void drawMiddleSection(int yOffset) {
  // Left column: Lock icon (centered in left column)
  int iconSize = 40;
  int iconX = (MIDDLE_DIVIDER_X - iconSize) / 2;
  int iconY = yOffset + 18;  // Push down from header divider

  if (currentState == STATE_LOCKED) {
    drawLockedLock(iconX, iconY, iconSize);
  } else {
    drawUnlockedLock(iconX, iconY, iconSize);
  }

  // Right column: System message
  int msgX = MIDDLE_DIVIDER_X + 8;
  int msgY = yOffset + 30;  // Baseline for first line of text (push down)

  switch (currentState) {
    case STATE_UNLOCKABLE:
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(msgX, msgY);
      display.print("Ready");
      display.setFont();
      display.setCursor(msgX, msgY + 16);
      display.print("Press button");
      display.setCursor(msgX, msgY + 28);
      display.print("to open");
      break;

    case STATE_LOCKED: {
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(msgX, msgY);
      display.print("LOCKED");

      // Calculate and display time remaining
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
      display.setCursor(msgX, msgY + 25);
      display.print(timeStr);
      break;
    }

    case STATE_UNLOCKING:
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(msgX, msgY);
      display.print("OPEN");
      display.setFont();
      display.setCursor(msgX, msgY + 16);
      display.print("Close lid");
      display.setCursor(msgX, msgY + 28);
      display.print("to lock");
      break;

    case STATE_LOW_BATTERY:
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(msgX, msgY);
      display.print("LOW");
      display.setCursor(msgX, msgY + 22);
      display.print("BATTERY");
      break;
  }
}

void drawBottomSection(int yOffset) {
  // Small key icon in top-left of section
  drawKeyIcon(5, yOffset + 8, 12);

  // Keyholder message with word wrapping
  display.setFont(&FreeMonoBold9pt7b);

  int textX = 22;
  int textY = yOffset + 22;
  int lineHeight = 18;
  int maxCharsPerLine = 15;  // Approximate for this font
  int maxLines = 3;

  String text = String(displayText);
  int currentLine = 0;
  int startIdx = 0;

  while (startIdx < (int)text.length() && currentLine < maxLines) {
    // Find the end of this line
    int endIdx = startIdx + maxCharsPerLine;
    if (endIdx >= (int)text.length()) {
      // Last chunk - just print it
      display.setCursor(textX, textY + (currentLine * lineHeight));
      display.print(text.substring(startIdx));
      break;
    }

    // Look backwards for a space to break on (word wrap)
    int breakIdx = endIdx;
    while (breakIdx > startIdx && text.charAt(breakIdx) != ' ') {
      breakIdx--;
    }

    // If no space found, force break at maxCharsPerLine
    if (breakIdx == startIdx) {
      breakIdx = endIdx;
    }

    // Print this line
    display.setCursor(textX, textY + (currentLine * lineHeight));
    display.print(text.substring(startIdx, breakIdx));

    // Move to next line, skip the space if we broke on one
    startIdx = breakIdx;
    if (startIdx < (int)text.length() && text.charAt(startIdx) == ' ') {
      startIdx++;
    }
    currentLine++;
  }
}

// Small key icon for keyholder message section
void drawKeyIcon(int x, int y, int size) {
  // Key head (circle with hole)
  int headR = size / 3;
  display.fillCircle(x + headR, y + headR, headR, GxEPD_BLACK);
  display.fillCircle(x + headR, y + headR, headR / 2, GxEPD_WHITE);

  // Key shaft
  int shaftLen = size - headR;
  int shaftH = size / 4;
  display.fillRect(x + headR, y + headR - shaftH/2, shaftLen, shaftH, GxEPD_BLACK);

  // Key teeth
  int toothW = size / 6;
  int toothH = size / 4;
  display.fillRect(x + size - toothW*2, y + headR + shaftH/2, toothW, toothH, GxEPD_BLACK);
  display.fillRect(x + size - toothW*4, y + headR + shaftH/2, toothW, toothH, GxEPD_BLACK);
}

// Draw a rounded rectangle (for lock body)
void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
  // Main body
  display.fillRect(x + r, y, w - 2*r, h, color);
  display.fillRect(x, y + r, w, h - 2*r, color);
  // Corners
  display.fillCircle(x + r, y + r, r, color);
  display.fillCircle(x + w - r - 1, y + r, r, color);
  display.fillCircle(x + r, y + h - r - 1, r, color);
  display.fillCircle(x + w - r - 1, y + h - r - 1, r, color);
}

// Draw an unlocked padlock icon (Bootstrap bi-unlock style)
// Same as locked but with left leg "cut" by white box
void drawUnlockedLock(int x, int y, int size) {
  // Draw the full locked lock first
  drawLockedLock(x, y, size);

  // Now cut through the left leg with a white rectangle to show "unlocked"
  float scale = size / 16.0;
  int shackleOuterL = x + (int)(4 * scale);  // Match wider shackle
  int shackleThick = (int)(2.0 * scale);  // Match thicker shackle
  if (shackleThick < 3) shackleThick = 3;

  // White box to cut through left leg (middle portion)
  int cutY = y + (int)(3 * scale);
  int cutH = (int)(3.5 * scale);
  display.fillRect(shackleOuterL - 1, cutY, shackleThick + 2, cutH, GxEPD_WHITE);
}

// Draw a locked padlock icon (Bootstrap bi-lock style)
// Based on 16x16 SVG viewBox, scaled to size
void drawLockedLock(int x, int y, int size) {
  float scale = size / 16.0;

  // Body: rounded rectangle from y=7 to y=15 in SVG
  int bodyX = x + (int)(2 * scale);
  int bodyY = y + (int)(7 * scale);
  int bodyW = (int)(12 * scale);
  int bodyH = (int)(8 * scale);
  int bodyR = (int)(1.5 * scale);
  if (bodyR < 1) bodyR = 1;

  fillRoundRect(bodyX, bodyY, bodyW, bodyH, bodyR, GxEPD_BLACK);

  // Shackle: closed U-shape with rounded top
  // Made 30% wider and thicker than original
  int shackleThick = (int)(2.0 * scale);  // Thicker
  if (shackleThick < 3) shackleThick = 3;

  // 30% wider: original was x=5 to x=11 (width 6), now ~width 8
  int shackleOuterL = x + (int)(4 * scale);
  int shackleOuterR = x + (int)(12 * scale);
  int shackleW = shackleOuterR - shackleOuterL;
  int shackleTopY = y + (int)(1 * scale);
  int shackleBotY = bodyY;

  // Left leg
  display.fillRect(shackleOuterL, shackleTopY + shackleW/2, shackleThick, shackleBotY - shackleTopY - shackleW/2, GxEPD_BLACK);

  // Right leg
  display.fillRect(shackleOuterR - shackleThick, shackleTopY + shackleW/2, shackleThick, shackleBotY - shackleTopY - shackleW/2, GxEPD_BLACK);

  // Top arc - draw outer semicircle, then cut out inner
  int arcCenterX = shackleOuterL + shackleW / 2;
  int arcCenterY = shackleTopY + shackleW / 2;
  int outerR = shackleW / 2;
  int innerR = outerR - shackleThick;

  // Draw outer filled semicircle (top half)
  display.fillCircle(arcCenterX, arcCenterY, outerR, GxEPD_BLACK);
  // Cut out inner circle
  if (innerR > 0) {
    display.fillCircle(arcCenterX, arcCenterY, innerR, GxEPD_WHITE);
  }
  // Cut out bottom half to leave only top arc
  display.fillRect(shackleOuterL, arcCenterY, shackleW, outerR + 1, GxEPD_WHITE);

  // Redraw the legs that might have been cut
  display.fillRect(shackleOuterL, arcCenterY, shackleThick, shackleBotY - arcCenterY, GxEPD_BLACK);
  display.fillRect(shackleOuterR - shackleThick, arcCenterY, shackleThick, shackleBotY - arcCenterY, GxEPD_BLACK);
}

// Draw lid status as text
void drawLidIcon(int x, int y, bool closed) {
  display.setFont();  // Small default font
  if (!closed) {
    // Show "Open" when lid is open
    display.setCursor(x, y);
    display.print("Open");
  }
  // Show nothing when closed
}

// Old screen functions removed - now using section-based drawing

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

  // Calculate unlock time
  lockEndTime = millis() + (unsigned long)minutes * 60 * 1000;

  // Move servo to locked position
  setServoPosition(SERVO_LOCKED);

  // Update state
  currentState = STATE_LOCKED;

  // Update display
  updateDisplay();

  // Notify connected device
  notifyBLEStatus();
}

// Show message when lock is queued waiting for lid close
void showPendingLockMessage() {
  Serial.println("Showing pending lock message");

  display.setPartialWindow(0, MIDDLE_Y, 200, MIDDLE_H + 1);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawLine(0, MIDDLE_Y, 200, MIDDLE_Y, GxEPD_BLACK);
    display.drawLine(MIDDLE_DIVIDER_X, MIDDLE_Y, MIDDLE_DIVIDER_X, BOTTOM_Y, GxEPD_BLACK);
    display.drawLine(0, BOTTOM_Y, 200, BOTTOM_Y, GxEPD_BLACK);

    // Left column: unlocked icon
    int iconSize = 40;
    int iconX = (MIDDLE_DIVIDER_X - iconSize) / 2;
    drawUnlockedLock(iconX, MIDDLE_Y + 18, iconSize);

    // Right column: pending message
    int msgX = MIDDLE_DIVIDER_X + 8;
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(msgX, MIDDLE_Y + 30);
    display.print("Close");
    display.setCursor(msgX, MIDDLE_Y + 50);
    display.print("lid to");
    display.setFont();
    display.setCursor(msgX, MIDDLE_Y + 70);
    display.print("start session");
  } while (display.nextPage());

  display.hibernate();
}

void unlockBox() {
  Serial.println("Unlocking box - entering UNLOCKABLE state");

  // Move servo to locked position (stays locked until button press)
  setServoPosition(SERVO_LOCKED);

  // Reset timing
  lockEndTime = 0;

  // Update state - now unlockable by button
  currentState = STATE_UNLOCKABLE;

  // Update display
  updateDisplay();

  // Notify connected device
  notifyBLEStatus();
}

// Called when button is pressed in UNLOCKABLE state
void startUnlockCycle() {
  Serial.println("Starting unlock cycle - open the lid");

  // Move servo to unlocked position
  setServoPosition(SERVO_UNLOCKED);

  // Track that we need lid to open then close
  lidOpenedDuringUnlock = false;

  // Update state
  currentState = STATE_UNLOCKING;

  // Update display
  updateDisplay();

  // Notify connected device
  notifyBLEStatus();
}

// Called when lid closes during UNLOCKING state
void completeUnlockCycle() {
  Serial.println("Unlock cycle complete - lid closed, locking servo");

  // Move servo back to locked position
  setServoPosition(SERVO_LOCKED);

  // Stay in UNLOCKABLE state (can unlock again with button)
  currentState = STATE_UNLOCKABLE;

  // Reset tracking
  lidOpenedDuringUnlock = false;

  // Update display
  updateDisplay();

  // Notify connected device
  notifyBLEStatus();
}

// Show temporary "Cannot Open" message in middle section
void showCannotUnlockMessage() {
  Serial.println("Cannot unlock - device is locked");

  // Partial refresh middle section only
  display.setPartialWindow(0, MIDDLE_Y, 200, MIDDLE_H + 1);
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    // Draw dividers (absolute coordinates)
    display.drawLine(0, MIDDLE_Y, 200, MIDDLE_Y, GxEPD_BLACK);
    display.drawLine(MIDDLE_DIVIDER_X, MIDDLE_Y, MIDDLE_DIVIDER_X, BOTTOM_Y, GxEPD_BLACK);
    display.drawLine(0, BOTTOM_Y, 200, BOTTOM_Y, GxEPD_BLACK);

    // Left column: Locked icon (absolute coordinates)
    int iconSize = 40;
    int iconX = (MIDDLE_DIVIDER_X - iconSize) / 2;
    drawLockedLock(iconX, MIDDLE_Y + 18, iconSize);

    // Right column: Cannot Open message (absolute coordinates)
    int msgX = MIDDLE_DIVIDER_X + 8;
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(msgX, MIDDLE_Y + 30);
    display.print("Cannot");
    display.setCursor(msgX, MIDDLE_Y + 55);
    display.print("Open!");

    display.setFont();
    display.setCursor(msgX, MIDDLE_Y + 75);
    display.print("Time remaining");
  } while (display.nextPage());

  display.hibernate();

  delay(1500);  // Show message briefly
  refreshMiddle();  // Return to normal display
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
    } else {
      Serial.println("LID: OPEN (magnet not detected)");
    }

    // Handle UNLOCKING state: wait for lid open, then close
    if (currentState == STATE_UNLOCKING) {
      if (!currentLidState && !lidOpenedDuringUnlock) {
        // Lid just opened during unlock cycle
        Serial.println("Lid opened - waiting for close to complete cycle");
        lidOpenedDuringUnlock = true;
      } else if (currentLidState && lidOpenedDuringUnlock) {
        // Lid just closed after being opened - complete the cycle
        lidClosed = currentLidState;
        completeUnlockCycle();
        return;
      }
    }

    // Check for pending lock when lid closes
    if (currentLidState && pendingLockMinutes > 0) {
      Serial.println("Lid closed - activating pending lock");
      lidClosed = currentLidState;
      int minutes = pendingLockMinutes;
      pendingLockMinutes = 0;  // Clear pending before calling lockBox
      lockBox(minutes);
      return;
    }

    // Update lid state and refresh header only (lid status is in header)
    lidClosed = currentLidState;
    refreshHeader();
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

    if (currentState == STATE_UNLOCKABLE) {
      // Can unlock - start the unlock cycle
      startUnlockCycle();
    } else if (currentState == STATE_LOCKED) {
      // Cannot unlock - show message
      showCannotUnlockMessage();
    } else if (currentState == STATE_UNLOCKING) {
      // Already unlocking - ignore
      Serial.println("Already in unlock cycle");
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
  if (batteryPercent < 10 && currentState == STATE_UNLOCKABLE) {
    currentState = STATE_LOW_BATTERY;
    updateDisplay();
  }
}

// ============================================
// MAIN SETUP
// ============================================

void setup() {
  Serial.begin(115200);
  delay(2000);  // Wait for serial

  Serial.println();
  Serial.println("================================");
  Serial.println("  Keypr Firmware v1.0");
  Serial.println("  ESP32-C3-DevKitC-02");
  Serial.println("================================");
  Serial.println();

  // Initialize components
  Serial.println("Starting BLE...");
  setupBLE();
  Serial.println("BLE done.");

  Serial.println("Starting Servo...");
  setupServo();
  Serial.println("Servo done.");

  Serial.println("Starting Reed Switch...");
  setupReedSwitch();
  Serial.println("Reed Switch done.");

  Serial.println("Starting Button...");
  setupButton();
  Serial.println("Button done.");

  Serial.println("Starting Display...");
  setupDisplay();
  Serial.println("Display done.");

  // Initial display update - full refresh on startup
  updateDisplayFull();

  Serial.println();
  Serial.println("Setup complete!");
  Serial.println("BLE Name: Keypr");
  Serial.println("Waiting for connections...");
  Serial.println();
}

// ============================================
// MAIN LOOP
// ============================================

void loop() {
  unsigned long now = millis();

  // Check button presses
  checkButton();

  // Check lid state changes
  checkLidState();

  // Check if lock time has expired
  if (currentState == STATE_LOCKED && now >= lockEndTime) {
    Serial.println("Lock time expired - entering UNLOCKABLE state");
    unlockBox();
  }

  // Periodic display update (for countdown) - use partial refresh on middle section
  if (currentState == STATE_LOCKED &&
      (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL)) {
    refreshMiddle();  // Partial refresh - timer is in middle section
    lastDisplayUpdate = now;
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

  // State change detection - update display
  if (currentState != previousState) {
    updateDisplay();
    previousState = currentState;
  }

  // Small delay to prevent tight loop
  delay(50);
}
