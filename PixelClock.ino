#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <NeoPixelAnimator.h>
#include <NeoPixelBus.h>
#include <ESP8266WiFi.h>
#include <TimeLib.h>
#include <NTPClient.h>
#include "RTClib.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "characters.h"

RTC_DS1307 rtc;

boolean NTPset = false;
uint32_t timeSetMillis = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

const char *ssid = "YOUR_SSID";
const char *pass = "YOUR_WIFI_PASSWORD";
unsigned int localPort = 2390;      // local port to listen for UDP packets

String HOST_NAME = "PixelClock";
const char* mqttServer = "MQTT_IP_ADDRESS";
const int mqttPort = 1883;
const char* mqttUser = "MQTT_USER";
const char* mqttPassword = "MQTT_PASS";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

int status = WL_IDLE_STATUS;

typedef ColumnMajorAlternating180Layout   MyPanelLayout;

const uint8_t PanelWidth = 32;
const uint8_t PanelHeight = 8;
const uint16_t PixelCount = PanelWidth * PanelHeight;
const uint8_t PixelPin = 3; //RX

NeoTopology<MyPanelLayout> topo(PanelWidth, PanelHeight);

NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod > strip(PixelCount, PixelPin);

int printPos = 0;

uint8_t cBrightness = 20;
uint8_t cHalfBright = cBrightness / 2;
uint8_t cQuarterBright = cBrightness / 4;
uint8_t c10thBright = cBrightness / 10;
uint8_t c20thBright = cBrightness / 20;

float fBrightness = cBrightness / 255.0;
float fHalfBrightness = cHalfBright / 255.0;
float fQuarterBrightness = cQuarterBright / 255.0;
float f10thBrightness = c10thBright / 255.0;
float f20thBrightness = c20thBright / 255.0;

RgbColor red(cBrightness, 0, 0);
RgbColor green(0, cBrightness, 0);
RgbColor blue(0, 0, cBrightness);
RgbColor white(cBrightness);
RgbColor halfWhite(cBrightness / 2);
RgbColor quarterWhite(cBrightness / 4);
RgbColor tenthWhite(cBrightness / 10);

RgbColor black(0);

boolean updateMeter = false;
const unsigned long refreshInterval = 30;
unsigned long lastRefresh = 0;

boolean printing = false;
unsigned long lastProgress = 0;
const unsigned long progressTimeout = 300000;

int meterProgress = 0;
boolean meterOnOff = false;
int meterBrightness = 3;
int meterHeat = 0;

boolean printerActive = false;
unsigned int printProgress = 0;
unsigned int toolTemp = 0;
unsigned int toolTarget = 0;
unsigned int bedTemp = 0;
unsigned int bedTarget = 0;
uint16_t printTimeLeft = 0;

void mqttReconnect() {
  mqttClient.connect(HOST_NAME.c_str(), mqttUser, mqttPassword );
  while (!mqttClient.connected()) {
    delay(5000);
    break;
  }
  mqttClient.subscribe("Ender3_8266/tool/target");
  mqttClient.subscribe("Ender3_8266/tool/actual");
  mqttClient.subscribe("Ender3_8266/bed/target");
  mqttClient.subscribe("Ender3_8266/bed/actual");
  mqttClient.subscribe("Ender3_8266/progress");
  mqttClient.subscribe("Ender3_8266/state");
  mqttClient.subscribe("Ender3_8266/is_active");
  mqttClient.subscribe("Ender3_8266/timeLeft");
  
  mqttClient.subscribe("PixelClock/updateNTP");
}

void mqttCallback(char* topic, byte * payload, unsigned int length) {
  char payloadString[length];
  for (int i = 0; i < length; i++) {
    payloadString[i] = (char)payload[i];
  }
  if (strcmp(topic, "Ender3_8266/progress") == 0) {
    printProgress = atoi(payloadString);
    printing = true;
    lastProgress = millis();
    updateMeter = true;
  } else if (strcmp(topic, "Ender3_8266/tool/actual") == 0) {
    toolTemp = atoi(payloadString);
    updateMeter = true;
  } else if (strcmp(topic, "Ender3_8266/tool/target") == 0) {
    toolTarget = atoi(payloadString);
  } else if (strcmp(topic, "Ender3_8266/bed/target") == 0) {
    bedTarget = atoi(payloadString);
  } else if (strcmp(topic, "Ender3_8266/bed/actual") == 0) {
    bedTemp = atoi(payloadString);
  } else if (strcmp(topic, "Ender3_8266/is_active") == 0) {
    printerActive = atoi(payloadString);
  } else if (strcmp(topic, "Ender3_8266/timeLeft") == 0) {
    printTimeLeft = atoi(payloadString);
  } else if (strcmp(topic, "PixelClock/updateNTP") == 0) {
    getNTPtime();
  }
}

void printWiFiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

void printNumberAt(uint8_t pnDigit, int8_t pnX, int8_t pnY, RgbColor pnColor, RgbColor bgColor) {
  uint8_t pnPos = 0;
  for (int8_t thisY = pnY; thisY < pnY + 6; thisY++) {
    for (int8_t thisX = pnX; thisX < pnX + 4; thisX++) {
      if (numbers46[pnDigit][pnPos] == 1) {
        strip.SetPixelColor(topo.Map(thisX, thisY), pnColor);
      } else {
        strip.SetPixelColor(topo.Map(thisX, thisY), bgColor);
      }
      pnPos++;
    }
  }
}

void print35NumberAt(uint8_t pnDigit, int8_t pnX, int8_t pnY, RgbColor pnColor, RgbColor bgColor) {
  uint8_t pnPos = 0;
  for (int8_t thisY = pnY; thisY < pnY + 5; thisY++) {
    for (int8_t thisX = pnX; thisX < pnX + 3; thisX++) {
      if (numbers35[pnDigit][pnPos] == 1) {
        strip.SetPixelColor(topo.Map(thisX, thisY), pnColor);
      } else {
        strip.SetPixelColor(topo.Map(thisX, thisY), bgColor);
      }
      pnPos++;
    }
  }
}

void print46TimeAt(int8_t ptX, int8_t ptY, RgbColor ptColor, RgbColor bgColor) {
  DateTime now = rtc.now();

  uint8_t thisHour = now.hour();
  uint8_t thisMinutes = now.minute();
  printNumberAt(thisHour / 10, ptX, ptY, ptColor, bgColor);
  printNumberAt(thisHour % 10, ptX + 5, ptY, ptColor, bgColor);
  printNumberAt(thisMinutes / 10, ptX + 10, ptY, ptColor, bgColor);
  printNumberAt(thisMinutes % 10, ptX + 15, ptY, ptColor, bgColor);
  if (now.second() % 2 == 0) {
    strip.SetPixelColor(topo.Map(ptX + 9, ptY + 1), blue);
    strip.SetPixelColor(topo.Map(ptX + 9, ptY + 4), blue);
  } else {
    strip.SetPixelColor(topo.Map(ptX + 9, ptY + 1), green);
    strip.SetPixelColor(topo.Map(ptX + 9, ptY + 4), green);
  }
}

void print35TimeAt(int8_t ptX, int8_t ptY, RgbColor ptColor, RgbColor bgColor) {
  DateTime now = rtc.now();

  print35NumberAt(now.hour() / 10, ptX, ptY, ptColor, bgColor);
  print35NumberAt(now.hour() % 10, ptX + 4, ptY, ptColor, bgColor);
  print35NumberAt(now.minute() / 10, ptX + 8, ptY, ptColor, bgColor);
  print35NumberAt(now.minute() % 10, ptX + 12, ptY, ptColor, bgColor);
  if (now.second() % 2 == 0) {
    strip.SetPixelColor(topo.Map(ptX + 7, ptY + 1), blue);
    strip.SetPixelColor(topo.Map(ptX + 7, ptY + 3), blue);
  } else {
    strip.SetPixelColor(topo.Map(ptX + 7, ptY + 1), green);
    strip.SetPixelColor(topo.Map(ptX + 7, ptY + 3), green);
  }
}

void print35DateAt(int8_t ptX, int8_t ptY, RgbColor ptColor, RgbColor bgColor) {
  DateTime now = rtc.now();

  print35NumberAt(now.month() / 10, ptX, ptY, ptColor, bgColor);
  print35NumberAt(now.month() % 10, ptX + 4, ptY, ptColor, bgColor);
  print35NumberAt(now.day() / 10, ptX + 8, ptY, ptColor, bgColor);
  print35NumberAt(now.day() % 10, ptX + 12, ptY, ptColor, bgColor);
  strip.SetPixelColor(topo.Map(ptX + 7, ptY + 1), blue);
  strip.SetPixelColor(topo.Map(ptX + 7, ptY + 2), blue);
  strip.SetPixelColor(topo.Map(ptX + 7, ptY + 3), blue);
  for (uint8_t i = 0; i < 7; i++) {
    if (i == now.dayOfTheWeek()) {
      strip.SetPixelColor(topo.Map(ptX + i * 2 + 1, ptY + 5), blue);
    } else {
      strip.SetPixelColor(topo.Map(ptX + i * 2 + 1, ptY + 5), RgbColor(3, 0, 0));
    }
  }
}

