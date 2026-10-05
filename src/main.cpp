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
#include <math.h>

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

// --- POMODORO CONSTANTS ---
#define POMODORO_SPRINT_MINS 25              ///< Work segment length
#define POMODORO_SHORT_BREAK_MINS 5          ///< Short break length (after sprints 1-3)
#define POMODORO_LONG_BREAK_MINS 20          ///< Long break length (after the 4th sprint)
#define POMODORO_SPRINTS_BEFORE_LONG_BREAK 4 ///< Sprints per cycle before the long break
// "Nth Sprint" intro, in 3 stages (title is shown throughout): blinking green
// side bars, then those bars expanding inward until they meet (full green
// background), then the full green background blinking on/off a few times.
#define POMODORO_INTRO_EDGE_BLINK_MS 2000
#define POMODORO_INTRO_WIPE_MS 1000
#define POMODORO_INTRO_FULL_BLINK_COUNT 3        ///< Number of on/off blinks in the final stage
#define POMODORO_INTRO_FULL_BLINK_PERIOD_MS 400  ///< Duration of each on (or off) half-blink
#define POMODORO_INTRO_FULL_BLINK_MS (POMODORO_INTRO_FULL_BLINK_COUNT * 2 * POMODORO_INTRO_FULL_BLINK_PERIOD_MS)
#define POMODORO_INTRO_DURATION_MS (POMODORO_INTRO_EDGE_BLINK_MS + POMODORO_INTRO_WIPE_MS + POMODORO_INTRO_FULL_BLINK_MS)
#define POMODORO_BREAK_INTRO_DURATION_MS 1800 ///< Red blink signal before a short break
#define POMODORO_FIREWORKS_DURATION_MS 8000  ///< Celebration effect length after the 4th sprint

// --- TASK COMPLETE CONSTANTS ---
#define TASK_COMPLETE_EFFECT_DURATION_MS 10000 ///< Celebration length when a todo task is marked done

// How long an unstarted task-preview ("-----> ...") stays on screen after the
// last new candidate arrives, before it's hidden for being stale
#define POMODORO_TASK_SELECT_TIMEOUT_MS (2UL * 60UL * 1000UL)

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
String previousMode = "bigClock";
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
String todoLine1 = "TODO:";
String todoLine2 = "None";
bool todoClockRunning = false;
unsigned long todoStartMillis = 0;
unsigned long todoFinalElapsedSecs = 0;
bool pendingCommand = false;

/**
 * @brief Seconds accumulated on the active todo task: the frozen baseline
 * (todoFinalElapsedSecs) plus whatever has ticked by since the clock last
 * started, if it's currently running.
 */
unsigned long getCurrentElapsedSecs() {
  if (todoClockRunning) {
    return todoFinalElapsedSecs + (millis() - todoStartMillis) / 1000;
  }
  return todoFinalElapsedSecs;
}

/**
 * @brief Formats and publishes the current elapsed time to nextTODO/personal/elapsed,
 * remembering the payload in a short rolling history so the handler for that same
 * topic can recognize this publish bouncing back off the broker (we're subscribed
 * to nextTODO/#) and not mistake our own echo for an externally-pushed baseline.
 *
 * A single "last published" value isn't enough: if two of our own publishes go out
 * close together (e.g. STOP immediately followed by RESET), the earlier one's echo
 * can arrive after the later publish has already moved "last" on, making it look
 * like a brand-new external baseline and corrupting state we just reset. Keeping a
 * short history of recent self-publishes (each valid for a few seconds) closes
 * that race.
 */
struct SelfPublish { String msg; unsigned long atMillis; };
SelfPublish recentSelfPublishes[4];
int selfPublishIdx = 0;
const unsigned long SELF_ECHO_WINDOW_MS = 5000;

void publishElapsed(unsigned long secs) {
  unsigned long mins = (secs / 60) % 60;
  unsigned long hours = secs / 3600;
  char pubBuf[16];
  sprintf(pubBuf, "%02lu:%02lu", hours, mins);
  recentSelfPublishes[selfPublishIdx] = {String(pubBuf), millis()};
  selfPublishIdx = (selfPublishIdx + 1) % 4;
  if (mqtt.connected()) {
    mqtt.publish("nextTODO/personal/elapsed", pubBuf);
    DEBUG_PRINTLN("Published elapsed time: " + String(pubBuf));
  }
}

static bool isRecentSelfEcho(const String &msg) {
  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    if (recentSelfPublishes[i].msg == msg && (now - recentSelfPublishes[i].atMillis) < SELF_ECHO_WINDOW_MS) {
      return true;
    }
  }
  return false;
}

// Scrolling & Positioning
int16_t scrollPos = 0;
int16_t scrollWidth = 0;

// Event Data
bool activeEvent = false;
time_t eventTime = 0;
String eventSubject = "", eventHost = "", eventAttendees = "";

// Messaging Data
String messageType = "", messageSender = "";
unsigned long lastMessageTime = 0;

// Pomodoro State
enum PomodoroPhase { POMO_SPRINT_INTRO, POMO_SPRINT, POMO_BREAK_INTRO, POMO_BREAK, POMO_FIREWORKS, POMO_LONG_BREAK };
PomodoroPhase pomodoroPhase = POMO_SPRINT_INTRO;
int pomodoroSprintNum = 1;                 ///< 1..POMODORO_SPRINTS_BEFORE_LONG_BREAK
bool pomodoroPaused = false;
unsigned long pomodoroPhaseStartMillis = 0;
unsigned long pomodoroPauseStartMillis = 0;
bool pomodoroShowTask = true;              ///< Toggle via ledSign/pomodoro/taskEN
int pomodoroEffectIndex = -1;              ///< Configured celebration effect; -1 = random each cycle (default)
int activeCelebrationEffect = 0;           ///< Effect resolved for the celebration currently playing

// Standalone celebration-effect preview, independent of the pomodoro cycle
bool effectPreviewActive = false;
int effectPreviewIndex = 0;
unsigned long effectPreviewStartMillis = 0;

// Task-complete celebration (independent of the pomodoro cycle; triggered by
// nextTodo/select/completed, i.e. marking the active todo task done)
unsigned long taskCompleteStartMillis = 0;
int taskCompleteEffectIndex = 0;
String modeBeforeTaskComplete = "bigClock"; ///< Where modeTaskComplete() reverts to once the celebration ends

// Whether the todo elapsed clock is currently paused for a pomodoro break
// (as opposed to not running at all because no task is active)
bool todoPausedForBreak = false;

// Whether a candidate task is actively being cycled through (a new value was
// recently sent to nextTODO/personal or line1/line2), as opposed to just
// stale leftover text from a previous session -- gates the "-----> " preview
// on the pomodoro sprint screen. Cleared whenever pomodoro (re)starts or the
// task is committed to (started/stopped/completed/reset), and expires on its
// own after POMODORO_TASK_SELECT_TIMEOUT_MS of no new candidates.
bool taskBeingCycled = false;
unsigned long lastTaskCycleMillis = 0;

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

// Forward declarations: pomodoro control functions, defined with the other render-mode
// code later in the file, but invoked from onMqttMessage() below.
void resetPomodoroCycle();
void advancePomodoroPhase();
int parseEffectIndex(const String &msg);
int pickRandomEffectIndex();
void pauseTodoForBreak();
void resumeTodoAfterBreak();

