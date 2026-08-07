#include <M5StickCPlus.h>
#include <driver/i2s.h>
#include <esp_idf_version.h>

#define MIC_CLK_PIN 0
#define MIC_DATA_PIN 34
#define MIC_SAMPLE_RATE 44100
#define MIC_SAMPLES 256
#define MIC_READ_LEN (MIC_SAMPLES * 2)
#define MIC_GAIN_FACTOR 60

static uint8_t micBuffer[MIC_READ_LEN];
TFT_eSprite canvas = TFT_eSprite(&M5.Lcd);

enum PetMode {
  MODE_IDLE,
  MODE_EATING,
  MODE_PETTING,
  MODE_PLAYING,
  MODE_DANCING
};

PetMode mode = MODE_IDLE;

int fullness = 72;
int cleanliness = 82;
int happiness = 76;

uint32_t lastStatTick = 0;
uint32_t lastDrawTick = 0;
uint32_t actionUntil = 0;
uint32_t actionStartedAt = 0;
uint32_t lastNameTrigger = 0;
uint32_t lastMicRead = 0;
uint32_t lastMicDebug = 0;
uint32_t btnAHoldStart = 0;
uint32_t micCalibrateUntil = 0;

float ambientMic = 600.0f;
int lastMicLevel = 0;
bool micReady = false;
bool petHoldFired = false;
int micCalibrateSamples = 0;
float micCalibrateSum = 0.0f;
int loudNameFrames = 0;
int micErrorCount = 0;
int micErrorStreak = 0;
int lastMicDelta = 0;
int lastMicPeak = 0;
int lastVoiceScore = 0;

#define RGB565(r, g, b) \
  (uint16_t)((((r)&0xF8) << 8) | (((g)&0xFC) << 3) | ((b) >> 3))

const uint16_t cream = RGB565(255, 244, 210);
const uint16_t caramel = RGB565(188, 125, 55);
const uint16_t pawPink = RGB565(255, 154, 172);
const uint16_t mint = RGB565(120, 210, 184);
const uint16_t coral = RGB565(238, 108, 77);
const uint16_t blush = RGB565(255, 185, 196);
const uint16_t fishBlue = RGB565(75, 156, 210);
const uint16_t furWhite = RGB565(252, 250, 242);
const uint16_t furBlack = RGB565(20, 22, 24);
const uint16_t eyeGold = RGB565(213, 176, 54);
const uint16_t ink = RGB565(48, 54, 61);
const uint16_t bg = RGB565(250, 242, 224);
const uint16_t bgWarm = RGB565(255, 247, 232);
const uint16_t panel = RGB565(238, 226, 204);
const uint16_t panelLight = RGB565(250, 240, 220);
const uint16_t roofYellow = RGB565(246, 206, 64);
const uint16_t roofShade = RGB565(224, 178, 39);
const uint16_t houseWall = RGB565(246, 238, 219);
const uint16_t houseLine = RGB565(229, 216, 194);
const uint16_t curtainBlue = RGB565(216, 211, 197);
const uint16_t cushionPink = RGB565(236, 190, 183);
const uint16_t floorLine = RGB565(222, 204, 176);
const uint16_t shadow = RGB565(225, 213, 196);
const uint16_t softInk = RGB565(92, 86, 78);
const uint16_t warmWhite = RGB565(255, 252, 244);

int clampStat(int value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return value;
}

int smoothStepPercent(int value) {
  value = constrain(value, 0, 100);
  return (value * value * (300 - 2 * value)) / 10000;
}

void i2sInit() {
  i2s_driver_uninstall(I2S_NUM_0);

  i2s_config_t i2sConfig = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
      .sample_rate = MIC_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ALL_RIGHT,
#if ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 1, 0)
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
#else
      .communication_format = I2S_COMM_FORMAT_I2S,
#endif
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 2,
      .dma_buf_len = 128,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };

  i2s_pin_config_t pinConfig = {};
#if ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 3, 0)
  pinConfig.mck_io_num = I2S_PIN_NO_CHANGE;
