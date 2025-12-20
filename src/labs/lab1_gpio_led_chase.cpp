#include <Arduino.h>

// (1) 控制 GPIO 输出：点亮 LED + 流水灯
//
// 连接方式（建议）：每个 LED 串一个 220~1k 电阻到 GPIO，LED 另一端接 GND。
// 注意：ESP32 单个 IO 电流有限，别直接带大功率负载。
//
// 说明：
// - 如果你只接了 1 颗 LED：会表现为闪烁。
// - 如果你接了多颗 LED：会按数组顺序做“流水灯”。

// 默认给 5 路 LED 用的 GPIO（相对安全、非输入专用）。
// 如果你已经接到别的引脚：直接改这里。
static const uint8_t LED_PINS[] = {16, 17, 18, 19, 21};
static const uint32_t STEP_MS = 200;

static void allOff() {
  const size_t n = sizeof(LED_PINS) / sizeof(LED_PINS[0]);
  for (size_t i = 0; i < n; i++) {
    digitalWrite(LED_PINS[i], LOW);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("Lab1: GPIO output -> LED + chase");
  Serial.println("Tip: If pins don't match your wiring, edit LED_PINS[] in this file.");

  const size_t n = sizeof(LED_PINS) / sizeof(LED_PINS[0]);
  for (size_t i = 0; i < n; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }
}

void loop() {
  const size_t n = sizeof(LED_PINS) / sizeof(LED_PINS[0]);
  if (n == 0) return;

  static size_t index = 0;

  allOff();
  digitalWrite(LED_PINS[index], HIGH);
  index = (index + 1) % n;
  delay(STEP_MS);
}