/**
 * @brief Whether pomodoro is "contextually" active: either it's the current
 * mode outright, or we're mid-taskComplete-celebration and about to revert
 * back to it. Checking currentMode == "pomodoro" alone misses the latter --
 * e.g. the external Todoist collator's normal habit of publishing
 * ledSign/mode=showTodo to preview a task, or auto-issuing START for it,
 * can land squarely inside the few-second celebration window right after a
 * task completes, and would otherwise wrongly be treated as "not pomodoro".
 */
static bool isPomodoroContext() {
  return currentMode == "pomodoro" ||
         (currentMode == "taskComplete" && modeBeforeTaskComplete == "pomodoro");
}

/**
 * @brief Modes allowed to pre-empt pomodoro's own display while it's
 * contextually active (isPomodoroContext()). This is deliberately an
 * allowlist, not a blocklist: "showTodo" and then "message" each turned out
 * to silently hijack the display because they weren't on an ever-growing
 * list of special cases to block. Blocking everything by default except
 * these few makes every *other* mode -- including ones added later --
 * safe automatically, instead of one more thing to remember.
 */
static bool isModeAllowedDuringPomodoro(const String &mode) {
  return mode == "pomodoro" ||       // re-selecting it always resets the cycle
         mode == "eventCountdown" || // urgent alert, explicitly allowed to interrupt
         mode == "taskComplete";     // internal transition, never requested externally
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
    } else if (isPomodoroContext() && !isModeAllowedDuringPomodoro(msg)) {
      DEBUG_PRINTLN("Ignored mode request: " + msg + " blocked during pomodoro");
    } else {
      DEBUG_PRINTLN("Mode Change Request: " + msg);
      if (currentMode != msg && currentMode != "message" && currentMode != "test" &&
          currentMode != "messageTest" && currentMode != "todoClock" && currentMode != "taskComplete") {
        previousMode = currentMode;
      }
      currentMode = msg;
      if (currentMode == "message") {
        lastMessageTime = millis();
      }
      if (currentMode == "pomodoro") {
        resetPomodoroCycle(); // Selecting pomodoro always (re)starts the cycle at sprint 1
      }
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
    lastMessageTime = millis();
  }
  else if (t == "ledSign/message/type") {
    messageType = msg;
    pendingCommand = true;
    lastMessageTime = millis();
  }
  else if (t == "ledSign/message/sender") {
    messageSender = msg;
    pendingCommand = true;
    lastMessageTime = millis();
  }
  else if (t == "ledSign/message/text") {
    static String lastMsgBodyReceived = "";
    static unsigned long lastMsgTimeReceived = 0;
    
    if (msg == lastMsgBodyReceived && (millis() - lastMsgTimeReceived < 10000)) {
      DEBUG_PRINTLN("Ignored duplicate message: " + msg);
      return;
    }
    lastMsgBodyReceived = msg;
    lastMsgTimeReceived = millis();

    msgBody = msg;
    pendingCommand = true;
    lastMessageTime = millis();
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
  else if (t == "ledSign/pomodoro") {
    String cmd = msg;
    cmd.trim();
    cmd.toLowerCase();
    if (cmd == "start") {
      currentMode = "pomodoro";
      resetPomodoroCycle();
      pendingCommand = true;
      DEBUG_PRINTLN("Pomodoro: start");
    } else if (cmd == "stop") {
      if (currentMode == "pomodoro") {
        currentMode = previousMode;
        pendingCommand = true;
        DEBUG_PRINTLN("Pomodoro: stop -> " + previousMode);
      }
    } else if (cmd == "pause") {
      if (currentMode == "pomodoro" && !pomodoroPaused) {
        pomodoroPaused = true;
        pomodoroPauseStartMillis = millis();
        pauseTodoForBreak(); // task time shouldn't advance while pomodoro itself is paused
        DEBUG_PRINTLN("Pomodoro: pause");
      }
    } else if (cmd == "resume") {
      if (currentMode == "pomodoro" && pomodoroPaused) {
        pomodoroPhaseStartMillis += millis() - pomodoroPauseStartMillis;
        pomodoroPaused = false;
        resumeTodoAfterBreak();
        DEBUG_PRINTLN("Pomodoro: resume");
      }
    } else if (cmd == "skip") {
      if (currentMode == "pomodoro") {
        // Advance exactly one phase. Moving into the next segment should
        // still play that segment's own intro graphics (green sprint intro
        // or red break intro), not jump straight past them.
        advancePomodoroPhase();
        DEBUG_PRINTLN("Pomodoro: skip");
      }
    } else if (cmd == "reset") {
      resetPomodoroCycle();
      currentMode = "pomodoro";
      pendingCommand = true;
      DEBUG_PRINTLN("Pomodoro: reset");
    } else if (cmd == "toggle") {
      if (currentMode == "pomodoro") {
        currentMode = previousMode;
        resetPomodoroCycle(); // so the next start always begins at sprint 1
        pendingCommand = true;
        DEBUG_PRINTLN("Pomodoro: toggle -> stop (" + previousMode + ") + reset");
      } else {
        currentMode = "pomodoro";
        resetPomodoroCycle();
        pendingCommand = true;
        DEBUG_PRINTLN("Pomodoro: toggle -> start");
      }
    } else {
      DEBUG_PRINTLN("Pomodoro: unrecognized command " + cmd);
    }
  }
  else if (t == "ledSign/pomodoro/taskEN") {
    pomodoroShowTask = !msg.equalsIgnoreCase("OFF");
    DEBUG_PRINTLN("Pomodoro Task Display Enable: " + String(pomodoroShowTask));
  }
  else if (t == "ledSign/pomodoro/effect") {
    pomodoroEffectIndex = parseEffectIndex(msg);
    DEBUG_PRINTLN("Pomodoro celebration effect set to index " + String(pomodoroEffectIndex));
  }
  else if (t == "ledSign/pomodoro/testEffect") {
    effectPreviewIndex = parseEffectIndex(msg);
    // Resolve "random" immediately so the preview doesn't re-roll every frame.
    // 10 must match celebrationEffectCount (celebrationEffects[] below).
    if (effectPreviewIndex == -1) effectPreviewIndex = random(0, 10);
    effectPreviewActive = true;
    effectPreviewStartMillis = millis();
    DEBUG_PRINTLN("Pomodoro effect preview: index " + String(effectPreviewIndex));
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
  else if (t.startsWith("nextTodo/") || t.startsWith("nextTODO/") || t.startsWith("nexttodo/")) {
    DEBUG_PRINTLN("Todo Data [" + t + "]: " + msg);
    
    String tLower = t;
    tLower.toLowerCase();

    if (tLower == "nexttodo/personal" || tLower == "nexttodo/personal") {
      int nlIdx = msg.indexOf('\n');
      if (nlIdx == -1) nlIdx = msg.indexOf('|');
      if (nlIdx != -1) {
        todoLine1 = msg.substring(0, nlIdx);
        todoLine2 = msg.substring(nlIdx + 1);
      } else {
        todoLine1 = "TODO:";
        todoLine2 = msg;
      }
      todoLine1.trim();
      todoLine2.trim();
      taskBeingCycled = true;
      lastTaskCycleMillis = millis();
    }
    else if (tLower == "nexttodo/personal/line1") {
      todoLine1 = msg;
      taskBeingCycled = true;
      lastTaskCycleMillis = millis();
    }
    else if (tLower == "nexttodo/personal/line2") {
      todoLine2 = msg;
      taskBeingCycled = true;
      lastTaskCycleMillis = millis();
    }
    else if (tLower == "nexttodo/personal/elapsed") {
      // This topic is also where WE publish status (periodic ticks, stop/reset
      // results), and we're subscribed to nextTODO/#, so our own publishes
      // bounce back here too. Recognize those against our recent self-publish
      // history and ignore them; anything else is a real external baseline
      // (e.g. prior logged time pushed by the Todoist collator) and must be
      // honored whether it arrives before START or — as is typical, since
      // computing it requires a Todoist lookup that finishes after the sign
      // has already started counting — after.
      if (isRecentSelfEcho(msg)) {
        DEBUG_PRINTLN("Ignored elapsed echo: " + msg);
      } else {
        unsigned long hours = 0, mins = 0;
        if (sscanf(msg.c_str(), "%lu:%lu", &hours, &mins) == 2) {
          todoFinalElapsedSecs = hours * 3600UL + mins * 60UL;
          if (todoClockRunning) {
            // Rebase the running start point so the new baseline takes effect
            // immediately without double-counting time already elapsed.
            todoStartMillis = millis();
          }
          DEBUG_PRINTLN("Todo clock baseline set from elapsed topic: " + String(todoFinalElapsedSecs) + "s");
        }
      }
    }
    else if (tLower.startsWith("nexttodo/select")) {
      String trimmedMsg = msg;
      trimmedMsg.trim();
      
      bool isStart = (tLower == "nexttodo/select/start" || 
                      (tLower == "nexttodo/select" && trimmedMsg.equalsIgnoreCase("start")));
                      
      bool isStop = (tLower == "nexttodo/select/stop" || 
                     (tLower == "nexttodo/select" && trimmedMsg.equalsIgnoreCase("stop")));
                     
      bool isReset = (tLower == "nexttodo/select/reset" ||
                      (tLower == "nexttodo/select" && trimmedMsg.equalsIgnoreCase("reset")));

      bool isCompleted = (tLower == "nexttodo/select/completed" ||
                          (tLower == "nexttodo/select" && trimmedMsg.equalsIgnoreCase("completed")));

      if (isStart) {
        // If the clock was left running (e.g. STOP was never pressed before
        // this START), freeze and publish its elapsed time first so that
        // session isn't lost.
        if (todoClockRunning) {
          todoFinalElapsedSecs = getCurrentElapsedSecs();
          publishElapsed(todoFinalElapsedSecs);
        }
        todoPausedForBreak = false; // a fresh START always supersedes any stale break-pause
        taskBeingCycled = false; // committed to this task now, not previewing it anymore

        if (isPomodoroContext()) {
          // Pomodoro owns the display already (and shows the task text
          // itself during sprints) -- just start the background elapsed
          // clock without switching modes. If we're mid-taskComplete
          // celebration, leave that alone too; it'll revert to "pomodoro"
          // on its own once the celebration finishes.
          todoStartMillis = millis();
          todoClockRunning = true;
          DEBUG_PRINTLN("Todo Timer STARTED (pomodoro) at millis " + String(todoStartMillis) + " from baseline " + String(todoFinalElapsedSecs) + "s");
        } else {
          // Remember what the sign was showing before the todo clock took over,
          // so STOP/COMPLETED can return to it immediately instead of a hardcoded
          // mode. "showTodo" is excluded along with the other todo-related/transient
          // modes: it's just the task preview shown right before START, not a real
          // "home" mode — reverting to it after the task is stopped/done would leave
          // the (now stale) task on screen instead of going back to the clock.
          if (currentMode != "todoClock" && currentMode != "showTodo" && currentMode != "message" &&
              currentMode != "test" && currentMode != "messageTest" && currentMode != "taskComplete") {
            previousMode = currentMode;
          }

          // todoFinalElapsedSecs is the baseline to resume from: left over from
          // the block above, from a previous STOP, or pushed in externally via
          // nextTODO/personal/elapsed while idle (see that topic's handler
          // above). START must never reset it — only RESET explicitly zeroes
          // the clock.
          todoStartMillis = millis();
          todoClockRunning = true;
          currentMode = "todoClock"; // Switch to todoClock mode automatically
          DEBUG_PRINTLN("Todo Timer STARTED at millis " + String(todoStartMillis) + " from baseline " + String(todoFinalElapsedSecs) + "s");
        }
      } else if (isStop) {
        todoFinalElapsedSecs = getCurrentElapsedSecs();
        publishElapsed(todoFinalElapsedSecs);

        todoClockRunning = false;
        todoPausedForBreak = false;
        taskBeingCycled = false; // don't re-show this (now stopped, not completed) task as a preview
        // While in pomodoro (or mid-taskComplete celebration en route back to
        // it), stay there -- it's still driving (or about to resume driving)
        // the display. Otherwise revert immediately to whatever was showing
        // before the standalone todoClock mode took over.
        if (!isPomodoroContext()) {
          currentMode = previousMode;
        }
        DEBUG_PRINTLN("Todo Timer STOPPED -> " + currentMode);
        pendingCommand = true;
      } else if (isCompleted) {
        // Task marked done (nextTODO/select/completed): publish final elapsed
        // time, clear the clock, celebrate briefly, then modeTaskComplete()
        // reverts to pomodoro (if that's what was active -- it keeps driving
        // the display and will pick up the next task once nextTodo/personal
        // is updated) or to previousMode otherwise (the standalone todoClock
        // flow's original behavior).
        publishElapsed(getCurrentElapsedSecs());

        todoStartMillis = 0;
        todoClockRunning = false;
        todoFinalElapsedSecs = 0;
        todoPausedForBreak = false;
        taskBeingCycled = false; // don't re-show the just-completed task as a preview

        taskCompleteEffectIndex = pickRandomEffectIndex();
        taskCompleteStartMillis = millis();
        modeBeforeTaskComplete = isPomodoroContext() ? "pomodoro" : previousMode;
        currentMode = "taskComplete";
        pendingCommand = true;
        DEBUG_PRINTLN("Todo Task COMPLETED -> celebrating, then reverting to " + modeBeforeTaskComplete);
      } else if (isReset) {
        // Publish elapsed time before resetting
        publishElapsed(getCurrentElapsedSecs());

        todoStartMillis = 0;
        todoClockRunning = false;
        todoFinalElapsedSecs = 0;
        todoPausedForBreak = false;
        taskBeingCycled = false;
        currentMode = "bigClock";
        pendingCommand = true;
        DEBUG_PRINTLN("Todo Timer RESET -> Reverted to bigClock");
      }
    }
    pendingCommand = true;
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
      mqtt.subscribe("nextTodo/#");
      mqtt.subscribe("nextTODO/#");
      mqtt.subscribe("nexttodo/#");
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
  unsigned long startMillis;
};
ScrollState scroll1 = {96, 0, false, 0};
ScrollState scroll2 = {96, 0, false, 0};

/**
 * @brief Draws text with scrolling if it exceeds a maximum width.
 * @return true if the scrolling wrapped around (completed a cycle) on this frame.
 */
static bool drawScrollingLine(int16_t x_min, int16_t x_max, int16_t y, const String &text, const GFXfont* font, uint16_t color, ScrollState &state) {
  display->setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  
  int16_t max_w = x_max - x_min;
  bool wrapped = false;
  
  if (w > max_w) {
    if (millis() - state.startMillis < 1000) {
      state.pos = x_min; // Pause at the start position (showing as much text as possible)
    } else {
      if (millis() - state.lastScroll > 40) { // ~25 FPS scroll
        state.pos--;
        if (state.pos + (int16_t)w + 32 < x_min) {
          state.pos = x_max;
          wrapped = true;
        }
        state.lastScroll = millis();
      }
    }
    display->setCursor(state.pos, y);
  } else {
    display->setCursor(x_min, y);
  }
  display->setTextColor(color);
  display->print(text);
  return wrapped;
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
 * @brief Displays the GMT clock in the format "HH:MMZ".
 */
void modeGmtClock() {
  time_t now;
  time(&now);
  struct tm *timeinfo = gmtime(&now);
  if (!timeinfo) return;
  char buffer[16];
  strftime(buffer, sizeof(buffer), "%H:%M:%SZ", timeinfo);
  String timeStr = String(buffer);

  display->setFont(&FreeSansBold9pt7b);
  uint16_t colorOrange = display->color565(255, 128, 0);
  drawCentered(13, timeStr, colorOrange);
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
    currentMode = previousMode;
    return;
  }
  
  static String lastMsgBody = "";
  static String lastL1 = "";
  static bool wasMessageMode = false;
  static unsigned long messageDisplayStartTime = 0;

  String l1 = messageType + (messageSender.length() > 0 ? " || " + messageSender : "");
  if (l1 == "") l1 = msgHeader;

  // Consume pendingCommand so it doesn't linger
  if (pendingCommand) {
    pendingCommand = false;
  }

  // Calculate widths to check if scrolling is active
  int16_t x1, y1;
  uint16_t w1 = 0, h1 = 0;
  display->setFont(&Picopixel);
  display->getTextBounds(l1, 0, 4, &x1, &y1, &w1, &h1);

  uint16_t w2 = 0, h2 = 0;
  display->setFont(&FreeSans7pt7b);
  display->getTextBounds(msgBody, 0, 14, &x1, &y1, &w2, &h2);

  // If the message body/header changed or we just entered message mode,
  // reset the scroll positions and record the display start time.
  if (msgBody != lastMsgBody || l1 != lastL1 || !wasMessageMode) {
    scroll1.pos = 96;
    scroll2.pos = 96;
    lastMsgBody = msgBody;
    lastL1 = l1;
    wasMessageMode = true;
    messageDisplayStartTime = millis();
  }

  bool wrapped1 = drawScrollingLine(0, 96, 4, l1, &Picopixel, 0x001F, scroll1);
  bool wrapped2 = drawScrollingLine(0, 96, 14, msgBody, &FreeSans7pt7b, 0xFFE0, scroll2);

  // Exit conditions:
  bool shouldExit = false;
  
  if (w2 > 96) {
    // Main message is scrolling, exit when it completes 1 scroll
    if (wrapped2) shouldExit = true;
  } else if (w1 > 96) {
    // Header is scrolling (but main msg is static), exit when header completes 1 scroll
    if (wrapped1) shouldExit = true;
  } else {
    // Both are static, exit after 5 seconds of actual display time
    if (millis() - messageDisplayStartTime > 5000) shouldExit = true;
  }

  // Fail-safe timeout dynamically adjusted to the length of the longest scrolling line,
  // or 60 seconds if static, to ensure it doesn't get cut off early.
  unsigned long failSafeTimeout = 60000;
  unsigned long maxW = (w1 > w2) ? w1 : w2;
  if (maxW > 96) {
    failSafeTimeout = (96 + maxW + 32) * 40 + 5000; // Expected scroll time with 32px padding + 5s buffer
  }

  if (millis() - messageDisplayStartTime > failSafeTimeout) shouldExit = true;

  if (shouldExit) {
    currentMode = previousMode;
    msgBody = "";
    wasMessageMode = false; // Reset for next message entry
    return;
  }
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
 * @brief Displays the next personal TODO item using a static top line and a scrolling bottom line.
 */
void modeNextTodo() {
  if (pendingCommand) {
    scroll2.pos = 0;
    scroll2.startMillis = millis();
    pendingCommand = false;
  }

  // Line 1: Header/Title (using primaryColor) - static, Org_01 font
  display->setFont(&Org_01);
  display->setTextSize(1);
  display->setTextColor(primaryColor);
  display->setCursor(0, 4);
  display->print(todoLine1);

  // Line 2: Todo Item Details (vibrant Cyan) - Default 5x7 font (nullptr), scrolls if it exceeds display width
  drawScrollingLine(0, 96, 8, todoLine2, nullptr, display->color565(0, 255, 255), scroll2);
}

/**
 * @brief Displays the next personal TODO item with a green elapsed clock in the top right.
 */
void modeNextTodoClock() {
  // Calculate elapsed time
  unsigned long elapsedSecs = getCurrentElapsedSecs();
  unsigned long mins = (elapsedSecs / 60) % 60;
  unsigned long hours = elapsedSecs / 3600;
  char clockBuf[16];
  sprintf(clockBuf, "T+%lu:%02lu", hours, mins);



  display->setFont(&Org_01);
  display->setTextSize(1);

  // Compute width of the green clock
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(clockBuf, 0, 4, &x1, &y1, &w, &h);
  int16_t clockX = 96 - (int16_t)w;

  if (pendingCommand) {
    scroll1.pos = 0;
    scroll1.startMillis = millis();
    scroll2.pos = 0;
    scroll2.startMillis = millis();
    pendingCommand = false;
  }

  // Line 1 Left: Header/Title (using primaryColor) - scroll if it exceeds clock boundary (using baseline Y=4)
  drawScrollingLine(0, clockX - 2, 4, todoLine1, &Org_01, primaryColor, scroll1);

  // Line 1 Right: Elapsed Clock (Green) - static (aligned to baseline Y=4)
  display->setFont(&Org_01);
  display->setTextColor(display->color565(0, 255, 0)); // Green
  display->setCursor(clockX, 4);
  display->print("T+");
  display->print(hours);

  // Print the colon (either in green or black depending on even/odd seconds)
  if (elapsedSecs % 2 == 0) {
    display->setTextColor(display->color565(0, 255, 0)); // Green
  } else {
    display->setTextColor(0); // Black
  }
  display->print(":");

  // Print the minutes (restore green color)
  display->setTextColor(display->color565(0, 255, 0)); // Green
  if (mins < 10) display->print("0");
  display->print(mins);

  // Line 2: Todo Item Details (vibrant Cyan) - Default 5x7 font (nullptr), scrolls if it exceeds display width
  drawScrollingLine(0, 96, 8, todoLine2, nullptr, display->color565(0, 255, 255), scroll2);
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

// --- POMODORO MODE ---

/**
 * @brief Cheap deterministic integer hash (Wang hash), used to drive the
 * celebration effects from elapsed time alone instead of mutable particle
 * state, so every effect is just a pure function of (elapsed, duration).
 */
static uint32_t wangHash(uint32_t seed) {
  seed = (seed ^ 61) ^ (seed >> 16);
  seed *= 9;
  seed ^= (seed >> 4);
  seed *= 0x27d4eb2d;
  seed ^= (seed >> 15);
  return seed;
}

/**
 * @brief Maps a 0-255 position to a color around the rainbow wheel.
 * (Same formula already used inline by modeRainbowClock, factored out for reuse.)
 */
static uint16_t colorWheel(uint8_t pos) {
  if (pos < 85) {
    return display->color565(pos * 3, 255 - pos * 3, 0);
  } else if (pos < 170) {
    pos -= 85;
    return display->color565(255 - pos * 3, 0, pos * 3);
  } else {
    pos -= 170;
    return display->color565(0, pos * 3, 255 - pos * 3);
  }
}

static String pomodoroOrdinal(int n) {
  switch (n) {
    case 1: return "1st";
    case 2: return "2nd";
    case 3: return "3rd";
    default: return "4th";
  }
}

/**
 * @brief Generic progress bar: a dim track across the full width with a
 * filled portion proportional to frac (0..1). Reused by sprint/break/long-break.
 */
static void drawProgressBar(int16_t y, int16_t h, float frac, uint16_t fillColor) {
  int16_t w = display->width();
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  int16_t filled = (int16_t)(w * frac);
  display->fillRect(0, y, w, h, 0); // unfilled track: LEDs off, not a dim fill
  if (filled > 0) display->fillRect(0, y, filled, h, fillColor);
}

/**
 * @brief Centers a digits/colon string (e.g. a countdown) against a reference
 * built from the widest digit glyph, rather than the string's own measured
 * width. Proportional fonts render "1" narrower than "0"/"8", so measuring the
 * real string every frame makes the whole centered string visibly shift by a
 * pixel whenever a "1" enters or leaves it. Centering against a fixed-shape
 * reference instead keeps the position stable regardless of which digits show.
 */
static void drawCenteredDigits(int16_t y, const String &text, uint16_t color) {
  String ref = text;
  for (size_t i = 0; i < ref.length(); i++) {
    if (isDigit(ref[i])) ref[i] = '8';
  }
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(ref, 0, y, &x1, &y1, &w, &h);
  int16_t x = (display->width() - (int16_t)w) / 2;
  display->setTextColor(color);
  display->setCursor(x, y);
  display->print(text);
}

// --- Celebration Effects (played after the 4th sprint, and previewable via testEffect) ---

static void drawEffectParticles(unsigned long elapsed, unsigned long duration) {
  display->fillScreen(0);
  const unsigned long LAUNCH_MS = 1200;
  const unsigned long LIFETIME_MS = 900;
  const int NUM_PARTICLES = 14;
  int16_t w = display->width(), h = display->height();
  long launchIndex = elapsed / LAUNCH_MS;
  unsigned long age = elapsed % LAUNCH_MS;

  for (long li = (launchIndex > 0 ? launchIndex - 1 : 0); li <= launchIndex; li++) {
    unsigned long thisAge = (li == launchIndex) ? age : age + LAUNCH_MS;
    if (thisAge > LIFETIME_MS) continue;
    uint32_t seed = wangHash((uint32_t)li * 7919u + 13u);
    int originX = 10 + (seed % (uint32_t)(w - 20));
    uint8_t baseHue = (uint8_t)(seed >> 8);
    float t = (float)thisAge / (float)LIFETIME_MS;
    for (int i = 0; i < NUM_PARTICLES; i++) {
      uint32_t pseed = wangHash(seed + (uint32_t)i * 101u);
      float angle = (float)(pseed % 360) * 0.0174533f;
      float speed = 6.0f + (float)((pseed >> 8) % 10);
      float radius = speed * t;
      int x = originX + (int)(radius * cosf(angle));
      int y = 5 + (int)(radius * sinf(angle) * 0.6f);
      if (x < 0 || x >= w || y < 0 || y >= h) continue;
      if (t > 0.6f && ((pseed >> 3) % 3 == 0)) continue; // flicker out near the end
      display->drawPixel(x, y, colorWheel((uint8_t)(baseHue + i * 10)));
    }
  }
}

static void drawEffectRings(unsigned long elapsed, unsigned long duration) {
  display->fillScreen(0);
  const unsigned long RING_MS = 1000;
  const unsigned long LIFETIME_MS = 800;
  int16_t w = display->width(), h = display->height();
  long idx = elapsed / RING_MS;
  unsigned long age = elapsed % RING_MS;

  for (long i = (idx > 0 ? idx - 1 : 0); i <= idx; i++) {
    unsigned long thisAge = (i == idx) ? age : age + RING_MS;
    if (thisAge > LIFETIME_MS) continue;
    uint32_t seed = wangHash((uint32_t)i * 2654435761u);
    int cx = 10 + (seed % (uint32_t)(w - 20));
    int cy = 4 + (seed >> 8) % (h - 4);
    uint16_t color = colorWheel((uint8_t)(seed % 255));
    int r = (int)(((float)thisAge / (float)LIFETIME_MS) * 20);
    display->drawCircle(cx, cy, r, color);
    if (r > 3) display->drawCircle(cx, cy, r - 3, color);
  }
}

static void drawEffectSpiral(unsigned long elapsed, unsigned long duration) {
  display->fillScreen(0);
  int16_t w = display->width(), h = display->height();
  float cx = w / 2.0f, cy = h / 2.0f;
  float t = elapsed / 1000.0f;
  for (int i = 0; i < 140; i++) {
    float frac = i / 140.0f;
    float angle = frac * 6.2831853f * 3.0f + t * 3.0f;
    float radius = frac * (h * 1.3f);
    int x = (int)(cx + radius * cosf(angle));
    int y = (int)(cy + radius * sinf(angle) * 0.6f);
    if (x < 0 || x >= w || y < 0 || y >= h) continue;
    display->drawPixel(x, y, colorWheel((uint8_t)((frac * 255) + t * 60)));
  }
}

static void drawEffectDissolve(unsigned long elapsed, unsigned long duration) {
  int16_t w = display->width(), h = display->height();
  float frac = (float)elapsed / (float)duration;
  // Triangle wave: dissolves in over the first half, out over the second.
  float phase = frac < 0.5f ? frac * 2.0f : (1.0f - frac) * 2.0f;
  uint16_t color = colorWheel((uint8_t)((elapsed / 30) % 255));
  unsigned long frame = elapsed / 80;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint32_t seed = wangHash((uint32_t)(x * 97 + y * 193) + (uint32_t)frame * 7u);
      float threshold = (seed % 1000) / 1000.0f;
      display->drawPixel(x, y, (threshold < phase) ? color : 0);
    }
  }
}

static void drawEffectWipe(unsigned long elapsed, unsigned long duration) {
  int16_t w = display->width(), h = display->height();
  // Explicit sweep-then-hold timing (rather than a continuous modulo sweep)
  // guarantees a run of frames actually render the fully-filled screen before
  // resetting — a pure modulo sweep can jump from "almost full" straight back
  // to 0 between two frames, so the right edge never visibly gets reached.
  const unsigned long SWEEP_MS = 900;
  const unsigned long HOLD_MS = 300;
  const unsigned long CYCLE_MS = SWEEP_MS + HOLD_MS;
  unsigned long age = elapsed % CYCLE_MS;
  uint16_t color = colorWheel((uint8_t)((elapsed / 10) % 255));
  display->fillScreen(0);
  int fillW = (age < SWEEP_MS) ? (int)(((float)age / (float)SWEEP_MS) * w) : w;
  if (fillW > 0) display->fillRect(0, 0, fillW, h, color);
}

static void drawEffectRainbowWave(unsigned long elapsed, unsigned long duration) {
  int16_t w = display->width(), h = display->height();
  float t = elapsed / 20.0f;
  for (int x = 0; x < w; x++) {
    uint8_t hue = (uint8_t)((x * 4) + t);
    display->fillRect(x, 0, 1, h, colorWheel(hue));
  }
}

static void drawEffectMatrixRain(unsigned long elapsed, unsigned long duration) {
  int16_t w = display->width(), h = display->height();
  display->fillScreen(0);
  uint16_t green = display->color565(0, 255, 0);
  uint16_t dim = display->color565(0, 80, 0);
  for (int x = 0; x < w; x += 2) {
    uint32_t seed = wangHash((uint32_t)x * 733u);
    float speed = 8.0f + (seed % 10);
    unsigned long offset = seed % 2000;
    float pos = fmodf((float)(elapsed + offset) / 1000.0f * speed, (float)(h + 8)) - 4.0f;
    int headY = (int)pos;
    for (int ty = 0; ty < 5; ty++) {
      int y = headY - ty;
      if (y < 0 || y >= h) continue;
      display->drawPixel(x, y, ty == 0 ? green : dim);
    }
  }
}

static void drawEffectConfetti(unsigned long elapsed, unsigned long duration) {
  display->fillScreen(0);
  int16_t w = display->width(), h = display->height();
  unsigned long frame = elapsed / 60;
  for (int i = 0; i < 40; i++) {
    uint32_t seed = wangHash((uint32_t)(frame / 5) * 131u + (uint32_t)i * 977u);
    if ((seed % 10) > 3) continue; // only a fraction light up each window
    int x = seed % w;
    int y = (seed >> 8) % h;
    display->drawPixel(x, y, colorWheel((uint8_t)(seed >> 16)));
  }
}

static void drawEffectPlasma(unsigned long elapsed, unsigned long duration) {
  int16_t w = display->width(), h = display->height();
  float t = elapsed / 400.0f;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      float v = sinf(x * 0.3f + t) + sinf(y * 0.5f + t * 1.3f) + sinf((x + y) * 0.2f - t);
      uint8_t hue = (uint8_t)(((v + 3.0f) / 6.0f) * 255);
      display->drawPixel(x, y, colorWheel(hue));
    }
  }
}

