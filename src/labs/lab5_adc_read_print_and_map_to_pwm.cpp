#include <Arduino.h>

// (5) ADC 模数转换实验
// - 读 ADC 引脚电压（0..4095），串口打印
// - 同时把 ADC 值映射到 PWM，占空比 0..255（可选，直观）
//
// 推荐接法：10k 电位器
// - 两端接 3.3V/GND
// - 中间滑动端接 ADC_PIN（默认 GPIO34）
// 注意：GPIO34/35/36/39 是“输入专用”，适合做 ADC。

static const int ADC_PIN = 34; // ADC1_CH6

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

static const int LED_PIN = LED_BUILTIN;
static const bool LED_ACTIVE_LOW = true;
static const uint32_t PWM_FREQ_HZ = 5000;
static const uint8_t PWM_RES_BITS = 8;
static const uint8_t PWM_CHANNEL = 0;

static void pwmWriteDuty255(int duty255) {
  const int maxDuty = (1 << PWM_RES_BITS) - 1;
  if (duty255 < 0) duty255 = 0;
  if (duty255 > maxDuty) duty255 = maxDuty;
  int effective = LED_ACTIVE_LOW ? (maxDuty - duty255) : duty255;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(LED_PIN, effective);
#else
  ledcWrite(PWM_CHANNEL, effective);
#endif
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("Lab5: ADC read -> print (+ map to PWM)");
  Serial.println("Tip: Use a potentiometer on GPIO34, else readings may float.");

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  if (!ledcAttach(LED_PIN, PWM_FREQ_HZ, PWM_RES_BITS)) {
    Serial.println("ledcAttach failed");
  }
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(LED_PIN, PWM_CHANNEL);
#endif

  // ESP32 Arduino: analogRead 默认 12bit(0..4095)
  analogSetPinAttenuation(ADC_PIN, ADC_11db); // 量程更适合 0~3.3V（不同板子略有差异）
}

void loop() {
  int raw = analogRead(ADC_PIN); // 0..4095
  int duty = map(raw, 0, 4095, 0, 255);

  pwmWriteDuty255(duty);
  Serial.printf("ADC raw=%d, duty=%d/255\n", raw, duty);

  delay(200);
}
