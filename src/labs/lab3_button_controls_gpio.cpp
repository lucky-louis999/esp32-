#include <Arduino.h>

// (3) 按键输入控制 GPIO 输出
// - 用 BOOT 按键（GPIO0）控制板载 LED（GPIO2）开/关（每按一次切换）

static const uint8_t BTN_PIN = 0;  // BOOT
static const uint8_t LED_PIN = 2;  // 常见板载 LED
static const bool LED_ACTIVE_LOW = true;
static const uint32_t DEBOUNCE_MS = 30;

static void ledWrite(bool on) {
  digitalWrite(LED_PIN, LED_ACTIVE_LOW ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println("Lab3: button -> GPIO output toggle");

  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  ledWrite(false);
}

void loop() {
  static bool ledOn = false;
  static bool lastStable = HIGH;
  static bool lastReading = HIGH;
  static uint32_t lastChangeMs = 0;

  bool reading = digitalRead(BTN_PIN);
  if (reading != lastReading) {
    lastReading = reading;
    lastChangeMs = millis();
  }

  if ((millis() - lastChangeMs) > DEBOUNCE_MS && lastStable != lastReading) {
    lastStable = lastReading;

    // 按下沿（HIGH->LOW）切换
    if (lastStable == LOW) {
      ledOn = !ledOn;
      ledWrite(ledOn);
      Serial.printf("LED is now %s\n", ledOn ? "ON" : "OFF");
    }
  }
}