#endif
  pinConfig.bck_io_num = I2S_PIN_NO_CHANGE;
  pinConfig.ws_io_num = MIC_CLK_PIN;
  pinConfig.data_out_num = I2S_PIN_NO_CHANGE;
  pinConfig.data_in_num = MIC_DATA_PIN;

  esp_err_t installResult = i2s_driver_install(I2S_NUM_0, &i2sConfig, 0, NULL);
  esp_err_t pinResult = i2s_set_pin(I2S_NUM_0, &pinConfig);
  esp_err_t clkResult = i2s_set_clk(I2S_NUM_0, MIC_SAMPLE_RATE,
                                    I2S_BITS_PER_SAMPLE_16BIT,
                                    I2S_CHANNEL_MONO);
  if (installResult != ESP_OK || pinResult != ESP_OK || clkResult != ESP_OK) {
    micErrorCount++;
    micErrorStreak = 10;
  }
}

int readMicLevel() {
  size_t bytesRead = 0;
  esp_err_t result = i2s_read(I2S_NUM_0, (char *)micBuffer, MIC_READ_LEN,
                              &bytesRead, 20 / portTICK_PERIOD_MS);
  if (result != ESP_OK || bytesRead < 4) {
    micErrorCount++;
    micErrorStreak++;
    return 0;
  }
  micErrorStreak = 0;

  int16_t *samples = (int16_t *)micBuffer;
  int count = bytesRead / 2;
  long mean = 0;
  for (int i = 0; i < count; i++) {
    mean += samples[i];
  }
  mean /= count;

  long sum = 0;
  int peak = 0;
  int rawPeak = 0;
  for (int i = 0; i < count; i++) {
    int delta = abs(samples[i] - mean);
    int raw = abs(samples[i]);
    sum += delta;
    if (delta > peak) peak = delta;
    if (raw > rawPeak) rawPeak = raw;
  }
  lastMicPeak = peak;
  long level = max(sum / count, (long)peak / 3) * MIC_GAIN_FACTOR;
  long rawLevel = ((long)rawPeak * MIC_GAIN_FACTOR) / 18;
  level = max(level, rawLevel);
  if (level > 32000) level = 32000;
  return level;
}

void finishMicCalibration() {
  if (micCalibrateSamples > 0) {
    ambientMic = micCalibrateSum / micCalibrateSamples;
  } else if (lastMicLevel > 0) {
    ambientMic = lastMicLevel;
  } else {
    ambientMic = 600.0f;
  }
  micReady = true;
  lastMicDelta = 0;
  loudNameFrames = 0;
  lastNameTrigger = millis() - 1800;
}

void drawBar(int x, int y, int w, int h, int value, uint16_t color) {
  canvas.fillRoundRect(x, y, w, h, 3, warmWhite);
  canvas.drawRoundRect(x, y, w, h, 3, RGB565(210, 196, 174));
  int fill = map(clampStat(value), 0, 100, 0, w - 4);
  if (fill > 0) {
    canvas.fillRoundRect(x + 2, y + 2, fill, h - 4, 2, color);
  }
}

void drawStatus() {
  canvas.fillRoundRect(4, 3, 232, 15, 5, panelLight);
  canvas.drawRoundRect(4, 3, 232, 15, 5, RGB565(232, 216, 190));
  canvas.setTextSize(1);
  canvas.setTextColor(softInk, panelLight);
  canvas.drawString("FOOD", 9, 7, 1);
  drawBar(36, 6, 36, 9, fullness, caramel);
  canvas.drawString("CLEAN", 78, 7, 1);
  drawBar(112, 6, 34, 9, cleanliness, mint);
  canvas.drawString("HAPPY", 153, 7, 1);
  drawBar(188, 6, 36, 9, happiness, coral);
}

void drawBackground() {
  canvas.fillScreen(bgWarm);
  canvas.fillRect(0, 88, 240, 47, bg);
  canvas.drawLine(0, 88, 239, 88, floorLine);
  canvas.drawLine(14, 101, 226, 101, RGB565(235, 221, 198));
  canvas.fillCircle(26, 36, 1, RGB565(238, 225, 204));
  canvas.fillCircle(214, 43, 1, RGB565(238, 225, 204));
  canvas.fillCircle(198, 73, 1, RGB565(238, 225, 204));
}

