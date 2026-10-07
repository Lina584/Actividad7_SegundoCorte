/*
  nodo_aco.ino — Enjambre de 3 carritos con ACO en ESP32 + gemelo digital PyBullet
  Microcontroladores y Laboratorio — UMNG

  UN SOLO SKETCH PARA LOS 3 ESP32: solo cambia NODO_ID antes de cargar.
    NODO_ID 1 -> crea la red Wi-Fi (modo AP, 192.168.4.1) y además corre ACO.
    NODO_ID 2 -> se conecta al AP como estación (IP fija 192.168.4.12).
    NODO_ID 3 -> se conecta al AP como estación (IP fija 192.168.4.13).

  Qué hace cada nodo:
    1. Cada ITER_MS corre una iteración del algoritmo de colonia de hormigas
       (aco_core.h) sobre el laberinto (laberinto.h).
    2. Envía su tabla de feromonas + su mejor ruta a los otros 2 nodos por UDP
       (paquete binario "ACOF") y mezcla las feromonas que recibe.
    3. Mueve su carrito celda por celda sobre la mejor ruta conocida (si
       USAR_MOTORES = 1 mueve motores reales; si no, el movimiento es virtual).
    4. Reporta su estado al gemelo digital (PC/Docker) en JSON por UDP.

  Red:   SSID "ENJAMBRE_ACO", clave "hormigas123"
  UDP:   puerto 4210 = enjambre (nodos)   puerto 4211 = gemelo digital (PC)
  El PC/Docker se registra enviando "HOLA_GEMELO" al puerto 4210 de cada nodo.

  Placa: "ESP32 Dev Module" (core esp32 de Espressif). No requiere librerías extra.
*/
#include <WiFi.h>
#include <WiFiUdp.h>
#include "laberinto.h"
#include "aco_core.h"

// ======================= CONFIGURACIÓN =======================
#ifndef NODO_ID
#define NODO_ID       1        // <-- CAMBIAR: 1, 2 o 3
#endif
#ifndef USAR_MOTORES
#define USAR_MOTORES  0        // 1 = carrito físico con puente H (L298N/TB6612)
#endif

const char* SSID  = "ENJAMBRE_ACO";
const char* CLAVE = "hormigas123";
const uint16_t PUERTO_ENJAMBRE = 4210;
const uint16_t PUERTO_GEMELO   = 4211;

const uint32_t ITER_MS   = 1200;   // cada cuánto se corre una iteración ACO
const uint32_t PASO_MS   = 450;    // tiempo para avanzar una celda
const uint32_t ESTADO_MS = 200;    // cada cuánto se reporta al gemelo
const uint8_t  LED_PIN   = 2;      // LED integrado: parpadea al recibir feromonas

// IPs fijas del enjambre (índice = NODO_ID - 1)
const IPAddress IP_NODOS[3] = {IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 12),
                               IPAddress(192, 168, 4, 13)};
const IPAddress GATEWAY(192, 168, 4, 1), MASCARA(255, 255, 255, 0);

#if USAR_MOTORES
// Puente H (L298N): IN1/IN2 motor izq, IN3/IN4 motor der, ENA/ENB PWM
const uint8_t IN1 = 26, IN2 = 27, IN3 = 14, IN4 = 12, ENA = 25, ENB = 33;
const uint8_t VELOCIDAD = 180;     // 0..255
const uint32_t GIRO90_MS = 380;    // calibrar: tiempo de un giro de 90°
const uint32_t CELDA_MS  = 600;    // calibrar: tiempo de avanzar una celda
#endif
// =============================================================

aco::Colonia colonia;
WiFiUDP udp;

// --- paquete de feromonas (binario) ---
//  "ACOF" | id(1) | iteracion(4, LE) | nRuta(1) | ruta[nRuta] | tau[N_CELDAS*4]
static uint8_t txBuf[4 + 1 + 4 + 1 + MAX_RUTA + N_CELDAS * N_DIR];
static uint8_t rxBuf[sizeof(txBuf) + 16];

// --- gemelo digital (PC) ---
IPAddress ipGemelo;
bool hayGemelo = false;

// --- estado del carrito ---
uint8_t  rutaCarro[MAX_RUTA];
uint16_t nRutaCarro = 0, pasoCarro = 0;
uint16_t vueltas = 0;
int      orientacion = 0;              // 0=E,1=S,2=O,3=N
uint32_t tIter = 0, tPaso = 0, tEstado = 0, tLed = 0;
uint32_t paquetesRx = 0;

