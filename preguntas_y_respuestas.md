# Preguntas y respuestas para la defensa

## Diseño

### ¿Qué simula el programa?

Simula la planificación de procesos en una sola CPU.
Cada proceso simulado es un hilo POSIX (`pthread`), no un proceso Linux.

### ¿Por qué se usan hilos?

Los hilos representan trabajos concurrentes dentro de un programa.
El simulador controla el avance lógico de cada ráfaga sin gobernar Linux.

### ¿Hay variables globales?

No hay variables globales mutables.
El estado reside en `os_context_t`, creado en `main` y pasado por puntero.

## Señales

### ¿Por qué se usa `sigwait()`?

Un handler asíncrono permite muy pocas funciones seguras.
`sigwait()` recibe señales en un hilo donde es seguro usar mutexes y pthreads.

### ¿Qué hace `SIGUSR1`?

Solicita un proceso nuevo.
Si hay menos de 20 activos, se crea y el estado pasa de `NEW` a `READY`.

### ¿Qué hacen `SIGINT` y `SIGTERM`?

Solicitan un apagado ordenado.
Se despierta a los hilos, se hace `pthread_join()` y se liberan recursos.

## Planificación

### ¿Cómo hay a lo sumo un proceso en `RUNNING`?

El scheduler es el único que realiza `READY -> RUNNING`.
También actualiza `os->running_process` con `os->mutex` tomado.

### ¿Cuál es la diferencia entre algoritmos?

- **FCFS:** menor orden de llegada; no expropiativo.
- **SJF:** menor ráfaga total; no expropiativo.
- **SRTF:** menor tiempo restante; expropia si llega uno menor.
- **RR:** concede un quantum y reencola al proceso al vencer.

### ¿Cómo se resuelven empates?

FCFS usa llegada; SJF y SRTF usan llegada como segundo criterio.
RR usa `ready_seq`, el orden de entrada o reingreso a `READY`.

### ¿Cómo funciona la expropiación sin cancelar hilos?

Es cooperativa.
Cada worker trabaja en unidades de 10 ms y descuenta tiempo si sigue `RUNNING`.
Si el scheduler lo cambia a `READY`, el worker detecta el cambio y espera.

## Concurrencia

### ¿Qué protege el mutex?

Protege lista, estados, tiempos, contadores, eventos y la bandera de apagado.

### ¿Cómo se evita busy waiting?

Los workers usan `pthread_cond_wait()`.
Scheduler y monitor esperan condiciones; RR usa una espera temporizada.

### ¿Por qué se libera el mutex antes de dormir o hacer join?

`nanosleep()` y `pthread_join()` pueden bloquear.
Retener el mutex impediría que otros hilos cambien estados o terminen.

### ¿Por qué el monitor toma un snapshot?

Copia el estado bajo el mutex y dibuja después.
Así `ncurses` no bloquea al scheduler y la vista es coherente.

## Pruebas y límites

### ¿Qué prueban los tests actuales?

`selection_test` verifica criterios y desempates.
`smoke.sh` ejecuta algoritmos, crea procesos por señales y valida terminación.

### ¿Qué se debería probar además?

Una expropiación SRTF, rotación RR, límite de 20 y shutdown con workers activos.
También conviene ejecutar sanitizadores de memoria.

### ¿Cuál es la limitación principal del modelo?

La expropiación se observa en el siguiente punto cooperativo.
La granularidad máxima es una unidad de trabajo de 10 ms.