static void drawEffectStarfield(unsigned long elapsed, unsigned long duration) {
  display->fillScreen(0);
  int16_t w = display->width(), h = display->height();
  float cx = w / 2.0f, cy = h / 2.0f;
  const int NUM_STARS = 30;
  for (int i = 0; i < NUM_STARS; i++) {
    uint32_t seed = wangHash((uint32_t)i * 9973u);
    float angle = (seed % 360) * 0.0174533f;
    float speed = 4.0f + (seed % 8);
    unsigned long cycleMs = (unsigned long)(30000.0f / speed);
    unsigned long age = (elapsed + (seed % 3000)) % cycleMs;
    float radiusFrac = (float)age / (float)cycleMs;
    float radius = radiusFrac * (w * 0.7f);
    int x = (int)(cx + radius * cosf(angle));
    int y = (int)(cy + radius * sinf(angle) * 0.4f);
    if (x < 0 || x >= w || y < 0 || y >= h) continue;
    uint8_t b = (uint8_t)(50 + radiusFrac * 205);
    display->drawPixel(x, y, display->color565(b, b, b));
  }
}

typedef void (*EffectFn)(unsigned long elapsed, unsigned long duration);
struct EffectInfo { EffectFn fn; const char* name; };
static const EffectInfo celebrationEffects[] = {
  { drawEffectParticles,   "particles" },
  { drawEffectRings,       "rings" },
  { drawEffectSpiral,      "spiral" },
  { drawEffectDissolve,    "dissolve" },
  { drawEffectWipe,        "wipe" },
  { drawEffectRainbowWave, "rainbowwave" },
  { drawEffectMatrixRain,  "matrixrain" },
  { drawEffectConfetti,    "confetti" },
  { drawEffectPlasma,      "plasma" },
  { drawEffectStarfield,   "starfield" },
};
static const int celebrationEffectCount = sizeof(celebrationEffects) / sizeof(celebrationEffects[0]);

