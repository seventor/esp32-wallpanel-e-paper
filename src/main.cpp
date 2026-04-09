/*
 * VG RSS news reader for Waveshare E-Paper ESP32 Driver Board + GxEPD2.
 * Fetches https://www.vg.no/rss/feed/?format=rss over WiFi and shows headlines.
 *
 * Large panels (e.g. 7.5"): each full refresh draws heavy current and flashes for many
 * seconds. We only init the EPD once WiFi is up, then do a single full draw (or one
 * error screen). Status is printed on Serial — not on the panel — to avoid brownouts.
 */

#include <Arduino.h>
#include <SPI.h>
#include <time.h>
#include <esp_system.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "display_selection.h"
#include "font_noto_sans_u8g2.h"
#include "wifi_config.h"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

static const char *kRssUrl = "https://www.vg.no/rss/feed/?format=rss";
static const int kNewsCount = 10;
static const uint32_t kRefreshMs = 15UL * 60UL * 1000UL;

#define MAX_DISPLAY_BUFFER_SIZE 65536ul
#define MAX_HEIGHT(EPD) \
  (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) ? EPD::HEIGHT \
                                                             : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

static GxEPD2_BW<EPD_DRIVER_CLASS, MAX_HEIGHT(EPD_DRIVER_CLASS)> display(
    EPD_DRIVER_CLASS(/*CS=*/15, /*DC=*/27, /*RST=*/26, /*BUSY=*/25));

static SPIClass hspi(HSPI);
static U8G2_FOR_ADAFRUIT_GFX u8Fonts;

static String gTitles[kNewsCount];
static int gTitleCount;

static uint32_t gNextRssFetchMs;
static bool gRssScheduleActive = false;

static void initEpd() {
  hspi.begin(/*SCK=*/13, /*MISO=*/12, /*DIN=*/14, /*CS=*/15);
  display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200);
  u8Fonts.begin(display);
}

static void decodeXmlEntities(String &s) {
  s.replace("&amp;", "&");
  s.replace("&quot;", "\"");
  s.replace("&apos;", "'");
  s.replace("&lt;", "<");
  s.replace("&gt;", ">");
  s.replace("&#39;", "'");
}

static bool extractRssTitles(const String &xml) {
  gTitleCount = 0;
  size_t pos = 0;
  while (gTitleCount < kNewsCount) {
    int i0 = xml.indexOf("<item>", pos);
    if (i0 < 0) break;
    int i1 = xml.indexOf("</item>", i0);
    if (i1 < 0) break;
    String block = xml.substring(i0, i1);
    pos = i1 + 7;

    int t0 = block.indexOf("<title>");
    int t1 = block.indexOf("</title>", t0);
    if (t0 < 0 || t1 < 0) continue;
    String title = block.substring(t0 + 7, t1);
    title.trim();
    if (title.startsWith("<![CDATA[")) {
      int end = title.indexOf("]]>");
      if (end > 9) title = title.substring(9, end);
    }
    decodeXmlEntities(title);
    if (title.length() == 0) continue;
    gTitles[gTitleCount++] = title;
  }
  return gTitleCount > 0;
}

static bool fetchRss(String &out) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(client, kRssUrl)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  out = http.getString();
  http.end();
  return out.length() > 0;
}

static int utf8CharLen(uint8_t c) {
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xe0) == 0xc0) return 2;
  if ((c & 0xf0) == 0xe0) return 3;
  if ((c & 0xf8) == 0xf0) return 4;
  return 1;
}

static String fitHeadlineToWidth(const char *text, uint16_t maxPx) {
  String s(text);
  if (u8Fonts.getUTF8Width(s.c_str()) <= (int)maxPx) return s;
  String out;
  size_t i = 0;
  while (i < s.length()) {
    int cl = utf8CharLen(s[i]);
    if (cl <= 0) break;
    String trial = out + s.substring(i, i + (size_t)cl);
    if (u8Fonts.getUTF8Width(trial.c_str()) > (int)maxPx) break;
    out = trial;
    i += (size_t)cl;
  }
  if (out.length() == 0) return String("…");
  return out + "…";
}

// Google Noto Sans → U8g2 bitmap (src/noto*.c); TTF cannot run on ESP32 without conversion.
static const uint8_t *kFontTitle = u8g2_font_noto36_tf;
static const uint8_t *kFontBody = u8g2_font_noto28_tf;
static const uint8_t *kFontFooter = u8g2_font_noto22_tf;

static void printCenteredBaseline(int16_t baselineY, const char *text) {
  int w = u8Fonts.getUTF8Width(text);
  int x = (display.width() - w) / 2;
  if (x < 0) x = 0;
  u8Fonts.setCursor(x, baselineY);
  u8Fonts.print(text);
}

static void formatUpdateTime(char *buf, size_t len) {
  time_t t = time(nullptr);
  if (t < 1000000000) {
    snprintf(buf, len, "Oppdatert: tid ikke synkronisert (NTP)");
    return;
  }
  struct tm tm;
  localtime_r(&t, &tm);
  strftime(buf, len, "Oppdatert %d.%m.%Y kl. %H:%M:%S", &tm);
}

static void syncNetworkTime() {
  // Do NOT use configTime(0,0,...) after setenv(TZ): on ESP32 Arduino, configTime always
  // calls setTimeZone() at the end and overwrites TZ with UTC — localtime stays UTC.
  // configTzTime sets TZ once, then starts SNTP (same as configTime but keeps POSIX TZ).
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org", "time.nist.gov", nullptr);
  uint32_t start = millis();
  while (time(nullptr) < 1000000000 && millis() - start < 15000) {
    delay(200);
    yield();
  }
}

