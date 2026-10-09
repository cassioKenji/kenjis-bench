#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

#include "secrets.h"  // WIFI_SSID e WIFI_PASS (veja include/secrets.example.h)

// LCD 20x4 HD44780 com adaptador I2C (PCF8574), endereço detectado sozinho
const int LCD_SDA = 9;
const int LCD_SCL = 8;
const int LCD_COLS = 20;
const int LCD_ROWS = 4;
const uint32_t DISPLAY_REFRESH_MS = 1000;

hd44780_I2Cexp lcd;
bool lcdOk = false;

// Access point com senha (WPA2, AP_PASS em secrets.h) que repassa a internet da rede de casa (NAT)
#ifndef AP_PASS
#error "Defina AP_PASS em include/secrets.h (veja include/secrets.example.h)"
#endif
static_assert(sizeof(AP_PASS) - 1 >= 8, "AP_PASS precisa ter pelo menos 8 caracteres (exigência do WPA2)");
const char *AP_SSID = "ESP32-S3";
IPAddress AP_IP(192, 168, 4, 1);      // sub-rede diferente da rede de casa
IPAddress AP_MASK(255, 255, 255, 0);
IPAddress AP_LEASE_START(192, 168, 4, 2);
IPAddress AP_DNS(8, 8, 8, 8);

const uint8_t BRIGHTNESS = 64;      // 0-255; o LED é bem forte no máximo
const uint32_t BLINK_MS = 500;      // tempo aceso e apagado ao piscar

// Clima e qualidade do ar pela Open-Meteo (sem chave de API). Ajuste latitude/longitude para a sua região.
#define LATITUDE "-23.4442"
#define LONGITUDE "-46.9190"
#define TIMEZONE_PARAM "&timezone=America%2FSao_Paulo"
const char *WEATHER_URL =
  "https://api.open-meteo.com/v1/forecast?latitude=" LATITUDE "&longitude=" LONGITUDE
  "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,precipitation_probability,uv_index"
  "&daily=sunrise,sunset&forecast_days=1" TIMEZONE_PARAM;
const char *AIR_URL =
  "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" LATITUDE "&longitude=" LONGITUDE
  "&current=us_aqi" TIMEZONE_PARAM;
const uint32_t WEATHER_REFRESH_MS = 10 * 60 * 1000;  // atualiza a cada 10 min
const uint32_t WEATHER_RETRY_MS = 60 * 1000;         // se falhar, tenta de novo em 1 min
const uint32_t STATUS_PAGE_MS = 5000;                // tempo da tela de status no LCD
const uint32_t WEATHER_PAGE_MS = 60000;               // tempo da tela de clima no LCD

// Relógio pela internet (NTP), horário de Brasília (sem horário de verão)
const char *TIMEZONE = "<-03>3";

// Caracteres desenhados no LCD (posições 1-7; a 0 não dá para usar dentro de texto)
enum LcdChar : uint8_t { CH_I_ACUTE = 1, CH_DROP, CH_UMBRELLA, CH_SUNRISE, CH_SUNSET, CH_WIND };
const uint8_t LCD_CHARS[][8] = {
  {0b00010, 0b00100, 0b00000, 0b01100, 0b00100, 0b00100, 0b01110, 0b00000},  // í
  {0b00100, 0b00100, 0b01010, 0b01010, 0b10001, 0b10001, 0b01110, 0b00000},  // gota (umidade)
  {0b00100, 0b01110, 0b11111, 0b00100, 0b00100, 0b10100, 0b01000, 0b00000},  // guarda-chuva
  {0b00100, 0b01110, 0b10101, 0b00100, 0b00000, 0b11111, 0b00000, 0b00000},  // nascer do sol
  {0b00100, 0b00100, 0b10101, 0b01110, 0b00100, 0b00000, 0b11111, 0b00000},  // pôr do sol
  {0b00000, 0b11110, 0b00001, 0b11110, 0b00000, 0b11100, 0b00010, 0b11100},  // vento
};

struct Weather {
  bool ok = false;
  float temperature = 0;
  int humidity = 0;
  int rainChance = 0;
  float wind = 0;
  float uvIndex = 0;
  char sunrise[6] = "--:--";
  char sunset[6] = "--:--";
  char error[LCD_COLS + 1] = "buscando...";
};
Weather weather;