// ------------------------------------------------------------------
void conectarRed() {
  if (NODO_ID == 1) {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IP_NODOS[0], GATEWAY, MASCARA);
    WiFi.softAP(SSID, CLAVE, 6, 0, 8);
    Serial.printf("[AP] Red '%s' creada en %s\n", SSID, WiFi.softAPIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.config(IP_NODOS[NODO_ID - 1], GATEWAY, MASCARA);
    WiFi.begin(SSID, CLAVE);
    Serial.print("[STA] Conectando al AP");
    while (WiFi.status() != WL_CONNECTED) {
      delay(300);
      Serial.print(".");
    }
    Serial.printf("\n[STA] Conectado, IP %s\n", WiFi.localIP().toString().c_str());
  }
  udp.begin(PUERTO_ENJAMBRE);
}

bool redLista() { return NODO_ID == 1 || WiFi.status() == WL_CONNECTED; }

// ------------------------------------------------------------------
void enviarFeromonas() {
  uint16_t k = 0;
  txBuf[k++] = 'A'; txBuf[k++] = 'C'; txBuf[k++] = 'O'; txBuf[k++] = 'F';
  txBuf[k++] = NODO_ID;
  uint32_t it = colonia.iteracion;
  memcpy(&txBuf[k], &it, 4); k += 4;               // ESP32 es little-endian
  txBuf[k++] = (uint8_t)colonia.nMejor;
  memcpy(&txBuf[k], colonia.mejor, colonia.nMejor); k += colonia.nMejor;
  colonia.exportarTau(&txBuf[k]); k += N_CELDAS * N_DIR;

  for (int i = 0; i < 3; i++) {
    if (i == NODO_ID - 1) continue;                 // no a sí mismo
    udp.beginPacket(IP_NODOS[i], PUERTO_ENJAMBRE);
    udp.write(txBuf, k);
    udp.endPacket();
  }
  if (hayGemelo) {                                  // el gemelo dibuja las feromonas
    udp.beginPacket(ipGemelo, PUERTO_GEMELO);
    udp.write(txBuf, k);
    udp.endPacket();
  }
}

void recibirPaquetes() {
  int n;
  while ((n = udp.parsePacket()) > 0) {
    int leido = udp.read(rxBuf, sizeof(rxBuf));
    if (leido >= 11 && memcmp(rxBuf, "HOLA_GEMELO", 11) == 0) {
      if (!hayGemelo || ipGemelo != udp.remoteIP())
        Serial.printf("[RED] Gemelo digital registrado en %s\n", udp.remoteIP().toString().c_str());
      ipGemelo = udp.remoteIP();
      hayGemelo = true;
      continue;
    }
    if (leido < 10 || memcmp(rxBuf, "ACOF", 4) != 0) continue;
    uint8_t id = rxBuf[4];
    if (id == NODO_ID || id < 1 || id > 3) continue;
    uint8_t nR = rxBuf[9];
    int esperado = 10 + nR + N_CELDAS * N_DIR;
    if (leido != esperado || nR > MAX_RUTA) continue;
    colonia.mezclar(&rxBuf[10 + nR], &rxBuf[10], nR);  // <-- cooperación del enjambre
    paquetesRx++;
    digitalWrite(LED_PIN, HIGH);
    tLed = millis();
  }
}

void enviarEstado() {
  if (!hayGemelo) return;
  static char json[900];
  uint8_t celda = nRutaCarro ? rutaCarro[pasoCarro] : colonia.inicio;
  uint8_t sig = (nRutaCarro && pasoCarro + 1 < nRutaCarro) ? rutaCarro[pasoCarro + 1] : celda;
  int k = snprintf(json, sizeof(json),
                   "{\"t\":\"E\",\"id\":%d,\"it\":%lu,\"mejor\":%d,\"celda\":[%d,%d],"
                   "\"sig\":[%d,%d],\"vueltas\":%u,\"rx\":%lu,\"ms\":%lu,\"ruta\":[",
                   NODO_ID, (unsigned long)colonia.iteracion, colonia.nMejor ? colonia.nMejor - 1 : -1,
                   aco::Colonia::fil(celda), aco::Colonia::col(celda), aco::Colonia::fil(sig),
                   aco::Colonia::col(sig), vueltas, (unsigned long)paquetesRx,
                   (unsigned long)millis());
  for (uint16_t i = 0; i < nRutaCarro && k < (int)sizeof(json) - 8; i++)
    k += snprintf(json + k, sizeof(json) - k, i ? ",%d" : "%d", rutaCarro[i]);
  k += snprintf(json + k, sizeof(json) - k, "]}");
  udp.beginPacket(ipGemelo, PUERTO_GEMELO);
  udp.write((const uint8_t*)json, k);
  udp.endPacket();
}

