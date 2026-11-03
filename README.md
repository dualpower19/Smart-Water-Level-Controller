# Smart Water Level Controller

ESP32 firmware for a pump controller that keeps an overhead tank filled. It uses:

- an **RS485 (Modbus RTU) 750 cm ultrasonic level sensor** to measure the tank level,
- a **current transformer (CT)** on the ESP32 ADC to measure the pump's current (true RMS),
- an optional **ZMPT101B** module to measure mains voltage,
- a **relay that drives the pump contactor coil**,
- an **SSD1306 OLED** to show status on the panel,
- a **Wi-Fi web dashboard** to control the pump, change settings and update firmware over the air.

## Features

| | |
|---|---|
| **Auto pump control** | Starts at the *start level* (default 30 %) and stops at the *stop level* (default 95 %). If a fill is interrupted, it resumes once the problem clears. |
| **Modes** | **AUTO**: fills on its own. **MANUAL**: fills only when you press Start, and still stops when the tank is full. **OFF**: the pump never runs. The mode is kept across reboots. |
| **Overload** | Trips when CT current stays above the limit for the trip delay (default 8 A for 3 s). Short inrush spikes are ignored. This fault **locks**: someone has to press Reset. |
| **Underload / dry run** | Trips when CT current stays below the limit (default 3 A) after a priming grace period. It retries automatically after 30 min, up to 3 times, then locks. An unplugged CT reads 0 A, so it trips this too. |
| **Level not rising** | Trips if the level rises less than 2 % in a 10 min window while pumping, which catches a lost prime, a closed valve or a burst pipe. Same retry and lock rules as dry run. |
| **Max run time** | Optional hard limit on one run (default 60 min). |
| **Over / under voltage** | *Optional. Needs a ZMPT101B and is off by default.* Trips outside 180–260 V, with a trip delay and hysteresis. Clears automatically after the voltage has been back in range for 30 s. |
| **Sensor fail-safe** | Stops the pump if the level sensor stops giving valid readings for 10 s, or the CT/voltage input loses its bias (a broken bias network or wiring). Clears automatically when the signal returns. |
| **Anti short-cycle** | Minimum off time between runs (default 60 s). The same delay applies after power-up, so the pump never starts the instant mains returns. |
| **Sump float (optional)** | A float switch in the sump/underground tank holds the pump off while the sump is empty. |
| **Watchdog** | If the firmware hangs for more than 5 s the ESP32 reboots and the relay drops out. |
| **OLED** | Tank graphic, %, litres, pump state and run time, pump current (and voltage), and the fault or status. |
| **Web dashboard** | Live status with a current bar showing both trip limits, Start/Stop/Reset, mode selection, an event log, every setting, one-click tank and CT calibration, and OTA updates. |

## Hardware

| Part | Notes |
|---|---|
| ESP32 DevKit V1 (ESP32-WROOM-32) | |
| RS485 ultrasonic level sensor, 0–750 cm, Modbus RTU | Usually powered from 12–24 V DC. Check its datasheet for the distance register. |
| TTL↔RS485 module | Prefer a 3.3 V part (MAX3485/SP3485). A plain MAX485 needs 5 V, so add a level shift on RO, or use an auto-direction module. |
| Current transformer | Clamp-on **SCT-013-030** (30 A / 1 V, built-in burden) is the default. Any CT works: see *CT input* below. |
| ZMPT101B voltage module | Optional. Only needed for over/under-voltage protection. |
| 1-channel relay module | Switches the **contactor coil**. Never switch the motor directly with the relay. |
| Contactor + MCB + **thermal overload relay / MPCB** | The software protections add to proper motor protection. They do not replace it. |
| SSD1306 128×64 I²C OLED | Optional. The controller runs without it. |
| Push button, active buzzer, sump float switch | Optional. |
| 5 V supply for the ESP32 (HLK-PM01 or similar) | |

### Wiring

| ESP32 GPIO | Connects to |
|---|---|
| 16 (RX2) | RS485 module RO |
| 17 (TX2) | RS485 module DI |
| 4 | RS485 module DE + RE, tied together (set `PIN_RS485_DE -1` for auto-direction modules) |
| 34 (ADC1) | CT signal, through the bias network |
| 35 (ADC1) | ZMPT101B OUT (optional) |
| 21 / 22 | OLED SDA / SCL |
| 25 | Relay IN (`RELAY_ACTIVE_HIGH` in `include/config.h`; set it to 0 for active-low boards) |
| 32 | Push button → GND |
| 14 | Sump float switch → GND (closed = water present) |
| 33 | Active buzzer (through an NPN transistor) |
| 2 | On-board status LED |

