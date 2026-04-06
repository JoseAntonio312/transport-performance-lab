# TAPS - Transferencia de ficheros y benchmarking en C++23

Este proyecto implementa una base de trabajo en **C++23** para estudiar transferencia de ficheros sobre **TAPS**, con foco en:

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

Además de comparar distintas tecnologías de transporte, esta versión incorpora también la comparación entre ejecutables compilados con **G++** y ejecutables compilados con **Clang++**, con el objetivo de estudiar si el compilador introduce diferencias medibles sobre una implementación basada en **TAPS**.

---

## Objetivo del proyecto

El objetivo actual del proyecto es disponer de una implementación experimental con **TAPS** que sirva como base para estudiar:

- transferencia de ficheros;
- concurrencia con múltiples clientes;
- benchmarking de rendimiento;
- medición de consumo energético;
- diferencias de comportamiento entre binarios compilados con **G++** y **Clang++**.

En esta fase se está trabajando con una implementación basada en **TAPS sobre Asio standalone**, manteniendo la lógica experimental y ampliando el estudio a una segunda dimensión: el compilador.

---

## Sobre la librería TAPS

La librería **TAPS** usada en este proyecto **no es de autoría propia**. La autoría de dicha librería corresponde a un tercero, y en este trabajo se utiliza como base de transporte para construir los ejemplos experimentales de:

- servidor TCP;
- cliente TCP;
- benchmark TCP.

El trabajo propio de este proyecto se centra en:

- integrar la librería TAPS en un proyecto separado de benchmarking;
- adaptar el protocolo de transferencia de fichero;
- mantener una versión comparable frente a BSD sockets, Boost.Asio y Asio standalone;
- ejecutar campañas de rendimiento y consumo energético sobre esa base;
- repetir las campañas con binarios compilados con distintos compiladores.

---

## Estructura del proyecto

```text
benchmark_taps/
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

La librería TAPS puede mantenerse **fuera de este proyecto**, por ejemplo en un directorio hermano:

```text
taps-asio/
├── proyecto_taps/        # librería TAPS original
└── benchmark_taps/       # este proyecto experimental
```

Esta organización suele ser más limpia a nivel profesional, porque separa claramente:

- la **librería base**;
- los **ejemplos y benchmarks propios** que dependen de ella.

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

La concurrencia se gestiona mediante **TAPS** con interfaz asíncrona y corrutinas en **C++23**.

### 2. Cliente TCP

El cliente:

- se conecta al servidor indicando IP y puerto;
- recibe la cabecera del protocolo;
- obtiene nombre y tamaño del fichero;
- descarga el contenido;
- lo guarda en disco;
- también minimiza la salida por pantalla para mantener estabilidad en pruebas.

La comunicación se gestiona mediante **TAPS** con corrutinas.

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

## Adaptación específica al caso TAPS

Para poder usar TAPS en este proyecto, se realizaron varias adaptaciones respecto a las versiones previas con BSD sockets, Boost.Asio y Asio standalone.

### 1. Separación entre librería y proyecto experimental

En lugar de incrustar toda la librería dentro del mismo proyecto, se ha preparado un proyecto independiente de benchmarks que enlaza con la librería TAPS ubicada fuera del árbol principal.

Esto permite:

- mantener la librería original aislada;
- evitar mezclar código de infraestructura con código experimental;
- facilitar comparaciones con otras implementaciones;
- simplificar mantenimiento y reproducibilidad.

### 2. Uso de Asio standalone del sistema

La implementación TAPS depende de **Asio standalone**. En este entorno se ha detectado disponible en:

```text
/usr/include/asio.hpp
```

Por ello, la configuración se adapta para usar:

```cmake
set(ASIO_INCLUDE_DIR "/usr/include")
```

### 3. Compilación de la librería TAPS fuera del proyecto principal

La librería TAPS se compila aparte y el proyecto de benchmarking enlaza contra ella mediante `add_subdirectory(...)` apuntando al directorio correspondiente, o bien mediante un target exportado por la propia librería, según la organización elegida.

### 4. Adaptación del cliente y benchmark a la semántica de recepción de TAPS

Como la recepción en TAPS no replica exactamente la semántica de `recv()` o `async_read()` byte a byte, se ha introducido una lógica de **buffer intermedio de flujo** para:

- reconstruir el protocolo por campos;
- consumir exactamente `N` bytes cuando hace falta;
- preservar sobrantes de una recepción para la siguiente etapa del protocolo;
- mantener el comportamiento comparable con el resto de implementaciones.

### 5. Mantenimiento de la misma lógica experimental

Aunque cambia la librería de transporte, se han mantenido intencionadamente:

- el mismo protocolo;
- nombres de variables equivalentes;
- misma estructura general de servidor, cliente y benchmark;
- mínima salida por pantalla;
- foco en evitar copias innecesarias de memoria dentro de lo posible con la API disponible.

### 6. Comparación adicional por compilador

Además de la comparativa entre capas de transporte, esta versión incorpora una segunda dimensión experimental: el compilador.

El mismo código puede compilarse con:

- **G++**
- **Clang++**

y ejecutarse bajo las mismas condiciones experimentales para observar posibles diferencias en:

- tiempo de ejecución;
- throughput;
- escalabilidad;
- consumo energético;
- estabilidad de resultados.

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

### Asio standalone

En este entorno, Asio standalone está disponible desde los paquetes del sistema:

```bash
sudo apt update
sudo apt install libasio-dev
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

