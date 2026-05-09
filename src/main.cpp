/**
 * @file main.cpp
 * @brief ESP32 LED Sign Controller for HUB75 Matrix Panels.
 * 
 * Hardware Layout:
 * - Panel Dimensions: 32x16 pixels
 * - Panel Chain: 3 panels (Total Resolution: 96x16)
 * - ESP32 Controller with custom pin mapping.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <time.h>

// Fonts
#include <Fonts/Picopixel.h>
#include <Fonts/Org_01.h>
#include <Fonts/TomThumb.h>
#include <Fonts/Tiny3x3a2pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansOblique9pt7b.h>
#include <Fonts/FreeSansBoldOblique9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoOblique9pt7b.h>
#include <Fonts/FreeMonoBoldOblique9pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <Fonts/FreeSerifItalic9pt7b.h>
#include <Fonts/FreeSerifBoldItalic9pt7b.h>
#include "FreeSans7pt7b.h"
#include "FreeSans6pt7b.h"
#include "FreeSansBold6pt7b.h"
#include "secrets.h"

// --- HARDWARE CONFIGURATION ---
#define PANEL_RES_X 32             ///< Individual panel width
#define PANEL_RES_Y 16             ///< Individual panel height
#define PANEL_CHAIN 3              ///< Number of panels chained together
#define PIN_R1 18
#define PIN_G1 25
#define PIN_B1 5
#define PIN_R2 17
#define PIN_G2 33
#define PIN_B2 16
#define PIN_A 4
#define PIN_B 3
#define PIN_C 0
#define PIN_D 21
#define PIN_E 32
#define PIN_LAT 19
#define PIN_OE 15
#define PIN_CLK 2

// --- SOFTWARE CONSTANTS ---
#define ENABLE_DEBUG 1             ///< Enable serial and WebSerial debug output
#define RUN_TEST_ON_BOOT 0         ///< Set to 0 to skip the test pattern on boot
#define BOOT_TEST_DURATION 10000   ///< How long to run test pattern on boot (ms)
#define FRAME_RATE_LIMIT 50        ///< Limit rendering to ~20 FPS (ms per frame)
#define FONT_CYCLE_SPEED 2000      ///< Time to show each font (ms)
#define MQTT_SERVER "mqtt.mccarthyinternet.net"
#define MQTT_PORT 1883
#define DEFAULT_TIMEZONE -5        ///< Default Timezone Offset (e.g. -5 for EST)

// --- DEBUG MACROS ---
#if ENABLE_DEBUG
  #define DEBUG_PRINT(x) { Serial.print(x); WebSerial.print(x); }
  #define DEBUG_PRINTLN(x) { Serial.println(x); WebSerial.println(x); }
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
#endif

// --- GLOBALS ---
MatrixPanel_I2S_DMA *display = nullptr;
AsyncWebServer server(80);
WiFiClient espClient;
PubSubClient mqtt(espClient);

// Font Structure
struct FontInfo {
  const GFXfont* font;
  String name;
};

const FontInfo fonts[] = {
  {nullptr, "Default 5x7"},
  {&Picopixel, "Picopixel"},
  {&Org_01, "Org_01"},
  {&TomThumb, "TomThumb"},
  {&Tiny3x3a2pt7b, "Tiny 3x3"},
  {&FreeSans6pt7b, "Sans 6pt "},
  {&FreeSansBold6pt7b, "Bold 6pt"},
  {&FreeSans7pt7b, "Sans 7pt"},
  {&FreeSans9pt7b, "Sans 9pt"},
  {&FreeSansBold9pt7b, "Sans Bold"},
  {&FreeSansOblique9pt7b, "Sans Obl"},
  {&FreeSansBoldOblique9pt7b, "Sans BO"},
  {&FreeMono9pt7b, "Mono 9pt"},
  {&FreeMonoBold9pt7b, "Mono Bo"},
  {&FreeMonoOblique9pt7b, "Mono Obl"},
  {&FreeMonoBoldOblique9pt7b, "Mono"},
  {&FreeSerif9pt7b, "Serif 9pt"},
  {&FreeSerifBold9pt7b, "Serif Bold"},
  {&FreeSerifItalic9pt7b, "Serif Ital"},
  {&FreeSerifBoldItalic9pt7b, "Serif BItal"}
};
const int fontCount = sizeof(fonts) / sizeof(fonts[0]);

// System State
String currentMode = RUN_TEST_ON_BOOT ? "test" : "bigClock";
uint16_t primaryColor = 0x001F;   ///< Primary text color (Blue default)
bool isDisplayOn = true;          ///< Global display toggle
bool countdownEnabled = true;     ///< Feature toggle for event countdown
bool messageEnabled = true;       ///< Feature toggle for messages
bool hasBooted = !RUN_TEST_ON_BOOT; ///< Tracks completion of boot sequence
bool forceCountdown = false;      ///< If true, show countdown regardless of time
unsigned long lastFrameTime = 0;   ///< Timing for frame rate control
int timeZoneOffset = DEFAULT_TIMEZONE; ///< Current offset in hours

// Content Buffers
String statusLine = "Sign Online";
String msgHeader = "", msgBody = "";
String line1 = "", line2 = "", line3 = "";
bool pendingCommand = false;

// Scrolling & Positioning
int16_t scrollPos = 0;
int16_t scrollWidth = 0;

// Event Data
bool activeEvent = false;
time_t eventTime = 0;
String eventSubject = "", eventHost = "", eventAttendees = "";

// Messaging Data
String messageType = "", messageSender = "";

// --- UTILITY FUNCTIONS ---

/**
 * @brief Updates the system timezone.
 * @param offsetHours Hours relative to UTC.
 */
