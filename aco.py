"""aco.py — Laberinto + protocolo + ACO en Python.

- Lee el MISMO laberinto que usan los ESP32 (firmware/nodo_aco/laberinto.h).
- Arma/decodifica los paquetes UDP del enjambre (mismo formato que el .ino).
- Trae una copia en Python del algoritmo de aco_core.h para el emulador de
  nodos (cuando no hay ESP32 físicos).
"""
import json
import os
import random
import re
import struct

AQUI = os.path.dirname(os.path.abspath(__file__))
RUTA_LAB = os.environ.get(
    "LABERINTO_H", os.path.join(AQUI, "..", "firmware", "nodo_aco", "laberinto.h"))

PUERTO_ENJAMBRE = 4210
PUERTO_GEMELO = 4211
DFIL = (0, 1, 0, -1)   # E, S, O, N
DCOL = (1, 0, -1, 0)


def cargar_laberinto(ruta=RUTA_LAB):
    with open(ruta, encoding="utf-8") as f:
        texto = f.read()
    cuerpo = texto[texto.index("LABERINTO["):]
    filas = re.findall(r'"([#.SG]+)"', cuerpo)
    assert filas and all(len(f) == len(filas[0]) for f in filas), "laberinto.h mal formado"
    return filas


LAB = cargar_laberinto()
FILAS, COLS = len(LAB), len(LAB[0])
N_CELDAS = FILAS * COLS


def idx(f, c):
    return f * COLS + c


def fc(i):
    return divmod(i, COLS)


def libre(f, c):
    return 0 <= f < FILAS and 0 <= c < COLS and LAB[f][c] != "#"


INICIO = next(idx(f, c) for f in range(FILAS) for c in range(COLS) if LAB[f][c] == "S")
META = next(idx(f, c) for f in range(FILAS) for c in range(COLS) if LAB[f][c] == "G")


def direccion(a, b):
    (fa, ca), (fb, cb) = fc(a), fc(b)
    for d in range(4):
        if (DFIL[d], DCOL[d]) == (fb - fa, cb - ca):
            return d
    return -1


# ------------------------------------------------------------------ protocolo
def armar_feromonas(nodo_id, iteracion, ruta, tau_q):
    return (b"ACOF" + struct.pack("<BIB", nodo_id, iteracion, len(ruta))
            + bytes(ruta) + bytes(tau_q))


def leer_paquete(datos):
    """Devuelve ('F', dict) para feromonas, ('E', dict) para estado o (None, None)."""
    if datos[:4] == b"ACOF" and len(datos) >= 10:
        nodo_id, it, n = struct.unpack_from("<BIB", datos, 4)
        if len(datos) != 10 + n + N_CELDAS * 4:
            return None, None
        return "F", {"id": nodo_id, "it": it, "ruta": list(datos[10:10 + n]),
                     "tau": list(datos[10 + n:])}
    try:
        msg = json.loads(datos.decode("utf-8"))
        if msg.get("t") == "E":
            return "E", msg
    except (UnicodeDecodeError, ValueError):
        pass
    return None, None


# ------------------------------------------------------------------ ACO
class Colonia:
    """Traducción directa de aco::Colonia (aco_core.h)."""

    def __init__(self, semilla, hormigas=3, alpha=1.0, beta=1.0, rho=0.10, Q=1.0,
                 elite=2.0, tau_min=0.05, tau_max=5.0, tau0=1.0, w_mezcla=0.5):
        self.rng = random.Random(semilla)
        self.hormigas, self.alpha, self.beta, self.rho = hormigas, alpha, beta, rho
        self.Q, self.elite, self.tau_min, self.tau_max, self.w = Q, elite, tau_min, tau_max, w_mezcla
        self.tau = [[tau0] * 4 for _ in range(N_CELDAS)]
        self.mejor, self.iteracion = [], 0

    def _eta(self, i):
        (f, c), (fm, cm) = fc(i), fc(META)
        return 1.0 / (1 + abs(f - fm) + abs(c - cm))

    def caminar(self):
        pila, visit = [INICIO], {INICIO}
        while pila[-1] != META:
            f, c = fc(pila[-1])
            opciones = []
            for d in range(4):
                nf, nc = f + DFIL[d], c + DCOL[d]
                if libre(nf, nc) and idx(nf, nc) not in visit:
                    peso = self.tau[pila[-1]][d] ** self.alpha * self._eta(idx(nf, nc)) ** self.beta
                    opciones.append((peso, idx(nf, nc)))
            if not opciones:
                pila.pop()           # callejón sin salida: retroceder
                continue
            r, sig = self.rng.random() * sum(p for p, _ in opciones), opciones[-1][1]
            for p, cel in opciones:
                r -= p
                if r <= 0:
                    sig = cel
                    break
            visit.add(sig)
            pila.append(sig)
        return pila

    def _depositar(self, ruta, cant):
        for a, b in zip(ruta, ruta[1:]):
            self.tau[a][direccion(a, b)] += cant

    def _limitar(self):
        for fila in self.tau:
            for d in range(4):
                fila[d] = min(self.tau_max, max(self.tau_min, fila[d]))

    def iterar(self):
        rutas = [self.caminar() for _ in range(self.hormigas)]
        for fila in self.tau:
            for d in range(4):
                fila[d] *= 1 - self.rho
        for r in rutas:
            self._depositar(r, self.Q / (len(r) - 1))
        mejor_it = min(rutas, key=len)
        if not self.mejor or len(mejor_it) < len(self.mejor):
            self.mejor = mejor_it
        self._depositar(self.mejor, self.elite * self.Q / (len(self.mejor) - 1))
        self._limitar()
        self.iteracion += 1
        return len(mejor_it) - 1

    def exportar_tau(self):
        return [min(255, max(0, int(t / self.tau_max * 255 + 0.5))) for fila in self.tau for t in fila]

    def mezclar(self, tau_q, ruta_ajena):
        for i in range(N_CELDAS):
            for d in range(4):
                otro = tau_q[i * 4 + d] * self.tau_max / 255.0
                self.tau[i][d] = (1 - self.w) * self.tau[i][d] + self.w * otro
        if len(ruta_ajena) > 1 and ruta_ajena[0] == INICIO and ruta_ajena[-1] == META and \
                (not self.mejor or len(ruta_ajena) < len(self.mejor)):
            self.mejor = list(ruta_ajena)
        self._limitar()
