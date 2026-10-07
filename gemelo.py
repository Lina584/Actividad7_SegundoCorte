"""gemelo.py — Gemelo digital en PyBullet del enjambre de 3 carritos ACO.

Escucha por UDP (puerto 4211) lo que reportan los 3 ESP32 (o el emulador):
  - paquetes "ACOF" con la tabla de feromonas y la mejor ruta de cada nodo
  - paquetes JSON de estado con la celda actual de cada carrito
y replica el movimiento de los 3 carritos en un almacén/laberinto 3D.

Vista:
  - --gui         abre la ventana nativa de PyBullet (uso local, sin Docker)
  - por defecto   modo DIRECT + visor web en http://localhost:8080 (ideal en Docker)
  - --gif X.gif   graba una animación de N segundos (--segundos) y termina

Uso con ESP32 reales: conectar el PC a la red ENJAMBRE_ACO y correr
    python gemelo.py --nodos 192.168.4.1,192.168.4.12,192.168.4.13
"""
import argparse
import io
import math
import socket
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pybullet as p
import pybullet_data
from PIL import Image, ImageDraw, ImageFont

import aco

CELDA = 0.5                     # metros por celda
COLORES = {1: (230, 57, 70), 2: (29, 120, 214), 3: (240, 180, 20)}
OFFSET = {1: (-0.09, 0.0), 2: (0.0, 0.0), 3: (0.09, 0.0)}   # para que no se encimen
ESCALA_CARRO = 0.95
W3D, H3D, W2D = 640, 480, 460


def celda_a_xy(f, c):
    x = (c - (aco.COLS - 1) / 2) * CELDA
    y = ((aco.FILAS - 1) / 2 - f) * CELDA
    return x, y


# ------------------------------------------------------------------ estado compartido
class Enjambre:
    def __init__(self):
        self.lock = threading.Lock()
        self.estado = {}      # id -> último JSON de estado
        self.tau = {}         # id -> lista de feromona cuantizada
        self.visto = {}       # id -> time.time() del último paquete
        self.paquetes = 0

    def recibir(self, datos):
        tipo, m = aco.leer_paquete(datos)
        if tipo is None or m.get("id") not in (1, 2, 3):
            return
        with self.lock:
            self.paquetes += 1
            self.visto[m["id"]] = time.time()
            if tipo == "F":
                self.tau[m["id"]] = m["tau"]
            else:
                self.estado[m["id"]] = m


def hilo_udp(enj, puerto, nodos):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", puerto))
    s.settimeout(0.5)
    ultimo_hola = 0
    while True:
        if nodos and time.time() - ultimo_hola > 2:   # registrarse en los ESP32
            ultimo_hola = time.time()
            for ip in nodos:
                try:
                    s.sendto(b"HOLA_GEMELO", (ip, aco.PUERTO_ENJAMBRE))
                except OSError:
                    pass
        try:
            datos, _ = s.recvfrom(4096)
            enj.recibir(datos)
        except socket.timeout:
            pass