void updateTimezone(int offsetHours) {
  timeZoneOffset = offsetHours;
  long offsetSeconds = (long)offsetHours * 3600;
  configTime(offsetSeconds, 0, "pool.ntp.org", "time.nist.gov");
  DEBUG_PRINTLN("Timezone updated to UTC" + String(offsetHours));
}

/**
 * @brief Parses a comma-separated RGB string into a 565 color value.
 */
static uint16_t parseColor(const String &msg) {
  int r = 0, g = 0, b = 0;
  if (sscanf(msg.c_str(), "%d,%d,%d", &r, &g, &b) == 3) {
    return display->color565(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
  }
  return 0x001F;
}

/**
 * @brief Formats current local time.
 */
static String getFormattedTime(const char *format) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) return "?";
  char buffer[64];
  strftime(buffer, sizeof(buffer), format, &timeinfo);
  return String(buffer);
}

// --- DRAWING HELPERS ---

/**
 * @brief Simple text drawing wrapper.
 */
static void drawLine(int16_t x, int16_t y, const String &text, uint16_t color = primaryColor) {
  display->setTextColor(color);
  display->setCursor(x, y);
  display->print(text);
}

/**
 * @brief Draws centered text on a specific Y coordinate.
 * Uses getTextBounds for accurate pixel-perfect centering.
 */
static void drawCentered(int16_t y, const String &text, uint16_t color = primaryColor) {
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = (display->width() - (int16_t)w) / 2;
  
  display->setTextColor(color);
  display->setCursor(x, y);
  display->print(text);
}

// --- MQTT HANDLER ---

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  DEBUG_PRINT("MQTT ["); DEBUG_PRINT(topic); DEBUG_PRINTLN("] " + msg);

  String t = String(topic);
  
  if (t == "ledSign/mode") {
    if (msg.equalsIgnoreCase("ON") || msg.equalsIgnoreCase("OFF")) {
      DEBUG_PRINTLN("Ignored power command on mode topic: " + msg);
    } else if (msg == "eventCountdown" && !countdownEnabled) {
      DEBUG_PRINTLN("Ignored mode request: countdown disabled");
    } else if (msg == "message" && !messageEnabled) {
      DEBUG_PRINTLN("Ignored mode request: message disabled");
    } else {
      DEBUG_PRINTLN("Mode Change Request: " + msg);
      currentMode = msg;
      pendingCommand = true;
      forceCountdown = (currentMode == "eventCountdown");
    }
  } 
  else if (t == "ledSign/color") {
    DEBUG_PRINTLN("Color Update: " + msg);
    primaryColor = parseColor(msg);
  }
  else if (t == "ledSign/timezone") {
    DEBUG_PRINTLN("Timezone Update: " + msg);
    updateTimezone(msg.toInt());
  }
  else if (t == "ledSign/message/header") {
    msgHeader = msg;
    pendingCommand = true;
  }
  else if (t == "ledSign/message/type") {
    messageType = msg;
    pendingCommand = true;
  }
  else if (t == "ledSign/message/sender") {
    messageSender = msg;
    pendingCommand = true;
  }
  else if (t == "ledSign/message/text") {
    msgBody = msg;
    pendingCommand = true;
  }
  else if (t == "ledSign/line1") {
    line1 = msg;
    pendingCommand = true;
  }
  else if (t == "ledSign/line2") {
    line2 = msg;
    pendingCommand = true;
  }
  else if (t == "ledSign/line3") {
    line3 = msg;
    pendingCommand = true;
  }
  else if (t == "ledSign/EN") {
    isDisplayOn = !msg.equalsIgnoreCase("OFF");
    DEBUG_PRINTLN("Display Enable: " + String(isDisplayOn));
  }
  else if (t == "ledSign/countdownEN") {
    countdownEnabled = !msg.equalsIgnoreCase("OFF");
    DEBUG_PRINTLN("Countdown Enable: " + String(countdownEnabled));
  }
  else if (t == "ledSign/messageEN") {
    messageEnabled = !msg.equalsIgnoreCase("OFF");
    DEBUG_PRINTLN("Message Enable: " + String(messageEnabled));
  }
  else if (t.startsWith("nextEvent/")) {
    activeEvent = true;
    DEBUG_PRINTLN("Event Data [" + t + "]: " + msg);
    if (t.endsWith("timeStamp")) {
      eventTime = (time_t)strtoll(msg.c_str(), NULL, 10);
      DEBUG_PRINTLN("Parsed Event Time (strtoll): " + String((int64_t)eventTime));
    }
    else if (t.endsWith("subject")) {
      eventSubject = msg;
    }
    else if (t.endsWith("organizer")) eventHost = msg;
    else if (t.endsWith("attendees")) eventAttendees = msg;
  }
}

