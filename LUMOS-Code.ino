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
  if(!digitalRead(btnPin)){
    strip.setPixelColor(0, strip.Color(50, 0, 50));
    strip.show();
    WiFiManager wifiManager;
    wifiManager.startConfigPortal(nodeName);
    ESP.reset();
  }
  else{
    strip.setPixelColor(0, strip.Color(0, 0, 50));
    strip.show();
    WiFiManager wifiManager;
    wifiManager.autoConnect(nodeName);
  }

  
  // Start up artnetnode library
  artnetnode.setName(nodeName);
  artnetnode.setStartingUniverse(1);
  while(artnetnode.begin(1) == false){
    Serial.print("X");
  }
  Serial.println();
  Serial.println("Connected");
  artnetnode.setDMXOutput(0,1,0);

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
  ticker.attach(5,beat);
  beat();
}

void loop()
{ 
  uint16_t code = artnetnode.read();
  if(code){
    if (code == OpDmx)
    {
      //Serial.print("D");
      analogWrite(pinR, artnetnode.returnDMXValue(0, 1));
      analogWrite(pinG, artnetnode.returnDMXValue(0, 2));
      analogWrite(pinB, artnetnode.returnDMXValue(0, 3));
    }
    else if (code == OpPoll) {
      Serial.println("Art Poll Packet");
    }
  }
  if (WiFi.status() != WL_CONNECTED){
    ESP.reset();
  }
}

void beat(){

  UdpSend.beginPacket({192,168,0,100},33333);
  sprintf(udpBeatPacket, "{\"mac\":\"%x:%x:%x:%x:%x:%x\",\"ip\":\"%d.%d.%d.%d\",\"voltage\":%d}", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], localIP[0],  localIP[1],  localIP[2],  localIP[3], analogRead(A0));
  UdpSend.write(udpBeatPacket, sizeof(udpBeatPacket)-1);
  UdpSend.endPacket();
}
