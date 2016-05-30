#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <Artnetnode.h>
#include <Ticker.h>
#include "FS.h"
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>
#endif
#include "config.h"
#include "dimmer_curve.h"


// Globals
bool ledsEnabled = true;
#define DMX_MAX 512 // max. number of DMX data packages.
uint8_t DMXBuffer[DMX_MAX];
char udpBeatPacketStart[185];
char udpBeatPacket[185];
uint8_t mac[6];
bool shuttingdown = false;
int lowestBattery = 0;

// Lib
Ticker ticker;
Ticker clearBlinkTick;
WiFiUDP UdpSend;
Artnetnode artnetnode;
IPAddress localIP;
// Status neopixel
Adafruit_NeoPixel statusNeo = Adafruit_NeoPixel();
// Strip neopixel output
Adafruit_NeoPixel strip = Adafruit_NeoPixel();
ESP8266WebServer server(80);



void setup()
{
  // Initial setup
  Serial.begin(9600);
  WiFi.macAddress(mac);
  char nodeName2[15];
  sprintf(nodeName2, "LUMOS-%02X%02X%02X", mac[3], mac[4], mac[5]);
  nodeName = (String)nodeName2;
  getConfigJSON();

  // Pin setup
  pinMode(pinR, OUTPUT);
  pinMode(pinG, OUTPUT);
  pinMode(pinB, OUTPUT);

  pinMode(btnPin, INPUT);

  analogWrite(pinR, 0);
  analogWrite(pinG, 0);
  analogWrite(pinB, 0);

  lowestBattery = analogRead(A0);

  // Status neopixel setup
  statusNeo.updateLength(1);
  statusNeo.updateType(NEO_GRB + NEO_KHZ800);
  statusNeo.setPin(onboardNeopixelPin);
  statusNeo.begin();
  statusNeo.show(); // Initialize all pixels to 'off'

  // String neopixel setup
  if (!ledOutputMode) { // Don't use the memory if we aren't in that mode
    strip.updateLength(stripLength);
    strip.updateType(NEO_GRB + NEO_KHZ800);
    strip.setPin(stripPin);
    strip.begin();
    strip.show(); // Initialize all pixels to 'off'
  }

  // Force wifi connection portal or attempt to connect
  if (!digitalRead(btnPin)) {
    // Force AP mode with captive portal for wifi setup
    statusNeo.setPixelColor(0, statusNeo.Color(100, 0, 100));
    statusNeo.show();
    WiFiManager wifiManager;
    wifiManager.startConfigPortal(nodeName.c_str());
    // Reset to use new settings
    ESP.reset();
    delay(1000);
  }
  else {
    // Try to connect with existing wifi settings
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 100));
    statusNeo.show();
    WiFiManager wifiManager;
    // Add callback to change status neopixel to orange if connection fails and we enter AP mode
    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setConfigPortalTimeout(180);
    // Attempt connection
    if (!wifiManager.autoConnect(nodeName.c_str())) {
      Serial.println("failed to connect and hit timeout");
      // Reset and try again
      statusNeo.setPixelColor(0, statusNeo.Color(100, 0, 0));
      statusNeo.show();
      delay(1000);
      statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
      statusNeo.show();
      ESP.reset();
      delay(1000);
    }
  }


  // Start up artnetnode library
  artnetnode.setName((char*)nodeName.c_str());
  artnetnode.setStartingUniverse(1);
  while (artnetnode.begin(1) == false) {
    Serial.print("X");
  }
  Serial.println();
  Serial.println("Connected");
  artnetnode.setDMXOutput(0, 1, 0);

  // Connected and happy, flash green
  statusNeo.setPixelColor(0, statusNeo.Color(0, 100, 0));
  statusNeo.show();
  delay(1000);
  statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
  statusNeo.show();

  // Decrease range - FIXME
  analogWriteRange(255);

  // Setup heartbeat packet
  UdpSend.begin(4000);
  localIP = WiFi.localIP();
  sprintf(udpBeatPacketStart, "{\"name\":\"%s\",\"hw_version\":\"%s\",\"sw_version\":\"%s\",\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"ip\":\"%d.%d.%d.%d\",\"current_voltage\":%%d,\"lowest_voltage\":%%d,\"output_enabled\":%%s}",
          nodeName.c_str(), hwVersion.c_str(), swVersion.c_str(), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], localIP[0],  localIP[1],  localIP[2],  localIP[3]);
  ticker.attach(5, beat);
  beat();

  // Setup webserver
  server.on("/", []() {
    if (!server.authenticate(www_username.c_str(), www_password.c_str())) {
      return server.requestAuthentication();
    }
    server.send(200, "text/plain", "Login OK");
  });
  server.on("/reset", []() {
    if (!server.authenticate(www_username.c_str(), www_password.c_str())) {
      return server.requestAuthentication();
    }
    server.send(200, "text/html", "Resetting now... <a href=\"/\">Return</a>");
    ESP.reset();
  });
  server.on("/findme", []() {
    if (!server.authenticate(www_username.c_str(), www_password.c_str())) {
      return server.requestAuthentication();
    }
    server.sendHeader("Location", String("/"), true);
    server.send ( 302, "text/plain", "");
    analogWrite(pinR, 0);
    analogWrite(pinG, 0);
    analogWrite(pinB, 0);
    statusNeo.setPixelColor(0, statusNeo.Color(255, 255, 255));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(255, 255, 255));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(255, 255, 255));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(255, 255, 255));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(255, 255, 255));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(255, 255, 255));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(255, 255, 255));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(255, 255, 255));
    statusNeo.show();
    delay(500);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(500);
  });
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", nodeName + " - " + ((Update.hasError()) ? "FAIL\n" : "OK\n"));
    ESP.restart();
  }, []() {
    // Check password
    if (!server.authenticate(www_username.c_str(), www_password.c_str())) {
      return server.requestAuthentication();
    }

    // Stop beating
    ticker.detach();

    // Turn us green/blue
    statusNeo.setPixelColor(0, statusNeo.Color(100, 0, 100));
    statusNeo.show();

    // Turn off led output
    analogWrite(pinR, 0);
    analogWrite(pinG, 0);
    analogWrite(pinB, 0);
    ledsEnabled = false;

    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.setDebugOutput(true);
      WiFiUDP::stopAll();
      Serial.print("Update: ");
      Serial.println(upload.filename);
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      Serial.setDebugOutput(false);
    }
    yield();
  });
  server.begin();
}

