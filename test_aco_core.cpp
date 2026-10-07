// Prueba del núcleo ACO en el PC (sin ESP32):
//   g++ -O2 -I../firmware/nodo_aco test_aco_core.cpp -o test_aco && ./test_aco [hormigas] [beta]
// Compara 3 colonias que comparten feromona (enjambre) contra 3 colonias aisladas.
#include <cstdio>
#include <cstdlib>
#include "aco_core.h"

static int gHormigas = 3;
static float gBeta = 1.0f;

static int ejecutar(bool compartir, uint32_t semillaBase, int* iterConv) {
  static aco::Colonia c[3];
  uint8_t buf[N_CELDAS * N_DIR];
  for (int k = 0; k < 3; k++) {
    c[k].P.hormigas = gHormigas;
    c[k].P.beta = gBeta;
    c[k].begin(semillaBase + 101 * (k + 1));
  }
  *iterConv = -1;
  for (int it = 0; it < 60; it++) {
    for (int k = 0; k < 3; k++) c[k].iterar();
    if (compartir) {
      static uint8_t tq[3][N_CELDAS * N_DIR];
      for (int k = 0; k < 3; k++) c[k].exportarTau(tq[k]);
      for (int k = 0; k < 3; k++)
        for (int j = 0; j < 3; j++)
          if (j != k) c[k].mezclar(tq[j], c[j].mejor, c[j].nMejor);
    }
    bool todos = true;
    for (int k = 0; k < 3; k++) todos &= (c[k].nMejor - 1 == 22);
    if (todos && *iterConv < 0) *iterConv = it + 1;
  }
  (void)buf;
  int peor = 0;
  for (int k = 0; k < 3; k++) {
    if (!c[k].rutaValida(c[k].mejor, c[k].nMejor)) { printf("RUTA INVALIDA\n"); return -1; }
    if (c[k].nMejor - 1 > peor) peor = c[k].nMejor - 1;
  }
  return peor;
}

int main(int argc, char** argv) {
  if (argc > 1) gHormigas = atoi(argv[1]);
  if (argc > 2) gBeta = atof(argv[2]);
  printf("hormigas/iter = %d, beta = %.2f\n", gHormigas, gBeta);
  int okE = 0, okA = 0, sumE = 0, sumA = 0;
  const int N = 50;
  for (int s = 1; s <= N; s++) {
    int ie, ia;
    int le = ejecutar(true, s * 7919, &ie);
    int la = ejecutar(false, s * 7919, &ia);
    if (le < 0 || la < 0) return 1;
    if (le == 22) { okE++; sumE += ie; }
    if (la == 22) { okA++; sumA += ia; }
  }
  printf("Ruta óptima del laberinto: 22 pasos\n");
  printf("Enjambre (comparte feromona): %d/%d corridas convergen, iter. promedio = %.1f\n",
         okE, N, okE ? (float)sumE / okE : 0.f);
  printf("Colonias aisladas          : %d/%d corridas convergen, iter. promedio = %.1f\n",
         okA, N, okA ? (float)sumA / okA : 0.f);
  return okE == N ? 0 : 2;
}
