#include <Arduino.h>

// (4) PWM 呼吸灯（LEDC）
// - 默认用板载 LED(GPIO2)，常见是低电平点亮（active-low）

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

static const int LED_PIN = LED_BUILTIN;
static const bool LED_ACTIVE_LOW = true;
static const uint32_t PWM_FREQ_HZ = 5000;
static const uint8_t PWM_RES_BITS = 8;
static const uint8_t PWM_CHANNEL = 0; // 仅 Arduino-ESP32 2.x 需要

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

  Serial.println("Lab4: PWM breathing LED");

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  if (!ledcAttach(LED_PIN, PWM_FREQ_HZ, PWM_RES_BITS)) {
    Serial.println("ledcAttach failed");
  }
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(LED_PIN, PWM_CHANNEL);
#endif

  pwmWriteDuty255(0);
}

void loop() {
  // 三角波呼吸：0->255->0
  for (int d = 0; d <= 255; d++) {
    pwmWriteDuty255(d);
    delay(5);
  }
  for (int d = 255; d >= 0; d--) {
    pwmWriteDuty255(d);
    delay(5);
  }
}