void drawMicMeter() {
  int meter = constrain(lastVoiceScore / 35, 0, 31);
  canvas.drawRoundRect(204, 118, 31, 9, 3, ink);
  if (meter > 0) {
    canvas.fillRoundRect(206, 120, meter, 5, 2, coral);
  }
  canvas.setTextSize(1);
  canvas.setTextColor(softInk, panelLight);
  char micText[8];
  if (micErrorStreak > 4) {
    snprintf(micText, sizeof(micText), "E%d", min(micErrorStreak, 99));
  } else if (!micReady) {
    snprintf(micText, sizeof(micText), "CAL");
  } else if (lastVoiceScore > 999) {
    snprintf(micText, sizeof(micText), "V%03d", min(lastVoiceScore / 10, 999));
  } else {
    snprintf(micText, sizeof(micText), "V%03d", lastVoiceScore);
  }
  canvas.drawString(micText, 171, 120, 1);
  if (micReady && lastVoiceScore > 70) {
    canvas.fillCircle(199, 122, 3, coral);
  }
}

void drawSmallOuterEightFace(int cx, int cy, bool blink, bool happy) {
  canvas.fillTriangle(cx - 22, cy - 12, cx - 16, cy - 37, cx - 2, cy - 18,
                      furBlack);
  canvas.fillTriangle(cx + 22, cy - 12, cx + 16, cy - 37, cx + 2, cy - 18,
                      furBlack);
  canvas.fillTriangle(cx - 16, cy - 16, cx - 13, cy - 29, cx - 7, cy - 19,
                      RGB565(96, 78, 84));
  canvas.fillTriangle(cx + 16, cy - 16, cx + 13, cy - 29, cx + 7, cy - 19,
                      RGB565(96, 78, 84));

  canvas.fillEllipse(cx, cy - 1, 25, 24, furBlack);
  canvas.fillRoundRect(cx - 21, cy - 5, 42, 28, 12, furBlack);

  canvas.fillTriangle(cx, cy - 4, cx - 20, cy + 20, cx - 3, cy + 22,
                      furWhite);
  canvas.fillTriangle(cx, cy - 4, cx + 20, cy + 20, cx + 3, cy + 22,
                      furWhite);
  canvas.fillRoundRect(cx - 15, cy + 8, 30, 15, 8, furWhite);
  canvas.fillEllipse(cx, cy + 14, 18, 10, furWhite);

  if (happy) {
    canvas.drawLine(cx - 17, cy - 1, cx - 13, cy + 2, furWhite);
    canvas.drawLine(cx - 13, cy + 2, cx - 8, cy - 1, furWhite);
    canvas.drawLine(cx + 8, cy - 1, cx + 13, cy + 2, furWhite);
    canvas.drawLine(cx + 13, cy + 2, cx + 17, cy - 1, furWhite);
  } else if (blink) {
    canvas.drawLine(cx - 17, cy, cx - 8, cy, furWhite);
    canvas.drawLine(cx + 8, cy, cx + 17, cy, furWhite);
  } else {
    canvas.fillEllipse(cx - 13, cy - 1, 7, 6, warmWhite);
    canvas.fillEllipse(cx + 13, cy - 1, 7, 6, warmWhite);
    canvas.fillEllipse(cx - 13, cy - 1, 5, 6, ink);
    canvas.fillEllipse(cx + 13, cy - 1, 5, 6, ink);
    canvas.fillCircle(cx - 11, cy - 4, 1, WHITE);
    canvas.fillCircle(cx + 15, cy - 4, 1, WHITE);
  }

  canvas.fillTriangle(cx - 3, cy + 7, cx + 3, cy + 7, cx, cy + 11,
                      RGB565(224, 145, 151));
  canvas.drawLine(cx, cy + 11, cx, cy + 15, ink);
  if (happy) {
    canvas.drawLine(cx, cy + 15, cx - 5, cy + 14, ink);
    canvas.drawLine(cx, cy + 15, cx + 5, cy + 14, ink);
    canvas.drawLine(cx - 5, cy + 14, cx - 8, cy + 12, ink);
    canvas.drawLine(cx + 5, cy + 14, cx + 8, cy + 12, ink);
  } else {
    canvas.drawLine(cx, cy + 15, cx - 4, cy + 18, ink);
    canvas.drawLine(cx, cy + 15, cx + 4, cy + 18, ink);
  }
  canvas.drawLine(cx - 15, cy + 6, cx - 29, cy + 3, RGB565(105, 107, 108));
  canvas.drawLine(cx + 15, cy + 6, cx + 29, cy + 3, RGB565(105, 107, 108));
  canvas.drawLine(cx - 15, cy + 10, cx - 28, cy + 12, RGB565(105, 107, 108));
  canvas.drawLine(cx + 15, cy + 10, cx + 28, cy + 12, RGB565(105, 107, 108));
}

