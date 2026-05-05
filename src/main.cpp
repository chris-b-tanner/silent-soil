#include <Arduino.h>
#include <SD.h>
#include <WiFi.h>
#include <driver/i2s.h>

// --- Pins ---
#define SENSOR_IN_PIN   4   // Entry beam,  ext pull-up — LOW = beam broken
#define SENSOR_OUT_PIN  16  // Exit beam,   ext pull-up — LOW = beam broken
#define BEEPER_PIN      22  // Click feedback

// SD card on default VSPI (SCK=18, MISO=19, MOSI=23)
#define SD_CS    5

// MAX98357A I2S amplifier
#define I2S_BCLK   26
#define I2S_LRCLK  25
#define I2S_DOUT   27

// Beam timing
#define DEBOUNCE_MS   30
#define MIN_BREAK_MS  80
#define MAX_BREAK_MS  8000

#define I2S_PORT    I2S_NUM_0
#define AUDIO_BUF   4096

// ---- Audio command flags (written by Core 1, read by Core 0) ----
enum AudioCmd { AUDIO_NONE, AUDIO_PLAY, AUDIO_STOP };
volatile AudioCmd audioCmd      = AUDIO_NONE;
volatile bool     audioPlaying  = false;
volatile int      audioTarget   = 0;  // track to play (1-5), set by Core 1
volatile int      audioCurrentTrack = 0;  // track currently open, set by Core 0

// ---- Beam sensor state machine ----
enum State { BEAM_INTACT, DEBOUNCING_BREAK, BEAM_BROKEN, DEBOUNCING_RESTORE };

struct Sensor {
    uint8_t  pin;
    State    state;
    uint32_t stateAt;
    uint32_t breakAt;
    uint32_t stuckLogAt;
};

Sensor sensorIn  = { SENSOR_IN_PIN,  BEAM_INTACT, 0, 0, 0 };
Sensor sensorOut = { SENSOR_OUT_PIN, BEAM_INTACT, 0, 0, 0 };

int32_t occupancy = 0;  // signed so underflow is detectable

// ---- WAV / audio state (Core 0 only) ----
File     wavFile;
uint32_t wavDataStart   = 0;
bool     i2sInstalled   = false;
uint8_t  audioBuf[AUDIO_BUF];
uint32_t fadeBytesLeft  = 0;
uint32_t fadeBytesTotal = 0;

