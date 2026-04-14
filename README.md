# TFM-Repo - Automatizacion global de compilacion y ejecucion

Este repositorio agrupa distintas implementaciones experimentales de transferencia de ficheros sobre varias capas de transporte en **C++23**, con el objetivo de comparar su comportamiento en términos de:

- tiempo;
- escalabilidad;
- throughput;
- consumo energético;
- comportamiento bajo concurrencia;
- diferencias entre compiladores;
- diferencias entre configuraciones monohilo y multihebra.

Cada subproyecto representa una capa de transporte distinta y mantiene su propia implementación de:

- servidor TCP;
- cliente TCP;
- benchmark con Google Benchmark;
- scripts de automatización;
- README específico.

Además de esos `README.md` individuales, en la raíz del repositorio se incluyen scripts globales para facilitar campañas completas de compilación y ejecución desde un único punto.

---

## Estructura general del repositorio

El repositorio agrupa varios subproyectos de benchmarking y transporte. Entre ellos, por ejemplo:

- `asio-standalone`
- `bechmark_taps`
- `becnhmark_async_berkeley`
- `boost-asio`
- `bsd-sockets`
- `corosio`

Cada uno de ellos puede tener:

- su propio `build_release.sh`;
- su propio `scripts/run_bench.py`;
- su propia carpeta `results/`;
- su propia lógica de compilación y ejecución.

La idea de los scripts globales es **coordinar todos esos proyectos**, sin duplicar la lógica interna de cada uno.

---

## Objetivo de los scripts globales

En la raíz del repositorio puedes tener dos scripts principales:

- `build_all_transports.sh`: compila todos los proyectos de transporte usando el `build_release.sh` propio de cada subproyecto.
- `run_all_benchmarks.sh`: ejecuta todos los `scripts/run_bench.py` disponibles en los subproyectos.

Con esto se consigue:

- lanzar compilaciones masivas desde un único punto;
- ejecutar campañas completas de benchmarking sin entrar proyecto por proyecto;
- mantener encapsulada la lógica específica dentro de cada implementación;
- facilitar la automatización del trabajo experimental del TFM.

---

## Proyectos contemplados

Los scripts globales están preparados para recorrer estos directorios si existen:

- `asio-standalone`
- `bechmark_taps`
- `becnhmark_async_berkeley`
- `boost-asio`
- `bsd-sockets`
- `corosio`

Si alguno de ellos no existe, o no contiene el script esperado, simplemente se omite y se informa por pantalla.

---

## Script global de compilación

### Nombre

```bash
./build.sh
```

### Qué hace

Este script:

- comprueba que se está ejecutando desde la raíz del repositorio;
- intenta asegurar que `python3-matplotlib` esté disponible;
- entra en cada subproyecto contemplado;
- ejecuta su `build_release.sh` si existe;
- muestra por pantalla el progreso de la compilación global.

La filosofía es que cada subproyecto siga decidiendo:

- cómo se compila;
- con qué compiladores;
- qué flags usa;
- qué dependencias necesita.

El script global solo actúa como coordinador.

---

## Instalación automática de matplotlib

El script global de compilación intenta instalar `python3-matplotlib` automáticamente si no está disponible, ya que algunos scripts de benchmarking generan gráficas al finalizar las campañas.

Orden de prioridad del intento de instalación:

1. `apt-get` con privilegios de administrador;
2. si no existe `apt-get`, intento con `python3 -m pip install matplotlib`.

En Ubuntu, lo normal es que se instale mediante:

```bash
sudo apt-get update
sudo apt-get install -y python3-matplotlib
```

---

## Instalación automática de pypdf

El script global de compilación o ejecución puede necesitar `pypdf` automáticamente si no está disponible, ya que se utiliza para fusionar los informes PDF generados por cada librería en un único documento final comparativo.

Orden de prioridad del intento de instalación:

1. `apt-get` con privilegios de administrador;
2. si no existe `apt-get`, intento con `python3 -m pip install pypdf`.

En Ubuntu, lo normal es que se instale mediante:

```bash
sudo apt-get update
sudo apt-get install -y python3-pypdf
```

---

## Script global de ejecución de benchmarks

### Nombre

```bash
sudo ./run.sh
```

### Qué hace

Este script:

- entra en cada subproyecto contemplado;
- busca su `scripts/run_bench.py`;
- ejecuta la campaña correspondiente si existe;
- deja que cada proyecto genere sus propios resultados, logs, JSON, CSV y gráficas según su configuración interna.

De nuevo, el control detallado sigue residiendo en cada subproyecto.

---

## Flujo recomendado de trabajo

Desde la raíz del repositorio:

```bash
sudo ./build.sh
sudo ./run.sh
```

Con este flujo:

1. se compilan todas las implementaciones disponibles;
2. se lanzan todas las campañas configuradas;
3. cada subproyecto guarda sus resultados en su propia carpeta `results/`.

---

## Permisos

Si al copiarlos a la raíz no tienen permiso de ejecución:

```bash
chmod +x build.sh
chmod +x run.sh
```

---

## Relación con los README individuales

Este README **no sustituye** a los `README.md` específicos de cada implementación.

Su función es complementar la documentación existente y explicar:

- cómo compilar todos los proyectos desde la raíz;
- cómo lanzar todas las campañas desde la raíz;
- cómo entender el papel de los scripts globales dentro del repositorio.

Para detalles concretos de cada tecnología, deben consultarse sus respectivos `README.md`.

---

## Notas finales

- Cada subproyecto sigue controlando su propia configuración de compilador, puertos, hebras, benchmarks y resultados.
- El script global solo coordina la ejecución general.
- Si renombrases directorios del repositorio, tendrías que actualizar el array `PROJECT_DIRS` dentro de los scripts globales.
- Esta organización permite mantener desacopladas las implementaciones concretas y, al mismo tiempo, automatizar el trabajo experimental a nivel de repositorio completo.