#include <Arduino.h>

// (2) 共阴极数码管显示：通过外接按键控制数字改变
//
// 重要：这个示例按“1 位共阴极数码管”来写（不做 4 位动态扫描）。
// - 共阴极：COM 接 GND。
// - 段码 a,b,c,d,e,f,g,dp 各自通过电阻接到 GPIO。
// - 段为“高电平点亮”（active-high）。
//
// 如果你是 4 位数码管（带位选），告诉我你的型号/接线，我再给你“动态扫描版”。

// 如果你的数码管实际是“共阳极”（COM 接 3.3V，段脚拉低点亮），把下面改成 0：
// 或者在 platformio.ini 里给 lab2 加 build_flags： -D SEG_ACTIVE_HIGH=0
#ifndef SEG_ACTIVE_HIGH
#define SEG_ACTIVE_HIGH 1
#endif

// 段引脚映射（按你的实际接线改）：
// a b c d e f g dp
static const uint8_t SEG_PINS[8] = {16, 17, 18, 19, 21, 22, 23, 5};
static const bool SEG_HAS_DP = true;

// 外接按键：一端接 GPIO，一端接 GND（用内部上拉 INPUT_PULLUP），按下为 LOW。
// 建议用“非启动相关”的普通 GPIO（避免 0/2/12/15 等）。
static const uint8_t BTN_INC_PIN = 4;

// 默认按键按下为 LOW（GPIO 通过 INPUT_PULLUP 上拉，按键接地）。
// 如果你把按键另一端接到 3.3V，并且外接了下拉电阻，改成 0。
#ifndef BTN_ACTIVE_LOW
#define BTN_ACTIVE_LOW 1
#endif

static const uint32_t DEBOUNCE_MS = 30;

// 0~9 的段码（a b c d e f g，1=亮）
static const uint8_t DIGITS[10] = {
    0b11111100, // 0
    0b01100000, // 1
    0b11011010, // 2
    0b11110010, // 3
    0b01100110, // 4
    0b10110110, // 5
    0b10111110, // 6
    0b11100000, // 7
    0b11111110, // 8
    0b11110110  // 9
};

static void segWritePin(uint8_t pin, bool on) {
  // 共阴极：on -> HIGH
  // 共阳极：on -> LOW
  uint8_t level = on ? (SEG_ACTIVE_HIGH ? HIGH : LOW) : (SEG_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(pin, level);
}

static void segWriteRaw(uint8_t abcdefg_dp) {
  for (int bit = 0; bit < 8; bit++) {
    bool on = (abcdefg_dp & (1 << (7 - bit))) != 0;
    if (!SEG_HAS_DP && bit == 7) on = false;
    segWritePin(SEG_PINS[bit], on);
  }
}

static void segShowDigit(uint8_t d) {
  d %= 10;
  segWriteRaw(DIGITS[d]);
}

static void segAllOff() {
  segWriteRaw(0);
}

static void segSelfTestOnce() {
  // 逐段点亮，便于你确认：
  // 1) COM 是否接对（共阴/共阳）
  // 2) 每根线对应的段（a/b/c.../dp）
  Serial.println("7-seg self-test: a b c d e f g dp");
  const char *names[8] = {"a", "b", "c", "d", "e", "f", "g", "dp"};
  for (int i = 0; i < 8; i++) {
    segAllOff();
    segWritePin(SEG_PINS[i], true);
    Serial.printf("  segment %s (GPIO %u) ON\n", names[i], (unsigned)SEG_PINS[i]);
    delay(400);
  }
  segAllOff();
  Serial.println("self-test done\n");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("Lab2: 1-digit 7-seg (common cathode) -> button controls digit");
  Serial.println("Tip: Edit SEG_PINS[] and BTN_INC_PIN to match wiring.");
  Serial.printf("SEG_ACTIVE_HIGH=%d (1=common-cathode, 0=common-anode)\n", (int)SEG_ACTIVE_HIGH);
  Serial.printf("BTN_INC_PIN=%u, BTN_ACTIVE_LOW=%d\n", (unsigned)BTN_INC_PIN, (int)BTN_ACTIVE_LOW);
  Serial.println("Button wiring (recommended): GPIO -> button -> GND (uses INPUT_PULLUP)");

  for (int i = 0; i < 8; i++) {
    pinMode(SEG_PINS[i], OUTPUT);
    digitalWrite(SEG_PINS[i], LOW);
  }

  pinMode(BTN_INC_PIN, INPUT_PULLUP);

  segShowDigit(0);

  // 上电先做一次逐段自检
  segSelfTestOnce();
}

void loop() {
  static uint8_t digit = 0;

  static bool lastStableLevel = HIGH;
  static bool lastReadingLevel = HIGH;
  static uint32_t lastChangeMs = 0;
  static uint32_t lastReportMs = 0;

  bool level = (digitalRead(BTN_INC_PIN) != 0);

  // 每秒打印一次当前电平，方便确认你确实在跑 lab2 新固件 & 引脚读数是否在变
  if (millis() - lastReportMs >= 1000) {
    lastReportMs = millis();
    Serial.printf("BTN level now: %s\n", level ? "HIGH" : "LOW");
  }

  if (level != lastReadingLevel) {
    lastReadingLevel = level;
    lastChangeMs = millis();
    Serial.printf("BTN level changed: %s\n", level ? "HIGH" : "LOW");
  }

  if ((millis() - lastChangeMs) > DEBOUNCE_MS && lastStableLevel != lastReadingLevel) {
    bool prevStable = lastStableLevel;
    lastStableLevel = lastReadingLevel;

    bool prevPressed = BTN_ACTIVE_LOW ? (prevStable == LOW) : (prevStable == HIGH);
    bool nowPressed = BTN_ACTIVE_LOW ? (lastStableLevel == LOW) : (lastStableLevel == HIGH);

    // 仅在“按下”瞬间加 1（避免长按连加）
    if (!prevPressed && nowPressed) {
      digit = (uint8_t)((digit + 1) % 10);
      segShowDigit(digit);
      Serial.printf("digit=%u\n", (unsigned)digit);
    }
  }
}
