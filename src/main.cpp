#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>

// 路线 1：ESP32 自己当 WebServer
// - 连接 Wi-Fi（STA）
// - 开一个网页（滑条 0..255）
// - 浏览器直接调板载 LED 亮度
//
// 使用方式：
// 1) 在 platformio.ini 里通过 build_flags 设置 WIFI_SSID / WIFI_PASS
// 2) 烧录后打开串口，看 ESP32 的 IP
// 3) 浏览器打开：http://<ESP32_IP>/

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
#endif

static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

static const int LED_PIN = LED_BUILTIN;
static const bool LED_ACTIVE_LOW = true; // 你的板子 D2 大概率是低电平点亮
static const uint32_t PWM_FREQ_HZ = 5000;
static const uint8_t PWM_RES_BITS = 8;
static const uint8_t PWM_CHANNEL = 0; // 仅 Arduino-ESP32 2.x 需要

static WebServer server(80);
static int gDuty255 = 0;

static int clampDuty255(int duty) {
  if (duty < 0) return 0;
  if (duty > 255) return 255;
  return duty;
}

static void pwmWriteDuty255(int duty255) {
  const int maxDuty = (1 << PWM_RES_BITS) - 1; // 255
  gDuty255 = clampDuty255(duty255);
  int effectiveDuty = LED_ACTIVE_LOW ? (maxDuty - gDuty255) : gDuty255;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  ledcWrite(LED_PIN, effectiveDuty);
#else
  ledcWrite(PWM_CHANNEL, effectiveDuty);
#endif
}

static bool wifiConnect() {
  Serial.printf("Connecting to Wi-Fi SSID: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connect timeout. Check SSID/password and 2.4GHz network.");
    return false;
  }

  Serial.println("Wi-Fi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

static void handleRoot() {
  // 极简网页：一个滑条 + 当前值显示
  String html;
  html.reserve(900);
  html += "<!doctype html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>ESP32 LED</title></head><body>";
  html += "<h3>ESP32 LED Brightness</h3>";
  html += "<div>Duty: <span id='v'>" + String(gDuty255) + "</span>/255</div>";
  html += "<input id='s' type='range' min='0' max='255' value='" + String(gDuty255) + "' style='width:100%'>";
  html += "<script>";
  html += "const s=document.getElementById('s');const v=document.getElementById('v');";
  html += "function send(x){fetch('/set?duty='+x).catch(()=>{});}";
  html += "s.addEventListener('input',()=>{v.textContent=s.value;send(s.value);});";
  html += "</script></body></html>";

  server.send(200, "text/html; charset=utf-8", html);
}

static void handleSet() {
  if (!server.hasArg("duty")) {
    server.send(400, "text/plain; charset=utf-8", "missing duty");
    return;
  }
  int duty = clampDuty255(server.arg("duty").toInt());
  pwmWriteDuty255(duty);
  server.send(200, "application/json", String("{\"duty\":") + String(gDuty255) + "}");
}

static void handleState() {
  server.send(200, "application/json", String("{\"duty\":") + String(gDuty255) + "}");
}

static void startWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/set", HTTP_GET, handleSet);
  server.on("/state", HTTP_GET, handleState);
  server.begin();
  Serial.println("WebServer started on port 80");
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("ESP32 WebServer -> PWM demo");

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  if (!ledcAttach(LED_PIN, PWM_FREQ_HZ, PWM_RES_BITS)) {
    Serial.println("ledcAttach failed");
  }
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(LED_PIN, PWM_CHANNEL);
#endif

  pwmWriteDuty255(0);

  if (String(WIFI_SSID) == "YOUR_WIFI_SSID") {
    Serial.println("Please set WIFI_SSID/WIFI_PASS in platformio.ini build_flags.");
  }

  if (wifiConnect()) {
    startWebServer();
    Serial.print("Open in browser: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");
  }
}

void loop() {
  server.handleClient();
  delay(2);
}