void loop()
{
  // Handle webserver
  server.handleClient();

  // Handle artnet
  uint16_t code = artnetnode.read();
  if (code) {
    if ((code == OpDmx) && (ledsEnabled)) {
      analogWrite(pinR, ledCurve[artnetnode.returnDMXValue(0, 1)]);
      analogWrite(pinG, ledCurve[artnetnode.returnDMXValue(0, 2)]);
      analogWrite(pinB, ledCurve[artnetnode.returnDMXValue(0, 3)]);
      batteryLog();
    }
    else if (code == OpPoll) {
      Serial.println("Art Poll Packet");
    }
  }
  if (shuttingdown) {
    Serial.print("Danger voltage, deep sleeping forever");
    statusNeo.setPixelColor(0, statusNeo.Color(50, 0, 0));
    statusNeo.show();
    delay(200);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(200);
    statusNeo.setPixelColor(0, statusNeo.Color(50, 0, 0));
    statusNeo.show();
    delay(200);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(200);
    statusNeo.setPixelColor(0, statusNeo.Color(50, 0, 0));
    statusNeo.show();
    delay(200);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(200);
    statusNeo.setPixelColor(0, statusNeo.Color(50, 0, 0));
    statusNeo.show();
    delay(200);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(200);
    Serial.println(".");
    ESP.deepSleep(0);
    while (true) {}
  }
  else if ((WiFi.status() != WL_CONNECTED) && (!shuttingdown)) {
    // Lost wifi connection, reset to reconnect
    statusNeo.setPixelColor(0, statusNeo.Color(100, 0, 0));
    statusNeo.show();
    Serial.print("Connection Lost, restarting");
    delay(2000);
    ESP.reset();
    delay(1000);
  }
}

void beat() {
  // Check battery voltage level for turn off
  batteryLog();
  int adcRead = analogRead(A0);

  if (!digitalRead(btnPin)) {
    // Lets toggle the enabled state
    ledsEnabled = !ledsEnabled;
  }

  // Act on voltage reads
  if ((adcRead <= minLEDVoltage) && (ledsEnabled)) {
    ledsEnabled = false;
    analogWrite(pinR, 0);
    analogWrite(pinG, 0);
    analogWrite(pinB, 0);
  }
  else if (adcRead <= minSelfVoltage) {
    // TODO - Put self into sleep mode and send alert packets
    shuttingdown = true;
  }

  // Send heartbeat packet
  UdpSend.beginPacket({192, 168, 0, 100}, 33333);
  sprintf(udpBeatPacket, udpBeatPacketStart, adcRead, lowestBattery, ledsEnabled ? "true" : "false");
  UdpSend.write(udpBeatPacket, sizeof(udpBeatPacket) - 1);
  UdpSend.endPacket();

  // Turn on status led for blink
  if (ledsEnabled) {
    statusNeo.setPixelColor(0, statusNeo.Color(0, 50, 0));
    statusNeo.show();
  }
  else {
    statusNeo.setPixelColor(0, statusNeo.Color(50, 0, 0));
    statusNeo.show();
  }
  clearBlinkTick.attach(0.1, clearBlink);
}

