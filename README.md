Replatforming of https://github.com/leibert/mqttLedSign from an ESP8266 to an ESP32 to control 3 HUB75 Led Panels

I'm using this carrier board for the ESP32 (https://www.amazon.com/dp/B0FVNMRRTB) which replaced a ESP8266 interface board I had made previously.


# mqttLedSign

A simple Python program for controlling an RGB LED matrix panel via MQTT.
Designed to run on a Raspberry Pi using the [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) library.

## Features

- Display time, scrolling messages, and countdown to events
- Control lines, colors, and modes through MQTT topics



### MQTT Topics

| Topic                     | Description                                |
|--------------------------|--------------------------------------------|
| `ledSign/line1`          | Text for first line                        |
| `ledSign/line2`          | Text for second line                       |
| `ledSign/line3`          | Text for third line                        |
| `ledSign/color`          | RGB color, comma separated e.g. `255,0,0`  |
| `ledSign/mode`           | Display mode (`clock`, `bigClock`, etc.)   |
| `ledSign/EN`             | `ON`/`OFF` to enable or disable display    |
| `nextEvent/<field>`      | Event details for countdown mode           |
