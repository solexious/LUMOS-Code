//const char* swVersion = "0.1";


String nodeName = "DEFAULT";
String hwVersion = "0.2";
String swVersion = "0.4";
int pinR = 15;
int pinG = 5;
int pinB = 4;
int onboardNeopixelPin = 13;
int btnPin = 16;
int minLEDVoltage = 745;
int maxVoltage = 900;
int minSelfVoltage = 725;
String www_username = "admin";
String www_password = "esp8266";
bool ledOutputMode = true; // true=12w flase=neopixels
int stripLength = 60;
int stripPin = 14;
IPAddress serverIP(192, 168, 0, 100);
String serverName = "command.lumos-project.com";
bool tryServerDNS = true;
int ledChannelMode = 0; // 0 - 3 channel (rgb), 1 - 4 channel (rgb dim), 2 - 6 channel (rrggbb), 3 - 8 channel (rrggbb dim dim)
int firstChannel = 1;
int universe = 0;
bool allowBroadcastDMX = false;
