# Guía de C para explicar el simulador

## Orden para presentar el código

Explica primero el flujo y luego los detalles.

```text
main -> inicializa contexto y bloquea señales
     -> crea signal_thread, scheduler y monitor
SIGUSR1 -> proc_create -> worker
scheduler -> selecciona y despierta un worker
worker -> consume una unidad, termina o espera
SIGINT/SIGTERM -> shutdown -> join -> liberar memoria
```

Para cada función, indica qué recibe, qué modifica, si necesita el mutex y qué
otro hilo despierta o espera.

## Estructuras, enum y punteros

```c
typedef struct process {
    int pid;
    process_state_t state;
    struct process *next;
} process_t;
```

`struct process` define un nodo.
`typedef` crea el alias `process_t`.
`next` permite construir una lista enlazada.

```c
typedef enum { PROC_NEW, PROC_READY, PROC_RUNNING, PROC_TERMINATED }
    process_state_t;
```

Un `enum` asigna nombres legibles a valores enteros.
`obj.campo` accede a una estructura.
`ptr->campo` equivale a `(*ptr).campo` y se usa con un puntero.

`os_context_t` agrupa el estado compartido y evita usar variables globales.

## Memoria dinámica

```c
process_t *process = calloc(1, sizeof(*process));
if (process == NULL) {
    return ENOMEM;
}
```

`calloc` reserva memoria e inicializa sus bytes a cero.
`sizeof(*process)` evita repetir el tipo y resiste cambios de declaración.
Cada reserva debe tener una ruta de `free`.

`process_t **link` es un puntero a puntero.
Permite modificar el enlace que apunta a un nodo, incluso la cabeza de la lista.

## Funciones y modificadores

```c
static process_t *select_next_locked(os_context_t *os);
```

- `static` en una función limita su visibilidad al archivo `.c`.
- `static` no crea una variable global ni memoria persistente en este caso.
- `const` indica que no se debe modificar un parámetro mediante ese puntero.
- `void *` es un puntero genérico; pthreads usa `void *funcion(void *)`.
- Pthreads devuelve cero al tener éxito y un código de error al fallar.

## Mutex y condiciones

```c
pthread_mutex_lock(&os->mutex);
/* estado compartido */
pthread_mutex_unlock(&os->mutex);
```

El mutex ofrece exclusión mutua.
Las funciones con sufijo `_locked` requieren que el llamador posea el mutex.

```c
while (!os->shutdown_requested && process->state != PROC_RUNNING) {
    pthread_cond_wait(&process->cond, &os->mutex);
}
```

La condición real es el estado del proceso, no el objeto `pthread_cond_t`.
Se usa `while`, no `if`, para tolerar despertares espurios.
`pthread_cond_wait` libera el mutex al dormir y lo recupera antes de retornar.
`signal` despierta uno; `broadcast` despierta todos.

No mantengas el mutex durante `nanosleep()`, dibujo o `pthread_join()`.
Esas operaciones pueden bloquear e impedir el progreso de otro hilo.

## Tiempo y preempción cooperativa

```c
request.tv_sec = milliseconds / 1000;
request.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
nanosleep(&request, &request);
```

`timespec` guarda segundos y nanosegundos.
Si una señal interrumpe `nanosleep`, se revisa `errno == EINTR` para reanudar.
El worker duerme una unidad y después valida que aún esté en `RUNNING`.
Ese patrón implementa la preempción cooperativa.

RR usa una espera temporizada con `CLOCK_MONOTONIC`.
Ese reloj no se altera cuando el usuario cambia la fecha del sistema.

## Validación y limpieza

```c
errno = 0;
value = strtol(text, &end, 10);
if (errno != 0 || end == text || *end != '\0') {
    return EINVAL;
}
```

`strtol` detecta texto inválido, desbordamientos y caracteres sobrantes.
Luego se valida el rango antes de convertir a `int`.

Revisa siempre resultados de asignación, pthreads y conversiones.
Si algo falla después de adquirir recursos, libera los recursos previos.
En el cierre: despertar waiters, hacer join, destruir sincronización y liberar.

## Errores que debes evitar al explicarlo

- Confundir procesos simulados con procesos Linux: son hilos `pthread`.
- Acceder a `process_list` sin `os->mutex`.
- Usar `if` en vez de `while` con `pthread_cond_wait`.
- Ejecutar `pthread_join` teniendo el mutex tomado.
- Destruir una condición mientras un worker aún podría esperarla.
- Usar `rand()` global; el proyecto guarda `rng_state` en el contexto.
- Decir que las señales crean workers dentro de un handler; usa `sigwait`.

## Frases útiles para la exposición

- “El scheduler decide; el worker avanza solo si recibió la CPU.”
- “El mutex garantiza que existe como máximo un proceso `RUNNING`.”
- “La preempción es cooperativa y tiene granularidad máxima de 10 ms.”
- “`sigwait` evita operaciones complejas dentro de un handler asíncrono.”
- “Se libera el mutex antes de bloquear para no detener a otros hilos.”