static uint16_t read16le(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t read32le(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

void applyFadeIn(uint8_t *buf, size_t bytes) {
    if (fadeBytesLeft == 0) return;
    int16_t *s = (int16_t *)buf;
    size_t   n = bytes / 2;
    for (size_t i = 0; i < n; i++) {
        if (fadeBytesLeft < 2) { fadeBytesLeft = 0; break; }
        uint32_t gain = ((fadeBytesTotal - fadeBytesLeft) << 8) / fadeBytesTotal;
        s[i] = (int16_t)(((int32_t)s[i] * (int32_t)gain) >> 8);
        fadeBytesLeft -= 2;
    }
}

bool openWav(int track) {
    if (wavFile) wavFile.close();

    char path[12];
    snprintf(path, sizeof(path), "/%d.wav", track);
    wavFile = SD.open(path);
    if (!wavFile) { Serial.println("[ERROR] Cannot open " + String(path)); return false; }

    uint8_t hdr[12];
    wavFile.read(hdr, 12);
    if (memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        Serial.println("[ERROR] Not a WAV file"); wavFile.close(); return false;
    }

    uint32_t sampleRate = 44100;
    uint16_t bits = 16, channels = 1;

    while (wavFile.available() >= 8) {
        uint8_t chunk[8];
        wavFile.read(chunk, 8);
        uint32_t sz = read32le(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0 && sz >= 16) {
            uint8_t fmt[16];
            wavFile.read(fmt, 16);
            channels   = read16le(fmt + 2);
            sampleRate = read32le(fmt + 4);
            bits       = read16le(fmt + 14);
            if (sz > 16) wavFile.seek(wavFile.position() + sz - 16);
        } else if (memcmp(chunk, "data", 4) == 0) {
            wavDataStart = wavFile.position();
            break;
        } else {
            wavFile.seek(wavFile.position() + sz);
        }
    }

    if (i2sInstalled) i2s_driver_uninstall(I2S_PORT);

    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = sampleRate;
    cfg.bits_per_sample      = (i2s_bits_per_sample_t)bits;
    cfg.channel_format       = (channels == 2) ? I2S_CHANNEL_FMT_RIGHT_LEFT
                                               : I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 16;
    cfg.dma_buf_len          = 1024;
    cfg.use_apll             = true;
    cfg.tx_desc_auto_clear   = true;
    i2s_driver_install(I2S_PORT, &cfg, 0, nullptr);

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = I2S_BCLK;
    pins.ws_io_num    = I2S_LRCLK;
    pins.data_out_num = I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin(I2S_PORT, &pins);
    i2sInstalled = true;

    i2s_zero_dma_buffer(I2S_PORT);
    fadeBytesTotal = fadeBytesLeft = (uint32_t)sampleRate * (bits / 8) * channels / 20;

    Serial.printf("WAV: %uHz %u-bit %uch\n", sampleRate, bits, channels);
    return true;
}

// Runs on Core 0 — dedicated audio feeding
void audioTask(void *) {
    while (true) {
        if (audioCmd == AUDIO_PLAY) {
            audioCmd = AUDIO_NONE;
            int track = audioTarget;
            if (openWav(track)) {
                audioCurrentTrack = track;
                audioPlaying = true;
                Serial.printf("Audio start: track %d\n", track);
            }
        }

        if (audioPlaying) {
            if (audioCmd == AUDIO_STOP) {
                audioCmd = AUDIO_NONE;
                i2s_zero_dma_buffer(I2S_PORT);
                wavFile.close();
                audioPlaying = false;
                audioCurrentTrack = 0;
                Serial.println("Audio stop");
            } else {
                if (!wavFile.available()) wavFile.seek(wavDataStart);
                size_t toRead    = min((int)AUDIO_BUF, (int)wavFile.available());
                size_t bytesRead = wavFile.read(audioBuf, toRead);
                applyFadeIn(audioBuf, bytesRead);
                size_t written;
                i2s_write(I2S_PORT, audioBuf, bytesRead, &written, portMAX_DELAY);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

// Returns true on a confirmed beam break, false otherwise
bool processSensor(Sensor &s) {
    const uint32_t now    = millis();
    const bool     intact = digitalRead(s.pin) == HIGH;

    switch (s.state) {
        case BEAM_INTACT:
            if (!intact) { s.state = DEBOUNCING_BREAK; s.stateAt = now; }
            break;

        case DEBOUNCING_BREAK:
            if (intact) {
                s.state = BEAM_INTACT;
            } else if (now - s.stateAt >= DEBOUNCE_MS) {
                s.state = BEAM_BROKEN; s.breakAt = now; s.stateAt = now;
            }
            break;

        case BEAM_BROKEN:
            if (intact) {
                s.state = DEBOUNCING_RESTORE; s.stateAt = now;
            } else if (now - s.breakAt >= MAX_BREAK_MS) {
                if (now - s.stuckLogAt >= 5000) {
                    Serial.printf("[WARN] Pin %d beam stuck\n", s.pin);
                    s.stuckLogAt = now;
                }
                s.state = BEAM_INTACT;
            }
            break;

        case DEBOUNCING_RESTORE:
            if (!intact) {
                s.state = BEAM_BROKEN;
            } else if (now - s.stateAt >= DEBOUNCE_MS) {
                uint32_t duration = now - s.breakAt;
                s.state = BEAM_INTACT;
                if (duration >= MIN_BREAK_MS) return true;
                Serial.printf("[DBG] Pin %d short break ignored (%lums)\n", s.pin, duration);
            }
            break;
    }
    return false;
}

static int trackForOccupancy(int32_t occ) {
    if (occ <= 0) return 0;
    if (occ >= 5) return 5;
    return (int)occ;
}

static void updateAudio() {
    int newTrack = trackForOccupancy(occupancy);
    if (newTrack == 0) {
        if (audioPlaying) audioCmd = AUDIO_STOP;
    } else if (newTrack != audioCurrentTrack || !audioPlaying) {
        audioTarget = newTrack;
        audioCmd    = AUDIO_PLAY;
    }
}

void onPersonIn() {
    occupancy++;
    digitalWrite(BEEPER_PIN, HIGH); delay(20); digitalWrite(BEEPER_PIN, LOW);
    Serial.printf("[IN ] People in room: %d\n", occupancy);
    updateAudio();
}

void onPersonOut() {
    if (occupancy > 0) occupancy--;
    digitalWrite(BEEPER_PIN, HIGH); delay(20); digitalWrite(BEEPER_PIN, LOW);
    Serial.printf("[OUT] People in room: %d\n", occupancy);
    updateAudio();
}

void setup() {
    Serial.begin(115200);

    pinMode(SENSOR_IN_PIN,  INPUT);   // External pull-up fitted
    pinMode(SENSOR_OUT_PIN, INPUT);   // External pull-up fitted
    pinMode(BEEPER_PIN, OUTPUT);
    digitalWrite(BEEPER_PIN, LOW);

    // Kill radio stack — WiFi/BT ISRs on Core 0 can delay I2S DMA interrupts
    WiFi.mode(WIFI_OFF);
    btStop();

    // 8 MHz SPI — 20 MHz generates RF noise that couples into I2S lines
    if (!SD.begin(SD_CS, SPI, 8000000)) {
        Serial.println("[ERROR] SD card mount failed — check wiring and card");
    } else {
        Serial.println("SD card ready");
    }

    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 2, nullptr, 0);

    Serial.println("People counter ready");
    Serial.println("[    ] People in room: 0");
}

void loop() {
    if (processSensor(sensorIn))  onPersonIn();
    if (processSensor(sensorOut)) onPersonOut();
}
