#include <Arduino.h>

// (附加) DAC 输出波形 -> ADC 连续采样 -> 计算 Vpp/频率 -> OLED 画波形
//
// 硬件连接（ESP32 经典款，带片上 DAC）：
// 1) DAC 输出脚（GPIO25 或 GPIO26） -> 用杜邦线接到 ADC 输入脚（建议 GPIO34）
// 2) GND 必须共地
// 3) OLED(SSD1306 I2C)：VCC/GND + SCL=22 SDA=21（常见默认）
//
// 说明：
// - ESP32 片上 DAC 是 8bit（0..255），用 dacWrite() 输出。
// - ADC 读数 0..4095，但电压换算会受校准/非线性影响；本实验以“相对值 + 近似电压”展示。

#include <Wire.h>
#include <Adafruit_GFX.h>

// OLED 驱动选择：有些模块虽然 I2C 地址同为 0x3C，但控制芯片不同。
// - OLED_USE_SH1106=0: SSD1306（你这块屏目前是这个才能点亮）
// - OLED_USE_SH1106=1: SH1106
#ifndef OLED_USE_SH1106
#define OLED_USE_SH1106 0
#endif

#if OLED_USE_SH1106
#include <Adafruit_SH110X.h>
#else
#include <Adafruit_SSD1306.h>
#endif

// ===== 可改参数 =====
// 示波器优先：只输出 DAC 波形，不做 ADC/OLED（接线更简单，波形更稳定）
// 设为 1：用示波器探头测 DAC_PIN 即可
// 设为 0：完整功能（DAC->ADC 采样 + 计算 + OLED 绘图）
#ifndef LAB6_SCOPE_ONLY
#define LAB6_SCOPE_ONLY 0
#endif

static const int DAC_PIN = 26;     // 25(DAC1) 或 26(DAC2)
static const int ADC_PIN = 34;     // ADC1 引脚，推荐 34/35/36/39（输入专用）
static const uint32_t SAMPLE_HZ = 4000;   // 采样率（Hz）
static const uint16_t N_SAMPLES = 128;    // OLED 宽 128，刚好一列一个采样点
static const uint16_t WAVE_FREQ_HZ = 200;  // 目标输出频率（Hz）

// DAC 更新率（点数/秒），越高波形越平滑（但 dacWrite() 有上限）
static const uint32_t DAC_UPDATE_HZ = 20000;

// 限幅：避免顶到电源轨导致失真（尤其在接负载时）
static const uint8_t DAC_MIN = 8;
static const uint8_t DAC_MAX = 247;

static const uint8_t BTN_MODE_PIN = 4;    // 外接按键切换波形：GPIO4 -> 按键 -> GND
static const uint32_t DEBOUNCE_MS = 30;

// OLED 常见差异点：地址(0x3C/0x3D)、分辨率(128x64/128x32)、是否需要外接 RST
#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3C
#endif
#ifndef OLED_WIDTH
#define OLED_WIDTH 128
#endif
#ifndef OLED_HEIGHT
#define OLED_HEIGHT 64
#endif
#ifndef OLED_RESET_PIN
#define OLED_RESET_PIN -1
#endif

static const uint8_t OLED_ADDR = OLED_I2C_ADDR;
static const int OLED_W = OLED_WIDTH;
static const int OLED_H = OLED_HEIGHT;

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 21
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 22
#endif
#ifndef I2C_FREQ_HZ
// 先用 100k 更稳，排除“400k 走线/模块质量”导致的玄学黑屏
#define I2C_FREQ_HZ 100000
#endif
// 单色 OLED 通用颜色常量：避免在 SH1106 分支引用 SSD1306_* 宏导致编译失败
static const uint16_t OLED_WHITE = 1;
static const uint16_t OLED_BLACK = 0;

// 板载 LED 各家 DevKit 不完全一致：优先使用 Arduino Core 提供的 LED_BUILTIN
#ifndef LED_HEARTBEAT_PIN
#ifdef LED_BUILTIN
#define LED_HEARTBEAT_PIN LED_BUILTIN
#else
#define LED_HEARTBEAT_PIN 2
#endif
#endif

// 有些板载 LED 是“低电平点亮”（active-low）。如果你发现亮灭颠倒，可以把它改成 1。
#ifndef LED_HEARTBEAT_ACTIVE_LOW
#define LED_HEARTBEAT_ACTIVE_LOW 0
#endif

static void setHeartbeatLed(bool on) {
  if (LED_HEARTBEAT_ACTIVE_LOW) {
    digitalWrite(LED_HEARTBEAT_PIN, on ? LOW : HIGH);
  } else {
    digitalWrite(LED_HEARTBEAT_PIN, on ? HIGH : LOW);
  }
}

