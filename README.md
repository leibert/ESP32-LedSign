Replatforming of https://github.com/leibert/mqttLedSign from an ESP8266 to an ESP32 to control 3 HUB75 LED panels.

I'm using this carrier board for the ESP32 (https://www.amazon.com/dp/B0FVNMRRTB) which replaced an ESP8266 interface board I had made previously.

## Hardware

- 3x 32x16 HUB75 panels chained for a 96x16 virtual display
- ESP32 Dev Module, custom pin mapping (see `src/main.cpp`)

## Features

- Time/date display in several styles (plain, big, GMT, rainbow)
- Scrolling text, static lines, and MQTT-driven messages
- Event countdown (meetings, etc.) with flashing urgency cues
- Personal todo display with a start/stop/complete elapsed-time clock
- **Pomodoro mode**: a self-driving 4-sprint work/break cycle with a library of
  LED celebration effects (see below)
- OTA updates via `ArduinoOTA` (hostname `ledsign`, so `ledsign.local` works
  as the `upload_port` in `platformio.ini` instead of a hardcoded IP)

## Pomodoro Mode

Select it with `ledSign/mode` = `pomodoro`. It then runs on its own, and owns
the display: while it's active (including the few seconds of its
post-completion celebration), any `ledSign/mode` request other than
`pomodoro`, `eventCountdown`, or the internal `taskComplete` is ignored
outright, rather than needing each disruptive mode to be named and blocked
individually.

```
1st/2nd/3rd Sprint -> Break -> 2nd/3rd/4th Sprint -> ... -> 4th Sprint
  -> celebration effect -> Long Break -> back to 1st Sprint (repeats)
```

Each sprint starts with a 3-stage green intro, with the "Nth Sprint" title
visible throughout: blinking green side bars, then those bars expanding
inward until they meet (a fully green background), then the green
background blinking on/off a few times. It then shows a countdown, a
progress bar, and (optionally) the current task from `nextTodo` while it
runs. Each short break starts with a red blink signal, then its own
countdown + progress bar. After the 4th sprint, instead of a short break it
plays a celebration effect before settling into the long break.

Segment lengths are compile-time constants in `src/main.cpp`:

| Constant                              | Default | Meaning                              |
|----------------------------------------|--------:|---------------------------------------|
| `POMODORO_SPRINT_MINS`                 | 25      | Work segment length                   |
| `POMODORO_SHORT_BREAK_MINS`            | 5       | Short break length (after sprints 1-3)|
| `POMODORO_LONG_BREAK_MINS`             | 20      | Long break length (after sprint 4)    |
| `POMODORO_SPRINTS_BEFORE_LONG_BREAK`   | 4       | Sprints per cycle before the long break |
| `POMODORO_INTRO_EDGE_BLINK_MS`         | 2000    | Sprint intro stage 1: blinking green side bars |
| `POMODORO_INTRO_WIPE_MS`               | 1000    | Sprint intro stage 2: side bars expand inward until they meet |
| `POMODORO_INTRO_FULL_BLINK_COUNT`      | 3       | Sprint intro stage 3: number of on/off blinks of the full green background |
| `POMODORO_INTRO_FULL_BLINK_PERIOD_MS`  | 400     | Sprint intro stage 3: duration of each on (or off) half-blink |
| `POMODORO_INTRO_DURATION_MS`           | 5400    | Sum of the 3 sprint-intro stages above (derived, not set directly) |
| `POMODORO_BREAK_INTRO_DURATION_MS`     | 1800    | Red blink signal before a short break |
| `POMODORO_FIREWORKS_DURATION_MS`       | 8000    | Celebration effect length after the 4th sprint; also the `testEffect` preview length |
| `TASK_COMPLETE_EFFECT_DURATION_MS`     | 3000    | Celebration length when a todo task is marked done   |

### Celebration effects

Ten effects, shared between the post-4th-sprint celebration, the task-complete
celebration, and the `effectCycle` demo mode: `particles`, `rings`, `spiral`,
`dissolve`, `wipe`, `rainbowwave`, `matrixrain`, `confetti`, `plasma`,
`starfield`.

### Task-complete celebration

When the active todo task is marked done (`nextTODO/select/completed`), the
sign plays a random celebration effect for `TASK_COMPLETE_EFFECT_DURATION_MS`
(default 3000ms), then returns to whatever clock mode was showing before the
task's countdown started.

### Todo task tracking during pomodoro

While `pomodoro` is the active mode, `nextTODO/select/start`/`stop`/`completed`
drive the same elapsed-time clock as the standalone `todoClock` mode, but
without leaving pomodoro's own sprint/break display:

