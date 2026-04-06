# COROSIO - Transferencia de ficheros y benchmarking en C++23

Este proyecto implementa una base de trabajo en **C++23** para estudiar transferencia de ficheros sobre **Corosio**, con foco en:

- implementación funcional de **servidor TCP**;
- implementación funcional de **cliente TCP**;
- soporte para **múltiples clientes concurrentes**;
- compilación en **modo Release** con **CMake**;
- compilación y comparación con **G++** y **Clang++**;
- benchmarking con **Google Benchmark**;
- campañas concurrentes con varios procesos `bench_tcp`;
- medición energética mediante **RAPL/powercap**.

La idea del proyecto es construir primero una versión funcional, limpia y reproducible y, a partir de ahí, realizar campañas de medición de:

- tiempo;
- escalabilidad;
- throughput;
- consumo energético;
- media;
- mediana;
- percentiles;
- máximos y mínimos.

Además de comparar distintas tecnologías de transporte, esta versión incorpora también la comparación entre ejecutables compilados con **G++** y ejecutables compilados con **Clang++**, con especial interés en el posible impacto del compilador sobre la optimización de corrutinas.

---

## Objetivo del proyecto

El objetivo actual del proyecto es disponer de una implementación propia con **Corosio** que sirva como base experimental para estudiar:

- transferencia de ficheros;
- concurrencia con múltiples clientes;
- benchmarking de rendimiento;
- medición de consumo energético;
- diferencias de comportamiento entre binarios compilados con **G++** y **Clang++**.

En esta fase se está trabajando con **Corosio** como librería de networking basada en corrutinas, manteniendo una estructura lo más comparable posible con la versión equivalente basada en BSD sockets y ampliando el análisis a una segunda dimensión experimental: el compilador empleado.

---

## Estructura del proyecto

```text
corosio/
├── benchmarks/
│   ├── CMakeLists.txt
│   └── bench_tcp.cpp
├── build/
├── results/
│   ├── macro_bench_results.json
│   └── micro_*.json
├── scripts/
│   └── run_bench.py
├── tcpclient/
│   ├── CMakeLists.txt
│   └── client.cpp
├── tcpserver/
│   ├── CMakeLists.txt
│   └── server.cpp
├── build_release.sh
└── CMakeLists.txt
```

---

## Componentes principales

### 1. Servidor TCP

El servidor:

- recibe por línea de comandos la ruta del fichero que debe servir;
- carga el fichero completo en memoria;
- construye un paquete con metadatos y contenido;
- escucha conexiones TCP;
- acepta múltiples clientes;
- envía el mismo fichero a todos los clientes conectados;
- trabaja con salida mínima por pantalla para no introducir ruido en las mediciones.

La concurrencia se gestiona con **Corosio**, empleando un modelo basado en **corrutinas asíncronas** y operaciones de E/S no bloqueantes sobre sockets TCP.

### 2. Cliente TCP

El cliente:

- se conecta al servidor indicando IP y puerto;
- recibe la cabecera del protocolo;
- obtiene nombre y tamaño del fichero;
- descarga el contenido;
- lo guarda en disco;
- también minimiza la salida por pantalla para mantener estabilidad en pruebas.

La descarga se realiza con operaciones asíncronas de lectura sobre **Corosio**, manteniendo una estructura sencilla y comparable con otras implementaciones.

### 3. Benchmark TCP

El benchmark:

- actúa como cliente de pruebas;
- se conecta al servidor real;
- descarga el fichero servido;
- mide el tiempo de descarga usando **Google Benchmark**;
- genera salida en consola y también en **JSON**.

Se utiliza como unidad básica de prueba. Las campañas concurrentes se construyen lanzando varios procesos `bench_tcp` en paralelo desde el script de automatización.

### 4. Script de automatización

El script en Python:

- arranca el servidor;
- ejecuta campañas de benchmark;
- puede lanzar uno o varios procesos `bench_tcp` en paralelo;
- mide energía antes y después de cada campaña;
- valida y procesa los JSON generados;
- guarda resultados combinados en JSON.