// ------------------------------------------------------------------
#if USAR_MOTORES
void motores(int izq, int der) {  // -255..255
  digitalWrite(IN1, izq > 0); digitalWrite(IN2, izq < 0);
  digitalWrite(IN3, der > 0); digitalWrite(IN4, der < 0);
  analogWrite(ENA, abs(izq)); analogWrite(ENB, abs(der));
}
// Movimiento a lazo abierto por tiempo (para la entrega es suficiente;
// con encoders o seguidor de línea se haría a lazo cerrado).
void moverFisico(int dirDeseada) {
  int giro = (dirDeseada - orientacion + 4) % 4;  // 0 recto, 1 der, 2 media vuelta, 3 izq
  if (giro == 1) { motores(VELOCIDAD, -VELOCIDAD); delay(GIRO90_MS); }
  if (giro == 3) { motores(-VELOCIDAD, VELOCIDAD); delay(GIRO90_MS); }
  if (giro == 2) { motores(VELOCIDAD, -VELOCIDAD); delay(2 * GIRO90_MS); }
  motores(VELOCIDAD, VELOCIDAD); delay(CELDA_MS);
  motores(0, 0);
}
#endif

void avanzarCarro() {
  // En la salida toma una "foto" de la mejor ruta conocida por el enjambre
  if (nRutaCarro == 0 || pasoCarro >= nRutaCarro - 1) {
    if (nRutaCarro && pasoCarro >= nRutaCarro - 1) {
      vueltas++;
      Serial.printf("[CARRO] Llegó a la meta. Vuelta %u en %d pasos\n", vueltas, nRutaCarro - 1);
      // vuelve a S (en el físico: se reubica manualmente o se recorre la ruta inversa)
    }
    if (colonia.nMejor < 2) { nRutaCarro = 0; return; }
    memcpy(rutaCarro, colonia.mejor, colonia.nMejor);
    nRutaCarro = colonia.nMejor;
    pasoCarro = 0;
    orientacion = 0;
    Serial.printf("[CARRO] Sale de S con ruta de %d pasos\n", nRutaCarro - 1);
    return;
  }
  int d = aco::Colonia::direccion(rutaCarro[pasoCarro], rutaCarro[pasoCarro + 1]);
#if USAR_MOTORES
  moverFisico(d);
#endif
  orientacion = d;
  pasoCarro++;
}

// ------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
#if USAR_MOTORES
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT); pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);
  motores(0, 0);
#endif
  delay(300);
  Serial.printf("\n=== Nodo ACO %d ===\n", NODO_ID);
  conectarRed();
  // Semilla distinta por nodo: cada colonia explora caminos diferentes
  colonia.begin(esp_random() ^ (NODO_ID * 2654435761u));
}

void loop() {
  uint32_t ahora = millis();

  if (!redLista()) {               // si se cae la conexión, reintentar
    WiFi.reconnect();
    delay(500);
    return;
  }
  recibirPaquetes();

  if (ahora - tIter >= ITER_MS) {
    tIter = ahora;
    int l = colonia.iterar();
    enviarFeromonas();
    Serial.printf("[ACO] it=%lu  mejor_iter=%d  mejor_global=%d  rx=%lu\n",
                  (unsigned long)colonia.iteracion, l, colonia.nMejor - 1, (unsigned long)paquetesRx);
  }
  if (ahora - tPaso >= PASO_MS) {
    tPaso = ahora;
    avanzarCarro();
  }
  if (ahora - tEstado >= ESTADO_MS) {
    tEstado = ahora;
    enviarEstado();
  }
  if (tLed && ahora - tLed > 40) {
    digitalWrite(LED_PIN, LOW);
    tLed = 0;
  }
}
