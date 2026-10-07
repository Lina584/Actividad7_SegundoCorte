// Stub mínimo de Arduino/ESP32 para compilar y CORRER nodo_aco.ino en un PC Linux.
// Las IPs 192.168.4.X del enjambre se traducen a 127.0.4.X (loopback), así los
// 3 "ESP32" se comunican por UDP real entre procesos del mismo PC.
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0

inline uint32_t millis() {
  static auto t0 = std::chrono::steady_clock::now();
  return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - t0).count();
}
inline void delay(uint32_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void analogWrite(int, int) {}
inline uint32_t esp_random() { static std::random_device rd; return rd(); }

class String {
 public:
  String(const std::string& s) : s_(s) {}
  const char* c_str() const { return s_.c_str(); }
 private:
  std::string s_;
};

struct HardwareSerial {
  void begin(long) {}
  void print(const char* s) { fputs(s, stdout); fflush(stdout); }
  void printf(const char* f, ...) {
    va_list a; va_start(a, f); vprintf(f, a); va_end(a); fflush(stdout);
  }
};
static HardwareSerial Serial;

class IPAddress {
 public:
  uint8_t b[4] = {0, 0, 0, 0};
  IPAddress() {}
  IPAddress(uint8_t a, uint8_t c, uint8_t d, uint8_t e) { b[0] = a; b[1] = c; b[2] = d; b[3] = e; }
  bool operator==(const IPAddress& o) const { return memcmp(b, o.b, 4) == 0; }
  bool operator!=(const IPAddress& o) const { return !(*this == o); }
  String toString() const {
    char t[20]; snprintf(t, sizeof(t), "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
    return String(t);
  }
  // 192.168.4.X -> 127.0.4.X (loopback del PC)
  in_addr_t aSocket() const {
    char t[20];
    if (b[0] == 192 && b[1] == 168 && b[2] == 4) snprintf(t, sizeof(t), "127.0.4.%d", b[3]);
    else snprintf(t, sizeof(t), "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
    return inet_addr(t);
  }
  static IPAddress deSocket(in_addr_t a) {
    uint8_t* p = (uint8_t*)&a;
    if (p[0] == 127 && p[1] == 0 && p[2] == 4) return IPAddress(192, 168, 4, p[3]);
    return IPAddress(p[0], p[1], p[2], p[3]);
  }
};
