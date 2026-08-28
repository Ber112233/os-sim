# Simulador de sistema operativo con pthreads

Simulador académico de planificación de CPU escrito en C para Linux. Cada
proceso simulado es un `pthread` cooperativo; no se crean procesos Linux con
`fork()` ni se suspenden hilos de manera forzada.

Implementa:

- FCFS/FIFO no expropiativo;
- SJF no expropiativo;
- SRTF expropiativo y cooperativo;
- Round Robin con quantum configurable;
- creación de procesos mediante `SIGUSR1`;
- apagado ordenado mediante `SIGINT` o `SIGTERM`;
- lista enlazada dinámica, máximo de 20 procesos activos;
- espera mediante variables de condición, sin busy waiting;
- recuperación de todos los workers con `pthread_join()`;
- monitor con `ncurses` y snapshots protegidos;
- historial acotado de procesos terminados y eventos recientes.

## Requisitos

El objetivo es Linux. En Debian, Ubuntu o Linux Mint:

```bash
sudo apt update
sudo apt install build-essential libncurses-dev
```

En Windows también puede compilarse para validación con la terminal **MSYS2
MSYS**:

```bash
pacman -S --needed gcc make ncurses-devel
cd /c/Users/berna/Downloads/os-simulator
make test
```

En ese entorno se genera `os_sim.exe` y las señales deben enviarse desde otra
terminal MSYS2. La plataforma de entrega y ejecución principal continúa siendo
Linux.

## Compilación

```bash
make
```

Se genera `./os_sim` con `-std=c11 -Wall -Wextra -Wpedantic -pthread` y
`-lncurses`.

```bash
make clean
make debug
```

## Ejecución

```bash
./os_sim fcfs
./os_sim sjf
./os_sim srtf
./os_sim rr
./os_sim rr 500
```

El quantum predeterminado de RR es 500 ms. Se aceptan valores entre 50 y
60000 ms. Al iniciar se muestra el PID Linux real:

```text
OS Simulator PID: 4812
```

Desde otra terminal:

```bash
kill -USR1 4812   # crear un proceso simulado
kill -INT 4812    # apagado ordenado
kill -TERM 4812   # apagado ordenado
```

`Ctrl+C` equivale a `SIGINT`. Cada proceso recibe una ráfaga aleatoria de 1 a
10 segundos. `P1`, `P2`, etc. son identificadores simulados, no PIDs Linux.

El monitor usa `ncurses` cuando la salida es un terminal compatible. Si se
ejecuta desde un script o con salida redirigida, el hilo monitor permanece
activo pero no inicializa la interfaz, lo que facilita pruebas automatizadas.

## Diseño

Los hilos principales son:

- `signal_thread`: recibe las señales bloqueadas mediante `sigwait()`;
- `scheduler`: selecciona y concede la CPU a un solo worker;
- `monitor`: toma snapshots breves y dibuja fuera de la sección crítica;
- workers dinámicos: esperan su turno en una variable de condición propia.

La lista de procesos y todo el estado mutable se protegen con `os->mutex`.
Las condiciones de cada proceso también usan ese mutex. Al existir un único
mutex de estado no hay adquisición anidada ni inversión del orden de locks.
Nunca se conserva el mutex mientras se hace `nanosleep()`, `pthread_join()` o
se dibuja con `ncurses`.

El scheduler es el único que realiza transiciones `READY -> RUNNING` y
`RUNNING -> READY`. Los workers ejecutan unidades de 10 ms y solo descuentan
tiempo si siguen autorizados como `RUNNING`. Así RR y SRTF simulan expropiación
sin `pthread_cancel()`, `SIGSTOP` ni suspensión arbitraria de un hilo.

Los criterios de desempate son estables:

- FCFS: menor orden de llegada;
- SJF: menor tiempo total y luego menor orden de llegada;
- SRTF: menor tiempo restante y luego menor orden de llegada;
- RR: menor número de entrada a la cola READY.

Al finalizar normalmente, un worker marca `TERMINATED`, avisa al scheduler y
retorna. El scheduler hace `pthread_join()`, destruye su condición y conserva
solo los datos de hasta diez procesos terminados para el monitor. En shutdown,
se hace `broadcast` a todas las condiciones, se recuperan los hilos restantes
y finalmente se libera toda la lista.

## Estructura

```text
include/os.h             tipos y contexto compartido
include/proc_list.h      interfaz de lista dinámica
include/proc_mngr.h      creación y recolección
src/main.c               argumentos, inicialización, joins y cleanup
src/os.c                 contexto, eventos y shutdown
src/proc_list.c          lista y criterios de selección
src/proc_mngr.c          workers dinámicos y pthread_join
src/scheduler.c          FCFS, SJF, SRTF y RR
src/worker.c             ejecución cooperativa
src/monitor.c            ncurses y snapshots
src/sig_handler.c        sigwait y señales
tests/smoke.sh           prueba automatizada básica
```

## Pruebas

El target de pruebas valida primero los criterios de selección y luego ejecuta
los cuatro algoritmos, crea tres workers por caso y verifica el resumen de
cierre:

```bash
make test
```

También puede ejecutar solamente la prueba de integración con
`sh tests/smoke.sh`.

Las variables `OS_SIM_MIN_MS` y `OS_SIM_MAX_MS` permiten acortar únicamente
las ráfagas de pruebas. No cambian la interfaz de línea de comandos:

```bash
OS_SIM_MIN_MS=200 OS_SIM_MAX_MS=200 ./os_sim rr 100
```

Prueba de memoria recomendada:

```bash
valgrind --leak-check=full --show-leak-kinds=all ./os_sim rr 100
```

Mientras corre, envíe varias señales `USR1`, espere finalizaciones y cierre con
`TERM`. El resultado esperado es cero bloques `definitely lost`, sin lecturas
inválidas y sin hilos pendientes.