## Compilación de la librería TAPS

Si la librería TAPS no está compilada todavía, primero hay que construirla.

### Ejemplo de compilación fuera del proyecto experimental

Suponiendo una estructura como esta:

```text
~/Escritorio/TFM/taps-asio/
├── proyecto_taps/
└── benchmark_taps/
```

Comandos típicos para compilar la librería:

```bash
cd ~/Escritorio/TFM/taps-asio/proyecto_taps
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

Si `asio.hpp` está en `/usr/include`, la configuración de TAPS debe apuntar a esa ruta.

---

## Compilación del proyecto experimental TAPS

El proyecto está preparado para compilarse en **Release**.

### Script recomendado

```bash
./build_release.sh
```

### Forma manual

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
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

- coste temporal de la implementación;
- throughput;
- escalabilidad;
- eficiencia energética;
- estabilidad de las mediciones.

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

### Lanzar el cliente manualmente

```bash
./build/tcpclient/tcpclient 127.0.0.1 8080
```

O indicando nombre de salida:

```bash
./build/tcpclient/tcpclient 127.0.0.1 8080 salida.bin
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

## Script de automatización

### Compatibilidad del script

El script de automatización **no depende de la librería concreta** usada internamente, siempre que los ejecutables mantengan los mismos nombres y la misma interfaz por línea de comandos.

Por tanto, si en el proyecto TAPS se conservan:

- `build/tcpserver/tcpserver`
- `build/benchmarks/bench_tcp`

el script puede reutilizarse sin cambios o con cambios mínimos en rutas.

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

## Nota metodológica

Que una versión basada en TAPS obtenga mejores resultados que una versión con BSD sockets o con `poll()` en un conjunto concreto de pruebas **no implica automáticamente** que TAPS sea siempre más rápido en términos generales.

La comparación debe interpretarse siempre como:

- resultado dependiente de la implementación concreta;
- dependiente del patrón de tráfico;
- dependiente del número de clientes;
- dependiente del tamaño de fichero y de buffer;
- dependiente de la sobrecarga interna de cada abstracción.

Por tanto, los resultados deben presentarse como resultados experimentales del entorno y de la implementación evaluada.

---

## Autor

**José Antonio García Montañez**

---

## Autoría externa de la librería TAPS

La **librería TAPS utilizada como dependencia no ha sido desarrollada por el autor de este proyecto**. En caso de distribución académica o pública, se recomienda dejar explícitamente identificada la autoría original de dicha librería en este repositorio, junto con su ruta, licencia y referencia correspondiente.