void maintainMqtt() {
  static unsigned long lastAttempt = 0;
  if (mqtt.connected()) {
    mqtt.loop();
    return;
  }

  if (millis() - lastAttempt > 5000) {
    lastAttempt = millis();
    DEBUG_PRINT("MQTT Connecting...");
    if (mqtt.connect("ESP32_Sign_96x16", MQTT_USER, MQTT_PASS)) {
      DEBUG_PRINTLN("Online");
      mqtt.subscribe("ledSign/#");
      mqtt.subscribe("nextEvent/#");
    } else {
      DEBUG_PRINTLN("Failed (rc=" + String(mqtt.state()) + ")");
    }
  }
}

/**
 * @brief Draws text centered both horizontally and vertically.
 */
static void drawCenteredBoth(const String &text, uint16_t color = primaryColor) {
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (display->width() - (int16_t)w) / 2 - x1;
  int16_t y = (display->height() - (int16_t)h) / 2 - y1;
  
  display->setTextColor(color);
  display->setCursor(x, y);
  display->print(text);
}

// Scrolling State
struct ScrollState {
  int16_t pos;
  unsigned long lastScroll;
  bool active;
};
ScrollState scroll1 = {96, 0, false};
ScrollState scroll2 = {96, 0, false};

/**
 * @brief Draws text with scrolling if it exceeds a maximum width.
 */
static void drawScrollingLine(int16_t x_min, int16_t x_max, int16_t y, const String &text, const GFXfont* font, uint16_t color, ScrollState &state) {
  display->setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  
  int16_t max_w = x_max - x_min;
  
  if (w > max_w) {
    if (millis() - state.lastScroll > 40) { // ~25 FPS scroll
      state.pos--;
      if (state.pos + (int16_t)w < x_min) state.pos = x_max;
      state.lastScroll = millis();
    }
    display->setCursor(state.pos, y);
  } else {
    display->setCursor(x_min, y);
  }
  display->setTextColor(color);
  display->print(text);
}

// --- RENDER MODES ---

/**
 * @brief Displays up to 3 static lines of text with scrolling for the first two.
 */
void modeStatic() {
  if (pendingCommand) {
    scroll1.pos = 96; scroll2.pos = 96;
    pendingCommand = false;
  }
  drawScrollingLine(0, 96, 5, line1, &Picopixel, primaryColor, scroll1);
  drawScrollingLine(0, 96, 10, line2, &Picopixel, primaryColor, scroll2);
  display->setFont(&Picopixel);
  display->setCursor(0, 15);
  display->print(line3);
}

/**
 * @brief Cycles through all defined fonts, displaying the font name in each.
 */
void modeFontTest() {
  int index = (millis() / FONT_CYCLE_SPEED) % fontCount;
  display->setFont(fonts[index].font);
  drawCenteredBoth(fonts[index].name, primaryColor);
}