- No active task (nothing started, `nextTODO/personal` is empty/`None`, or
  there's just stale leftover text from a previous session): the sprint
  screen omits the task line entirely. Selecting `pomodoro` never shows an
  old task as a preview on its own — only a genuinely new candidate does.
- A task is being previewed but not yet started (a new value arrived on
  `nextTODO/personal`/`line1`/`line2` but `start` hasn't been issued for it —
  e.g. while cycling through candidates): shown prefixed with a static arrow,
  scrolling only the task name if it's too long to fit: `-----> Sample next
  task`. If it sits unstarted for `POMODORO_TASK_SELECT_TIMEOUT_MS` (default
  2 minutes) with no further candidates arriving, the preview is hidden.
- A task is started: the arrow drops and it shows as the plain task name, and
  its elapsed time accrues in the background (not shown on screen, but
  tracked for the `nextTODO/personal/elapsed` publish on stop/complete).
- The elapsed clock automatically pauses for every break (from the moment a
  sprint ends until the next sprint begins, including the 4th-sprint
  celebration and the long break) and resumes when the next sprint starts —
  no time accrues while on a break. Manually pausing the whole pomodoro cycle
  (`ledSign/pomodoro` = `pause`) pauses the task clock the same way.
- Marking the task done celebrates as above, then returns to `pomodoro`
  (instead of whatever showed before pomodoro started) — the sprint screen
  then simply shows the next task once the external system updates
  `nextTODO/personal` for it.

## MQTT Topics

| Topic                      | Payload / Description                                    |
|-----------------------------|-----------------------------------------------------------|
| `ledSign/mode`              | Display mode (`clock`, `bigClock`, `rainbowClock`, `gmtClock`, `message`, `scroll`, `static`, `test`, `calibrate`, `fontTest`, `messageTest`, `eventCountdown`, `nextEvent`, `showTodo`, `todoClock`, `pomodoro`, `effectCycle`) |
| `ledSign/line1`/`2`/`3`     | Text for `static` mode's three lines                      |
| `ledSign/color`             | Primary text color, comma-separated RGB e.g. `255,0,0`    |
| `ledSign/timezone`          | UTC offset in hours, e.g. `-5`                             |
| `ledSign/EN`                | `ON`/`OFF` to enable or disable the whole display          |
| `ledSign/countdownEN`       | `ON`/`OFF` to allow/block `eventCountdown` mode requests    |
| `ledSign/messageEN`         | `ON`/`OFF` to allow/block `message` mode requests |
| `ledSign/message/header`    | Header line for `message` mode                             |
| `ledSign/message/type`      | Message type, shown with the sender on the header line      |
| `ledSign/message/sender`    | Message sender, shown with the type on the header line      |
| `ledSign/message/text`      | Message body                                                |
| `nextEvent/timeStamp`       | Unix timestamp of the next event (for `eventCountdown`/`nextEvent`) |
| `nextEvent/subject`        | Event subject                                               |
| `nextEvent/organizer`       | Event organizer                                             |
| `nextEvent/attendees`       | Event attendees                                             |
| `nextTODO/personal`         | Todo text; `line1\nline2` or `line1\|line2`, else treated as line2 with a default header |
| `nextTODO/personal/line1`   | Todo header line directly                                   |
| `nextTODO/personal/line2`   | Todo detail line directly                                   |
| `nextTODO/personal/elapsed` | `HH:MM` elapsed-time baseline (also where the sign publishes its own periodic ticks) |
| `nextTODO/select`/`start`   | Start the todo elapsed clock (switches to `todoClock` mode, unless `pomodoro` is already active — see below) |
| `nextTODO/select`/`stop`    | Stop the clock, revert to the mode showing beforehand (or stay in `pomodoro`) |
| `nextTODO/select`/`completed`| Mark the task done: celebrate, then revert to the mode showing beforehand (or to `pomodoro`) |
| `nextTODO/select`/`reset`   | Clear the clock and go straight to `bigClock` (no celebration) |
| `ledSign/pomodoro`          | `start`/`stop`/`toggle`/`pause`/`resume`/`skip`/`reset` — control the pomodoro cycle (`toggle` starts it if idle, or stops + resets it if running) |
| `ledSign/pomodoro/taskEN`   | `ON`/`OFF` — show the current `nextTODO` task during sprints (default ON) |
| `ledSign/pomodoro/effect`   | Effect name, numeric index, or `random` — celebration effect used after the 4th sprint |
| `ledSign/pomodoro/testEffect`| Effect name, numeric index, or `random` — previews that effect for 8s over whatever's currently on screen |

Accepts `nextTodo/`, `nextTODO/`, or `nexttodo/` (case-insensitive) for all of
the todo topics above.