void secondsTicker(uint8_t tX, uint8_t tY, uint8_t tW, uint8_t tH, int8_t tOffset, RgbColor tickColor) {
  DateTime now = rtc.now();
  uint16_t tickPos = ((now.second() + 60 + tOffset) % 60);// * (tW + tH + tW + tH) / 60;

  tW--;
  tH--;
  //  uint32_t tickTime = (millis() - timeSetMillis) % 60000;
  //  uint32_t tickPos = (tickTime + tOffset * 1000) * (tW + tH + tW + tH) / 60000;
  if (tickPos < 23) {
    strip.SetPixelColor(topo.Map(tX + tickPos, tY), tickColor);
  } else  if (tickPos < 30) {
    strip.SetPixelColor(topo.Map(tX + tW, tY + (tickPos - tW)), tickColor);
  } else  if (tickPos < 53) {
    strip.SetPixelColor(topo.Map(tX + 2 * tW + tH - tickPos, tY + tH), tickColor);
  } else {
    strip.SetPixelColor(topo.Map(tX, 2 * tW + 2 * tH - tickPos), tickColor);
  }
}

void secTicker(uint8_t tX, uint8_t tY, uint8_t tW, uint8_t tH, int8_t tOffset, RgbColor tickColor) {
  DateTime now = rtc.now();
  uint16_t tickPos = ((now.second() + 60 + tOffset) % 60) * (tW + tH + tW + tH) / 60;

  tW--;
  tH--;
  if (tickPos < tW) {
    strip.SetPixelColor(topo.Map(tX + tickPos, tY), tickColor);
  } else  if (tickPos < tW + tH) {
    strip.SetPixelColor(topo.Map(tX + tW, tY + (tickPos - tW)), tickColor);
  } else  if (tickPos < tW + tH + tW) {
    strip.SetPixelColor(topo.Map(tX + 2 * tW + tH - tickPos, tY + tH), tickColor);
  } else {
    strip.SetPixelColor(topo.Map(tX, 2 * tW + 2 * tH - tickPos), tickColor);
  }
}

void setBrightness(uint8_t newBrightness) {
  cBrightness = newBrightness;
  cHalfBright = newBrightness / 2;
  cQuarterBright = newBrightness / 4;
  c10thBright = newBrightness / 10;
  c20thBright = newBrightness / 20;

  fBrightness = cBrightness / 255.0;
  fHalfBrightness = cHalfBright / 255.0;
  fQuarterBrightness = cQuarterBright / 255.0;
  f10thBrightness = c10thBright / 255.0;
  f20thBrightness = c20thBright / 255.0;
}

void WiFiReconnect() {
  uint8_t WiFiRetry = 0;
  WiFi.mode(WIFI_STA);
  while ( (WiFi.waitForConnectResult() != WL_CONNECTED) && (WiFiRetry < 5)) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    WiFi.begin(ssid, pass);
    WiFiRetry++;
    // wait 10 seconds for connection:
    delay(10000);
    //        break;
  }
  printWiFiStatus();
}

