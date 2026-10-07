#!/usr/bin/env bash
# Corre los 3 nodos del firmware (nodo_aco.ino) como procesos Linux y el gemelo
# digital conectado a ellos. Requiere g++ y las dependencias de gemelo/.
#   bash tests/probar_firmware_en_pc.sh            -> visor web en http://localhost:8080
#   bash tests/probar_firmware_en_pc.sh --gui      -> ventana de PyBullet
set -e
cd "$(dirname "$0")"
mkdir -p build
for id in 1 2 3; do
  g++ -O2 -std=c++17 -DNODO_ID=$id -Ihost_stub -I../firmware/nodo_aco firmware_en_pc.cpp -o build/nodo$id
done
trap 'kill $(jobs -p) 2>/dev/null' EXIT
for id in 1 2 3; do ./build/nodo$id | sed "s/^/[ESP32-$id] /" & done
cd ../gemelo
python gemelo.py --nodos 127.0.4.1,127.0.4.12,127.0.4.13 "$@"
