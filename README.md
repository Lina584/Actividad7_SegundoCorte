# Enjambre de 3 carritos con algoritmo de hormigas (ACO) en ESP32 y gemelo digital en PyBullet
 

## Sobre la entrega
 
**Este proyecto no lo implementé en físico.** No tenía disponibles los 3 ESP32 ni los carritos, así que dejé el código de los ESP32 completo y listo para cargar, y probé todo el funcionamiento en el computador:
 
- Los 3 ESP32 se simularon con un programa que hace lo mismo que el código del ESP32 (`emulador_nodos.py`).
- También compilé el mismo código del ESP32 para que corriera en el PC, y así comprobar que los 3 nodos sí se comunican y encuentran la ruta.
- La parte virtual (PyBullet) sí está funcionando y es la que se ve en el GIF de arriba.
Por eso puede que al cargarlo en los ESP32 reales haya que hacer algunos ajustes.
 
## ¿De qué se trata?
 
La idea es que 3 carritos encuentren el camino más corto desde un punto A hasta una meta dentro de un laberinto. Para esto se usa el algoritmo de colonia de hormigas (ACO), que se inspira en cómo las hormigas encuentran comida: van dejando un rastro (feromona) y los caminos más cortos terminan con más rastro, entonces cada vez más hormigas los siguen.
 
El proyecto tiene tres partes:
 
**1. Mundo físico (ESP32):** cada ESP32 representa un carrito. Todos tienen el mismo código, solo cambia el número del nodo (1, 2 o 3). Cada uno corre el algoritmo de hormigas por su cuenta.
 
**2. Red (ESP en modo AP):** el ESP32 número 1 crea una red Wi-Fi llamada `ENJAMBRE_ACO` y los otros dos se conectan a ella. Por esa red se mandan las feromonas entre ellos, así lo que aprende un carrito lo aprovechan los otros.
 
**3. Parte virtual (Docker + PyBullet):** en el computador corre una simulación en PyBullet con el laberinto y 3 carritos virtuales que copian lo que hacen los ESP32. Todo esto va dentro de un contenedor de Docker y se ve desde el navegador.
 
## ¿Cómo funciona el algoritmo?
 
1. En cada ESP32 salen varias "hormigas" virtuales desde el punto A.
2. Cada hormiga va escogiendo hacia dónde moverse. Prefiere los caminos con más feromona y los que la acercan a la meta. Si se encuentra un callejón sin salida, se devuelve.
3. Cuando llegan a la meta, dejan feromona en el camino que hicieron. Si el camino fue corto, dejan más.
4. Con el tiempo la feromona se va evaporando, así los caminos malos se olvidan.
5. Cada ESP32 le manda su feromona a los otros dos y la combinan con la suya.
6. El carrito sigue el mejor camino que conoce el grupo hasta el momento.
Después de unas pocas vueltas los 3 carritos terminan usando el camino más corto, que en este laberinto es de 22 pasos.
 
## Resultados
 
Comparé qué pasa cuando los 3 nodos comparten la feromona y cuando cada uno trabaja solo, haciendo 50 pruebas de cada caso:
 
| Caso | Iteraciones promedio para encontrar el camino más corto |
|---|---|
| Compartiendo feromona (enjambre) | 1.2 |
| Cada uno por separado | 4.4 |
 
Cuando se comparten las feromonas, encuentran el camino mucho más rápido, que es justamente la ventaja de trabajar en enjambre.
 
En la simulación (el GIF) se ve a la derecha el laberinto con la feromona: las casillas más naranjas son las que más feromona tienen, y se nota cómo se concentra en el camino más corto.
 
## Archivos
 
| Carpeta | Contenido |
|---|---|
| `firmware/nodo_aco/` | Código de los ESP32 (`nodo_aco.ino`), el algoritmo (`aco_core.h`) y el mapa del laberinto (`laberinto.h`) |
| `gemelo/` | Simulación en PyBullet (`gemelo.py`) y el emulador de los 3 ESP32 (`emulador_nodos.py`) |
| `tests/` | Pruebas del algoritmo en el computador |
| `Dockerfile`, `docker-compose.yml` | Para correr la simulación en Docker |
| `docs/` | GIF de la simulación |
 
## Cómo correrlo
 
**Sin ESP32 (como lo probé yo):**
 
```bash
docker compose --profile emulador up --build
```
 
Luego se abre `http://localhost:8080` en el navegador.
 
**Con los ESP32 reales:**
 
1. En Arduino IDE instalar las placas ESP32 de Espressif.
2. Abrir `firmware/nodo_aco/nodo_aco.ino`, poner `#define NODO_ID 1` y cargarlo en el primer ESP32. Repetir con 2 y 3 en los otros.
3. Conectar el computador a la red `ENJAMBRE_ACO` (clave `hormigas123`).
4. Correr `docker compose up --build gemelo` y abrir `http://localhost:8080`.
## Conclusiones
 
- El algoritmo de hormigas sí logra encontrar el camino más corto en el laberinto.
- Compartir la feromona entre los nodos hace que el grupo aprenda más rápido que si cada uno trabaja solo.
- La simulación en PyBullet sirve para ver lo que pasa sin necesidad de tener los carritos armados.
- Como no lo implementé en físico, faltaría probarlo en los ESP32 reales y, si se usan carritos con motores, calibrar los giros y el avance.

 ## Autora 

 Lina María Moreno Ospina 

 Ingeniería Mecatrónica

 7004589