void getNTPtime() {
  timeClient.update();
  Serial.print("Setting RTC");
  if (! rtc.isrunning()) {
    rtc.adjust(timeClient.getEpochTime());
    timeSetMillis = millis();
    Serial.print("RTC Set: "); Serial.print(timeClient.getEpochTime()); Serial.print(" "); Serial.println(timeClient.getFormattedTime());
    mqttClient.publish("PixelClock/updateNTP/result", timeClient.getFormattedTime().c_str());
  } else {
    Serial.print("RTC not ready");
    mqttClient.publish("PixelClock/updateNTP/result", "failed");
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial); // wait for serial attach
  delay(2000);
  Serial.println();
  Serial.println("Initializing...");

  WiFiReconnect();

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttReconnect();


  timeClient.begin();
  timeClient.setTimeOffset(-4 * 60 * 60);
  ///////////////////////////////////////////////
  //  OTA STUFF
  ///////////////////////////////////////////////

  ArduinoOTA.setHostname("PixelClock");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

  });
  ArduinoOTA.onEnd([]() {  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {  });
  ArduinoOTA.onError([](ota_error_t error) {
    //Serial.printf("Error[%u]: ", error);
    //    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    //    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    //    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    //    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    //    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  //OTA

rtc.begin();
  delay(500);
  //  getNTPtime(); //sets rtc

  strip.Begin();
  strip.Show();

  Serial.println();
  Serial.println("Running...");

  strip.Show();

  printPos = 0;

  strip.ClearTo(black);  strip.Show();

}

void loop() {
  if (millis() - lastRefresh > refreshInterval) {

    if (printerActive || toolTarget > 0 || bedTarget > 0) {
      strip.ClearTo(black);

      print35TimeAt(0, 0, red, black);

      for (uint8_t i = 0; i < 32; i++) {
        if (i < (printProgress * 32 / 100)) {
          strip.SetPixelColor(topo.Map(i, 7), RgbColor(0, 10, 0));
        } else if (i == (printProgress * 32 / 100)) {
          strip.SetPixelColor(topo.Map(i, 7), RgbColor::LinearBlend(RgbColor(10, 0, 0), RgbColor(0, 10, 0), (printProgress * 32) % 100 / 100.0)); //RgbColor(5, 5, 0));
        } else {
          strip.SetPixelColor(topo.Map(i, 7), RgbColor(1, 0, 0));
        }
      }
      for (uint8_t i = 0; i < 32; i++) {
        if (toolTarget) {
          if (i < (32 * toolTemp / toolTarget)) {
            strip.SetPixelColor(topo.Map(i, 5), RgbColor(9, 3, 0));
          } else if (i == (32 * toolTemp / toolTarget)) {
            strip.SetPixelColor(topo.Map(i, 5), RgbColor::LinearBlend(RgbColor(7, 3, 0), RgbColor(0, 0, 5), (float)toolTemp / (float)toolTarget)); //RgbColor(5, 5, 0));
          } else {
            strip.SetPixelColor(topo.Map(i, 5), RgbColor(0, 0, 1));
          }
        } else {
          strip.SetPixelColor(topo.Map(i, 5), RgbColor(0, 0, 1));
        }
      }
      for (uint8_t i = 0; i < 32; i++) {
        if (bedTarget) {
          if (i < (32 * bedTemp / bedTarget)) {
            strip.SetPixelColor(topo.Map(i, 6), RgbColor(10, 0, 0));
          } else if (i == (32 * bedTemp / bedTarget)) {
            strip.SetPixelColor(topo.Map(i, 6), RgbColor::LinearBlend(RgbColor(5, 0, 0), RgbColor(0, 0, 5), (float)bedTemp / (float)bedTarget)); //RgbColor(5, 5, 0));
          } else {
            strip.SetPixelColor(topo.Map(i, 6), RgbColor(0, 0, 1));
          }
        } else {
          strip.SetPixelColor(topo.Map(i, 6), RgbColor(0, 0, 1));
        }
      }
      if (printTimeLeft > 36000) {
        uint8_t thisMin = (printTimeLeft / 60) % 60;
        uint8_t thisHour = (printTimeLeft/3600);
        print35NumberAt(thisHour/10,17,0,blue,black);
        print35NumberAt(thisHour%10,21,0,blue,black);
        if (printTimeLeft > 0) strip.SetPixelColor(topo.Map(24, 1), red);
        if (printTimeLeft > 0) strip.SetPixelColor(topo.Map(24, 3), red);
        print35NumberAt(thisMin/10,25,0,blue,black);
        print35NumberAt(thisMin%10,29,0,blue,black);
      } else if (printTimeLeft > 3600) {
        uint8_t thisSec = printTimeLeft % 60;
        uint8_t thisMin = (printTimeLeft / 60) % 60;
        uint8_t thisHour = (printTimeLeft/3600);
        print35NumberAt(thisHour%10,17,0,blue,black);
        if (printTimeLeft > 0) strip.SetPixelColor(topo.Map(20, 1), red);
        if (printTimeLeft > 0) strip.SetPixelColor(topo.Map(20, 3), red);
        print35NumberAt(thisMin/10,21,0,blue,black);
        print35NumberAt(thisMin%10,25,0,blue,black);
        if (printTimeLeft > 0) strip.SetPixelColor(topo.Map(28, 4), red);
        print35NumberAt(thisSec/6,29,0,blue,black);
      } else {
        if (printTimeLeft > 600) print35NumberAt(((printTimeLeft / 60 ) % 100 ) / 10, 21, 0, blue, black);
        if (printTimeLeft > 0) print35NumberAt(printTimeLeft / 60 % 10, 25, 0, blue, black);
        if (printTimeLeft > 0) strip.SetPixelColor(topo.Map(28, 4), red);
        if (printTimeLeft > 0) print35NumberAt(printTimeLeft / 10 % 10, 29, 0, blue, black);
      }
    } else {
      DateTime now = rtc.now();
      strip.ClearTo(black);

      if (now.second() % 20 < 5) {

        print35TimeAt(0, 0, red, black);
        print35DateAt(17, 0, green, black);
      } else {
        print46TimeAt(6, 1, red, black);
        secondsTicker(4, 0, 24, 8, 12, white);
        secondsTicker(4, 0, 24, 8, 11, quarterWhite);
        secondsTicker(4, 0, 24, 8, 10, tenthWhite);
      }
    }

    strip.Show();
  }
  ArduinoOTA.handle();
  mqttClient.loop();
}