static void playCelebrationEffect(unsigned long elapsed, unsigned long duration, int index) {
  if (index < 0 || index >= celebrationEffectCount) index = 0;
  celebrationEffects[index].fn(elapsed, duration);
}

int pickRandomEffectIndex() {
  return random(0, celebrationEffectCount);
}

/**
 * @brief Parses an effect-selection MQTT payload: an effect name (case-insensitive),
 * a numeric index, or "random" (-1, resolved freshly each time it's used).
 */
int parseEffectIndex(const String &msgIn) {
  String m = msgIn;
  m.trim();
  if (m.equalsIgnoreCase("random")) return -1;

  bool isNumeric = m.length() > 0;
  for (size_t i = 0; i < m.length(); i++) {
    if (!isDigit(m[i])) { isNumeric = false; break; }
  }
  if (isNumeric) {
    int idx = m.toInt();
    return (idx >= 0 && idx < celebrationEffectCount) ? idx : 0;
  }
  for (int i = 0; i < celebrationEffectCount; i++) {
    if (m.equalsIgnoreCase(celebrationEffects[i].name)) return i;
  }
  return 0;
}

/**
 * @brief Standalone sign mode (ledSign/mode = "effectCycle") that loops through
 * every celebration effect in turn, independent of the pomodoro cycle — handy
 * for showing them all off or sanity-checking a change to one of them.
 */