void drawCatHouse(int pose, bool showCat) {
  bool blink = ((millis() / 4200) % 2 == 1) && ((millis() % 4200) < 220);

  canvas.fillEllipse(120, 112, 78, 12, shadow);
  canvas.fillRoundRect(49, 53, 142, 56, 10, houseWall);
  canvas.fillRoundRect(55, 59, 130, 45, 9, RGB565(251, 244, 229));
  canvas.drawRoundRect(49, 53, 142, 56, 10, houseLine);
  canvas.drawLine(60, 80, 180, 80, RGB565(235, 222, 199));
  canvas.drawLine(120, 56, 120, 104, RGB565(235, 222, 199));

  canvas.fillTriangle(39, 55, 120, 26, 201, 55, roofYellow);
  canvas.fillRoundRect(44, 47, 152, 20, 8, roofYellow);
  canvas.drawLine(49, 63, 192, 63, roofShade);
  canvas.drawLine(130, 31, 190, 54, roofShade);
  canvas.drawLine(110, 31, 50, 54, RGB565(255, 224, 95));

  canvas.fillRoundRect(71, 62, 82, 47, 19, RGB565(235, 222, 202));
  canvas.fillRoundRect(76, 67, 72, 42, 17, RGB565(247, 239, 220));
  canvas.drawRoundRect(71, 62, 82, 47, 19, RGB565(197, 186, 170));
  canvas.fillTriangle(77, 66, 111, 69, 93, 109, curtainBlue);
  canvas.fillTriangle(147, 66, 113, 69, 132, 109, curtainBlue);
  canvas.drawLine(111, 69, 94, 108, RGB565(151, 149, 137));
  canvas.drawLine(114, 69, 131, 108, RGB565(151, 149, 137));

  canvas.fillRoundRect(50, 99, 126, 20, 9, cushionPink);
  canvas.fillRoundRect(59, 104, 108, 8, 4, RGB565(248, 216, 209));
  canvas.drawRoundRect(50, 99, 126, 20, 9, RGB565(212, 160, 155));

  if (showCat) {
    int peek = (pose % 2 == 0) ? 0 : 1;
    canvas.fillEllipse(112, 102, 30, 8, RGB565(230, 211, 194));
    drawSmallOuterEightFace(112, 84 + peek, blink, false);
    canvas.fillCircle(91, 106, 6, furWhite);
    canvas.fillCircle(133, 106, 6, furWhite);
    canvas.fillCircle(91, 108, 2, pawPink);
    canvas.fillCircle(133, 108, 2, pawPink);
  }
}

void drawPettingHand(int pose) {
  int stroke = (pose % 4 < 2) ? -3 : 4;
  uint16_t skin = RGB565(255, 214, 182);
  uint16_t skinLine = RGB565(216, 159, 130);
  uint16_t cuff = RGB565(116, 188, 178);
  uint16_t cuffLine = RGB565(76, 139, 132);

  int palmX = 112 + stroke;
  int palmY = 45;
  canvas.fillRoundRect(palmX + 27, palmY - 1, 16, 15, 7, cuff);
  canvas.drawRoundRect(palmX + 27, palmY - 1, 16, 15, 7, cuffLine);
  canvas.fillRoundRect(palmX, palmY, 35, 14, 7, skin);
  canvas.drawRoundRect(palmX, palmY, 35, 14, 7, skinLine);
  canvas.fillCircle(palmX + 1, palmY + 14, 4, skin);
  canvas.fillCircle(palmX + 10, palmY + 16, 4, skin);
  canvas.fillCircle(palmX + 19, palmY + 14, 4, skin);
  canvas.drawLine(palmX + 2, palmY + 12, palmX - 9, palmY + 18, skinLine);
}