- **RS485**: A→A, B→B. Use twisted pair. Fit a 120 Ω terminator at the far end on long runs, and connect the sensor supply's GND to the ESP32's GND.
- Analog inputs must be on **ADC1** (GPIO 32–39). ADC2 can't be read while Wi-Fi is on.

### CT input

The CT outputs AC, but the ESP32 can only measure 0–3.3 V. A bias network shifts the signal to sit around 1.65 V:

```
3.3V ──[10k]──┬──[10k]── GND
              ├──|10µF|── GND          bias ≈ 1.65 V
              │
              └── CT lead 1
                  CT lead 2 ──[1k]──┬── GPIO34
                                    └──|100nF|── GND
```

- Clamp the CT around **one** wire (the phase) of the pump feed, after the contactor. Clamping both wires makes the currents cancel to zero.
- **SCT-013-030** (and the other SCT-013 voltage-output types) already has its burden resistor, so wire it as shown.
- **Current-output CTs** (SCT-013-000, or a panel CT with a 5 A secondary via a small interposing CT) need a burden resistor across the CT leads. Pick it so the pump's *overload* current gives ≤ ~0.5 V RMS. Then set *Amps per ADC volt* = primary A ÷ (secondary A × burden Ω). For example, SCT-013-000 (100 A : 50 mA) with 33 Ω gives 60.6.
- Keep the waveform peaks inside about 0.15–3.1 V (the 11 dB ADC range). With an SCT-013-030 that allows up to about 50 A. The ADC is most accurate in the middle of the range, so a CT sized close to the pump's current gives the best resolution (SCT-013-010 or -015 for a 1 HP pump).
- The firmware samples 10 full mains cycles, removes the DC bias and computes true RMS. If the bias reads outside 0.9–2.4 V, the input counts as broken and the pump is stopped. The dashboard shows the bias voltage so you can check it.

### Voltage input (optional)

The ZMPT101B module's output already sits on a mid-rail bias. Power the module from **3.3 V** (not 5 V) so its output stays within the ADC range, and connect OUT → GPIO35. Then tick *Voltage → Sensor fitted* in the settings and calibrate it against a multimeter.
- Change pins in `include/config.h`. Avoid the strapping pins 0, 2, 5, 12 and 15 for anything that must not change state at boot.

> ⚠️ This switches mains-powered motors. Wire it in a proper enclosure with an MCB and a thermal overload relay, and have it checked by a qualified electrician.

## Build & flash