# ------------------------------------------------------------------ mundo PyBullet
class Mundo:
    def __init__(self, gui):
        self.gui = gui
        p.connect(p.GUI if gui else p.DIRECT)
        p.setAdditionalSearchPath(pybullet_data.getDataPath())
        p.setGravity(0, 0, -9.81)
        if gui:
            p.configureDebugVisualizer(p.COV_ENABLE_GUI, 0)
            p.resetDebugVisualizerCamera(6.2, 0, -62, [0, -0.3, 0])
        p.loadURDF("plane.urdf")
        self._construir_laberinto()
        self.carros = {}
        self.pose = {}
        for i in (1, 2, 3):
            x, y = self._xy_carro(i, aco.INICIO)
            cid = p.loadURDF("racecar/racecar.urdf", [x, y, 0.02], globalScaling=ESCALA_CARRO)
            r, g, b = (v / 255 for v in COLORES[i])
            for j in range(-1, p.getNumJoints(cid)):
                p.changeVisualShape(cid, j, rgbaColor=[r, g, b, 1])
            self.carros[i] = cid
            self.pose[i] = [x, y, 0.0]
        self.lineas = {}
        self.rutas_dibujadas = {}

    def _caja(self, x, y, sx, sy, sz, rgba, z=None):
        col = p.createCollisionShape(p.GEOM_BOX, halfExtents=[sx / 2, sy / 2, sz / 2])
        vis = p.createVisualShape(p.GEOM_BOX, halfExtents=[sx / 2, sy / 2, sz / 2], rgbaColor=rgba)
        return p.createMultiBody(0, col, vis, [x, y, sz / 2 if z is None else z])

    def _construir_laberinto(self):
        for f in range(aco.FILAS):
            for c in range(aco.COLS):
                x, y = celda_a_xy(f, c)
                ch = aco.LAB[f][c]
                if ch == "#":
                    self._caja(x, y, CELDA, CELDA, 0.35, [0.35, 0.38, 0.45, 1])
                elif ch == "S":
                    self._caja(x, y, CELDA * 0.95, CELDA * 0.95, 0.01, [0.2, 0.75, 0.3, 1])
                elif ch == "G":   # meta tipo bandera a cuadros
                    for a in range(4):
                        for b in range(4):
                            neg = (a + b) % 2 == 0
                            self._caja(x - CELDA * 3 / 8 + a * CELDA / 4, y - CELDA * 3 / 8 + b * CELDA / 4,
                                       CELDA / 4, CELDA / 4, 0.012,
                                       [0.05, 0.05, 0.05, 1] if neg else [0.95, 0.95, 0.95, 1])
                    for s in (-1, 1):
                        self._caja(x + s * CELDA * 0.42, y, 0.03, 0.03, 0.5, [0.8, 0.1, 0.1, 1])
                    self._caja(x, y, CELDA * 0.84, 0.02, 0.08, [0.9, 0.2, 0.2, 1], z=0.46)

    def _xy_carro(self, i, celda):
        x, y = celda_a_xy(*aco.fc(celda))
        return x + OFFSET[i][0], y + OFFSET[i][1]

    def actualizar(self, enj, dt):
        with enj.lock:
            estados = dict(enj.estado)
        for i, cid in self.carros.items():
            e = estados.get(i)
            if not e:
                continue
            tx, ty = celda_a_xy(*e["celda"])
            tx, ty = tx + OFFSET[i][0], ty + OFFSET[i][1]
            x, y, yaw = self.pose[i]
            k = min(1.0, dt * 6.0)               # suavizado (el ESP32 avanza a saltos de celda)
            nx, ny = x + (tx - x) * k, y + (ty - y) * k
            sf, sc = e["sig"]
            if [sf, sc] != e["celda"]:
                gx, gy = celda_a_xy(sf, sc)
                obj = math.atan2(gy - celda_a_xy(*e["celda"])[1], gx - celda_a_xy(*e["celda"])[0])
                dyaw = (obj - yaw + math.pi) % (2 * math.pi) - math.pi
                yaw += dyaw * min(1.0, dt * 8.0)
            self.pose[i] = [nx, ny, yaw]
            # el origen del racecar está en el eje trasero: centrarlo en la celda
            cx = nx - 0.18 * ESCALA_CARRO * math.cos(yaw)
            cy = ny - 0.18 * ESCALA_CARRO * math.sin(yaw)
            p.resetBasePositionAndOrientation(cid, [cx, cy, 0.02], p.getQuaternionFromEuler([0, 0, yaw]))
            if self.gui:
                self._dibujar_ruta_gui(i, e.get("ruta", []))
        p.stepSimulation()

    def _dibujar_ruta_gui(self, i, ruta):
        if self.rutas_dibujadas.get(i) == ruta:
            return
        for l in self.lineas.get(i, []):
            p.removeUserDebugItem(l)
        col = [v / 255 for v in COLORES[i]]
        ids = []
        for a, b in zip(ruta, ruta[1:]):
            xa, ya = self._xy_carro(i, a)
            xb, yb = self._xy_carro(i, b)
            ids.append(p.addUserDebugLine([xa, ya, 0.03 + 0.01 * i], [xb, yb, 0.03 + 0.01 * i], col, 4))
        self.lineas[i], self.rutas_dibujadas[i] = ids, list(ruta)

    def camara(self):
        vista = p.computeViewMatrixFromYawPitchRoll([0, -0.25, 0], 6.4, 0, -60, 0, 2)
        proy = p.computeProjectionMatrixFOV(50, W3D / H3D, 0.1, 30)
        _, _, rgba, _, _ = p.getCameraImage(W3D, H3D, vista, proy, renderer=p.ER_TINY_RENDERER,
                                            lightDirection=[2, -3, 6], shadow=1)
        return Image.frombytes("RGBA", (W3D, H3D), bytes(bytearray(rgba))).convert("RGB")


