# Razonamiento y lógica del simulador

## Propósito

El proyecto representa, de forma controlada, las decisiones que tomaría un
sistema operativo al planificar procesos para una sola CPU. No intenta crear un
sistema operativo real: cada proceso simulado es un hilo `pthread` y la CPU se
representa permitiendo que solamente uno de esos hilos avance a la vez.

Esta elección permite estudiar concurrencia, estados de proceso, algoritmos de
planificación, señales y sincronización sin usar `fork()` ni detener hilos de
forma insegura.

## Estado compartido, sin variables globales

Todo el estado mutable pertenece a una instancia local de `os_context_t`,
creada en `main`. Esta estructura contiene la lista de procesos, el proceso que
posee la CPU, los contadores, la configuración del algoritmo, el generador
aleatorio, las señales y los objetos de sincronización. Su dirección se pasa a
los hilos al crearlos.

Por ello no existen variables globales de programa. Las constantes se expresan
como macros de compilación y las funciones auxiliares `static` no mantienen
estado entre llamadas. Centralizar el estado en el contexto también hace clara
la propiedad de los datos y evita que dos ejecuciones del simulador compartan
información accidentalmente.

## Ciclo de vida de un proceso

Un `SIGUSR1` es recibido por `signal_thread` mediante `sigwait()`. Ese hilo
solicita la creación del proceso: se reserva un nodo de la lista, se genera una
ráfaga de CPU, se crea el worker y el estado pasa de `NEW` a `READY`.

El scheduler elige un proceso `READY`, lo cambia a `RUNNING` y despierta su
variable de condición. El worker no ejecuta una ráfaga completa de una vez;
trabaja en unidades de 10 ms. Después de cada unidad comprueba bajo el mutex si
sigue en `RUNNING`. Si fue desalojado, deja de descontar tiempo y vuelve a
esperar. Si agota su tiempo, pasa a `TERMINATED` y avisa al scheduler.

```text
SIGUSR1 -> NEW -> READY -> RUNNING -> TERMINATED
                          ^    |
                          |    +-- RR o SRTF: vuelve a READY
                          +------- siguiente despacho
```

## Decisión de planificación

La lista enlazada conserva todos los procesos relevantes. El scheduler es el
único componente que hace las transiciones `READY -> RUNNING` y `RUNNING ->
READY`, lo que mantiene una única fuente de verdad sobre quién usa la CPU.

- **FCFS/FIFO:** selecciona el menor orden de llegada. Una vez iniciado, el
  proceso no es desalojado.
- **SJF:** selecciona la menor ráfaga total; en empate conserva el orden de
  llegada. Tampoco es expropiativo.
- **SRTF:** compara el tiempo restante de los procesos listos con el proceso
  activo. Si aparece uno estrictamente más corto, el activo vuelve a `READY` y
  se despacha el nuevo.
- **Round Robin:** cada proceso recibe un quantum. Al vencer, vuelve a `READY`
  con un nuevo orden de cola y se selecciona el proceso listo más antiguo.

Los desempates explícitos hacen que la simulación sea reproducible desde el
punto de vista de las reglas, aun cuando el sistema anfitrión decida cuándo se
ejecuta cada hilo.

## Sincronización y preempción cooperativa

`os->mutex` protege la lista y todos los campos compartidos. La condición
`state_cond` despierta al scheduler y al monitor cuando cambia el estado. Cada
proceso tiene además su propia condición, que bloquea al worker hasta que sea
despachado.

No hay espera activa: los hilos esperan con `pthread_cond_wait()` o
`pthread_cond_timedwait()`. Tampoco se usa `pthread_cancel()`, `SIGSTOP` ni
ningún mecanismo que interrumpa un worker arbitrariamente. La expropiación es
cooperativa: el scheduler cambia el permiso de ejecución y el worker lo observa
al finalizar su unidad corta de trabajo. Así se modela el desalojo sin dejar
mutexes ni recursos en estados inconsistentes.

## Monitor y cierre ordenado

El monitor toma un snapshot breve bajo el mutex y dibuja fuera de la sección
crítica. Con ello `ncurses` nunca bloquea al planificador ni a los workers. Se
mantienen un historial limitado de terminados y eventos recientes para evitar
que la memoria crezca sin límite.

`SIGINT` y `SIGTERM` solicitan el cierre. Se marca el contexto como apagándose,
se despierta a todos los hilos y cada worker termina de forma segura. Después
se hace `pthread_join()` de cada worker, del scheduler, del monitor y del hilo
de señales; por último se liberan las condiciones, la lista y el contexto. El
orden evita fugas de memoria y deja el simulador en un estado final verificable.
