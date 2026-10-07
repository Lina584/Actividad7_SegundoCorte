// aco_core.h — Optimización por Colonia de Hormigas (ACO) sobre el laberinto
//
// Núcleo independiente de la plataforma: compila igual en el ESP32 (Arduino)
// y en el PC (g++) para poder probarlo sin hardware (ver tests/test_aco_core.cpp).
//
// Modelo:
//  - Cada celda libre es un nodo del grafo; cada celda tiene 4 aristas dirigidas
//    (E, S, O, N). La feromona tau[celda][dir] vive en esas aristas.
//  - Una hormiga sale de S y elige el siguiente movimiento con probabilidad
//        p(dir) ∝ tau^alpha * eta^beta,   eta = 1 / (1 + distManhattan(siguiente, meta))
//    No repite celdas (lista tabú). Si queda encerrada en un callejón sin salida,
//    retrocede (backtracking), así toda hormiga llega a la meta y su ruta no tiene lazos.
//  - Al final de cada iteración: evaporación tau *= (1 - rho), depósito Q/L de
//    cada hormiga sobre su ruta, depósito elitista extra sobre la mejor ruta global,
//    y límites [tauMin, tauMax] (estilo MAX-MIN Ant System) para no estancarse.
//  - Cooperación del enjambre: cada nodo mezcla la feromona recibida de los otros
//    nodos (promedio ponderado) y adopta la mejor ruta ajena si es más corta.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "laberinto.h"

#define N_CELDAS (LAB_FILAS * LAB_COLUMNAS)
#define N_DIR    4
#define MAX_RUTA N_CELDAS          // ruta = lista de celdas (incluye S y G)
#define MAX_HORMIGAS 16

namespace aco {

// Direcciones: 0=Este, 1=Sur, 2=Oeste, 3=Norte
static const int8_t DFIL[N_DIR] = {0, 1, 0, -1};
static const int8_t DCOL[N_DIR] = {1, 0, -1, 0};

struct Parametros {
  int   hormigas = 3;     // hormigas por iteración (en cada ESP32)
  float alpha    = 1.0f;  // peso de la feromona
  float beta     = 1.0f;  // peso de la heurística (cercanía a la meta)
  float rho      = 0.10f; // tasa de evaporación
  float Q        = 1.0f;  // cantidad de feromona depositada (Q / L)
  float elite    = 2.0f;  // refuerzo extra sobre la mejor ruta global
  float tauMin   = 0.05f;
  float tauMax   = 5.0f;
  float tau0     = 1.0f;  // feromona inicial
  float wMezcla  = 0.5f;  // peso de la feromona recibida al mezclar
};

class Colonia {
 public:
  Parametros P;
  float    tau[N_CELDAS][N_DIR];
  uint8_t  mejor[MAX_RUTA];      // mejor ruta conocida (celdas)
  uint16_t nMejor = 0;           // # de celdas de la mejor ruta (0 = ninguna)
  uint8_t  ultima[MAX_RUTA];     // ruta de la mejor hormiga de la última iteración
  uint16_t nUltima = 0;
  uint32_t iteracion = 0;
  uint8_t  inicio = 0, meta = 0;

  void begin(uint32_t semilla) {
    rng_ = semilla ? semilla : 0x12345678u;
    for (int f = 0; f < LAB_FILAS; f++)
      for (int c = 0; c < LAB_COLUMNAS; c++) {
        if (LABERINTO[f][c] == 'S') inicio = idx(f, c);
        if (LABERINTO[f][c] == 'G') meta = idx(f, c);
      }
    for (int i = 0; i < N_CELDAS; i++)
      for (int d = 0; d < N_DIR; d++) tau[i][d] = P.tau0;
    nMejor = nUltima = 0;
    iteracion = 0;
  }

  // ---- utilidades de la cuadrícula ----
  static inline uint8_t idx(int f, int c) { return (uint8_t)(f * LAB_COLUMNAS + c); }
  static inline int fil(uint8_t i) { return i / LAB_COLUMNAS; }
  static inline int col(uint8_t i) { return i % LAB_COLUMNAS; }
  static inline bool libre(int f, int c) {
    return f >= 0 && f < LAB_FILAS && c >= 0 && c < LAB_COLUMNAS && LABERINTO[f][c] != '#';
  }
  static int direccion(uint8_t a, uint8_t b) {  // dir para ir de a a b vecinos
    int df = fil(b) - fil(a), dc = col(b) - col(a);
    for (int d = 0; d < N_DIR; d++) if (DFIL[d] == df && DCOL[d] == dc) return d;
    return -1;
  }
  int distMeta(uint8_t i) const {
    int a = fil(i) - fil(meta), b = col(i) - col(meta);
    return (a < 0 ? -a : a) + (b < 0 ? -b : b);
  }