void drawPettingCat(int pose, int ox, int oy, bool blink, bool showHand) {
  int purr = (pose % 2 == 0) ? 0 : 1;

  canvas.fillEllipse(122 + ox, 105 + oy, 46, 9, shadow);
  canvas.fillRoundRect(91 + ox, 72 + oy + purr, 62, 31, 15, furWhite);
  canvas.fillRoundRect(98 + ox, 71 + oy + purr, 50, 16, 8, furBlack);
  canvas.fillTriangle(107 + ox, 85 + oy, 140 + ox, 85 + oy, 126 + ox,
                      103 + oy, furWhite);
  canvas.fillRoundRect(84 + ox, 96 + oy, 21, 8, 4, furWhite);
  canvas.fillRoundRect(139 + ox, 96 + oy, 21, 8, 4, furWhite);
  canvas.fillCircle(92 + ox, 103 + oy, 3, pawPink);
  canvas.fillCircle(151 + ox, 103 + oy, 3, pawPink);
  canvas.fillRoundRect(150 + ox, 80 + oy, 32, 8, 4, furBlack);
  canvas.fillCircle(181 + ox, 83 + oy, 5, furBlack);

  canvas.fillEllipse(123 + ox, 69 + oy + purr, 21, 15, furWhite);
  drawSmallOuterEightFace(123 + ox, 57 + oy + purr, blink, true);
  if (showHand) {
    drawPettingHand(pose);
  }

  canvas.setTextColor(coral, bgWarm);
  canvas.drawString("~", 67, 68 + purr, 2);
  canvas.drawString("~", 171, 66 - purr, 2);
}

void drawFullBodyCat(int pose) {
  uint32_t elapsed = millis() - actionStartedAt;
  int rawEmerge = constrain(map((int)elapsed, 0, 1200, 0, 100), 0, 100);
  int move = smoothStepPercent(map(constrain(rawEmerge, 42, 100), 42, 100, 0, 100));

  drawCatHouse(pose, rawEmerge < 42);
  if (rawEmerge < 42) {
    int paw = smoothStepPercent(map(rawEmerge, 0, 42, 0, 100));
    paw = map(paw, 0, 100, 0, 12);
    canvas.fillCircle(95 - paw / 2, 106, 5, furWhite);
    canvas.fillCircle(129 + paw / 2, 106, 5, furWhite);
    return;
  }

  int bob = 0;
  if (mode == MODE_DANCING && rawEmerge == 100) {
    bob = ((millis() / 260) % 2 == 0) ? 0 : -3;
  } else if (mode == MODE_PLAYING && rawEmerge == 100) {
    bob = ((millis() / 320) % 2 == 0) ? 0 : -2;
  }
  int tailLift = (mode == MODE_DANCING && rawEmerge == 100)
                     ? ((millis() / 260) % 2 == 0 ? -2 : -6)
                     : 0;
  int eatNod = (mode == MODE_EATING && rawEmerge == 100)
                   ? ((millis() / 300) % 2 == 0 ? 0 : 5)
                   : 0;
  int slideX = map(move, 0, 100, 22, 0);
  int slideY = map(move, 0, 100, 18, 0);
  int ox = slideX;
  int oy = bob + slideY;
  bool eatingBlink = mode == MODE_EATING && rawEmerge == 100 &&
                     ((millis() / 780) % 3 == 2);
  bool normalBlink = mode != MODE_DANCING && ((millis() / 4200) % 2 == 1) &&
                     ((millis() % 4200) < 220);
  bool blink = eatingBlink || normalBlink;

  canvas.drawLine(0, 106, 239, 106, floorLine);

  if (mode == MODE_PETTING && rawEmerge == 100) {
    bool happyBlink = ((millis() / 520) % 3) != 1;
    uint32_t petElapsed = millis() - actionStartedAt;
    bool showHand = petElapsed < 2200;
    drawPettingCat(pose, 0, 0, happyBlink, showHand);
    return;
  }

  canvas.fillEllipse(123 + ox, 104 + oy, 57, 10, shadow);

  canvas.fillRoundRect(84 + ox, 64 + oy, 80, 34, 16, furWhite);
  canvas.fillRoundRect(91 + ox, 58 + oy, 67, 22, 11, furBlack);
  canvas.fillTriangle(105 + ox, 75 + oy, 143 + ox, 76 + oy, 128 + ox,
                      97 + oy, furWhite);
  canvas.fillRoundRect(133 + ox, 76 + oy, 26, 19, 9, furWhite);
  canvas.fillRoundRect(148 + ox, 79 + oy, 22, 16, 8, furBlack);

  canvas.fillRoundRect(158 + ox, 62 + oy, 37, 8, 4, furBlack);
  canvas.fillCircle(193 + ox, 66 + oy + tailLift, 5, furBlack);

  canvas.fillRoundRect(54 + ox, 93 + oy, 35, 7, 4, furWhite);
  canvas.fillCircle(52 + ox, 96 + oy, 4, furWhite);
  canvas.fillRoundRect(89 + ox, 94 + oy, 41, 8, 4, furWhite);
  canvas.fillCircle(131 + ox, 98 + oy, 4, furWhite);
  canvas.fillRoundRect(143 + ox, 90 + oy, 25, 7, 3, furBlack);

  if (mode == MODE_EATING && rawEmerge == 100) {
    canvas.fillEllipse(42, 101, 18, 6, fishBlue);
    canvas.drawEllipse(42, 101, 18, 6, ink);
    canvas.fillCircle(48, 99, 2, WHITE);
    canvas.fillTriangle(25, 101, 13, 94, 13, 108, fishBlue);
    canvas.fillCircle(57, 101, 3, RGB565(255, 222, 126));
  }

  int headX = 74 + ox;
  int headY = 62 + oy + eatNod;
  canvas.fillEllipse(91 + ox, 79 + oy + eatNod / 2, 19, 17, furWhite);
  canvas.fillRoundRect(75 + ox, 81 + oy + eatNod / 2, 26, 15, 8, furWhite);
  drawSmallOuterEightFace(headX, headY, blink, mode == MODE_PLAYING);
  canvas.fillRoundRect(66 + ox, 86 + oy + eatNod / 2, 18, 12, 7, furWhite);
  canvas.fillCircle(65 + ox, 97 + oy, 4, furWhite);

  if (mode == MODE_DANCING && rawEmerge == 100) {
    canvas.fillCircle(42, 40 + (pose % 3), 4, coral);
    canvas.fillCircle(190, 42 - (pose % 3), 4, mint);
    canvas.drawString("*", 34, 52, 2);
    canvas.drawString("*", 198, 52, 2);
  }
}