/**
 * @brief Displays a compact single-line clock using the whole sign.
 * Format: "Day MM/DD   HH:MM:SS"
 */
void modeClock() {
  // String timeStr = getFormattedTime("%a %m/%d   %H:%M:%S");
  String timeStr = getFormattedTime("%H:%M:%S");

  display->setFont(&FreeSansBold9pt7b); // Ensure default font
  // display->setFont(&FreeSansBold9pt7b); // Ensure default font
  drawLine(13,13, timeStr, primaryColor); // Centered vertically in a single line
}

/**
 * @brief Displays the clock with cycling rainbow colors for each character.
 */
void modeRainbowClock() {
  String timeStr = getFormattedTime("%H:%M:%S");
  display->setFont(&FreeSansBold9pt7b);
  display->setCursor(13, 13);
  
  for (int i = 0; i < (int)timeStr.length(); i++) {
    uint8_t pos = (uint8_t)((millis() / 5 + i * 20) % 255);
    uint16_t color;
    if (pos < 85) {
      color = display->color565(pos * 3, 255 - pos * 3, 0);
    } else if (pos < 170) {
      pos -= 85;
      color = display->color565(255 - pos * 3, 0, pos * 3);
    } else {
      pos -= 170;
      color = display->color565(0, pos * 3, 255 - pos * 3);
    }
    display->setTextColor(color);
    display->print(timeStr[i]);
  }
}

/**
 * @brief Displays a stacked clock with manual centering and optimized heights.
 * Date: 6px line height (Picopixel) -> Rows 0-5.
 * Gap: Row 6.
 * Time: 8px high (Default font) -> Rows 7-14.
 */
void modeBigClock() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  String dateStr = getFormattedTime("%A %b %d");
  dateStr.toUpperCase();
  String timeStr = getFormattedTime("%H:%M:%S");

  uint16_t colorDate = display->color565(128, 128, 0); // Olive
  uint16_t colorTime = display->color565(128, 128, 128); // Grey

  // 1. Draw Date (Picopixel - 5px active, 6px total)
  // display->setFont(&Picopixel);
  display->setFont(&Org_01);
  // display->setFont(&TomThumb);


  // Manual center: Picopixel is ~3px wide + 1px space = 4px
  // int16_t xDate = (96 - (dateStr.length() * 4)) / 2;
  display->setTextColor(colorDate);
  // display->setCursor(10, 4); // Baseline 4 = Rows 0-4
  drawCentered(4, dateStr, colorDate);
  // display->setCursor(xDate, 4); // Baseline 4 = Rows 0-4

  // display->print(dateStr);
  
  // 2. Draw Time (Restore Default font - 8px high)
  // display->setFont(); 
  display->setFont(&FreeSans7pt7b);

  // Manual center: Default font is 6px wide
  int16_t xTime = (96 - (timeStr.length() * 6)) / 2;
  display->setTextColor(colorTime);
  // display->setCursor(xTime, 7); // Row 7 = Starts at Row 7 (leaving Row 6 as gap)
  display->setCursor(20, 15); // Row 7 = Starts at Row 7 (leaving Row 6 as gap)
  
  display->print(timeStr);
}

void modeScroll() {
  if (pendingCommand) {
    scroll1.pos = 96;
    pendingCommand = false;
  }
  drawScrollingLine(0, 96, 8, line1, &FreeSans7pt7b, primaryColor, scroll1);
}

void modeMessage() {
  if(msgBody.length()==0){
    pendingCommand = false;
    return;
  }
  if (pendingCommand) {
    scroll1.pos = 96; scroll2.pos = 96;
    pendingCommand = false;
  }
  
  String l1 = messageType + (messageSender.length() > 0 ? " || " + messageSender : "");
  if (l1 == "") l1 = msgHeader;

  drawScrollingLine(0, 96, 4, l1, &Picopixel, 0x001F, scroll1);
  drawScrollingLine(0, 96, 14, msgBody, &FreeSans7pt7b, 0xFFE0, scroll2);
}

/**
 * @brief Displays an event countdown with flashing backgrounds and alternating scrolling lines.
 */
