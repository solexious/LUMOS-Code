//const char* swVersion = "0.1";


String nodeName = "DEFAULT";
String hwVersion = "0.2";
String swVersion = "0.3";
int pinR = 15;
int pinG = 5;
int pinB = 4;
int onboardNeopixelPin = 13;
int btnPin = 16;
int minLEDVoltage = 775;
int minSelfVoltage = 725;
String www_username = "admin";
String www_password = "esp8266";
bool ledOutputMode = true; // true=12w flase=neopixels
int stripLength = 60;
int stripPin = 14;
IPAddress serverIP(192, 168, 0, 100);
String serverName = "command.lumos-project.com";
int tryServerDNS = true;
int ledChannelMode = 3; // 0 - 3 channel (rgb), 1 - 4 channel (rgb dim), 2 - 6 channel (rrggbb), 3 - 7 channel (rrggbb dim)
