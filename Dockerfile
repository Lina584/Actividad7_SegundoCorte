# Gemelo digital PyBullet del enjambre ACO
# Python 3.11: pybullet trae rueda precompilada (no hay que compilar Bullet)
FROM python:3.11-slim

RUN apt-get update && apt-get install -y --no-install-recommends fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY gemelo/requirements.txt gemelo/requirements.txt
RUN pip install --no-cache-dir -r gemelo/requirements.txt

# el gemelo lee el mismo laberinto que los ESP32
COPY firmware/nodo_aco/laberinto.h firmware/nodo_aco/laberinto.h
COPY gemelo/ gemelo/
WORKDIR /app/gemelo

ENV PYTHONUNBUFFERED=1
EXPOSE 8080 4211/udp
CMD ["python", "gemelo.py"]
