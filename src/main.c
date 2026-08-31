#include "include/monitor.h"
#include "include/os.h"
#include "include/proc_list.h"
#include "include/proc_mngr.h"
#include "include/scheduler.h"
#include "include/sig_handler.h"

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(const char *program) {
    fprintf(stderr,
            "Uso:\n"
            "  %s fcfs\n"
            "  %s sjf\n"
            "  %s srtf\n"
            "  %s rr [quantum_ms]\n",
            program, program, program, program);
}

static int parse_quantum(const char *text, int *quantum_ms) {
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value < OS_MIN_QUANTUM_MS ||
        value > 60000L) {
        return EINVAL;
    }
    *quantum_ms = (int)value;
    return 0;
}

static int parse_arguments(int argc, char **argv,
                           scheduler_algorithm_t *algorithm,
                           int *quantum_ms) {
    if (argc < 2) {
        return EINVAL;
    }

    if (strcmp(argv[1], "fcfs") == 0 || strcmp(argv[1], "fifo") == 0) {
        *algorithm = ALG_FCFS;
    } else if (strcmp(argv[1], "sjf") == 0) {
        *algorithm = ALG_SJF;
    } else if (strcmp(argv[1], "srtf") == 0) {
        *algorithm = ALG_SRTF;
    } else if (strcmp(argv[1], "rr") == 0) {
        *algorithm = ALG_RR;
    } else {
        return EINVAL;
    }

    *quantum_ms = OS_DEFAULT_QUANTUM_MS;
    if (*algorithm == ALG_RR) {
        if (argc > 3) {
            return EINVAL;
        }
        if (argc == 3) {
            return parse_quantum(argv[2], quantum_ms);
        }
    } else if (argc != 2) {
        return EINVAL;
    }

    return 0;
}

static int join_main_thread(pthread_t thread, const char *name) {
    int result;

    result = pthread_join(thread, NULL);
    if (result != 0) {
        fprintf(stderr, "pthread_join(%s): %s\n", name, strerror(result));
    }
    return result;
}

int main(int argc, char **argv) {
    scheduler_algorithm_t algorithm;
    int quantum_ms;
    os_context_t os;
    pthread_t signal_thread;
    pthread_t scheduler_thread;
    pthread_t monitor_thread;
    bool signal_created = false;
    bool scheduler_created = false;
    bool monitor_created = false;
    int result;
    int exit_status = EXIT_SUCCESS;
    int total_created;
    int finished_count;
    int interrupted_count;

    setlocale(LC_ALL, "");
    if (parse_arguments(argc, argv, &algorithm, &quantum_ms) != 0) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    result = os_context_init(&os, algorithm, quantum_ms);
    if (result != 0) {
        fprintf(stderr, "No se pudo inicializar el contexto: %s\n",
                strerror(result));
        return EXIT_FAILURE;
    }

    sigemptyset(&os.signal_set);
    sigaddset(&os.signal_set, SIGUSR1);
    sigaddset(&os.signal_set, SIGINT);
    sigaddset(&os.signal_set, SIGTERM);
    result = pthread_sigmask(SIG_BLOCK, &os.signal_set, NULL);
    if (result != 0) {
        fprintf(stderr, "pthread_sigmask: %s\n", strerror(result));
        os_context_destroy(&os);
        return EXIT_FAILURE;
    }

    pthread_mutex_lock(&os.mutex);
    os_log_event_locked(&os, "Simulador iniciado con %s",
                        os_algorithm_name(os.algorithm));
    pthread_mutex_unlock(&os.mutex);

    printf("OS Simulator PID: %ld\n", (long)getpid());
    printf("Algorithm: %s", os_algorithm_name(os.algorithm));
    if (os.algorithm == ALG_RR) {
        printf(" | Quantum: %d ms", os.quantum_ms);
    }
    printf("\nSignals: kill -USR1 %ld | kill -INT %ld | kill -TERM %ld\n",
           (long)getpid(), (long)getpid(), (long)getpid());
    fflush(stdout);

    result = pthread_create(&signal_thread, NULL, signal_thread_main, &os);
    if (result == 0) {
        signal_created = true;
    } else {
        fprintf(stderr, "No se pudo crear signal thread: %s\n",
                strerror(result));
        exit_status = EXIT_FAILURE;
        os_request_shutdown(&os, "Fallo al crear signal thread");
    }

    if (exit_status == EXIT_SUCCESS) {
        result = pthread_create(&scheduler_thread, NULL, scheduler_main, &os);
        if (result == 0) {
            scheduler_created = true;
        } else {
            fprintf(stderr, "No se pudo crear scheduler: %s\n",
                    strerror(result));
            exit_status = EXIT_FAILURE;
            os_request_shutdown(&os, "Fallo al crear scheduler");
        }
    }

    if (exit_status == EXIT_SUCCESS) {
        result = pthread_create(&monitor_thread, NULL, monitor_main, &os);
        if (result == 0) {
            monitor_created = true;
        } else {
            fprintf(stderr, "No se pudo crear monitor: %s\n", strerror(result));
            exit_status = EXIT_FAILURE;
            os_request_shutdown(&os, "Fallo al crear monitor");
        }
    }

    if (exit_status != EXIT_SUCCESS && signal_created) {
        pthread_kill(signal_thread, SIGTERM);
    }

    if (signal_created && join_main_thread(signal_thread, "signal") != 0) {
        exit_status = EXIT_FAILURE;
        os_request_shutdown(&os, "Fallo al recuperar signal thread");
    }
    if (scheduler_created &&
        join_main_thread(scheduler_thread, "scheduler") != 0) {
        exit_status = EXIT_FAILURE;
    }
    if (monitor_created && join_main_thread(monitor_thread, "monitor") != 0) {
        exit_status = EXIT_FAILURE;
    }

    if (proc_join_all(&os) < 0) {
        exit_status = EXIT_FAILURE;
    }

    pthread_mutex_lock(&os.mutex);
    total_created = os.total_created;
    finished_count = os.finished_count;
    interrupted_count = os.interrupted_count;
    pthread_mutex_unlock(&os.mutex);

    proc_list_destroy(&os);
    os_context_destroy(&os);

    printf("Shutdown complete. Created: %d | Finished: %d | Interrupted: %d\n",
           total_created, finished_count, interrupted_count);
    return exit_status;
}