void modeEventCountdown() {
  if (eventSubject == "") {
    currentMode = "bigClock";
    forceCountdown = false;
    return;
  }

  time_t now_t;
  time(&now_t);
  long diff = (long)eventTime - (long)now_t;

  // Exit condition: if not forced and more than 5m out, or if more than 2m past
  if (diff > 300 && !forceCountdown) {
    DEBUG_PRINTLN("Event too far out (>5m), returning to bigClock");
    currentMode = "bigClock";
    return;
  }

  // 1. Background Flashing Logic
  if (diff > 0) {
    if (diff < 300 && diff > 290 && (diff % 2)) display->fillScreen(display->color565(80, 80, 80));
    else if (diff < 120 && diff > 110 && (diff % 2)) display->fillScreen(display->color565(80, 80, 0));
    else if (diff < 60 && diff > 50 && (diff % 2)) display->fillScreen(display->color565(150, 0, 0));
    else if (diff < 10 && diff > 0 && (diff % 2)) display->fillScreen(display->color565(0, 60, 0));
  } else {
    if (diff > -5) display->fillScreen(display->color565(0, 100, 0));
    else if (diff < -120) {
      DEBUG_PRINTLN("Event finished, returning to bigClock");
      currentMode = "bigClock";
      forceCountdown = false;
      return;
    }
  }

  // 2. Right Column (Clock and Timer)
  String timeStr = getFormattedTime("%H:%M:%S");
  // display->setFont(&Picopixel);
  display->setFont(nullptr);

  display->setTextColor(display->color565(128, 128, 128));
  display->setCursor(67, 0);
  display->print(timeStr);

  long absDiff = abs(diff);
  String sign = "";
  if (diff > 0) sign = "-";
  else if (diff < 0) sign = "+";
  
  String timerStr = sign + String(absDiff / 60) + ":" + (absDiff % 60 < 10 ? "0" : "") + String(absDiff % 60);
  uint16_t timerColor = (diff >= 0 ? display->color565(160, 160, 0) : display->color565(0, 80, 0));
  
  static long lastLoggedDiff = -999;
  if (abs(diff - lastLoggedDiff) >= 1) {
    DEBUG_PRINTLN("Countdown: " + timerStr + " (Diff: " + String(diff) + ", Now: " + String(now_t) + ", Event: " + String(eventTime) + ")");
    lastLoggedDiff = diff;
  }

  int16_t timerX = (absDiff / 60 >= 10) ? 61 : 65;
  display->setFont(nullptr);
  display->setTextColor(timerColor);
  display->setCursor(timerX, 9);
  display->print(timerStr);

  // 3. Left Content (Scrolling lines)
  if (pendingCommand) {
    scroll1.pos = 64; scroll2.pos = 64;
    pendingCommand = false;
  }

  // drawScrollingLine(0, 64, 9, eventSubject, &Picopixel, display->color565(0, 0, 255), scroll1);
  drawScrollingLine(0, 64, 0, eventSubject, nullptr, display->color565(0, 0, 255), scroll1);

  String subLine = "";
  if ((now_t % 60) < 15) subLine = "Org:" + eventHost;
  else subLine = eventAttendees;
  
  drawScrollingLine(0, 64, 15, subLine, &Picopixel, display->color565(128, 128, 128), scroll2);
}

/**
 * @brief Displays upcoming event details with the start time instead of a countdown.
 */
void modeNextEvent() {
  if (eventSubject == "") {
    currentMode = "bigClock";
    return;
  }

  // 1. Current Clock (Top Right)
  String timeStr = getFormattedTime("%H:%M:%S");
  display->setFont(nullptr);
  display->setTextColor(display->color565(128, 128, 128));
  display->setCursor(67, 0);
  display->print(timeStr);

  // 2. Event Start Time (Bottom Right)
  struct tm *eventInfo = localtime(&eventTime);
  char startTimeBuf[10];
  strftime(startTimeBuf, sizeof(startTimeBuf), "%H:%M", eventInfo);
  
  display->setFont(nullptr);
  display->setTextColor(display->color565(0, 255, 0)); // Green for scheduled time
  display->setCursor(67, 9);
  display->print(startTimeBuf);

  // 3. Left Content (Scrolling lines)
  if (pendingCommand) {
    scroll1.pos = 64; scroll2.pos = 64;
    pendingCommand = false;
  }

  drawScrollingLine(0, 64, 0, eventSubject, nullptr, display->color565(0, 0, 255), scroll1);

  time_t now_t;
  time(&now_t);
  String subLine = "";
  if ((now_t % 60) < 15) subLine = "Org:" + eventHost;
  else subLine = eventAttendees;
  
  drawScrollingLine(0, 64, 15, subLine, &Picopixel, display->color565(128, 128, 128), scroll2);
}

