#pragma once
// ---------------------------------------------------------------------------
// Hardware pin map and compile-time defaults (ESP32 DevKit V1 / ESP32-WROOM-32)
// Runtime-tunable values (thresholds, tank geometry, Wi-Fi) live in Settings
// and can be changed from the web dashboard; the values here are only the
// factory defaults used on first boot or after "factory reset".
// ---------------------------------------------------------------------------

#define FW_NAME    "Smart Water Level Controller"
#define FW_VERSION "1.0.0"

// ---- RS485 ultrasonic level sensor (Modbus RTU) on UART2 via MAX485 -------
#define PIN_RS485_RX      16
#define PIN_RS485_TX      17
#define PIN_RS485_DE      4     // DE+RE tied together. Set -1 for auto-direction modules.

// ---- Analog inputs (must be ADC1 pins 32..39 - ADC2 is unusable with Wi-Fi on)
#define PIN_CT_ADC        34    // pump current transformer (SCT-013 etc.) via 1.65 V bias network
#define PIN_VT_ADC        35    // optional ZMPT101B mains voltage module, -1 if not fitted

// ---- I2C OLED (SSD1306 128x64) --------------------------------------------
#define PIN_I2C_SDA       21
#define PIN_I2C_SCL       22
#define OLED_I2C_ADDR     0x3C
#define OLED_WIDTH        128
#define OLED_HEIGHT       64

// ---- Outputs ---------------------------------------------------------------
#define PIN_PUMP_RELAY    25    // Drives a relay that switches the CONTACTOR coil (not the motor directly)
#define RELAY_ACTIVE_HIGH 1     // 1 = HIGH energises relay, 0 = LOW energises (most opto relay boards)
#define PIN_BUZZER        33    // Active buzzer, -1 to disable
#define PIN_STATUS_LED    2     // On-board LED

// ---- Inputs ----------------------------------------------------------------
#define PIN_BUTTON        32    // Push button to GND (internal pull-up). Short: reset fault / start-stop in MANUAL. Long (3 s): cycle mode
#define PIN_SUMP_FLOAT    14    // Optional sump/underground-tank float switch to GND. Closed = water present.

// ---- Wi-Fi defaults --------------------------------------------------------
#define DEFAULT_HOSTNAME  "waterlevel"          // http://waterlevel.local
#define AP_SSID           "WaterLevel-Setup"
#define AP_PASSWORD       "waterlevel"          // min 8 chars
#define DEFAULT_ADMIN_PASSWORD "admin1234"       // protects pump control & settings. CHANGE IT.

// ---- Timing ----------------------------------------------------------------
#define SENSOR_POLL_MS    1000
#define CT_POLL_MS        300   // pause between 200 ms RMS windows
#define CONTROL_TICK_MS   200
#define DISPLAY_MS        500
