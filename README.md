# Battery Monitor - INA226 Data Logger

A battery monitoring system with web interface for tracking voltage and current over time.

## Features
- 📊 Logs data every 10 minutes
- 💾 Stores up to 48 hours of data (288 points)
- 🔄 Data persists through power outages
- 📱 Mobile-responsive web interface
- 📈 Real-time voltage and current charts with relative time (-48h to now)
- 🎨 Color-coded indicators: voltage state, charge/discharge status, SOC level
- 🔢 Physical 7-segment LED displays showing voltage (##.#V) and current (-##.#A)
- 🔋 State of Charge (SOC) tracking with amp-hour integration
- 📐 Peukert's Law applied for accurate SOC at varying discharge rates
- ⚡ Automatic full battery detection (resets SOC to 100%)
- ⚡ Tracks both discharge and charging

## Color Coding

The dashboard uses intuitive color coding for quick battery status assessment:

### Voltage (12V Lead Acid):
- 🔴 **Red** (< 12.0V): Discharged - battery needs charging
- 🟠 **Amber** (12.0-12.5V): Partially charged
- 🟢 **Green** (≥ 12.5V): Good charge state

### Current:
- 🔴 **Red** (negative): Discharging - consuming power
- 🟢 **Green** (positive): Charging - receiving power

### State of Charge (SOC):
- 🔴 **Red** (< 60%): Low - charge soon
- 🟠 **Amber** (60-80%): Moderate
- 🟢 **Green** (≥ 80%): Good

### How SOC Tracking Works
1. **Initialization**: Starts at 100% or loads saved value from flash
2. **Integration**: Every 10 seconds, calculates amp-hours consumed/charged
3. **Peukert Correction**: When discharging, applies Peukert's Law to account for reduced capacity at higher discharge rates
4. **Calculation**: `Remaining Ah = Previous Ah + (Current × Time × Peukert Factor)`
5. **Percentage**: `SOC% = (Remaining Ah / 300 Ah) × 100`
6. **Full Detection**: Automatically resets to 100% when battery reaches full charge
7. **Persistence**: SOC saved every minute and restored after power loss

## Setup Instructions

### Hardware Requirements

**INA226 Current Sensor:**
- SDA → GPIO21
- SCL → GPIO22
- 0.0015Ω shunt resistor

**Display Operation:**
- Multiplexed at 500Hz (each digit lit 1/6 of the time)
- No additional driver ICs required
- Voltage always shows 1 decimal place (e.g., 12.5V)
- Current shows 1 decimal when |I| < 10A (e.g., -9.9A)
- Current shows 0 decimal when |I| ≥ 10A (e.g., -50A)
- Top row shows the SoC for 1 second every 10 seconds.
- When charging (negative current), decimal place of current line is flashing.

### Software Setup

### 1. Upload the Filesystem (IMPORTANT!)
Before uploading the main code, you must upload the web interface files to the ESP32's filesystem:

**In PlatformIO:**
- Click on the PlatformIO icon in the sidebar
- Under "PROJECT TASKS" → "Platform" → click "Build Filesystem Image"
- Then click "Upload Filesystem Image"

**Or via command line:**
```bash
pio run --target buildfs
pio run --target uploadfs
```

This uploads the `data/index.html` file to the ESP32's LittleFS filesystem.

### 2. Upload the Code
After uploading the filesystem, upload the main code as normal:
- Click "Upload" button in PlatformIO, or
- Run `pio run --target upload`

### 3. Connect and View
1. Wait for the ESP32 to boot
2. Connect to WiFi network (no password)
3. Open browser and navigate to `http://192.168.4.1`

## Configuration

### Shunt Resistor
Current shunt resistor value: **0.0015Ω** (1.5 milliohm)
Maximum measurable current: **~50A**

To change these values, edit in `src/main.cpp`:
```cpp
#define SHUNT_RESISTOR 0.0015
ina.setMaxCurrentShunt(50.0, SHUNT_RESISTOR);
```

### Battery Capacity
Battery bank capacity: to set, click the stats on the dashboard.

**Peukert's Law:**
The system applies Peukert's Law to account for reduced effective capacity at higher discharge rates. When discharging, the effective amp-hours consumed are multiplied by `(I/C20)^(n-1)` where:
- I = discharge current (A)
- C20 = 20-hour discharge rate (15A for 300Ah battery)
- n = Peukert exponent (1.1 for typical lead acid)

This means discharging at 30A consumes more "effective" amp-hours than the actual current would suggest.

**Full Battery Detection:**
Battery is considered full when:
- Voltage ≥ 13.8V AND
- Current < 1.0A (charging/discharging)
- Conditions sustained for 60 seconds

When detected, SOC resets to 100%. Adjust thresholds in `src/main.cpp`:
```cpp
#define FULL_VOLTAGE_THRESHOLD 13.8
#define FULL_CURRENT_THRESHOLD 1.0
#define FULL_DETECTION_TIME 60000
```

## File Structure
```
ina226_test/
├── platformio.ini          # PlatformIO configuration
├── src/
│   └── main.cpp           # Main ESP32 code
└── data/
    └── index.html         # Web interface (uploaded to ESP32)
```