void drawCat(int pose) {
  if (mode == MODE_IDLE) {
    drawCatHouse(pose, true);
  } else {
    drawFullBodyCat(pose);
  }
}

void drawActionIcon(int pose) {
  if (mode == MODE_EATING) {
    return;
  } else if (mode == MODE_PETTING) {
    return;
  } else if (mode == MODE_PLAYING) {
    int y = 78 + ((pose % 2) * 9);
    canvas.drawLine(28, 47, 56, y, ink);
    canvas.fillCircle(61, y, 7, coral);
    canvas.drawLine(65, y, 75, y - 9, mint);
    canvas.drawLine(65, y, 75, y + 9, mint);
  }
}

const char *currentMessage() {
  if (!micReady) return "Mic calibrating...";
  if (mode == MODE_EATING) return "NAIYOU came out to eat";
  if (mode == MODE_PETTING) return "Petting NAIYOU";
  if (mode == MODE_PLAYING) return "Playing outside";
  if (mode == MODE_DANCING) return "NAIYOU came out!";
  if (cleanliness < 35) return "Hold A to pet";
  if (fullness < 35) return "A: feed me";
  if (happiness < 35) return "B: play with me";
  return "Say NAIYOU / watch MIC";
}

void drawScreen() {
  int pose = (millis() / 280) % 4;
  drawBackground();
  drawCat(pose);
  drawActionIcon(pose);
  drawStatus();
  canvas.fillRoundRect(5, 115, 232, 17, 5, panelLight);
  canvas.drawRoundRect(5, 115, 232, 17, 5, RGB565(228, 211, 186));
  canvas.setTextColor(softInk, panelLight);
  canvas.setTextSize(1);
  canvas.drawString(currentMessage(), 10, 120, 1);
  drawMicMeter();
  canvas.pushSprite(0, 0);
}

void startAction(PetMode nextMode, uint32_t durationMs) {
  mode = nextMode;
  actionStartedAt = millis();
  actionUntil = millis() + durationMs;
  drawScreen();
}

void feedCat() {
  fullness = 100;
  happiness = clampStat(happiness + 5);
  cleanliness = clampStat(cleanliness - 5);
  startAction(MODE_EATING, 3200);
}

void petCat() {
  happiness = 100;
  cleanliness = clampStat(cleanliness + 10);
  fullness = clampStat(fullness - 2);
  startAction(MODE_PETTING, 3000);
}

