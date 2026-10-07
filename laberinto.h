// laberinto.h — Mapa del almacén/laberinto (compartido por ESP32 y gemelo digital)
// '#' = pared, '.' = libre, 'S' = salida (punto A), 'G' = meta
// El gemelo digital (gemelo/aco.py) lee ESTE MISMO archivo, así el mundo
// físico y el virtual siempre usan el mismo mapa.
#pragma once

#define LAB_FILAS    11
#define LAB_COLUMNAS 13

static const char* const LABERINTO[LAB_FILAS] = {
  "#############",
  "#S....#.....#",
  "#.###.#.###.#",
  "#.#.....#...#",
  "#.#.###.#.###",
  "#...#...#...#",
  "###.#.#####.#",
  "#...#.....#.#",
  "#.#####.#.#.#",
  "#.......#..G#",
  "#############",
};