void modeEffectCycle() {
  const unsigned long PER_EFFECT_MS = 8000;
  unsigned long index = (millis() / PER_EFFECT_MS) % celebrationEffectCount;
  unsigned long elapsed = millis() % PER_EFFECT_MS;
  playCelebrationEffect(elapsed, PER_EFFECT_MS, (int)index);

  static unsigned long lastIndex = 0xFFFFFFFF;
  if (index != lastIndex) {
    lastIndex = index;
    DEBUG_PRINTLN("Effect Cycle: now playing " + String(celebrationEffects[index].name));
  }
}

// --- Pomodoro phase rendering ---

/**
 * @brief Like drawCenteredBoth, but first paints a padded backing box behind
 * the text so it stays readable regardless of what's already on screen
 * (a pulsing/flashing full-screen fill, in practice) instead of relying on
 * the text color alone to contrast against it.
 */
static void drawCenteredBothBacked(const String &text, uint16_t textColor, uint16_t backingColor, int16_t pad = 2) {
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (display->width() - (int16_t)w) / 2 - x1;
  int16_t y = (display->height() - (int16_t)h) / 2 - y1;
  display->fillRect(x + x1 - pad, y + y1 - pad, (int16_t)w + pad * 2, (int16_t)h + pad * 2, backingColor);

  display->setTextColor(textColor);
  display->setCursor(x, y);
  display->print(text);
}