void playWithCat() {
  happiness = clampStat(happiness + 24);
  fullness = clampStat(fullness - 5);
  cleanliness = clampStat(cleanliness - 3);
  startAction(MODE_PLAYING, 3800);
}

void danceForName() {
  happiness = clampStat(happiness + 14);
  fullness = clampStat(fullness - 3);
  lastNameTrigger = millis();
  startAction(MODE_DANCING, 4200);
}

void updateStats() {
  if (millis() - lastStatTick < 7000) return;
  lastStatTick = millis();

  fullness = clampStat(fullness - 1);
  happiness = clampStat(happiness - 1);
  cleanliness = clampStat(cleanliness - 1);
}

void updateButtons() {
  M5.update();

  if (M5.BtnA.isPressed()) {
    if (btnAHoldStart == 0) btnAHoldStart = millis();
    if (!petHoldFired && millis() - btnAHoldStart > 700) {
      petHoldFired = true;
      petCat();
    }
  } else {
    if (btnAHoldStart != 0 && !petHoldFired) {
      feedCat();
    }
    btnAHoldStart = 0;
    petHoldFired = false;
  }

  if (M5.BtnB.wasPressed()) {
    playWithCat();
  }
}

void updateMic() {
  if (millis() - lastMicRead < 80) return;
  lastMicRead = millis();

  int level = readMicLevel();
  if (level <= 0) {
    lastMicDelta = 0;
    lastMicPeak = 0;
    lastVoiceScore = 0;
    if (!micReady && millis() >= micCalibrateUntil) {
      finishMicCalibration();
    }
    return;
  }
  lastMicLevel = level;
  lastMicDelta = max(0, level - (int)ambientMic);
  lastVoiceScore = max(lastMicDelta, lastMicPeak / 2);

  if (millis() < micCalibrateUntil) {
    if (level < 28000) {
      micCalibrateSum += level;
      micCalibrateSamples++;
      ambientMic = micCalibrateSum / micCalibrateSamples;
    }
    lastMicDelta = 0;
    lastVoiceScore = 0;
    loudNameFrames = 0;
    return;
  }

  if (!micReady) {
    finishMicCalibration();
    return;
  }

  bool inCooldown = millis() - lastNameTrigger < 1400;
  int triggerDelta = max(45, (int)(ambientMic * 0.08f));
  int strongTriggerDelta = max(95, (int)(ambientMic * 0.16f));
  int triggerVoice = max(triggerDelta, 55);
  int strongTriggerVoice = max(strongTriggerDelta, 110);

  if (lastVoiceScore > triggerVoice) {
    loudNameFrames++;
  } else if (loudNameFrames > 0) {
    loudNameFrames--;
  }

  bool heardName = loudNameFrames >= 1 || lastVoiceScore > strongTriggerVoice;
  if (!inCooldown && mode != MODE_DANCING && heardName) {
    loudNameFrames = 0;
    danceForName();
    return;
  }

  if (mode != MODE_DANCING && lastVoiceScore < triggerVoice && level < 28000) {
    ambientMic = ambientMic * 0.97f + level * 0.03f;
  }

  if (millis() - lastMicDebug > 500) {
    lastMicDebug = millis();
    Serial.printf("mic level=%d ambient=%d delta=%d peak=%d voice=%d trigger=%d errors=%d/%d\n",
                  level, (int)ambientMic, lastMicDelta, lastMicPeak,
                  lastVoiceScore, triggerVoice,
                  micErrorStreak, micErrorCount);
  }
}

void setup() {
  M5.begin();
  M5.Lcd.setRotation(3);
  canvas.setColorDepth(16);
  canvas.createSprite(240, 135);
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextFont(1);
  Serial.begin(115200);
  i2sInit();
  micCalibrateUntil = millis() + 1400;
  drawScreen();
}

void loop() {
  updateButtons();
  updateStats();
  updateMic();

  if (mode != MODE_IDLE && millis() > actionUntil) {
    mode = MODE_IDLE;
    drawScreen();
    lastDrawTick = millis();
  }

  uint32_t refreshMs = (mode == MODE_IDLE && micReady) ? 250 : 120;
  if (millis() - lastDrawTick > refreshMs) {
    lastDrawTick = millis();
    drawScreen();
  }
}