# ------------------------------------------------------------------ panel 2D (feromonas)
def fuente(t):
    for nombre in ("DejaVuSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(nombre, t)
        except OSError:
            pass
    return ImageFont.load_default()


F_TIT, F_TXT = fuente(17), fuente(13)


def panel(enj):
    img = Image.new("RGB", (W2D, H3D), (250, 250, 247))
    d = ImageDraw.Draw(img)
    with enj.lock:
        taus = {k: list(v) for k, v in enj.tau.items()}
        est = dict(enj.estado)
        visto = dict(enj.visto)
        npk = enj.paquetes
    d.text((14, 10), "Feromona compartida (ACO)", fill=(20, 20, 30), font=F_TIT)
    tam = min((W2D - 28) // aco.COLS, 250 // aco.FILAS)
    ox, oy = 14, 40
    # feromona media de los nodos: arista más fuerte que sale de cada celda
    nivel = [0.0] * aco.N_CELDAS
    if taus:
        for t in taus.values():
            for i in range(aco.N_CELDAS):
                nivel[i] += max(t[i * 4:i * 4 + 4]) / len(taus)
    libres = [nivel[aco.idx(f, c)] for f in range(aco.FILAS) for c in range(aco.COLS) if aco.LAB[f][c] != "#"]
    mn, mx = min(libres), max(libres)
    for f in range(aco.FILAS):
        for c in range(aco.COLS):
            x0, y0 = ox + c * tam, oy + f * tam
            ch = aco.LAB[f][c]
            if ch == "#":
                col = (70, 76, 92)
            else:
                v = (nivel[aco.idx(f, c)] - mn) / (mx - mn) if mx > mn else 0.0
                col = (int(252 - 20 * v), int(248 - 150 * v), int(240 - 215 * v))  # crema -> naranja
            d.rectangle([x0, y0, x0 + tam - 1, y0 + tam - 1], fill=col)
            if ch in "SG":
                d.text((x0 + tam // 3, y0 + tam // 5), "A" if ch == "S" else "M", fill=(0, 0, 0), font=F_TXT)
    for i in (1, 2, 3):
        e = est.get(i)
        if not e or len(e.get("ruta", [])) < 2:
            continue
        o = (i - 2) * 3
        pts = [(ox + aco.fc(c)[1] * tam + tam // 2 + o, oy + aco.fc(c)[0] * tam + tam // 2 + o) for c in e["ruta"]]
        d.line(pts, fill=COLORES[i], width=3)
        f, c = e["celda"]
        cx, cy = ox + c * tam + tam // 2 + o, oy + f * tam + tam // 2 + o
        d.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=COLORES[i], outline=(0, 0, 0))
    y = oy + aco.FILAS * tam + 14
    d.text((14, y), "Nodo   iter   mejor   vueltas   rx   estado", fill=(60, 60, 70), font=F_TXT)
    for i in (1, 2, 3):
        y += 22
        e = est.get(i, {})
        vivo = i in visto and time.time() - visto[i] < 3
        d.rectangle([14, y + 2, 26, y + 14], fill=COLORES[i])
        txt = (f"  {i}     {e.get('it', '-'):>4}   {e.get('mejor', '-'):>4}      {e.get('vueltas', '-'):>3}"
               f"    {e.get('rx', '-'):>4}   {'en línea' if vivo else 'sin datos'}")
        d.text((30, y), txt, fill=(20, 20, 30) if vivo else (160, 60, 60), font=F_TXT)
    d.text((14, H3D - 24), f"paquetes UDP recibidos: {npk}", fill=(110, 110, 120), font=F_TXT)
    return img


def componer(mundo, enj):
    lienzo = Image.new("RGB", (W3D + W2D, H3D), (255, 255, 255))
    lienzo.paste(mundo.camara(), (0, 0))
    lienzo.paste(panel(enj), (W3D, 0))
    ImageDraw.Draw(lienzo).text((12, 10), "Gemelo digital PyBullet - enjambre ACO", fill=(255, 255, 255), font=F_TIT)
    return lienzo


# ------------------------------------------------------------------ visor web (MJPEG)
ULTIMO_JPEG = {"b": b""}
HTML = b"""<!doctype html><html><head><meta charset="utf-8"><title>Gemelo ACO</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{margin:0;background:#1d2230;color:#eee;font-family:sans-serif;text-align:center}
img{max-width:100%;margin-top:12px;border-radius:8px}</style></head>
<body><h3>Enjambre de 3 carritos ACO &mdash; gemelo digital PyBullet</h3>
<img src="/stream"></body></html>"""


class Visor(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path == "/stream":
            self.send_response(200)
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
            self.end_headers()
            try:
                while True:
                    b = ULTIMO_JPEG["b"]
                    if b:
                        self.wfile.write(b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: "
                                         + str(len(b)).encode() + b"\r\n\r\n" + b + b"\r\n")
                    time.sleep(0.1)
            except (BrokenPipeError, ConnectionResetError):
                return
        body = HTML if self.path in ("/", "/index.html") else ULTIMO_JPEG["b"]
        self.send_response(200)
        self.send_header("Content-Type", "text/html" if body is HTML else "image/jpeg")
        self.end_headers()
        self.wfile.write(body)


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gui", action="store_true", help="ventana nativa de PyBullet")
    ap.add_argument("--puerto", type=int, default=aco.PUERTO_GEMELO)
    ap.add_argument("--web", type=int, default=8080, help="puerto del visor web (0 = apagado)")
    ap.add_argument("--nodos", default="192.168.4.1,192.168.4.12,192.168.4.13",
                    help="IPs de los ESP32 a los que se envía HOLA_GEMELO ('' = ninguno)")
    ap.add_argument("--gif", default="", help="grabar un GIF en esta ruta y salir")
    ap.add_argument("--segundos", type=float, default=25)
    a = ap.parse_args()

    enj = Enjambre()
    nodos = [ip.strip() for ip in a.nodos.split(",") if ip.strip()]
    threading.Thread(target=hilo_udp, args=(enj, a.puerto, nodos), daemon=True).start()
    if a.web:
        threading.Thread(target=ThreadingHTTPServer(("0.0.0.0", a.web), Visor).serve_forever,
                         daemon=True).start()
        print(f"Visor web en http://localhost:{a.web}")
    mundo = Mundo(a.gui)
    print(f"Gemelo escuchando UDP en el puerto {a.puerto} ...")

    cuadros, t0, t_ant, t_img = [], time.time(), time.time(), 0
    while True:
        ahora = time.time()
        mundo.actualizar(enj, ahora - t_ant)
        t_ant = ahora
        if a.web or a.gif:
            if ahora - t_img >= 0.12:
                t_img = ahora
                img = componer(mundo, enj)
                if a.web:
                    buf = io.BytesIO()
                    img.save(buf, "JPEG", quality=80)
                    ULTIMO_JPEG["b"] = buf.getvalue()
                if a.gif:
                    cuadros.append(img.resize((img.width * 3 // 4, img.height * 3 // 4)))
        if a.gif and ahora - t0 > a.segundos:
            cuadros[0].save(a.gif, save_all=True, append_images=cuadros[1::2], duration=240, loop=0,
                            optimize=True)
            cuadros[-1].save(a.gif.rsplit(".", 1)[0] + "_final.png")
            print(f"GIF guardado en {a.gif} ({len(cuadros[1::2]) + 1} cuadros)")
            return
        time.sleep(1 / 60 if a.gui else 0.02)


if __name__ == "__main__":
    main()