En la evolución actual del proyecto, este flujo está pensado para extenderse también a campañas que distingan explícitamente entre binarios generados con **G++** y con **Clang++**.

---

## Protocolo de transferencia

El servidor y el cliente siguen este protocolo:

```text
[u32 tam_nombre][nombre][u64 tam_fichero][contenido]
```

### Significado

- `u32 tam_nombre`: tamaño del nombre del fichero en bytes;
- `nombre`: nombre del fichero;
- `u64 tam_fichero`: tamaño del fichero en bytes;
- `contenido`: datos binarios del fichero.

Este formato permite que el cliente sepa exactamente:

- cuánto ocupa el nombre;
- cuánto ocupa el fichero;
- cuántos bytes debe recibir.

---

## Requisitos del entorno

### Sistema

- Ubuntu 24.04 LTS
- Kernel Linux con soporte `powercap`
- CPU de referencia: AMD Ryzen 7 7700
- RAM de referencia: 32 GB DDR5 6000 MT/s

### Compiladores

Se trabaja con dos compiladores en **C++23**:

- **G++**
- **Clang++**

La intención experimental es poder compilar el mismo código con ambos y comparar resultados de rendimiento, escalabilidad y consumo.

Ejemplo de configuración con GCC mediante `update-alternatives`:

```bash
sudo update-alternatives --install /usr/bin/g++ g++ /usr/local/gcc-14.1.0/bin/g++-14.1.0 14
sudo update-alternatives --install /usr/bin/gcc gcc /usr/local/gcc-14.1.0/bin/gcc-14.1.0 14
```

Ejemplo de instalación de Clang:

```bash
sudo apt update
sudo apt install clang
```

### CMake

```bash
sudo apt update
sudo apt install cmake
```

### Google Benchmark

```bash
sudo apt install libbenchmark-dev
```

### Python

```bash
sudo apt install python3 python3-pip
```

---

## Dependencias de Corosio

El proyecto usa **Corosio** como librería principal de networking y se integra mediante **CMake**. La base experimental se mantiene en **C++23**.

La resolución de dependencias se realiza desde el propio `CMakeLists.txt` del proyecto, de forma que no es necesario instalar manualmente Corosio como paquete del sistema si se utiliza `FetchContent`.

De forma general, la compilación de esta versión requiere:

- soporte de **corrutinas de C++23** en el compilador;
- **CMake**;
- **Google Benchmark** para los benchmarks;
- acceso a las dependencias que `CMake` descargará para Corosio.

---

## Compilación del proyecto

El proyecto está preparado para compilarse en **Release**.

### Script recomendado

```bash
sudo ./build_release.sh
```

### Compilación con distintos compiladores

La línea experimental actual contempla compilar y probar esta implementación tanto con **G++** como con **Clang++**.

El objetivo es generar dos conjuntos de ejecutables comparables:

- una versión compilada con **G++**;
- una versión compilada con **Clang++**.

De este modo, cada campaña podrá repetirse con ambos compiladores sin cambiar el código fuente, aislando el efecto del compilador dentro del estudio.

---

## Tipos de prueba

### Prueba individual

Consiste en ejecutar un único proceso `bench_tcp` para medir de forma controlada la descarga completa del fichero servido por el servidor.

### Campaña concurrente

Consiste en lanzar varios procesos `bench_tcp` en paralelo, simulando múltiples clientes atacando al mismo fichero al mismo tiempo.

Ejemplos de campañas posibles:

- 1 proceso `bench_tcp`
- 2 procesos `bench_tcp`
- 4 procesos `bench_tcp`
- 8 procesos `bench_tcp`
- 16 procesos `bench_tcp`

Este enfoque permite medir:

- tiempo total de campaña;
- consumo energético;
- comportamiento bajo concurrencia;
- escalabilidad del servidor.

