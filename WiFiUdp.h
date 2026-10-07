#pragma once
#include <fcntl.h>
#include "WiFi.h"

class WiFiUDP {
 public:
  void begin(uint16_t puerto) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    fcntl(fd_, F_SETFL, O_NONBLOCK);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(puerto);
    a.sin_addr.s_addr = WiFi.ip.aSocket();   // cada nodo en su IP de loopback
    if (bind(fd_, (sockaddr*)&a, sizeof(a)) < 0) { perror("bind"); exit(1); }
  }
  void beginPacket(IPAddress ip, uint16_t puerto) {
    dst_ = sockaddr_in{};
    dst_.sin_family = AF_INET;
    dst_.sin_port = htons(puerto);
    dst_.sin_addr.s_addr = ip.aSocket();
    ntx_ = 0;
  }
  size_t write(const uint8_t* b, size_t n) {
    if (ntx_ + n > sizeof(tx_)) n = sizeof(tx_) - ntx_;
    memcpy(tx_ + ntx_, b, n); ntx_ += n; return n;
  }
  int endPacket() { return sendto(fd_, tx_, ntx_, 0, (sockaddr*)&dst_, sizeof(dst_)) >= 0; }
  int parsePacket() {
    sockaddr_in de{}; socklen_t l = sizeof(de);
    int n = recvfrom(fd_, rx_, sizeof(rx_), 0, (sockaddr*)&de, &l);
    if (n <= 0) { nrx_ = 0; return 0; }
    nrx_ = n; remoto_ = IPAddress::deSocket(de.sin_addr.s_addr);
    return n;
  }
  int read(uint8_t* b, size_t n) {
    int k = (int)(n < (size_t)nrx_ ? n : nrx_);
    memcpy(b, rx_, k); return k;
  }
  IPAddress remoteIP() { return remoto_; }

 private:
  int fd_ = -1;
  sockaddr_in dst_{};
  uint8_t tx_[2048], rx_[2048];
  size_t ntx_ = 0;
  int nrx_ = 0;
  IPAddress remoto_;
};
