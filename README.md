# SilentSoil — People Counter with Audio Playback

An ESP32-based people counter that uses two IR beam-break sensors to track occupancy in a room. Audio plays automatically when the room is occupied and stops when it empties.

## How It Works

- **Entry beam (GPIO 4):** increments occupancy count
- **Exit beam (GPIO 16):** decrements occupancy count
- **Occupancy > 0:** loops `1.wav` from SD card through I2S amplifier
- **Occupancy reaches 0:** stops playback
- A short beep confirms each entry/exit event
- Current occupancy count is printed to serial

Audio runs on Core 0; beam sensing runs on Core 1 (loop), keeping I2S DMA feeding uninterrupted.

## Hardware Requirements

### Microcontroller
- **ESP32 dev board** (38-pin, dual-core)

### IR Beam-Break Sensors × 2
| Signal | GPIO |
|--------|------|
| Entry beam output | 4 |
| Exit beam output | 16 |

Both sensors require an **external pull-up resistor** to 3.3 V. Beam intact = HIGH; beam broken = LOW.

### MAX98357A I2S Amplifier Breakout
| Breakout pin | ESP32 GPIO |
|--------------|------------|
| LRC (LRCLK) | 25 |
| BCLK | 26 |
| DIN | 27 |
| GND | GND |
| VIN | **5 V** (use 5 V — 3.3 V causes significantly more noise) |
| SD | Pull HIGH to VIN/3.3 V to enable (or leave floating if breakout has internal pull-up) |
| GAIN | Leave floating for 9 dB gain (or connect to GND/VIN to change gain) |

Connect an **8 Ω speaker** to the breakout's speaker terminals.

Add a **100 µF electrolytic + 100 nF ceramic decoupling cap** on the VIN rail close to the breakout to reduce audio noise.

### SD Card Module (SPI)
| SD module pin | ESP32 GPIO |
|---------------|------------|
| CS | 5 |
| SCK | 18 (VSPI default) |
| MOSI | 23 (VSPI default) |
| MISO | 19 (VSPI default) |
| VCC | 3.3 V or 5 V (per module) |
| GND | GND |

SPI runs at 8 MHz. Higher speeds introduce RF noise that couples into I2S lines.

### Beeper
| | GPIO |
|--|------|
| Piezo/buzzer positive | 22 |
| Negative | GND |

A short 20 ms pulse confirms each entry or exit event.

## Audio File

Place a file named **`1.wav`** in the root of the SD card. The WAV parser supports:
- Any sample rate (44100 Hz recommended)
- 16-bit PCM
- Mono or stereo

The file loops continuously while the room is occupied. A 50 ms software fade-in on each play start suppresses the turn-on pop.

## Serial Output

Connect at **115200 baud**. Example output:

```
SD card ready
People counter ready
[    ] People in room: 0
[IN ] People in room: 1
[OUT] People in room: 0
```

## Beam Timing Parameters

Adjustable in `src/main.cpp`:

| `#define` | Default | Purpose |
|-----------|---------|---------|
| `DEBOUNCE_MS` | 30 ms | Ignores transitions shorter than this |
| `MIN_BREAK_MS` | 80 ms | Minimum break duration to count as a crossing |
| `MAX_BREAK_MS` | 8000 ms | Resets a stuck/blocked beam after this time |

## Building and Flashing

Requires [PlatformIO](https://platformio.org/).

```bash
pio run --target upload
pio device monitor
```

Platform: `espressif32@6.9.0` (IDF 4.x). No external libraries are required — the project uses the native ESP-IDF I2S driver and the built-in Arduino SD library.

## File Structure

```
SilentSoil/
├── platformio.ini
└── src/
    └── main.cpp
```