Requires [PlatformIO](https://platformio.org/) (VS Code extension or `pip install platformio`).

```bash
pio run -e esp32dev -t upload     # build + flash over USB
pio device monitor                # serial log, 115200 baud
pio test -e native                # run the control-logic unit tests on your PC
```

Later updates can be sent over Wi-Fi: **Settings → System → Firmware update**, then choose `.pio/build/esp32dev/firmware.bin`.

## First start

1. Power up. On a fresh board the OLED shows the setup access point.
2. Connect to Wi-Fi **`WaterLevel-Setup`** (password `waterlevel`) and open **http://192.168.4.1**.
3. Under **Settings → Network & security**, enter your Wi-Fi SSID and password, then **set a new admin password** (the default is `admin1234`). Save. The controller restarts and joins your network.
4. Open **http://waterlevel.local**, or the IP shown on the OLED.

Buttons that change anything ask for the admin password once. Username: `admin`.

If the controller loses Wi-Fi for 60 s, it brings the setup AP back up. Pump control never depends on Wi-Fi.

## Calibration

### 1. Level sensor
The status card shows the **raw** register value and the **distance** it was converted to.

- Defaults: slave address 1, 9600 baud, function 3 (holding register), register `0x0100`, 1 count = 1 mm.
- These 750 cm sensors differ by vendor. Check the datasheet for the address, baud, the distance register (often `0x0100` for the processed value or `0x0101` for the real-time value) and its unit. Adjust them under **Settings → Level sensor**.
- Measure the real distance to the water with a tape and compare it to the reading. Then change *mm per count* to match (10 if the sensor reports cm).

### 2. Tank
- **Sensor distance when EMPTY**: the distance from the sensor face to the water at your 0 % level, for example just above the outlet.
- **Sensor distance when FULL**: the distance at 100 %. Keep it **outside the sensor's blind zone** (typically 25–35 cm; see the datasheet).
- The quickest way is to drain or fill the tank to each point and press **Calibration → Set current reading as EMPTY / FULL**.
- **Usable capacity (L)** is used only for the litre readout.

### 3. Current transformer
1. With the pump **off**, press **Calibration → Zero CT**. This stores the noise floor, so an idle pump reads 0 A.
2. Run the pump. Measure the same wire with a clamp meter, enter the reading and press **Calibrate CT**. This adjusts *Amps per ADC volt*.
3. If fitted, enter a multimeter's mains reading and press **Calibrate voltage**.

### 4. Protection thresholds
Run the pump normally with water in **MANUAL**, and note the running current.

| Setting | Starting point |
|---|---|
| Overload current | 115–125 % of the motor's nameplate full-load current |
| Overload delay | 3–5 s (must cover the starting inrush) |
| Underload current | between the dry-running current and the normal running current (see below) |
| Priming grace | long enough for the pump to prime after a start (e.g. 20–60 s) |
| Level-not-rising | window × minimum rise must be achievable even while water is being used |
| Voltage band | 180–260 V for 230 V single-phase. Follow the pump maker's limits if they're tighter. |

**About underload by current:** an induction motor still draws its magnetising current when it runs dry. On small single-phase pumps the dry current can be 60–80 % of the normal running current, so the gap is smaller than with power-based detection. Find the dry current for your pump by briefly letting it lose prime, or from the maker's data. Set the trip about halfway between the dry and normal current. The *level-not-rising* check is the backup for cases where the current gap is too small.

Watch the event log for nuisance trips during the first few days and adjust.

## Using it

**Panel button**
- Short press: reset a **locked** fault. On a non-locked fault it silences the buzzer. Otherwise it starts or stops a fill.
- Hold for 3 s: cycle the mode AUTO → MANUAL → OFF.

**Status LED**: solid = pump running · fast blink = fault · short blink every 2 s = idle.

**Buzzer**: beeps for 10 s on an auto-clearing fault. For a locked fault it beeps every 5 s until reset.

**Stop in AUTO** cancels the current fill. The pump starts again when the level next drops to the start point. Use **OFF** to keep it off.

### Fault reference

| Fault | Clears |
|---|---|
| Overload | Manual reset only |
| Dry run (underload) / Level not rising / Max run time | Retries automatically after the retry delay. Locks after *max retries*; a successful fill resets the counter. |
| Over / Under voltage | Automatically, once the voltage has been inside the band (with hysteresis) for the restore delay |
| Level sensor fault | Automatically, when valid readings return |
| Current sensor fault | Automatically, when the CT input's bias is back in range |
| Voltage sensor fault | Automatically, when the voltage input is back (only when the voltage sensor is enabled) |

## HTTP API

All responses are JSON. The `[auth]` endpoints use HTTP Basic auth: user `admin`, password = admin password.

| | |
|---|---|
| `GET /api/status` | level, power, pump state |
| `GET /api/events` | last 40 events |
| `GET /api/config` | all settings (no passwords) |
| `POST /api/config` [auth] | partial JSON update, e.g. `{"ctl":{"startLevelPct":25}}` |
| `POST /api/cmd?cmd=…` [auth] | `start` `stop` `reset` `auto` `manual` `off` `cal_empty` `cal_full` `cal_ct_zero` `restart` `factory_reset` |
| `POST /api/cmd?cmd=cal_ct&value=5.2` [auth] | CT calibration against a clamp-meter reading (pump running) |
| `POST /api/cmd?cmd=cal_volt&value=231` [auth] | voltage calibration against a meter reading |
| `POST /api/update` [auth] | multipart firmware upload |

```bash
curl http://waterlevel.local/api/status
curl -u admin:yourpass -X POST "http://waterlevel.local/api/cmd?cmd=start"
```

## Project layout

```
include/config.h                 pins and compile-time defaults
lib/PumpController/              hardware-independent state machine + all protections
src/main.cpp                     setup/loop, relay, button, LED, buzzer
src/LevelSensor.*                RS485 Modbus sensor (own FreeRTOS task, median filter)
src/CurrentSensor.*              CT (and optional ZMPT101B) true-RMS sampling task
src/Display.*                    OLED
src/WifiManager.*                Wi-Fi STA + setup AP fallback, mDNS, NTP
src/WebUI.*, src/web_index.h     web server, JSON API, dashboard page
src/Settings.*                   settings persisted in NVS
test/test_pump_controller/       unit tests (pio test -e native)
```
