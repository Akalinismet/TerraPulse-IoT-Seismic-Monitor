#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* ssid = "Your_wifi_name";
const char* password = "Your_wifi_password";

ESP8266WebServer server(80);

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- PIN TANIMLAMALARI ---
#define SENSOR_801S D0
#define BUZZER D5
#define BTN_RESET D6  // Buton 1
#define BTN_UP D3     // Buton 2
#define BTN_DOWN D7   // Buton 3
#define BTN_SCREEN D4 // Buton 4 (D8 parazit yaptığı için D4'e taşındı)

// --- GLOBAL DEĞİŞKENLER ---
float threshold = 0.20; 
float net_g = 0;
float max_g = 0;
String durum = "NORMAL";
bool isWiFiConnected = false;
bool displayOn = true; 

void handleData() {
  String json = "{";
  json += "\"siddet\":" + String(net_g, 3) + ",";
  json += "\"max\":" + String(max_g, 3) + ",";
  json += "\"esik\":" + String(threshold, 2) + ","; 
  json += "\"durum\":\"" + durum + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void handleUpdateThreshold() {
  if (server.hasArg("val")) {
    threshold = server.arg("val").toFloat();
    server.send(200, "text/plain", "OK");
  }
}

void setup() {
  Serial.begin(115200);
  
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, HIGH); // Buzzer sustur

  Wire.begin(4, 5); 
  pinMode(SENSOR_801S, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SCREEN, INPUT_PULLUP);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("TerraPulse v2.0");
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 8000) { 
    delay(500); 
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    isWiFiConnected = true;
    server.on("/data", handleData);
    server.on("/set", handleUpdateThreshold);
    server.begin();
  }

  // MPU6050 Uyandırma
  Wire.beginTransmission(0x68);
  Wire.write(0x6B); 
  Wire.write(0);    
  Wire.endTransmission();
}

void loop() {
  if (isWiFiConnected && WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  // --- BUTON KONTROLLERİ (Gelişmiş Debounce ile) ---
  
  // 1. Reset
  if (digitalRead(BTN_RESET) == LOW) {
    max_g = 0;
    while(digitalRead(BTN_RESET) == LOW); // Elini çekene kadar bekle
    delay(50);
  }

  // 2. Hassasiyet Artır
  if (digitalRead(BTN_UP) == LOW) {
    threshold += 0.05;
    if (threshold > 2.0) threshold = 2.0;
    while(digitalRead(BTN_UP) == LOW);
    delay(50);
  }

  // 3. Hassasiyet Azalt
  if (digitalRead(BTN_DOWN) == LOW) {
    threshold -= 0.05;
    if (threshold < 0.05) threshold = 0.05;
    while(digitalRead(BTN_DOWN) == LOW);
    delay(50);
  }

  // 4. Ekran Aç/Kapat (Kendi kendine açılmayı önleyen mantık)
  if (digitalRead(BTN_SCREEN) == LOW) {
    delay(100); // Küçük parazitleri ele
    if (digitalRead(BTN_SCREEN) == LOW) {
      displayOn = !displayOn;
      if (displayOn) {
        display.ssd1306_command(SSD1306_DISPLAYON);
      } else {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
      }
      while(digitalRead(BTN_SCREEN) == LOW); // ELİNİ ÇEKENE KADAR BEKLE (Çözüm burası)
      delay(100);
    }
  }

  // --- SENSÖR OKUMA ---
  Wire.beginTransmission(0x68);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, 6, true);
  int16_t rawX = Wire.read() << 8 | Wire.read();
  int16_t rawY = Wire.read() << 8 | Wire.read();
  int16_t rawZ = Wire.read() << 8 | Wire.read();
  
  float ax = (float)rawX / 16384.0;
  float ay = (float)rawY / 16384.0;
  float az = (float)rawZ / 16384.0;
  
  net_g = abs(sqrt(ax*ax + ay*ay + az*az) - 1.0);
  if (net_g > max_g) max_g = net_g;

  // --- EKRAN GÜNCELLEME ---
  if (displayOn) {
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    
    if (WiFi.status() == WL_CONNECTED) {
      display.print("IP: "); display.println(WiFi.localIP());
    } else {
      display.println("Status: OFFLINE");
    }
    
    display.setCursor(0, 15);
    display.setTextSize(2);
    display.print("G: "); display.print(net_g, 3);
    
    display.setTextSize(1);
    display.setCursor(0, 40);
    display.print("Max: "); display.print(max_g, 3);
    display.print("  TH: "); display.print(threshold, 2);

    if (net_g > threshold) {
      durum = "DANGER";
      display.fillRect(0, 52, 128, 12, WHITE);
      display.setTextColor(BLACK);
      display.setCursor(10, 54);
      display.print("!!! DANGER !!!");
      display.setTextColor(WHITE);
    } else {
      durum = "NORMAL";
    }
    display.display();
  }

  // --- BUZZER KONTROL ---
  if (net_g > (threshold + 0.10)) {
     digitalWrite(BUZZER, LOW); 
  } else {
     digitalWrite(BUZZER, HIGH);
  }

  delay(50);
}
