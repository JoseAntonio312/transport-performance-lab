# ASIO STANDALONE - Transferencia de ficheros y benchmarking en C++23

Este proyecto implementa una base de trabajo en **C++23** para estudiar transferencia de ficheros sobre **Asio standalone**, con foco en:

- implementación funcional de **servidor TCP**;
- implementación funcional de **cliente TCP**;
- soporte para **múltiples clientes concurrentes**;
- compilación en **modo Release** con **CMake**;
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

---

## Objetivo del proyecto

El objetivo actual del proyecto es disponer de una implementación propia con **Asio standalone** que sirva como base experimental para estudiar:

- transferencia de ficheros;
- concurrencia con múltiples clientes;
- benchmarking de rendimiento;
- medición de consumo energético.

En esta fase se está trabajando **únicamente con Asio standalone** (sin dependencia de Boost).

---

## Estructura del proyecto

```text
asio/
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

La concurrencia se gestiona mediante **Asio asíncrono con corrutinas (C++23)**.

### 2. Cliente TCP

El cliente:

- se conecta al servidor indicando IP y puerto;
- recibe la cabecera del protocolo;
- obtiene nombre y tamaño del fichero;
- descarga el contenido;
- lo guarda en disco;
- también minimiza la salida por pantalla para mantener estabilidad en pruebas.

La comunicación se gestiona mediante **Asio asíncrono con corrutinas**.

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

### Compilador

Se usa **GCC 14.1.0** mediante `update-alternatives`, por lo que no hace falta fijar rutas manuales a binarios en el proyecto.

Ejemplo de configuración:

```bash
sudo update-alternatives --install /usr/bin/g++ g++ /usr/local/gcc-14.1.0/bin/g++-14.1.0 14
sudo update-alternatives --install /usr/bin/gcc gcc /usr/local/gcc-14.1.0/bin/gcc-14.1.0 14
```

### CMake

```bash
sudo apt update
sudo apt install cmake
```

### Boost

```bash
sudo apt install libboost-all-dev
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

## Compilación del proyecto

El proyecto está preparado para compilarse en **Release**.

### Script recomendado

```bash
./build_release.sh
```

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

---

## Ejecución

### Compilar

```bash
./build_release.sh
```

### Lanzar el servidor manualmente

```bash
./build/tcpserver/tcpserver ./archivo.txt 8080
```

### Lanzar una prueba individual

```bash
./build/benchmarks/bench_tcp --benchmark_min_time=1s --benchmark_repetitions=1 --benchmark_out=results/micro.json --benchmark_out_format=json
```

### Lanzar la campaña automática

```bash
python3 scripts/run_bench.py
```

o:

```bash
./scripts/run_bench.py
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

---

## Script de automatización

### Ruta

```text
sudo scripts/run_bench.py
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

---

## Ajustes recomendados para medir bien

### Fijar governor

```bash
sudo cpupower frequency-set -g performance
```

### Otras recomendaciones

- compilar siempre en `Release`;
- evitar `Debug`;
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

---

## Autor

**José Antonio García Montañez**