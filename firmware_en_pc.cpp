// Compila el MISMO nodo_aco.ino para Linux (stubs en host_stub/) y lo corre como
// un proceso. Lanzando 3 procesos (NODO_ID 1, 2, 3) se prueba el firmware real,
// con UDP real, sin tener los ESP32. Ver tests/probar_firmware_en_pc.sh
#include "Arduino.h"
#include "../firmware/nodo_aco/nodo_aco.ino"

int main() {
  setup();
  for (;;) {
    loop();
    delay(5);
  }
}