// ===== 波形选择 =====
enum Waveform : uint8_t {
  W_SINE = 0,
  W_SQUARE = 1,
  W_TRIANGLE = 2,
  W_COUNT
};

static const char *waveName(Waveform w) {
  switch (w) {
    case W_SINE: return "SINE";
    case W_SQUARE: return "SQUARE";
    case W_TRIANGLE: return "TRIANGLE";
    default: return "?";
  }
}

// ===== OLED =====
#if OLED_USE_SH1106
static Adafruit_SH1106G display(OLED_W, OLED_H, &Wire, OLED_RESET_PIN);
#else
static Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, OLED_RESET_PIN);
#endif

// ===== 采样缓存 =====
static uint16_t samples[N_SAMPLES];

static void i2cScanOnce() {
  Serial.println("I2C scan...");
  int found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      found++;
      Serial.printf("  found: 0x%02X\n", addr);
    }
  }
  if (found == 0) Serial.println("  (no I2C devices found)");
}

static void dacWrite8(uint8_t v) {
  // Arduino-ESP32 经典款支持 dacWrite
  dacWrite(DAC_PIN, v);
}

static uint8_t genSample8(Waveform w, uint16_t phase) {
  // phase: 0..255
  switch (w) {
    case W_SINE: {
      // 轻量 LUT：用 sinf() 直接算（频率不高，够用）
      float a = (2.0f * 3.1415926f * (float)phase) / 256.0f;
      float s = sinf(a); // -1..1
      float span = (float)(DAC_MAX - DAC_MIN);
      int v = (int)lroundf((s * 0.5f + 0.5f) * span + (float)DAC_MIN);
      if (v < (int)DAC_MIN) v = DAC_MIN;
      if (v > (int)DAC_MAX) v = DAC_MAX;
      return (uint8_t)v;
    }
    case W_SQUARE:
      return (phase < 128) ? DAC_MAX : DAC_MIN;
    case W_TRIANGLE: {
      // 0..255..0
      float spanF = (float)(DAC_MAX - DAC_MIN);
      if (phase < 128) {
        // 0..1
        float t = (float)phase / 127.0f;
        return (uint8_t)lroundf((float)DAC_MIN + t * spanF);
      }
      float t = (float)(255 - phase) / 127.0f;
      return (uint8_t)lroundf((float)DAC_MIN + t * spanF);
    }
    default:
      return 0;
  }
}

static void runDacOnly(Waveform w) {
  // 仅用于示波器：连续输出稳定波形
  const uint32_t usPerUpdate = 1000000UL / DAC_UPDATE_HZ;
  const float phaseStepF = 256.0f * (float)WAVE_FREQ_HZ / (float)DAC_UPDATE_HZ;

  static float phaseF = 0.0f;
  static uint32_t tNext = 0;
  if (tNext == 0) tNext = micros();

  // 每次 loop 可能来得不够快：这里补齐到当前时间
  while ((int32_t)((uint32_t)micros() - tNext) >= 0) {
    uint8_t phase8 = (uint8_t)phaseF;
    uint8_t out = genSample8(w, phase8);
    dacWrite8(out);

    phaseF += phaseStepF;
    if (phaseF >= 256.0f) phaseF -= 256.0f;
    tNext += usPerUpdate;
  }
}

static void acquireBlock(Waveform w) {
  // 生成并同步采样：每个采样点：输出 DAC -> 读 ADC -> 存入
  // 这样不用额外信号源，保证 ADC 有输入。

  const uint32_t usPerSample = 1000000UL / SAMPLE_HZ;
  // 每次采样相位增量：phaseStep = 256 * f / sampleRate
  const float phaseStepF = 256.0f * (float)WAVE_FREQ_HZ / (float)SAMPLE_HZ;
  float phaseF = 0.0f;

  uint32_t tNext = micros();
  for (uint16_t i = 0; i < N_SAMPLES; i++) {
    uint8_t phase8 = (uint8_t)phaseF;
    uint8_t out = genSample8(w, phase8);
    dacWrite8(out);

    // 给 DAC 稍微一点点建立时间（很短即可）
    delayMicroseconds(2);

    samples[i] = (uint16_t)analogRead(ADC_PIN);

    phaseF += phaseStepF;
    if (phaseF >= 256.0f) phaseF -= 256.0f;

    tNext += usPerSample;
    int32_t waitUs = (int32_t)(tNext - (uint32_t)micros());
    if (waitUs > 0) delayMicroseconds((uint32_t)waitUs);
  }
}