// statusLine: centered error/info (no footer). timeFooter: centered under news (nullptr = skip).
static void drawScreen(const char *statusLine, const char *timeFooter) {
  const int margin = 16;
  const uint16_t maxTextW = display.width() - 2 * margin;
  u8Fonts.setForegroundColor(GxEPD_BLACK);
  u8Fonts.setBackgroundColor(GxEPD_WHITE);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    if (statusLine) {
      u8Fonts.setFont(kFontBody);
      String msg = fitHeadlineToWidth(statusLine, maxTextW);
      int16_t baseline = (int16_t)(display.height() / 2 + u8Fonts.getFontAscent() / 2);
      printCenteredBaseline(baseline, msg.c_str());
    } else {
      u8Fonts.setFont(kFontTitle);
      const int marginTop = 16;
      int titleBaseline = marginTop + u8Fonts.getFontAscent();
      printCenteredBaseline(titleBaseline, "VG — Siste nytt");
      int yBelowTitle = titleBaseline - u8Fonts.getFontDescent() + 12;
      display.drawFastHLine(margin, yBelowTitle, display.width() - 2 * margin, GxEPD_BLACK);

      u8Fonts.setFont(kFontBody);
      int lineStep = u8Fonts.getFontAscent() - u8Fonts.getFontDescent() + 8;
      u8Fonts.setFont(kFontFooter);
      int footerH = u8Fonts.getFontAscent() - u8Fonts.getFontDescent() + 8;
      u8Fonts.setFont(kFontBody);
      int footerReserve = timeFooter ? (footerH + margin) : margin;

      int y = yBelowTitle + 2 + u8Fonts.getFontAscent() + 10;
      for (int n = 0; n < gTitleCount; n++) {
        if (y + lineStep > (int)display.height() - footerReserve) break;
        String line = String(n + 1) + ". " + gTitles[n];
        String shown = fitHeadlineToWidth(line.c_str(), maxTextW);
        printCenteredBaseline((int16_t)y, shown.c_str());
        y += lineStep;
      }
      if (timeFooter) {
        u8Fonts.setFont(kFontFooter);
        int footBaseline =
            (int)display.height() - margin + u8Fonts.getFontDescent();
        printCenteredBaseline((int16_t)footBaseline, timeFooter);
      }
    }
  } while (display.nextPage());
}

static bool connectWifi() {
  if (strlen(WIFI_SSID) == 0) {
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi connecting");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 60000) {
    delay(500);
    Serial.print(".");
    yield();
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

static void setupOta() {
  if (strlen(OTA_PASSWORD) == 0) {
    Serial.println("OTA: disabled (set OTA_PASSWORD in wifi_config.h)");
    return;
  }
  ArduinoOTA.setHostname("esp32-vg-news");
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("OTA: start"); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA: end"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total) {
      Serial.printf("OTA: %u%%\n", progress * 100 / total);
    }
  });
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("OTA: error %u\n", err);
  });
  ArduinoOTA.begin();
  Serial.println("OTA: ready (ArduinoOTA)");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("Reset reason: %d (1=POWERON, 4=PANIC, 5/6/7=WDG, 9=BROWNOUT)\n",
                (int)esp_reset_reason());

  if (strlen(WIFI_SSID) == 0) {
    Serial.println("WiFi: set WIFI_SSID in wifi_config.h");
    initEpd();
    drawScreen("WiFi: fyll inn SSID i wifi_config.h", nullptr);
    return;
  }

  if (!connectWifi()) {
    Serial.println("WiFi: failed");
    initEpd();
    drawScreen("WiFi feilet. Sjekk SSID (2.4 GHz) og passord.", nullptr);
    return;
  }

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  syncNetworkTime();
  if (time(nullptr) >= 1000000000) {
    Serial.println("Klokke synkronisert (NTP)");
  } else {
    Serial.println("Advarsel: NTP fikk ikke tid — tidsstempel kan mangle");
  }
  setupOta();

  Serial.println("Henter VG RSS (kun Serial til det er ferdig)...");
  String xml;
  if (!fetchRss(xml)) {
    Serial.println("RSS fetch failed");
    initEpd();
    drawScreen("Kunne ikke hente RSS (HTTPS/nettverk).", nullptr);
    return;
  }
  if (!extractRssTitles(xml)) {
    Serial.println("No items in RSS");
    initEpd();
    drawScreen("Fant ingen saker i RSS-feeden.", nullptr);
    return;
  }

  Serial.println("Viser pa epaper (ett fullt oppdatering)...");
  initEpd();
  delay(100);
  char timeBuf[64];
  formatUpdateTime(timeBuf, sizeof(timeBuf));
  drawScreen(nullptr, timeBuf);
  gNextRssFetchMs = millis() + kRefreshMs;
  gRssScheduleActive = true;
}

void loop() {
  ArduinoOTA.handle();
  if (WiFi.status() != WL_CONNECTED) {
    delay(200);
    return;
  }
  if (!gRssScheduleActive) {
    delay(50);
    return;
  }
  uint32_t now = millis();
  if ((int32_t)(now - gNextRssFetchMs) < 0) {
    delay(10);
    return;
  }
  gNextRssFetchMs = now + kRefreshMs;

  Serial.println("RSS periodisk oppdatering...");
  String xml;
  if (!fetchRss(xml) || !extractRssTitles(xml)) {
    drawScreen("Oppdatering feilet.", nullptr);
    return;
  }
  char timeBuf[64];
  formatUpdateTime(timeBuf, sizeof(timeBuf));
  drawScreen(nullptr, timeBuf);
}