static void drawSprintIntro(unsigned long elapsed, unsigned long duration) {
  int16_t w = display->width(), h = display->height();
  uint16_t green = display->color565(0, 255, 0);

  display->setFont(&FreeSansBold9pt7b);
  String text = pomodoroOrdinal(pomodoroSprintNum) + " Sprint";

  // Measure the text so the stage-1 side bars never overlap it. "1st"/"2nd"/
  // "3rd"/"4th" render at very slightly different widths in this proportional
  // font (digit glyphs aren't all the same width — same issue as the clock
  // digits elsewhere), so a fixed bar width that clears "1st Sprint" can end
  // up a couple pixels into "2nd Sprint". Clamp to whatever margin the
  // actual current text leaves, instead of guessing a constant.
  int16_t tx1, ty1;
  uint16_t tw, th;
  display->getTextBounds(text, 0, 0, &tx1, &ty1, &tw, &th);
  int16_t textLeftEdge = (w - (int16_t)tw) / 2; // the x1 bearing offset cancels out here
  int16_t sideBarWidth = 6;
  if (textLeftEdge < sideBarWidth) sideBarWidth = (textLeftEdge > 0) ? textLeftEdge : 0;

  // barWidth is how much green shows on each side, from the left/right edges
  // inward: 0 = none, sideBarWidth = the initial side bars, w/2 = the two
  // bars meet in the middle and the whole screen is green. No text backing
  // in any stage: a backing box wide enough to matter would permanently
  // punch a black hole in the middle of the screen, which defeats both the
  // stage-2 sweep (it'd never visibly reach the actual center) and the
  // stage-3 blink (it'd only blink at the edges, never as a true full-screen
  // blink). Plain white text on solid green reads fine without one.
  int16_t barWidth;
  if (elapsed < POMODORO_INTRO_EDGE_BLINK_MS) {
    // Stage 1: blinking green side bars.
    bool on = ((elapsed / 300) % 2) == 0;
    barWidth = on ? sideBarWidth : 0;
  } else if (elapsed < POMODORO_INTRO_EDGE_BLINK_MS + POMODORO_INTRO_WIPE_MS) {
    // Stage 2: same sweep mechanic as the "wipe" celebration effect
    // (drawEffectWipe) — a full 0-to-edge sweep, mirrored in from both sides
    // so the two bars grow to meet in the middle.
    unsigned long wipeElapsed = elapsed - POMODORO_INTRO_EDGE_BLINK_MS;
    float frac = (float)wipeElapsed / (float)POMODORO_INTRO_WIPE_MS;
    barWidth = (int16_t)((w / 2.0f) * frac);
  } else {
    // Stage 3: the entire green background blinks on/off a few times.
    unsigned long blinkElapsed = elapsed - POMODORO_INTRO_EDGE_BLINK_MS - POMODORO_INTRO_WIPE_MS;
    bool on = ((blinkElapsed / POMODORO_INTRO_FULL_BLINK_PERIOD_MS) % 2) == 0;
    barWidth = on ? (w / 2) : 0;
  }

  display->fillScreen(0);
  if (barWidth > 0) {
    display->fillRect(0, 0, barWidth, h, green);
    display->fillRect(w - barWidth, 0, barWidth, h, green);
  }

  drawCenteredBoth(text, 0xFFFF);
}

