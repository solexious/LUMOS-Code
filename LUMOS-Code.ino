#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <Artnetnode.h>
#include <Ticker.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>
#endif

//// Pin Settings - LUMOS
//int pinR = 15;
//int pinG = 4;
//int pinB = 5;
//int onboardNeopixelPin = 13;
//int btnPin = 16;

// Pin Settings - Tester
int pinR = 15;
int pinG = 12;
int pinB = 13;
int onboardNeopixelPin = 4;
int btnPin = 16;

// Temp - FIXME
char* ssid = "LUMOS";
char* password = "2784B508";

// Globals
bool ledsEnabled = true;
#define DMX_MAX 512 // max. number of DMX data packages.
uint8_t DMXBuffer[DMX_MAX];
char udpBeatPacket[70];
char nodeName[15];
uint8_t mac[6];

// Lib
Ticker ticker;
WiFiUDP UdpSend;
Artnetnode artnetnode;
IPAddress localIP;
Adafruit_NeoPixel strip = Adafruit_NeoPixel(1, onboardNeopixelPin, NEO_GRB + NEO_KHZ800);


void setup()
{
  // Initial setup
  Serial.begin(9600);
  WiFi.macAddress(mac);
  sprintf(nodeName, "LUMOS-%X%X%X", mac[3], mac[4], mac[5]);

  // Pin setup
  pinMode(pinR, OUTPUT);
  pinMode(pinG, OUTPUT);
  pinMode(pinB, OUTPUT);

  pinMode(btnPin, INPUT);

  analogWrite(pinR, 0);
  analogWrite(pinG, 0);
  analogWrite(pinB, 0);

  // Status neopixel setup
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  // Force wifi connection portal or attempt to connect
  if (!digitalRead(btnPin)) {
    // Force AP mode with captive portal for wifi setup
    strip.setPixelColor(0, strip.Color(50, 0, 50));
    strip.show();
    WiFiManager wifiManager;
    wifiManager.startConfigPortal(nodeName);
    // Reset to use new settings
    ESP.reset();
    delay(1000);
  }
  else {
    // Try to connect with existing wifi settings
    strip.setPixelColor(0, strip.Color(0, 0, 50));
    strip.show();
    WiFiManager wifiManager;
    // Add callback to change status neopixel to orange if connection fails and we enter AP mode
    wifiManager.setAPCallback(configModeCallback);
    // Attempt connection
    if (!wifiManager.autoConnect(nodeName)) {
      Serial.println("failed to connect and hit timeout");
      // Reset and try again
      strip.setPixelColor(0, strip.Color(50, 0, 0));
      strip.show();
      delay(1000);
      strip.setPixelColor(0, strip.Color(0, 0, 0));
      strip.show();
      ESP.reset();
      delay(1000);
    }
  }


  // Start up artnetnode library
  artnetnode.setName(nodeName);
  artnetnode.setStartingUniverse(1);
  while (artnetnode.begin(1) == false) {
    Serial.print("X");
  }
  Serial.println();
  Serial.println("Connected");
  artnetnode.setDMXOutput(0, 1, 0);

  // Connected and happy, flash green
  strip.setPixelColor(0, strip.Color(0, 50, 0));
  strip.show();
  delay(1000);
  strip.setPixelColor(0, strip.Color(0, 0, 0));
  strip.show();

  // Decrease range - FIXME
  analogWriteRange(255);

  // Setup heartbeat packet
  UdpSend.begin(4000);
  localIP = WiFi.localIP();
  ticker.attach(5, beat);
  beat();
}

void loop()
{
  uint16_t code = artnetnode.read();
  if (code) {
    if (code == OpDmx)
    {
      analogWrite(pinR, artnetnode.returnDMXValue(0, 1));
      analogWrite(pinG, artnetnode.returnDMXValue(0, 2));
      analogWrite(pinB, artnetnode.returnDMXValue(0, 3));
    }
    else if (code == OpPoll) {
      Serial.println("Art Poll Packet");
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    // Lost wifi connection, reset to reconnect
    ESP.reset();
    delay(1000);
  }
}

void beat() {
  // Send heartbeat packet
  UdpSend.beginPacket({192, 168, 0, 100}, 33333);
  sprintf(udpBeatPacket, "{\"mac\":\"%x:%x:%x:%x:%x:%x\",\"ip\":\"%d.%d.%d.%d\",\"voltage\":%d}", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], localIP[0],  localIP[1],  localIP[2],  localIP[3], analogRead(A0));
  UdpSend.write(udpBeatPacket, sizeof(udpBeatPacket) - 1);
  UdpSend.endPacket();
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  // Change status led
  strip.setPixelColor(0, strip.Color(50, 50, 0));
  strip.show();
}
