# Requisitos del proyecto

Este proyecto implementa la especificación documentada en este repositorio.
Sus requisitos verificables principales son:

- C sobre Linux, pthreads y monitor ncurses;
- workers dinámicos, de cero a veinte activos;
- estados NEW, READY, RUNNING y TERMINATED;
- FCFS, SJF, SRTF y Round Robin seleccionados mediante `argv`;
- creación mediante SIGUSR1 y cierre mediante SIGINT/SIGTERM;
- preempción cooperativa, variables de condición y ausencia de busy waiting;
- lista enlazada dinámica y acceso compartido protegido con mutex;
- apagado ordenado, `pthread_join()` de cada worker y liberación de recursos.

Las decisiones de implementación, compilación, pruebas y defensa están
documentadas en `README.md`.