static void computeMetrics(const uint16_t *buf, uint16_t n, float *outVpp, float *outFreq) {
  uint16_t vmin = 4095;
  uint16_t vmax = 0;
  uint32_t sum = 0;
  for (uint16_t i = 0; i < n; i++) {
    uint16_t v = buf[i];
    if (v < vmin) vmin = v;
    if (v > vmax) vmax = v;
    sum += v;
  }

  // 近似电压：假设 0..4095 ~ 0..3.3V（仅演示，实际会受校准影响）
  float vpp = ((float)(vmax - vmin) * 3.3f) / 4095.0f;
  *outVpp = vpp;

  // 频率估计（改进版）：
  // - 使用带滞回的“上升沿过中点”
  // - 用“首末两个过零点的时间差”来算频率，避免 128 点窗口里计数过粗导致跳到 218.75Hz 这种值
  float mid = (float)sum / (float)n;
  float hyst = 0.05f * (float)(vmax - vmin); // 5% 幅度滞回
  if (hyst < 10.0f) hyst = 10.0f;           // 兜底：至少 10 个 ADC count

  bool armed = false; // 先跌到 mid-hyst，再等上穿 mid+hyst
  int edges = 0;
  float firstEdgeT = 0.0f;
  float lastEdgeT = 0.0f;

  for (uint16_t i = 1; i < n; i++) {
    float a = (float)buf[i - 1];
    float b = (float)buf[i];

    if (!armed) {
      if (a < (mid - hyst)) armed = true;
      continue;
    }

    // 检测上穿 mid+hyst
    if (a < (mid + hyst) && b >= (mid + hyst)) {
      // 线性插值估计 crossing 的小数采样点位置
      float denom = (b - a);
      float frac = (denom != 0.0f) ? (((mid + hyst) - a) / denom) : 0.0f;
      float t = ((float)(i - 1) + frac) / (float)SAMPLE_HZ;

      if (edges == 0) firstEdgeT = t;
      lastEdgeT = t;
      edges++;
      armed = false;
    }
  }

  if (edges >= 2) {
    float dt = lastEdgeT - firstEdgeT;
    *outFreq = (dt > 0.0f) ? ((float)(edges - 1) / dt) : 0.0f;
  } else {
    *outFreq = 0.0f;
  }
}

static void drawWaveform(const uint16_t *buf, uint16_t n, Waveform w, float vpp, float freq) {
  uint16_t vmin = 4095;
  uint16_t vmax = 0;
  for (uint16_t i = 0; i < n; i++) {
    uint16_t v = buf[i];
    if (v < vmin) vmin = v;
    if (v > vmax) vmax = v;
  }
  if (vmax <= vmin) vmax = vmin + 1;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(OLED_WHITE);
  display.setCursor(0, 0);
  display.print(waveName(w));
  display.print("  Vpp=");
  display.print(vpp, 2);
  display.print("V");

  display.setCursor(0, 10);
  display.print("f=");
  display.print(freq, 1);
  display.print("Hz");

  // 波形区域：从 y=20 到 y=63
  const int yTop = 20;
  const int yBot = OLED_H - 1;
  const int h = yBot - yTop;

  // 画中线
  display.drawFastHLine(0, yTop + h / 2, OLED_W, OLED_WHITE);

  for (uint16_t x = 0; x < n && x < OLED_W; x++) {
    uint16_t v = buf[x];
    float norm = ((float)(v - vmin)) / (float)(vmax - vmin); // 0..1
    int y = yBot - (int)lroundf(norm * (float)h);
    if (y < yTop) y = yTop;
    if (y > yBot) y = yBot;
    display.drawPixel((int)x, y, OLED_WHITE);
  }

  display.display();
}

static bool pollButtonPressedEdge() {
  static bool lastStable = HIGH;
  static bool lastReading = HIGH;
  static uint32_t lastChange = 0;

  bool level = (digitalRead(BTN_MODE_PIN) != 0);
  if (level != lastReading) {
    lastReading = level;
    lastChange = millis();
  }

  if ((millis() - lastChange) > DEBOUNCE_MS && lastStable != lastReading) {
    bool prevStable = lastStable;
    lastStable = lastReading;

    // INPUT_PULLUP：按下为 LOW
    bool prevPressed = (prevStable == LOW);
    bool nowPressed = (lastStable == LOW);
    if (!prevPressed && nowPressed) return true;
  }

  return false;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("Lab6: DAC waveform -> ADC sample -> Vpp/freq -> OLED plot");
  Serial.println("Wiring: DAC(GPIO25/26) -> ADC(GPIO34), GND common.");
  Serial.println("If Serial Monitor shows nothing: close other monitors, confirm COM port, then press EN(reset).");

  pinMode(LED_HEARTBEAT_PIN, OUTPUT);
  setHeartbeatLed(false);
  // 上电立刻快速闪烁 10 次：用于确认“板载灯引脚/极性”是否正确
  for (int i = 0; i < 10; i++) {
    setHeartbeatLed(true);
    delay(80);
    setHeartbeatLed(false);
    delay(80);
  }

  pinMode(BTN_MODE_PIN, INPUT_PULLUP);

#if !LAB6_SCOPE_ONLY
  // ADC 配置
  analogSetPinAttenuation(ADC_PIN, ADC_11db);

  // OLED
  Serial.printf("OLED cfg: driver=%s addr=0x%02X size=%dx%d I2C=%dHz SDA=%d SCL=%d RST=%d\n",
                OLED_USE_SH1106 ? "SH1106" : "SSD1306",
                OLED_ADDR,
                OLED_W,
                OLED_H,
                (int)I2C_FREQ_HZ,
                (int)I2C_SDA_PIN,
                (int)I2C_SCL_PIN,
                (int)OLED_RESET_PIN);
  if (OLED_RESET_PIN >= 0) {
    pinMode(OLED_RESET_PIN, OUTPUT);
    digitalWrite(OLED_RESET_PIN, HIGH);
    delay(10);
    digitalWrite(OLED_RESET_PIN, LOW);
    delay(20);
    digitalWrite(OLED_RESET_PIN, HIGH);
    delay(20);
  }

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_FREQ_HZ);
  i2cScanOnce();