static void drawSprintActive(unsigned long elapsed, unsigned long duration) {
  unsigned long remaining = (elapsed >= duration) ? 0 : (duration - elapsed);
  unsigned long remSecs = remaining / 1000;
  float frac = (float)elapsed / (float)duration;

  drawProgressBar(0, 2, frac, display->color565(0, 200, 0));

  char buf[8];
  sprintf(buf, "%02lu:%02lu", remSecs / 60, remSecs % 60);
  display->setFont(&FreeSans7pt7b);
  drawCenteredDigits(10, String(buf), display->color565(220, 220, 220));

  bool haveTask = todoLine2.length() > 0 && !todoLine2.equalsIgnoreCase("None");
  bool previewFresh = taskBeingCycled && (millis() - lastTaskCycleMillis < POMODORO_TASK_SELECT_TIMEOUT_MS);
  // Show the task line only if it's actually started, or it's a candidate
  // that's been cycled in recently (not stale leftover text, and not a
  // preview that's sat unselected past the timeout).
  bool showTask = pomodoroShowTask && haveTask && (todoClockRunning || previewFresh);

  if (showTask) {
    uint16_t taskColor = display->color565(0, 255, 255);

    // Reset the scroll whenever the text itself changes, or when switching
    // between the arrow-prefixed preview and the plain started name (the
    // scrollable region's width differs between the two), so neither shows
    // a stale mid-scroll position for what is effectively new content.
    static String lastShownTaskKey = "";
    String taskKey = (todoClockRunning ? "started:" : "preview:") + todoLine2;
    if (taskKey != lastShownTaskKey) {
      lastShownTaskKey = taskKey;
      scroll2.pos = 96;
      scroll2.startMillis = millis();
    }

    if (todoClockRunning) {
      // Committed to this task: plain name, full width.
      drawScrollingLine(0, 96, 15, todoLine2, &Picopixel, taskColor, scroll2);
    } else {
      // Previewing a candidate, not yet started: keep the arrow static at
      // the left and only scroll the task name in the remaining width.
      static const char *ARROW = "-----> ";
      display->setFont(&Picopixel);
      int16_t ax1, ay1;
      uint16_t aw, ah;
      display->getTextBounds(ARROW, 0, 15, &ax1, &ay1, &aw, &ah);

      drawScrollingLine((int16_t)aw, 96, 15, todoLine2, &Picopixel, taskColor, scroll2);

      // drawScrollingLine doesn't clip to its [x_min, x_max) window, so the
      // scrolled name can still paint past the arrow's edge. Mask that
      // sliver back to black and redraw the arrow on top so it stays
      // static and the two never visually overlap.
      display->fillRect(0, 15 + ay1, (int16_t)aw, (int16_t)ah, 0);
      display->setFont(&Picopixel);
      display->setTextColor(taskColor);
      display->setCursor(0, 15);
      display->print(ARROW);
    }
  }
}

static void drawBreakIntro(unsigned long elapsed, unsigned long duration) {
  bool on = ((elapsed / 300) % 2) == 0;
  display->fillScreen(on ? display->color565(255, 120, 0) : 0); // same orange as the break's bar/label
  display->setFont(&FreeSansBold9pt7b);
  drawCenteredBothBacked("Break", 0xFFFF, 0);
}

static void drawBreakActive(unsigned long elapsed, unsigned long duration, bool isLong) {
  unsigned long remaining = (elapsed >= duration) ? 0 : (duration - elapsed);
  unsigned long remSecs = remaining / 1000;
  float frac = (float)elapsed / (float)duration;
  uint16_t barColor = isLong ? display->color565(0, 120, 255) : display->color565(255, 120, 0);

  drawProgressBar(0, 2, frac, barColor);

  char buf[8];
  sprintf(buf, "%02lu:%02lu", remSecs / 60, remSecs % 60);
  display->setFont(&FreeSans7pt7b);
  drawCenteredDigits(10, String(buf), display->color565(220, 220, 220));

  display->setFont(&Org_01);
  drawCentered(15, isLong ? "LONG BREAK" : "BREAK", barColor);
}

static unsigned long pomodoroPhaseDurationMs() {
  switch (pomodoroPhase) {
    case POMO_SPRINT_INTRO: return POMODORO_INTRO_DURATION_MS;
    case POMO_SPRINT:       return (unsigned long)POMODORO_SPRINT_MINS * 60000UL;
    case POMO_BREAK_INTRO:  return POMODORO_BREAK_INTRO_DURATION_MS;
    case POMO_BREAK:        return (unsigned long)POMODORO_SHORT_BREAK_MINS * 60000UL;
    case POMO_FIREWORKS:    return POMODORO_FIREWORKS_DURATION_MS;
    case POMO_LONG_BREAK:   return (unsigned long)POMODORO_LONG_BREAK_MINS * 60000UL;
  }
  return 1000;
}

/**
 * @brief Resets the pomodoro cycle back to sprint 1 / intro phase. Called whenever
 * `pomodoro` is (re)selected as the sign mode, or on an explicit reset command —
 * selecting it always starts fresh, it never resumes a prior cycle.
 */
/**
 * @brief Freezes the todo elapsed clock for a pomodoro break, if a task is
 * actually running -- remembers that it was paused *for a break* (as opposed
 * to not running at all) so resumeTodoAfterBreak() knows to restart it.
 */
