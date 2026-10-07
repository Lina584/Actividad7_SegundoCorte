"""emulador_nodos.py — 3 "ESP32 virtuales" que hacen exactamente lo mismo que
nodo_aco.ino (ACO + intercambio de feromonas por UDP + carrito + estado al gemelo).

Sirve para probar y grabar el gemelo digital sin tener los ESP32 a la mano.

    python emulador_nodos.py --gemelo 127.0.0.1
"""
import argparse
import json
import socket
import threading
import time

import aco

ITER_MS, PASO_MS, ESTADO_MS = 1200, 450, 200   # mismos tiempos del firmware


class NodoVirtual(threading.Thread):
    def __init__(self, nodo_id, puertos, gemelo, escala):
        super().__init__(daemon=True)
        self.id, self.puertos, self.gemelo, self.k = nodo_id, puertos, gemelo, escala
        self.col = aco.Colonia(semilla=1000 * nodo_id + int(time.time()))
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", puertos[nodo_id - 1]))
        self.sock.setblocking(False)
        self.ruta, self.paso, self.vueltas, self.rx = [], 0, 0, 0

    def enviar_feromonas(self):
        pkt = aco.armar_feromonas(self.id, self.col.iteracion, self.col.mejor, self.col.exportar_tau())
        for i, p in enumerate(self.puertos):
            if i != self.id - 1:
                self.sock.sendto(pkt, ("127.0.0.1", p))
        self.sock.sendto(pkt, self.gemelo)

    def recibir(self):
        while True:
            try:
                datos, _ = self.sock.recvfrom(2048)
            except BlockingIOError:
                return
            tipo, m = aco.leer_paquete(datos)
            if tipo == "F" and m["id"] != self.id:
                self.col.mezclar(m["tau"], m["ruta"])
                self.rx += 1

    def avanzar(self):
        if not self.ruta or self.paso >= len(self.ruta) - 1:
            if self.ruta:
                self.vueltas += 1
            if len(self.col.mejor) < 2:
                self.ruta = []
                return
            self.ruta, self.paso = list(self.col.mejor), 0
            return
        self.paso += 1

    def enviar_estado(self):
        celda = self.ruta[self.paso] if self.ruta else aco.INICIO
        sig = self.ruta[self.paso + 1] if self.ruta and self.paso + 1 < len(self.ruta) else celda
        msg = {"t": "E", "id": self.id, "it": self.col.iteracion,
               "mejor": len(self.col.mejor) - 1 if self.col.mejor else -1,
               "celda": list(aco.fc(celda)), "sig": list(aco.fc(sig)),
               "vueltas": self.vueltas, "rx": self.rx, "ms": int(time.time() * 1000),
               "ruta": self.ruta}
        self.sock.sendto(json.dumps(msg).encode(), self.gemelo)

    def run(self):
        t0 = time.time()
        ti = tp = te = -1e9
        while True:
            ahora = (time.time() - t0) * 1000 * self.k
            self.recibir()
            if ahora - ti >= ITER_MS:
                ti = ahora
                l = self.col.iterar()
                self.enviar_feromonas()
                print(f"[nodo {self.id}] it={self.col.iteracion:3d} mejor_iter={l:3d} "
                      f"mejor_global={len(self.col.mejor) - 1:3d} rx={self.rx}", flush=True)
            if ahora - tp >= PASO_MS:
                tp = ahora
                self.avanzar()
            if ahora - te >= ESTADO_MS:
                te = ahora
                self.enviar_estado()
            time.sleep(0.01)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gemelo", default="127.0.0.1", help="host del gemelo digital")
    ap.add_argument("--puerto-gemelo", type=int, default=aco.PUERTO_GEMELO)
    ap.add_argument("--escala", type=float, default=1.0, help=">1 acelera el tiempo")
    a = ap.parse_args()
    gemelo = (socket.gethostbyname(a.gemelo), a.puerto_gemelo)
    puertos = [5001, 5002, 5003]
    for i in (1, 2, 3):
        NodoVirtual(i, puertos, gemelo, a.escala).start()
        time.sleep(0.15)
    print(f"3 nodos virtuales enviando al gemelo en {gemelo}. Ctrl+C para salir.")
    while True:
        time.sleep(1)


if __name__ == "__main__":
    main()
