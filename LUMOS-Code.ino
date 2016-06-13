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
#include "html_progmem.h"


// Globals
bool ledsEnabled = true;
#define DMX_MAX 512 // max. number of DMX data packages.
uint8_t DMXBuffer[DMX_MAX];
char udpBeatPacketStart[200];
char udpBeatPacket[200];
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
    strip.clear();
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
      flashStatus(100, 0, 0, 1, 1000);
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
  artnetnode.setDMXOutput(0, 1, universe);
  artnetnode.allowBroadcastDMX(allowBroadcastDMX);

  // Connected and happy, turn green
  statusNeo.setPixelColor(0, statusNeo.Color(0, 100, 0));

  // Set write range based on setting
  if ((ledChannelMode == 0) || (ledChannelMode == 1)) {
    analogWriteRange(255);
  }
  else {
    analogWriteRange(1023);
  }

  // Try to find the control server through dns if option selected
  if (tryServerDNS) {
    IPAddress ipa;
    if (WiFi.hostByName(serverName.c_str(), ipa)) {
      Serial.println("Server found");
      serverIP = ipa;
    }
    else {
      Serial.println("Server not found");
    }
  }

  // Finished dns lookups, turn off status
  statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));

  // Setup heartbeat packet
  UdpSend.begin(4000);
  makeUDPStartPacket();
  ticker.attach(5, beat);
  beat();

  // Setup webserver
  server.on("/", []() {
    if (!server.authenticate(www_username.c_str(), www_password.c_str())) {
      return server.requestAuthentication();
    }

    String page = FPSTR(HTTP_HEAD);
    page.replace("{v}", "Lumos");
    page += FPSTR(HTTP_STYLE);
    page += FPSTR(HTTP_HEAD_END);
    page += "<div class=\"c\">\"{n}\"</div>";
    page.replace("{n}", nodeName);
    page += "<div class=\"c\">LUMOS Config and Control</div>";
    page += FPSTR(HTTP_LINK);
    page.replace("{a}", "/");
    page.replace("{n}", "Home");
    page += FPSTR(HTTP_LINK);
    page.replace("{a}", "/settings");
    page.replace("{n}", "Settings");
    page += FPSTR(HTTP_LINK);
    page.replace("{a}", "/findme");
    page.replace("{n}", "Find Me");
    page += FPSTR(HTTP_LINK);
    page.replace("{a}", "/reset");
    page.replace("{n}", "Reset");
    page += FPSTR(HTTP_END);
    
    server.send(200, "text/html", page);
  });
  server.on("/settings", []() {
    if (!server.authenticate(www_username.c_str(), www_password.c_str())) {
      return server.requestAuthentication();
    }

    String page = FPSTR(HTTP_HEAD);
    page.replace("{v}", "Lumos - Settings");
    page += FPSTR(HTTP_STYLE);
    page += FPSTR(HTTP_HEAD_END);
    page += "<div class=\"c\">\"{n}\"</div>";
    page.replace("{n}", nodeName);
    page += FPSTR(HTTP_LINK);
    page.replace("{a}", "/");
    page.replace("{n}", "<< Back");

    page += "<br/><div class=\"c\">Node Settings</div><br/>";
    page += FPSTR(HTTP_FORM_START_GENERIC);
    page.replace("{t}", "GET");
    page.replace("{a}", "/settingsSave");

    page += "Node Name:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "nodeName");
    page.replace("{n}", "nodeName");
    page.replace("{p}", nodeName);
    page.replace("{v}", nodeName);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Hardware Version:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "hwVersion");
    page.replace("{n}", "hwVersion");
    page.replace("{p}", hwVersion);
    page.replace("{v}", hwVersion);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Software Version:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "swVersion");
    page.replace("{n}", "swVersion");
    page.replace("{p}", swVersion);
    page.replace("{v}", swVersion);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>R PWM Pin:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "pinR");
    page.replace("{n}", "pinR");
    page.replace("{p}", (String)pinR);
    page.replace("{v}", (String)pinR);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>G PWM Pin:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "pinG");
    page.replace("{n}", "pinG");
    page.replace("{p}", (String)pinG);
    page.replace("{v}", (String)pinG);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>B PWM Pin:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "pinB");
    page.replace("{n}", "pinB");
    page.replace("{p}", (String)pinB);
    page.replace("{v}", (String)pinB);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Neopixel Pin:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "onboardNeopixelPin");
    page.replace("{n}", "onboardNeopixelPin");
    page.replace("{p}", (String)onboardNeopixelPin);
    page.replace("{v}", (String)onboardNeopixelPin);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Button Pin:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "btnPin");
    page.replace("{n}", "btnPin");
    page.replace("{p}", (String)btnPin);
    page.replace("{v}", (String)btnPin);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Max Voltage:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "maxVoltage");
    page.replace("{n}", "maxVoltage");
    page.replace("{p}", (String)maxVoltage);
    page.replace("{v}", (String)maxVoltage);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Min LED Voltage:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "minLEDVoltage");
    page.replace("{n}", "minLEDVoltage");
    page.replace("{p}", (String)minLEDVoltage);
    page.replace("{v}", (String)minLEDVoltage);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Min Self Voltage:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "minSelfVoltage");
    page.replace("{n}", "minSelfVoltage");
    page.replace("{p}", (String)minSelfVoltage);
    page.replace("{v}", (String)minSelfVoltage);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Web Username:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "www_username");
    page.replace("{n}", "www_username");
    page.replace("{p}", www_username);
    page.replace("{v}", www_username);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Web Password:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "www_password");
    page.replace("{n}", "www_password");
    page.replace("{p}", www_password);
    page.replace("{v}", www_password);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>LED Output Mode:";
    page += FPSTR(HTTP_SELECT);
    page.replace("{n}", "ledOutputMode");
    page += FPSTR(HTTP_OPTION);
    page.replace("{v}", "true");
    page.replace("{n}", "12W LEDs");
    if (ledOutputMode) {
      page.replace("{s}", "selected");
    }
    else {
      page.replace("{s}", "");
    }
    page += FPSTR(HTTP_OPTION);
    page.replace("{v}", "false");
    page.replace("{n}", "Neopixel Strip");
    if (!ledOutputMode) {
      page.replace("{s}", "selected");
    }
    else {
      page.replace("{s}", "");
    }
    page += FPSTR(HTTP_SELECT_END);

    page += "<br/><br/>Strip Length:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "stripLength");
    page.replace("{n}", "stripLength");
    page.replace("{p}", (String)stripLength);
    page.replace("{v}", (String)stripLength);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Strip Pin:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "stripPin");
    page.replace("{n}", "stripPin");
    page.replace("{p}", (String)stripPin);
    page.replace("{v}", (String)stripPin);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Control Server IP:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "serverIP");
    page.replace("{n}", "serverIP");
    String ipPrint = (String)serverIP[0] + "." + (String)serverIP[1] + "." + (String)serverIP[2] + "." + (String)serverIP[3];
    page.replace("{p}", ipPrint);
    page.replace("{v}", ipPrint);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Control Server DNS Address:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "serverName");
    page.replace("{n}", "serverName");
    page.replace("{p}", serverName);
    page.replace("{v}", serverName);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Control Server Locate:";
    page += FPSTR(HTTP_SELECT);
    page.replace("{n}", "tryServerDNS");
    page += FPSTR(HTTP_OPTION);
    page.replace("{v}", "true");
    page.replace("{n}", "Try DNS address, fallback to IP");
    if (ledOutputMode) {
      page.replace("{s}", "selected");
    }
    else {
      page.replace("{s}", "");
    }
    page += FPSTR(HTTP_OPTION);
    page.replace("{v}", "false");
    page.replace("{n}", "IP address only");
    if (!ledOutputMode) {
      page.replace("{s}", "selected");
    }
    else {
      page.replace("{s}", "");
    }
    page += FPSTR(HTTP_SELECT_END);

    page += "<br/><br/>LED Channel Mode:";
    page += FPSTR(HTTP_SELECT);
    page.replace("{n}", "ledChannelMode");
    page += FPSTR(HTTP_OPTION);
    page.replace("{v}", "0");
    page.replace("{n}", "3 channel (rgb)");
    if (ledChannelMode == 0) {
      page.replace("{s}", "selected");
    }
    else {
      page.replace("{s}", "");
    }
    page += FPSTR(HTTP_OPTION);
    page.replace("{v}", "1");
    page.replace("{n}", "4 channel (rgb dim)");
    if (ledChannelMode == 1) {
      page.replace("{s}", "selected");
    }
    else {
      page.replace("{s}", "");
    }
    page += FPSTR(HTTP_OPTION);
    page.replace("{v}", "2");
    page.replace("{n}", "6 channel (rrggbb)");
    if (ledChannelMode == 2) {
      page.replace("{s}", "selected");
    }
    else {
      page.replace("{s}", "");
    }
    page += FPSTR(HTTP_OPTION);
    page.replace("{v}", "3");
    page.replace("{n}", "8 channel (rrggbb dim dim)");
    if (ledChannelMode == 3) {
      page.replace("{s}", "selected");
    }
    else {
      page.replace("{s}", "");
    }
    page += FPSTR(HTTP_SELECT_END);

    page += "<br/><br/>First DMX Channel:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "firstChannel");
    page.replace("{n}", "firstChannel");
    page.replace("{p}", (String)firstChannel);
    page.replace("{v}", (String)firstChannel);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>DMX Universe:";
    page += FPSTR(HTTP_FORM_PARAM);
    page.replace("{i}", "universe");
    page.replace("{n}", "universe");
    page.replace("{p}", (String)universe);
    page.replace("{v}", (String)universe);
    page.replace("{c}", "");
    page.replace("{l}", "''");

    page += "<br/><br/>Allow Broadcast DMX:";
    page += FPSTR(HTTP_SELECT);
    page.replace("{n}", "allowBroadcastDMX");
    page += FPSTR(HTTP_OPTION);
    page.replace("{v}", "true");
    page.replace("{n}", "Allow");
    if (allowBroadcastDMX) {
      page.replace("{s}", "selected");
    }
    else {
      page.replace("{s}", "");
    }
    page += FPSTR(HTTP_OPTION);
    page.replace("{v}", "false");
    page.replace("{n}", "Disalow");
    if (!allowBroadcastDMX) {
      page.replace("{s}", "selected");
    }
    else {
      page.replace("{s}", "");
    }
    page += FPSTR(HTTP_SELECT_END);

    page += "<br/><br/>";
    page += FPSTR(HTTP_FORM_END);
    page += FPSTR(HTTP_END);

    server.send(200, "text/html", page);
  });
  server.on("/settingsSave", []() {
    if (!server.authenticate(www_username.c_str(), www_password.c_str())) {
      return server.requestAuthentication();
    }

    if (ledOutputMode) {
      analogWrite(pinR, 0);
      analogWrite(pinG, 0);
      analogWrite(pinB, 0);
    }
    else {
      clearStrip();
    }

    if (server.hasArg("nodeName") && (server.arg("nodeName") != "")){
      nodeName = server.arg("nodeName");
    }
    if (server.hasArg("hwVersion") && (server.arg("hwVersion") != "")){
      hwVersion = server.arg("hwVersion");
    }
    if (server.hasArg("swVersion") && (server.arg("swVersion") != "")){
      swVersion = server.arg("swVersion");
    }
    if (server.hasArg("pinR") && (server.arg("pinR") != "")){
      pinR = server.arg("pinR").toInt();
    }
    if (server.hasArg("pinG") && (server.arg("pinG") != "")){
      pinG = server.arg("pinG").toInt();
    }
    if (server.hasArg("pinB") && (server.arg("pinB") != "")){
      pinB = server.arg("pinB").toInt();
    }
    if (server.hasArg("onboardNeopixelPin") && (server.arg("onboardNeopixelPin") != "")){
      onboardNeopixelPin = server.arg("onboardNeopixelPin").toInt();
    }
    if (server.hasArg("btnPin") && (server.arg("btnPin") != "")){
      btnPin = server.arg("btnPin").toInt();
    }
    if (server.hasArg("maxVoltage") && (server.arg("maxVoltage") != "")){
      minLEDVoltage = server.arg("maxVoltage").toInt();
    }
    if (server.hasArg("minLEDVoltage") && (server.arg("minLEDVoltage") != "")){
      minLEDVoltage = server.arg("minLEDVoltage").toInt();
    }
    if (server.hasArg("minSelfVoltage") && (server.arg("minSelfVoltage") != "")){
      minSelfVoltage = server.arg("minSelfVoltage").toInt();
    }
    if (server.hasArg("www_username") && (server.arg("www_username") != "")){
      www_username = server.arg("www_username");
    }
    if (server.hasArg("www_password") && (server.arg("www_password") != "")){
      www_password = server.arg("www_password");
    }
    if (server.hasArg("ledOutputMode") && (server.arg("ledOutputMode") != "")){
      if (server.arg("ledOutputMode") == "true") {
        ledOutputMode = true;
      }
      else {
        ledOutputMode = false;
      }
    }
    if (server.hasArg("stripLength") && (server.arg("stripLength") != "")){
      stripLength = server.arg("stripLength").toInt();
    }
    if (server.hasArg("stripPin") && (server.arg("stripPin") != "")){
      stripPin = server.arg("stripPin").toInt();
    }
    if (server.hasArg("serverIP") && (server.arg("serverIP") != "")){
      serverIP.fromString(server.arg("serverIP"));
    }
    if (server.hasArg("serverName") && (server.arg("serverName") != "")){
      serverName = server.arg("serverName");
    }
    if (server.hasArg("tryServerDNS") && (server.arg("tryServerDNS") != "")){
      if (server.arg("tryServerDNS") == "true") {
        tryServerDNS = true;
      }
      else {
        tryServerDNS = false;
      }
    }
    if (server.hasArg("ledChannelMode") && (server.arg("ledChannelMode") != "")){
      ledChannelMode = server.arg("ledChannelMode").toInt();
    }
    if (server.hasArg("firstChannel") && (server.arg("firstChannel") != "")){
      firstChannel = server.arg("firstChannel").toInt();
    }
    if (server.hasArg("universe") && (server.arg("universe") != "")){
      universe = server.arg("universe").toInt();
    }
    if (server.hasArg("allowBroadcastDMX") && (server.arg("allowBroadcastDMX") != "")){
      if (server.arg("allowBroadcastDMX") == "true") {
        allowBroadcastDMX = true;
      }
      else {
        allowBroadcastDMX = false;
      }
    }

    // Save new var settings
    defaultConfigJSON();
    makeUDPStartPacket();

    String page = FPSTR(HTTP_HEAD);
    page.replace("{v}", "Lumos - Settings Saved");
    page += FPSTR(HTTP_STYLE);
    page += FPSTR(HTTP_HEAD_END);
    page += "<div class=\"c\">\"{n}\"</div>";
    page.replace("{n}", nodeName);
    page += "<div class=\"c\">Settings Saved...</div>";
    page += FPSTR(HTTP_LINK);
    page.replace("{a}", "/");
    page.replace("{n}", "Home");
    page += FPSTR(HTTP_LINK);
    page.replace("{a}", "/settings");
    page.replace("{n}", "Return to Settings");
    page += FPSTR(HTTP_LINK);
    page.replace("{a}", "/reset");
    page.replace("{n}", "Reset");
    page += FPSTR(HTTP_END);
    
    server.send(200, "text/html", page);
  });
  server.on("/reset", []() {
    if (!server.authenticate(www_username.c_str(), www_password.c_str())) {
      return server.requestAuthentication();
    }

    String page = FPSTR(HTTP_HEAD);
    page.replace("{v}", "Lumos - Resetting");
    page += FPSTR(HTTP_STYLE);
    page += FPSTR(HTTP_HEAD_END);
    page += "<div class=\"c\">\"{n}\"</div>";
    page.replace("{n}", nodeName);
    page += "<div class=\"c\">Resetting 30 seconds...</div>";
    page += FPSTR(HTTP_LINK);
    page.replace("{a}", "/");
    page.replace("{n}", "Home");
    page += FPSTR(HTTP_END);
    
    server.send(200, "text/html", page);
    ESP.reset();
  });
  server.on("/shutdown", []() {
    if (!server.authenticate(www_username.c_str(), www_password.c_str())) {
      return server.requestAuthentication();
    }

    String page = FPSTR(HTTP_HEAD);
    page.replace("{v}", "Lumos - Shutting Down");
    page += FPSTR(HTTP_STYLE);
    page += FPSTR(HTTP_HEAD_END);
    page += "<div class=\"c\">\"{n}\"</div>";
    page.replace("{n}", nodeName);
    page += "<div class=\"c\">Shutting down...</div>";
    page += "<div class=\"c\">Physical reset needed to wake</div>";
    page += FPSTR(HTTP_END);
    
    server.send(200, "text/html", page);
    ESP.deepSleep(0);
  });
  server.on("/findme", []() {
    if (!server.authenticate(www_username.c_str(), www_password.c_str())) {
      return server.requestAuthentication();
    }

    String page = FPSTR(HTTP_HEAD);
    page.replace("{v}", "Lumos - Find Me");
    page += FPSTR(HTTP_STYLE);
    page += FPSTR(HTTP_HEAD_END);
    page += "<div class=\"c\">\"{n}\"</div>";
    page.replace("{n}", nodeName);
    page += "<div class=\"c\">Flashing Status LED</div>";
    page += FPSTR(HTTP_LINK);
    page.replace("{a}", "/");
    page.replace("{n}", "Home");
    page += FPSTR(HTTP_END);
    
    server.send(200, "text/html", page);
    
    if (ledOutputMode) {
      analogWrite(pinR, 0);
      analogWrite(pinG, 0);
      analogWrite(pinB, 0);
    }
    else {
      clearStrip();
    }
    flashStatus(255, 255, 255, 8, 500);
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
    if (ledOutputMode) {
      analogWrite(pinR, 0);
      analogWrite(pinG, 0);
      analogWrite(pinB, 0);
    }
    else {
      clearStrip();
    }
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
      if (ledOutputMode) {
        if (ledChannelMode == 0) {
          analogWrite(pinR, ledCurve8bit[artnetnode.returnDMXValue(0, firstChannel)]);
          analogWrite(pinG, ledCurve8bit[artnetnode.returnDMXValue(0, firstChannel + 1)]);
          analogWrite(pinB, ledCurve8bit[artnetnode.returnDMXValue(0, firstChannel + 2)]);
        }
        else if (ledChannelMode == 1) {
          uint32_t factor = 255;
          analogWrite(pinR, ledCurve8bit[((((uint32_t)artnetnode.returnDMXValue(0, firstChannel)*factor) / 255) * (uint32_t)artnetnode.returnDMXValue(0, firstChannel + 3)) / factor]);
          analogWrite(pinG, ledCurve8bit[((((uint32_t)artnetnode.returnDMXValue(0, firstChannel + 1)*factor) / 255) * (uint32_t)artnetnode.returnDMXValue(0, firstChannel + 3)) / factor]);
          analogWrite(pinB, ledCurve8bit[((((uint32_t)artnetnode.returnDMXValue(0, firstChannel + 2)*factor) / 255) * (uint32_t)artnetnode.returnDMXValue(0, firstChannel + 3)) / factor]);
        }
        else if (ledChannelMode == 2) {
          analogWrite(pinR, ((uint16_t)((artnetnode.returnDMXValue(0, firstChannel) << 8) | artnetnode.returnDMXValue(0, firstChannel + 1)) / 64));
          analogWrite(pinG, ((uint16_t)((artnetnode.returnDMXValue(0, firstChannel + 2) << 8) | artnetnode.returnDMXValue(0, firstChannel + 3)) / 64));
          analogWrite(pinB, ((uint16_t)((artnetnode.returnDMXValue(0, firstChannel + 4) << 8) | artnetnode.returnDMXValue(0, firstChannel + 5)) / 64));
        }
        else if (ledChannelMode == 3) {
          uint32_t rdim = ((uint16_t)((artnetnode.returnDMXValue(0, firstChannel) << 8) | artnetnode.returnDMXValue(0, firstChannel + 1)) / 64);
          uint32_t gdim = ((uint16_t)((artnetnode.returnDMXValue(0, firstChannel + 2) << 8) | artnetnode.returnDMXValue(0, firstChannel + 3)) / 64);
          uint32_t bdim = ((uint16_t)((artnetnode.returnDMXValue(0, firstChannel + 4) << 8) | artnetnode.returnDMXValue(0, firstChannel + 5)) / 64);
          uint32_t dim = ((uint16_t)((artnetnode.returnDMXValue(0, firstChannel + 6) << 8) | artnetnode.returnDMXValue(0, firstChannel + 7)) / 64);
          uint32_t factor = 1023;
          analogWrite(pinR, (((rdim * factor) / 1023)*dim) / factor);
          analogWrite(pinG, (((gdim * factor) / 1023)*dim) / factor);
          analogWrite(pinB, (((bdim * factor) / 1023)*dim) / factor);
        }
      }
      else {
        setStrip();
      }
      batteryLog();
    }
    else if (code == OpPoll) {
      Serial.println("Art Poll Packet");
    }
  }
  if (shuttingdown) {
    Serial.print("Danger voltage, deep sleeping forever");
    flashStatus(50, 0, 0, 4, 200);
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

void makeUDPStartPacket() {
  memset(udpBeatPacketStart,0,sizeof(udpBeatPacketStart));
  localIP = WiFi.localIP();
  sprintf(udpBeatPacketStart, "{\"name\":\"%s\",\"hw_version\":\"%s\",\"sw_version\":\"%s\",\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"ip\":\"%d.%d.%d.%d\",\"current_voltage\":%%d,\"lowest_voltage\":%%d,\"output_enabled\":%%s}",
          nodeName.c_str(), hwVersion.c_str(), swVersion.c_str(), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], localIP[0],  localIP[1],  localIP[2],  localIP[3]);
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
  UdpSend.beginPacket(serverIP, 33333);
  memset(udpBeatPacket,0,sizeof(udpBeatPacket));
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

void flashStatus(int r, int g, int b, int times, int delayLen) {
  for (int a = 0; a < times; a++) {
    statusNeo.setPixelColor(0, statusNeo.Color(r, g, b));
    statusNeo.show();
    delay(delayLen);
    statusNeo.setPixelColor(0, statusNeo.Color(0, 0, 0));
    statusNeo.show();
    delay(delayLen);
  }
}

void setStrip() {
  strip.clear();
  for (int a = 0; a < stripLength; a++) {
    strip.setPixelColor((a * 3), artnetnode.returnDMXValue(0, (a * 3) + firstChannel), artnetnode.returnDMXValue(0, (a * 3) + firstChannel + 1), artnetnode.returnDMXValue(0, (a * 3) + firstChannel + 2));
  }
  strip.show();
}

void clearStrip() {
  strip.clear();
  strip.show();
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

    if (!swVersionConstStr.equals(swVersion)) {
      Serial.println("check failed");
      // Out of date config file, update it!
      // Close the file and remove
      configJSONfile.close();
      SPIFFS.remove("/config.json");
      // Create new default
      defaultConfigJSON();
      // Reset to load new config
      ESP.reset();
      delay(1000);
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
    maxVoltage = configJSONroot["maxVoltage"];
    minLEDVoltage = configJSONroot["minLEDVoltage"];
    minSelfVoltage = configJSONroot["minSelfVoltage"];
    const char* www_usernameConst = configJSONroot["www_username"];
    www_username = (String)www_usernameConst;
    const char* www_passwordConst = configJSONroot["www_password"];
    www_password = (String)www_passwordConst;
    ledOutputMode = configJSONroot["ledOutputMode"];
    stripLength = configJSONroot["stripLength"];
    stripPin = configJSONroot["stripPin"];
    serverIP[0] = configJSONroot["serverIP"][0];
    serverIP[1] = configJSONroot["serverIP"][1];
    serverIP[2] = configJSONroot["serverIP"][2];
    serverIP[3] = configJSONroot["serverIP"][3];
    const char* serverNameConst = configJSONroot["serverName"];
    serverName = (String)serverNameConst;
    tryServerDNS = configJSONroot["tryServerDNS"];
    ledChannelMode = configJSONroot["ledChannelMode"];
    firstChannel = configJSONroot["firstChannel"];
    universe = configJSONroot["universe"];
    allowBroadcastDMX = configJSONroot["allowBroadcastDMX"];

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
  root["maxVoltage"] = maxVoltage;
  root["minLEDVoltage"] = minLEDVoltage;
  root["minSelfVoltage"] = minSelfVoltage;
  root["www_username"] = (const char*)www_username.c_str();
  root["www_password"] = (const char*)www_password.c_str();
  root["ledOutputMode"] = ledOutputMode;
  root["stripLength"] = stripLength;
  root["stripPin"] = stripPin;
  JsonArray& data = root.createNestedArray("serverIP");
  data.add(serverIP[0]);
  data.add(serverIP[1]);
  data.add(serverIP[2]);
  data.add(serverIP[3]);
  root["serverName"] = (const char*)serverName.c_str();
  root["tryServerDNS"] = tryServerDNS;
  root["ledChannelMode"] = ledChannelMode;
  root["firstChannel"] = firstChannel;
  root["universe"] = universe;
  root["allowBroadcastDMX"] = allowBroadcastDMX;

  File configJSONfile = SPIFFS.open("/config.json", "w+");
  root.printTo(configJSONfile);
  configJSONfile.close();
}