struct AirQuality {
  bool ok = false;
  int aqi = 0;  // índice americano (US AQI): 0-50 boa ... 300+ perigosa
};
AirQuality air;

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("Conectado em %s, IP %s\n", WIFI_SSID, WiFi.STA.localIP().toString().c_str());
      WiFi.AP.enableNAPT(true);
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("Desconectado da rede de casa, tentando de novo...");
      WiFi.AP.enableNAPT(false);
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.printf("AP \"%s\" (WPA2) em %s\n", AP_SSID, AP_IP.toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      Serial.printf("Cliente conectado no AP: %s\n", IPAddress(info.wifi_ap_staipassigned.ip.addr).toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Serial.println("Cliente saiu do AP");
      break;
    default: break;
  }
}

// Escreve uma linha inteira, completando com espaços (sobrescreve sem limpar a tela, sem piscar)
void lcdLine(int row, const char *fmt, ...) {
  char buf[LCD_COLS + 1];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  for (int i = max(len, 0); i < LCD_COLS; i++) buf[i] = ' ';
  buf[LCD_COLS] = '\0';
  lcd.setCursor(0, row);
  lcd.print(buf);
}

void drawStatus() {
  lcdLine(0, "Casa: %s", WIFI_SSID);
  if (WiFi.status() == WL_CONNECTED) {
    lcdLine(1, "%s %ddB", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    lcdLine(1, "conectando...");
  }
  lcdLine(2, "AP: %s (WPA2)", AP_SSID);
  lcdLine(3, "Clientes: %u", WiFi.AP.stationCount());
}

// GET em uma URL HTTPS + leitura do JSON; em caso de erro devolve false e escreve o motivo em error
bool httpGetJson(const char *url, JsonDocument &doc, char *error, size_t errorSize) {
  WiFiClientSecure client;
  client.setInsecure();  // sem validar certificado (ok para dados públicos)
  HTTPClient http;
  http.begin(client, url);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(error, errorSize, "erro HTTP %d", code);
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    snprintf(error, errorSize, "JSON invalido");
    return false;
  }
  return true;
}

// Copia "HH:MM" de um horário ISO ("2026-10-09T05:40")
void copyHourMinute(char *dest, const char *iso) {
  if (iso && strlen(iso) >= 16) {
    memcpy(dest, iso + 11, 5);
    dest[5] = '\0';
  }
}

// Busca clima e qualidade do ar; devolve false se o clima falhar (motivo em weather.error)
bool fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    snprintf(weather.error, sizeof(weather.error), "sem Wi-Fi");
    return false;
  }

  JsonDocument doc;
  if (!httpGetJson(WEATHER_URL, doc, weather.error, sizeof(weather.error))) {
    Serial.printf("Clima: %s\n", weather.error);
    return false;
  }
  JsonObject current = doc["current"];
  weather.temperature = current["temperature_2m"];
  weather.humidity = current["relative_humidity_2m"];
  weather.rainChance = current["precipitation_probability"];
  weather.wind = current["wind_speed_10m"];
  weather.uvIndex = current["uv_index"];
  copyHourMinute(weather.sunrise, doc["daily"]["sunrise"][0]);
  copyHourMinute(weather.sunset, doc["daily"]["sunset"][0]);
  weather.ok = true;
  Serial.printf("Clima: %.1f C, umidade %d%%, chuva %d%%, vento %.1f km/h, UV %.1f, sol %s-%s\n",
                weather.temperature, weather.humidity, weather.rainChance, weather.wind, weather.uvIndex,
                weather.sunrise, weather.sunset);

  // Qualidade do ar é opcional: se falhar, o clima continua valendo
  JsonDocument airDoc;
  char airError[LCD_COLS + 1];
  if (httpGetJson(AIR_URL, airDoc, airError, sizeof(airError))) {
    air.aqi = airDoc["current"]["us_aqi"];
    air.ok = true;
    Serial.printf("Ar: AQI %d\n", air.aqi);
  } else {
    Serial.printf("Ar: %s\n", airError);
  }
  return true;
}

const char *aqiLabel(int aqi) {
  if (aqi <= 50) return "Boa";
  if (aqi <= 100) return "Mod";
  if (aqi <= 150) return "Sens";  // ruim para grupos sensíveis
  if (aqi <= 200) return "Ruim";
  if (aqi <= 300) return "MRuim";
  return "Perig";
}