void pauseTodoForBreak() {
  if (todoClockRunning) {
    todoFinalElapsedSecs = getCurrentElapsedSecs();
    todoClockRunning = false;
    todoPausedForBreak = true;
    DEBUG_PRINTLN("Todo clock paused for pomodoro break at " + String(todoFinalElapsedSecs) + "s");
  }
}

/**
 * @brief Resumes the todo elapsed clock after a pomodoro break, but only if
 * pauseTodoForBreak() actually paused one -- a no-op when no task was active.
 */
void resumeTodoAfterBreak() {
  if (todoPausedForBreak) {
    todoStartMillis = millis();
    todoClockRunning = true;
    todoPausedForBreak = false;
    DEBUG_PRINTLN("Todo clock resumed for pomodoro sprint from " + String(todoFinalElapsedSecs) + "s");
  }
}

void resetPomodoroCycle() {
  pomodoroSprintNum = 1;
  pomodoroPhase = POMO_SPRINT_INTRO;
  pomodoroPhaseStartMillis = millis();
  pomodoroPaused = false;
  scroll2.pos = 96;
  scroll2.startMillis = millis();
  // A fresh pomodoro session starts with no task preview showing, even if
  // todoLine2 still holds stale leftover text from a previous session --
  // the arrow only reappears once a genuinely new candidate is cycled in.
  taskBeingCycled = false;
  // Resetting always lands on a fresh sprint-to-be (SPRINT_INTRO -> SPRINT),
  // so any task that was paused for a break should resume immediately rather
  // than staying stuck paused with no BREAK/LONG_BREAK transition left to
  // resume it.
  resumeTodoAfterBreak();
}

/**
 * @brief Advances to the next phase in the pomodoro state machine (see the
 * transition graph in the design: intro -> sprint -> break (or fireworks on the
 * 4th) -> ... -> long break -> back to sprint 1). Also pauses/resumes the
 * background todo elapsed clock in lockstep: paused for the whole
 * break/celebration span (SPRINT -> ... -> next SPRINT_INTRO), running during
 * sprints.
 */
void advancePomodoroPhase() {
  switch (pomodoroPhase) {
    case POMO_SPRINT_INTRO:
      pomodoroPhase = POMO_SPRINT;
      scroll2.pos = 96;
      scroll2.startMillis = millis();
      break;
    case POMO_SPRINT:
      pauseTodoForBreak();
      if (pomodoroSprintNum >= POMODORO_SPRINTS_BEFORE_LONG_BREAK) {
        pomodoroPhase = POMO_FIREWORKS;
        activeCelebrationEffect = (pomodoroEffectIndex == -1)
            ? random(0, celebrationEffectCount)
            : pomodoroEffectIndex;
      } else {
        pomodoroPhase = POMO_BREAK_INTRO;
      }
      break;
    case POMO_BREAK_INTRO:
      pomodoroPhase = POMO_BREAK;
      break;
    case POMO_BREAK:
      pomodoroSprintNum++;
      pomodoroPhase = POMO_SPRINT_INTRO;
      resumeTodoAfterBreak();
      break;
    case POMO_FIREWORKS:
      pomodoroPhase = POMO_LONG_BREAK;
      break;
    case POMO_LONG_BREAK:
      pomodoroSprintNum = 1;
      pomodoroPhase = POMO_SPRINT_INTRO;
      resumeTodoAfterBreak();
      break;
  }
  pomodoroPhaseStartMillis = millis();
  pomodoroPaused = false;
}

void modePomodoro() {
  unsigned long duration = pomodoroPhaseDurationMs();
  unsigned long rawElapsed = pomodoroPaused
      ? (pomodoroPauseStartMillis - pomodoroPhaseStartMillis)
      : (millis() - pomodoroPhaseStartMillis);
  // Clamp (rather than advance-then-draw) so the final frame of a phase is
  // actually rendered at 100%/0:00 before switching — otherwise the phase
  // swaps out from under the draw call on the exact frame completion is
  // detected, and the "done" state is never visible (same underlying issue
  // as the wipe effect's sweep never visibly reaching the right edge).
  unsigned long elapsed = (rawElapsed > duration) ? duration : rawElapsed;

  switch (pomodoroPhase) {
    case POMO_SPRINT_INTRO: drawSprintIntro(elapsed, duration); break;
    case POMO_SPRINT:       drawSprintActive(elapsed, duration); break;
    case POMO_BREAK_INTRO:  drawBreakIntro(elapsed, duration); break;
    case POMO_BREAK:        drawBreakActive(elapsed, duration, false); break;
    case POMO_FIREWORKS:    playCelebrationEffect(elapsed, duration, activeCelebrationEffect); break;
    case POMO_LONG_BREAK:   drawBreakActive(elapsed, duration, true); break;
  }

  if (!pomodoroPaused && rawElapsed >= duration) {
    advancePomodoroPhase();
  }
}

/**
 * @brief Plays a random celebration effect when a todo task is marked complete
 * (nextTodo/select/completed), then reverts to modeBeforeTaskComplete -- either
 * back to "pomodoro" (which will pick up the next task once nextTodo/personal
 * is updated) or to whatever mode was showing before the standalone todoClock
 * flow took over.
 */
void modeTaskComplete() {
  unsigned long elapsed = millis() - taskCompleteStartMillis;
  if (elapsed >= TASK_COMPLETE_EFFECT_DURATION_MS) {
    currentMode = modeBeforeTaskComplete;
    pendingCommand = true;
    return;
  }
  playCelebrationEffect(elapsed, TASK_COMPLETE_EFFECT_DURATION_MS, taskCompleteEffectIndex);
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

  if (effectPreviewActive) {
    unsigned long elapsed = millis() - effectPreviewStartMillis;
    if (elapsed >= POMODORO_FIREWORKS_DURATION_MS) {
      effectPreviewActive = false;
    } else {
      playCelebrationEffect(elapsed, POMODORO_FIREWORKS_DURATION_MS, effectPreviewIndex);
    }
  }
  else if (currentMode == "clock") modeClock();
  else if (currentMode == "bigClock") modeBigClock();
  else if (currentMode == "rainbowClock") modeRainbowClock();
  else if (currentMode == "gmtClock") modeGmtClock();
  else if (currentMode == "message") modeMessage();
  else if (currentMode == "scroll") modeScroll();
  else if (currentMode == "static") modeStatic();
  else if (currentMode == "test") modeTestPattern();
  else if (currentMode == "calibrate") modeCalibration();
  else if (currentMode == "fontTest") modeFontTest();
  else if (currentMode == "messageTest") modeMessageTest();
  else if (currentMode == "eventCountdown") modeEventCountdown();
  else if (currentMode == "nextEvent") modeNextEvent();
  else if (currentMode == "showTodo") modeNextTodo();
  else if (currentMode == "todoClock") modeNextTodoClock();
  else if (currentMode == "pomodoro") modePomodoro();
  else if (currentMode == "effectCycle") modeEffectCycle();
  else if (currentMode == "taskComplete") modeTaskComplete();
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

  // Periodic publish of elapsed time to nextTODO/personal/elapsed every 10 seconds
  static unsigned long lastMqttPublishMillis = 0;
  if (todoClockRunning) {
    if (millis() - lastMqttPublishMillis >= 10000) {
      lastMqttPublishMillis = millis();
      publishElapsed(getCurrentElapsedSecs());
    }
  } else {
    lastMqttPublishMillis = 0;
  }

  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 60000) {
    lastHeartbeat = millis();
    DEBUG_PRINTLN("Heartbeat - Uptime: " + String(millis()/1000) + "s");
  }
}