void clearBlink() {
  statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
  statusNeo.show();
  clearBlinkTick.detach();
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  // Change status led
  statusNeo.setPixelColor(0, statusNeo.Color(100, 100, 0));
  statusNeo.show();
}

void batteryLog() {
  int check = analogRead(A0);
  if (check < lowestBattery) {
    lowestBattery = check;
  }
}

bool getConfigJSON() {
  // Check if config file exists
  SPIFFS.begin();
  File configJSONfile = SPIFFS.open("/config.json", "r");
  if (!configJSONfile) {
    // Doesn't exist, create it
    Serial.println("json not found");
    defaultConfigJSON();
    configJSONfile = SPIFFS.open("/config.json", "r");
  }
  if (configJSONfile) {
    Serial.println("found the file");
    String jsonText = configJSONfile.readString();
    // Now we have either a new config or existing, load it into json parser
    DynamicJsonBuffer configJSON;
    JsonObject& configJSONroot = configJSON.parseObject(jsonText);

    Serial.println(jsonText);

    const char* swVersionConst = configJSONroot["swVersion"];
    String swVersionConstStr = (String)swVersionConst;

    //    Serial.println(swVersion);
    //    Serial.println(sizeof(swVersion));
    //    Serial.println(swVersionJSON);
    //    Serial.println(sizeof(swVersionJSON));

    if (!swVersionConstStr.equals(swVersion)) {
      Serial.println("check failed");
      // Out of date config file, update it!
      // Close the file and remove
      configJSONfile.close();
      SPIFFS.remove("/config.json");
      // Create new default
      defaultConfigJSON();

    }

    const char* nodeNameConst = configJSONroot["nodeName"];
    nodeName = (String)nodeNameConst;
    const char* hwVersionConst = configJSONroot["hwVersion"];
    hwVersion = (String)hwVersionConst;
    //const char* swVersionConst = configJSONroot["swVersion"];
    swVersion = (String)swVersionConst;
    pinR = configJSONroot["pinR"];
    pinG = configJSONroot["pinG"];
    pinB = configJSONroot["pinB"];
    onboardNeopixelPin = configJSONroot["onboardNeopixelPin"];
    btnPin = configJSONroot["btnPin"];
    minLEDVoltage = configJSONroot["minLEDVoltage"];
    minSelfVoltage = configJSONroot["minSelfVoltage"];
    const char* www_usernameConst = configJSONroot["www_username"];
    www_username = (String)www_usernameConst;
    const char* www_passwordConst = configJSONroot["www_password"];
    www_password = (String)www_passwordConst;
    ledOutputMode = configJSONroot["ledOutputMode"];
    stripLength = configJSONroot["stripLength"];
    stripPin = configJSONroot["stripPin"];

    return configJSONroot.success();
  }
  else {
    Serial.println("didn't find the file");
    return false;
  }
}

void defaultConfigJSON() {
  // Take the default and save to file
  Serial.println("making new config json");
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  root["nodeName"] = (const char*)nodeName.c_str();
  root["hwVersion"] = (const char*)hwVersion.c_str();
  root["swVersion"] = (const char*)swVersion.c_str();
  root["pinR"] = pinR;
  root["pinG"] = pinG;
  root["pinB"] = pinB;
  root["onboardNeopixelPin"] = onboardNeopixelPin;
  root["btnPin"] = btnPin;
  root["minLEDVoltage"] = minLEDVoltage;
  root["minSelfVoltage"] = minSelfVoltage;
  root["www_username"] = (const char*)www_username.c_str();
  root["www_password"] = (const char*)www_password.c_str();
  root["ledOutputMode"] = ledOutputMode;
  root["stripLength"] = stripLength;
  root["stripPin"] = stripPin;

  File configJSONfile = SPIFFS.open("/config.json", "w+");
  root.printTo(configJSONfile);
  configJSONfile.close();
}