// Tela de clima (20x4), por exemplo:
//   S. Parnaíba    14:35
//   30.3°C ◆50% ☂0%
//   ↑05:40 ↓18:09 UV8
//   ≈~6km/h Ar 42 Boa
void drawWeather() {
  char clock[6] = "--:--";
  struct tm now;
  if (getLocalTime(&now, 0)) strftime(clock, sizeof(clock), "%H:%M", &now);
  lcdLine(0, "S. Parna%cba    %s", CH_I_ACUTE, clock);

  if (!weather.ok) {
    lcdLine(1, "%s", weather.error);
    lcdLine(2, "");
    lcdLine(3, "");
    return;
  }
  // 0xDF = símbolo de grau no LCD
  lcdLine(1, "%.1f\xDF" "C %c%d%% %c%d%%", weather.temperature, CH_DROP, weather.humidity, CH_UMBRELLA,
          weather.rainChance);
  lcdLine(2, "%c%s %c%s UV%ld", CH_SUNRISE, weather.sunrise, CH_SUNSET, weather.sunset, lroundf(weather.uvIndex));
  if (air.ok) {
    lcdLine(3, "%c~%ldkm/h Ar %d %s", CH_WIND, lroundf(weather.wind), air.aqi, aqiLabel(air.aqi));
  } else {
    lcdLine(3, "%c~%ldkm/h Ar --", CH_WIND, lroundf(weather.wind));
  }
}

void initLcd() {
  int status = lcd.begin(LCD_COLS, LCD_ROWS);
  lcdOk = status == 0;
  if (lcdOk) {
    for (size_t i = 0; i < sizeof(LCD_CHARS) / sizeof(LCD_CHARS[0]); i++) {
      lcd.createChar(CH_I_ACUTE + i, LCD_CHARS[i]);
    }
    Serial.println("LCD ok");
  } else {
    Serial.printf("LCD não encontrado no I2C (erro %d); confira SDA/SCL e alimentação\n", status);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("PSRAM: %u bytes\n", ESP.getPsramSize());
  Serial.printf("Flash: %u bytes\n", ESP.getFlashChipSize());

  Wire.begin(LCD_SDA, LCD_SCL);
  initLcd();

  Network.onEvent(onWifiEvent);

  WiFi.AP.begin();
  WiFi.AP.config(AP_IP, AP_IP, AP_MASK, AP_LEASE_START, AP_DNS);
  WiFi.AP.create(AP_SSID, AP_PASS);  // com senha = WPA2

  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Acerta o relógio pela internet assim que o Wi-Fi conectar
  configTzTime(TIMEZONE, "pool.ntp.org", "time.google.com");
}

void loop() {
  // Pisca verde enquanto houver alguém conectado no AP; senão, apagado
  bool clientConnected = WiFi.AP.stationCount() > 0;
  bool blinkOn = (millis() / BLINK_MS) % 2 == 0;
  rgbLedWrite(RGB_BUILTIN, 0, clientConnected && blinkOn ? BRIGHTNESS : 0, 0);

  // Busca o clima quando o Wi-Fi conectar e depois a cada 10 min (ou 1 min se falhar)
  static uint32_t lastFetch = 0;
  static uint32_t fetchInterval = 0;
  static bool fetchedOnce = false;
  if (WiFi.status() == WL_CONNECTED && (!fetchedOnce || millis() - lastFetch >= fetchInterval)) {
    fetchedOnce = true;
    lastFetch = millis();
    fetchInterval = fetchWeather() ? WEATHER_REFRESH_MS : WEATHER_RETRY_MS;
  }

  // Se o LCD não respondeu, tenta de novo a cada 5 s (permite ligar os fios com a placa rodando)
  static uint32_t lastLcdRetry = 0;
  if (!lcdOk && millis() - lastLcdRetry >= 5000) {
    lastLcdRetry = millis();
    initLcd();
  }

  static uint32_t lastDraw = 0;
  if (lcdOk && millis() - lastDraw >= DISPLAY_REFRESH_MS) {
    lastDraw = millis();
    // Ciclo: primeiro a tela de status, depois a de clima
    bool weatherPage = millis() % (STATUS_PAGE_MS + WEATHER_PAGE_MS) >= STATUS_PAGE_MS;
    if (weatherPage) {
      drawWeather();
    } else {
      drawStatus();
    }
  }
  delay(50);
}