  // ---- una iteración completa del algoritmo ----
  // Devuelve la longitud (pasos) de la mejor hormiga de esta iteración.
  int iterar() {
    int mejorIter = 0x7fff;
    int nh = P.hormigas > MAX_HORMIGAS ? MAX_HORMIGAS : P.hormigas;
    for (int h = 0; h < nh; h++) {
      uint16_t n = caminarHormiga(rutaTmp_[h]);
      largoTmp_[h] = n;
      if (n > 1 && (int)(n - 1) < mejorIter) {
        mejorIter = n - 1;
        memcpy(ultima, rutaTmp_[h], n);
        nUltima = n;
      }
    }
    // evaporación
    for (int i = 0; i < N_CELDAS; i++)
      for (int d = 0; d < N_DIR; d++) tau[i][d] *= (1.0f - P.rho);
    // depósito de cada hormiga
    for (int h = 0; h < nh; h++)
      if (largoTmp_[h] > 1) depositar(rutaTmp_[h], largoTmp_[h], P.Q / (largoTmp_[h] - 1));
    // actualizar mejor global + refuerzo elitista
    if (nUltima > 1 && (nMejor == 0 || nUltima < nMejor)) {
      memcpy(mejor, ultima, nUltima);
      nMejor = nUltima;
    }
    if (nMejor > 1) depositar(mejor, nMejor, P.elite * P.Q / (nMejor - 1));
    limitar();
    iteracion++;
    return mejorIter;
  }

  // ---- cooperación entre nodos (llamar al recibir un paquete de otro ESP32) ----
  void mezclar(const uint8_t* tauQ, const uint8_t* rutaAjena, uint16_t nAjena) {
    float w = P.wMezcla;
    for (int i = 0; i < N_CELDAS; i++)
      for (int d = 0; d < N_DIR; d++) {
        float otro = decodificar(tauQ[i * N_DIR + d]);
        tau[i][d] = (1.0f - w) * tau[i][d] + w * otro;
      }
    if (nAjena > 1 && rutaValida(rutaAjena, nAjena) && (nMejor == 0 || nAjena < nMejor)) {
      memcpy(mejor, rutaAjena, nAjena);
      nMejor = nAjena;
    }
    limitar();
  }

  // Feromona cuantizada a 1 byte por arista para enviarla por red (N_CELDAS*4 bytes)
  void exportarTau(uint8_t* out) const {
    for (int i = 0; i < N_CELDAS; i++)
      for (int d = 0; d < N_DIR; d++) out[i * N_DIR + d] = codificar(tau[i][d]);
  }
  uint8_t codificar(float t) const {
    float x = t / P.tauMax * 255.0f + 0.5f;
    if (x < 0) x = 0;
    if (x > 255) x = 255;
    return (uint8_t)x;
  }
  float decodificar(uint8_t q) const { return q * P.tauMax / 255.0f; }

  bool rutaValida(const uint8_t* r, uint16_t n) const {
    if (n < 2 || r[0] != inicio || r[n - 1] != meta) return false;
    for (uint16_t k = 0; k < n; k++) {
      if (r[k] >= N_CELDAS || !libre(fil(r[k]), col(r[k]))) return false;
      if (k && direccion(r[k - 1], r[k]) < 0) return false;
    }
    return true;
  }

 private:
  uint32_t rng_ = 1;
  uint8_t  rutaTmp_[MAX_HORMIGAS][MAX_RUTA];
  uint16_t largoTmp_[MAX_HORMIGAS];

  uint32_t aleatorio() {  // xorshift32: rápido y sin depender de la plataforma
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return rng_;
  }
  float uniforme() { return (aleatorio() & 0xFFFFFF) / 16777216.0f; }

  uint16_t caminarHormiga(uint8_t* pila) {
    uint8_t visitada[(N_CELDAS + 7) / 8];
    memset(visitada, 0, sizeof(visitada));
    uint16_t top = 0;
    pila[top++] = inicio;
    visitada[inicio >> 3] |= 1 << (inicio & 7);
    while (pila[top - 1] != meta) {
      uint8_t act = pila[top - 1];
      float peso[N_DIR], suma = 0;
      for (int d = 0; d < N_DIR; d++) {
        peso[d] = 0;
        int f = fil(act) + DFIL[d], c = col(act) + DCOL[d];
        if (!libre(f, c)) continue;
        uint8_t sig = idx(f, c);
        if (visitada[sig >> 3] & (1 << (sig & 7))) continue;
        float eta = 1.0f / (1.0f + distMeta(sig));
        peso[d] = powf(tau[act][d], P.alpha) * powf(eta, P.beta);
        suma += peso[d];
      }
      if (suma <= 0) {          // callejón sin salida: retroceder
        top--;
        if (top == 0) return 0; // no debería pasar si existe ruta
        continue;
      }
      float r = uniforme() * suma;
      int elegida = -1;
      for (int d = 0; d < N_DIR; d++) {
        if (peso[d] <= 0) continue;
        elegida = d;
        r -= peso[d];
        if (r <= 0) break;
      }
      uint8_t sig = idx(fil(act) + DFIL[elegida], col(act) + DCOL[elegida]);
      visitada[sig >> 3] |= 1 << (sig & 7);
      pila[top++] = sig;
    }
    return top;
  }

  void depositar(const uint8_t* r, uint16_t n, float cantidad) {
    for (uint16_t k = 0; k + 1 < n; k++) {
      int d = direccion(r[k], r[k + 1]);
      if (d >= 0) tau[r[k]][d] += cantidad;
    }
  }

  void limitar() {
    for (int i = 0; i < N_CELDAS; i++)
      for (int d = 0; d < N_DIR; d++) {
        if (tau[i][d] < P.tauMin) tau[i][d] = P.tauMin;
        if (tau[i][d] > P.tauMax) tau[i][d] = P.tauMax;
      }
  }
};

}  // namespace aco
