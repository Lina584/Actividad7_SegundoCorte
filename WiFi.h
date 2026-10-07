#pragma once
#include "Arduino.h"

#define WIFI_AP 2
#define WIFI_STA 1
#define WL_CONNECTED 3

struct WiFiClass {
  IPAddress ip;
  void mode(int) {}
  bool softAPConfig(IPAddress l, IPAddress, IPAddress) { ip = l; return true; }
  bool softAP(const char*, const char*, int = 1, int = 0, int = 4) { return true; }
  IPAddress softAPIP() { return ip; }
  bool config(IPAddress l, IPAddress, IPAddress) { ip = l; return true; }
  void begin(const char*, const char*) {}
  int status() { return WL_CONNECTED; }
  IPAddress localIP() { return ip; }
  void reconnect() {}
};
static WiFiClass WiFi;