### Campaña por compilador

Además de la dimensión de concurrencia, las pruebas pueden repetirse para dos familias de binarios:

- binarios compilados con **G++**;
- binarios compilados con **Clang++**.

Esto permite observar si existen diferencias relevantes en:

- coste temporal de las corrutinas;
- throughput;
- escalabilidad;
- eficiencia energética;
- estabilidad de las mediciones.

---

## Ejecución

### Compilar

```bash
sudo ./build_release.sh
```

### Lanzar el servidor manualmente

```bash
./build/tcpserver/tcpserver ./archivo.txt 8080
```

### Lanzar una prueba individual

```bash
./build/benchmarks/bench_tcp   --benchmark_min_time=1s   --benchmark_repetitions=1   --benchmark_out=results/micro.json   --benchmark_out_format=json
```

### Lanzar la campaña automática

```bash
sudo python3 scripts/run_bench.py
```

o:

```bash
sudo ./scripts/run_bench.py
```

---

## Resultados generados

En las pruebas individuales, Google Benchmark produce resultados con:

- tiempo real;
- tiempo de CPU;
- número de iteraciones;
- repeticiones;
- agregados como media, mediana, desviación estándar y coeficiente de variación.

En las campañas concurrentes, el script añade además:

- tiempo total de campaña;
- energía consumida;
- número de procesos lanzados;
- iteraciones válidas;
- bytes transferidos;
- throughput agregado.

En la extensión por compilador, los resultados deberán poder distinguir también:

- compilador utilizado;
- binario ejecutado;
- comparabilidad entre resultados equivalentes generados con **G++** y **Clang++**.

---

## Script de automatización

### Ruta

```text
scripts/run_bench.py
```

### Qué hace

El script:

1. arranca el servidor;
2. espera a que quede realmente listo;
3. mide energía inicial;
4. lanza una campaña de benchmark;
5. mide energía final;
6. valida y procesa los ficheros `micro_*.json`;
7. genera `results/macro_bench_results.json`.

En su evolución natural dentro de este proyecto, el script podrá ejecutarse también separando campañas por compilador para comparar de forma sistemática los binarios generados con **G++** y **Clang++**.

---

## Ajustes recomendados para medir bien

### Fijar governor

```bash
sudo cpupower frequency-set -g performance
```

### Otras recomendaciones

- compilar siempre en `Release`;
- evitar `Debug`;
- repetir el mismo caso con **G++** y **Clang++** bajo las mismas condiciones;
- cerrar programas pesados;
- repetir experimentos varias veces;
- controlar temperatura inicial;
- evitar ruido térmico y de frecuencia.

---

## Ficheros de salida

### `results/micro_*.json`

Contienen la salida de Google Benchmark en JSON para cada proceso `bench_tcp` ejecutado durante la campaña.

### `results/macro_bench_results.json`

Contiene un resumen procesado con:

- tiempo total de campaña;
- energía total consumida;
- procesos válidos y fallidos;
- iteraciones válidas;
- bytes transferidos;
- throughput agregado.

En la fase comparativa por compilador, conviene que estos resultados puedan asociarse también al compilador con el que se generó cada ejecutable.

---

## Notas de implementación

Esta versión sustituye la multiplexación manual con `poll()` por un modelo basado en **corrutinas** con **Corosio**, manteniendo:

- el mismo protocolo de transferencia;
- una organización del proyecto equivalente;
- una implementación con salida mínima;
- la comparabilidad experimental con otras versiones del mismo trabajo.

Además, se incorpora una nueva dimensión experimental basada en el compilador utilizado, de modo que la misma implementación pueda analizarse con:

- **G++**;
- **Clang++**.

El objetivo es que la comparación entre tecnologías se haga sobre una base lo más homogénea posible en estructura, protocolo y metodología de medida, y que también sea posible observar el posible impacto del compilador sobre la generación de código para corrutinas.

---

## Autor

**José Antonio García Montañez**