#if OLED_USE_SH1106
  if (!display.begin(OLED_ADDR, true)) {
    Serial.println("OLED init failed (SH1106). Will still print to Serial.");
  } else {
    Serial.println("OLED init OK");

    // 开机自检：全亮->全灭->反色闪一下，便于肉眼确认屏幕确实在工作
    display.clearDisplay();
    display.fillRect(0, 0, OLED_W, OLED_H, OLED_WHITE);
    display.display();
    delay(200);

    display.clearDisplay();
    display.display();
    delay(200);

    display.invertDisplay(true);
    delay(200);
    display.invertDisplay(false);

    // 设高对比度（有些屏默认对比度偏低，看起来像黑屏）
    display.setContrast(0xFF);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(OLED_WHITE);
    display.setCursor(0, 0);
    display.println("Lab6 ready");
    display.display();
  }
#else
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed (SSD1306, addr maybe 0x3C/0x3D). Will still print to Serial.");
  } else {
    Serial.println("OLED init OK");

    // 开机自检：全亮->全灭->反色闪一下，便于肉眼确认屏幕确实在工作
    display.clearDisplay();
    display.fillRect(0, 0, OLED_W, OLED_H, OLED_WHITE);
    display.display();
    delay(200);

    display.clearDisplay();
    display.display();
    delay(200);

    display.invertDisplay(true);
    delay(200);
    display.invertDisplay(false);

    // 设高对比度（有些屏默认对比度偏低，看起来像黑屏）
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(0xFF);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(OLED_WHITE);
    display.setCursor(0, 0);
    display.println("Lab6 ready");
    display.display();
  }
#endif
#else
  Serial.println("LAB6_SCOPE_ONLY=1: DAC-only mode. Probe DAC_PIN to view waveform.");
  Serial.println("Tip: set scope coupling=DC, probe x1/x10, trigger on rising edge.");
#endif
}

void loop() {
  static Waveform wave = W_SINE;
  static uint32_t lastBlinkMs = 0;
  static bool ledOn = false;
  static uint32_t lastHeartbeatMs = 0;

  if (millis() - lastBlinkMs >= 500) {
    lastBlinkMs = millis();
    ledOn = !ledOn;
    setHeartbeatLed(ledOn);
  }

  if (pollButtonPressedEdge()) {
    wave = (Waveform)((wave + 1) % W_COUNT);
    Serial.printf("Waveform -> %s\n", waveName(wave));
  }

#if LAB6_SCOPE_ONLY
  runDacOnly(wave);
  // 不要 delay()，避免输出断续导致示波器波形抖动

  if (millis() - lastHeartbeatMs >= 1000) {
    lastHeartbeatMs = millis();
    Serial.printf("alive=%lums wave=%s (DAC-only)\n", (unsigned long)millis(), waveName(wave));
  }
#else
  acquireBlock(wave);

  float vpp = 0.0f;
  float freq = 0.0f;
  computeMetrics(samples, N_SAMPLES, &vpp, &freq);

  // 降低打印频率，避免刷屏；同时保证你随时打开串口都能看到输出
  if (millis() - lastHeartbeatMs >= 200) {
    lastHeartbeatMs = millis();
    Serial.printf("%s  Vpp=%.2fV  f=%.1fHz  (target=%uHz)\n", waveName(wave), vpp, freq, (unsigned)WAVE_FREQ_HZ);
  }

  // 画图（若 OLED 没初始化成功，display.display() 会无效，不影响串口输出）
  drawWaveform(samples, N_SAMPLES, wave, vpp, freq);

  delay(50);
#endif
}