/**
 * @brief Test mode for messages to try out all fonts in the message body position.
 */
void modeMessageTest() {
  int index = (millis() / FONT_CYCLE_SPEED) % fontCount;
  const GFXfont* f = fonts[index].font;
  String name = fonts[index].name;

  if (pendingCommand) {
    scroll1.pos = 96; scroll2.pos = 96;
    pendingCommand = false;
  }

  // Line 1: Font Name (Picopixel)
  display->setFont(&Picopixel);
  display->setTextColor(display->color565(0, 255, 255)); // Cyan
  display->setCursor(0, 4);
  display->print("FONT: " + name);

  // Line 2: Sample text in the target font
  drawScrollingLine(0, 96, 15, "ABCDEFGHIJ klmnopqrst 1234567890", f, 0xFFE0, scroll2);
}

void modeTestPattern() {
  unsigned long t = millis() % 12000;
  int16_t w = display->width();
  int16_t h = display->height();
  display->setFont();
  
  if (t < 3000) {
    uint16_t c[] = {display->color565(255,0,0), display->color565(0,255,0), display->color565(0,0,255)};
    display->fillScreen(c[t / 1000]);
  } 
  else if (t < 6000) {
    display->drawRect(0, 0, w, h, 0xFFE0);
    display->drawLine(0, 0, w-1, h-1, 0x07FF);
    drawCentered(4, String(w) + "x" + String(h), 0xFFFF);
  } 
  else {
    for(int i = 0; i < w; i++) {
        uint8_t r = (i * 255) / w;
        display->drawLine(i, 0, i, h-1, display->color565(r, 255-r, 128));
    }
  }
}

void modeCalibration() {
  int16_t w = display->width();
  int16_t h = display->height();
  display->drawRect(0, 0, w, h, 0xFFFF);
  display->drawPixel(0, 0, 0xF800);
  display->drawPixel(w-1, h-1, 0x001F);
  display->setFont();
  drawCentered(4, "96x16 CALIBRATE", 0xFFE0);
}

void render() {
  if (millis() - lastFrameTime < FRAME_RATE_LIMIT) return;
  lastFrameTime = millis();

  display->clearScreen();
  if (!isDisplayOn) {
    display->flipDMABuffer();
    return;
  }

  display->setTextSize(1);

  if (currentMode == "clock") modeClock();
  else if (currentMode == "bigClock") modeBigClock();
  else if (currentMode == "rainbowClock") modeRainbowClock();
  else if (currentMode == "message") modeMessage();
  else if (currentMode == "scroll") modeScroll();
  else if (currentMode == "static") modeStatic();
  else if (currentMode == "test") modeTestPattern();
  else if (currentMode == "calibrate") modeCalibration();
  else if (currentMode == "fontTest") modeFontTest();
  else if (currentMode == "messageTest") modeMessageTest();
  else if (currentMode == "eventCountdown") modeEventCountdown();
  else if (currentMode == "nextEvent") modeNextEvent();
  else modeClock();

  display->flipDMABuffer();
}

void setupHardware() {
  HUB75_I2S_CFG::i2s_pins pins = {
    PIN_R1, PIN_G1, PIN_B1, PIN_R2, PIN_G2, PIN_B2, 
    PIN_A, PIN_B, PIN_C, PIN_D, PIN_E, 
    PIN_LAT, PIN_OE, PIN_CLK
  };
  
  HUB75_I2S_CFG config(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN, pins);
  config.double_buff = true;
  config.i2sspeed = HUB75_I2S_CFG::HZ_10M;
  
  display = new MatrixPanel_I2S_DMA(config);
  display->begin();
  display->setBrightness8(200);
  display->setTextWrap(false);
}

void setupNetwork() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  
  updateTimezone(DEFAULT_TIMEZONE);

  WebSerial.begin(&server);
  server.begin();
  ArduinoOTA.setHostname("ledsign");
  ArduinoOTA.begin();

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  setupHardware();
  setupNetwork();
}

void loop() {
  ArduinoOTA.handle();
  maintainMqtt();

  if (!hasBooted && millis() > BOOT_TEST_DURATION) {
    currentMode = "bigClock";
    hasBooted = true;
    DEBUG_PRINTLN("Startup sequence finished. Mode: BigClock");
  }

  render();

  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 60000) {
    lastHeartbeat = millis();
    DEBUG_PRINTLN("Heartbeat - Uptime: " + String(millis()/1000) + "s");
  }
}
