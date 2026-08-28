#include "proc_mngr.h"

#include "proc_list.h"
#include "worker.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_duration(const char *name, int fallback) {
    const char *text = getenv(name);
    char *end = NULL;
    long value;

    if (text == NULL || *text == '\0') {
        return fallback;
    }
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < OS_WORK_UNIT_MS ||
        value > 600000L) {
        return fallback;
    }
    return (int)value;
}

static unsigned int next_random_locked(os_context_t *os) {
    unsigned int value = os->rng_state;

    if (value == 0U) {
        value = 0x6d2b79f5U;
    }
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    os->rng_state = value;
    return value;
}

static int random_duration_locked(os_context_t *os) {
    int minimum = parse_duration("OS_SIM_MIN_MS", 1000);
    int maximum = parse_duration("OS_SIM_MAX_MS", 10000);
    unsigned int span;
    int duration;

    if (maximum < minimum) {
        int temporary = minimum;
        minimum = maximum;
        maximum = temporary;
    }
    span = (unsigned int)(maximum - minimum) + 1U;
    duration = minimum + (int)(next_random_locked(os) % span);
    duration = ((duration + OS_WORK_UNIT_MS - 1) / OS_WORK_UNIT_MS) *
               OS_WORK_UNIT_MS;
    return duration;
}

int proc_create(os_context_t *os) {
    process_t *process = calloc(1, sizeof(*process));
    int result;

    if (process == NULL) {
        pthread_mutex_lock(&os->mutex);
        os_log_event_locked(os, "No se pudo reservar memoria para un proceso");
        pthread_mutex_unlock(&os->mutex);
        return ENOMEM;
    }

    result = pthread_cond_init(&process->cond, NULL);
    if (result != 0) {
        free(process);
        return result;
    }
    process->cond_initialized = true;
    process->os = os;

    pthread_mutex_lock(&os->mutex);
    if (os->shutdown_requested) {
        pthread_mutex_unlock(&os->mutex);
        pthread_cond_destroy(&process->cond);
        free(process);
        return ECANCELED;
    }
    if (os->active_count >= OS_MAX_ACTIVE_PROCESSES) {
        os_log_event_locked(os, "SIGUSR1 rechazado: limite de %d activos",
                            OS_MAX_ACTIVE_PROCESSES);
        pthread_cond_broadcast(&os->state_cond);
        pthread_mutex_unlock(&os->mutex);
        pthread_cond_destroy(&process->cond);
        free(process);
        return EAGAIN;
    }

    process->pid = os->next_pid++;
    process->arrival_seq = ++os->next_arrival_seq;
    process->state = PROC_NEW;
    process->total_time_ms = random_duration_locked(os);
    process->remaining_time_ms = process->total_time_ms;
    process->counted_active = true;
    proc_list_add_locked(os, process);
    os->active_count++;
    os_log_event_locked(os, "P%d creado: NEW, rafaga=%d ms", process->pid,
                        process->total_time_ms);

    result = pthread_create(&process->thread, NULL, worker_main, process);
    if (result != 0) {
        os_log_event_locked(os, "No se pudo crear worker P%d: %s", process->pid,
                            strerror(result));
        proc_list_remove_locked(os, process);
        os->active_count--;
        pthread_mutex_unlock(&os->mutex);
        pthread_cond_destroy(&process->cond);
        free(process);
        return result;
    }

    process->worker_started = true;
    process->state = PROC_READY;
    process->ready_seq = ++os->next_ready_seq;
    os->total_created++;
    os_log_event_locked(os, "P%d: NEW -> READY", process->pid);
    pthread_cond_broadcast(&os->state_cond);
    pthread_mutex_unlock(&os->mutex);
    return 0;
}

int proc_reap_finished(os_context_t *os) {
    int reaped = 0;

    for (;;) {
        process_t *process = NULL;
        process_t *candidate;
        int result;

        pthread_mutex_lock(&os->mutex);
        for (candidate = os->process_list; candidate != NULL;
             candidate = candidate->next) {
            if (candidate->worker_started && candidate->worker_exited &&
                !candidate->joined && !candidate->reaping) {
                process = candidate;
                process->reaping = true;
                break;
            }
        }
        pthread_mutex_unlock(&os->mutex);

        if (process == NULL) {
            break;
        }

        result = pthread_join(process->thread, NULL);
        pthread_mutex_lock(&os->mutex);
        process->reaping = false;
        if (result == 0) {
            process->joined = true;
            if (process->cond_initialized) {
                pthread_cond_destroy(&process->cond);
                process->cond_initialized = false;
            }
            os_log_event_locked(os, "P%d recuperado con pthread_join",
                                process->pid);
            proc_list_prune_history_locked(os);
            reaped++;
        } else {
            os_log_event_locked(os, "pthread_join(P%d) fallo: %s", process->pid,
                                strerror(result));
        }
        pthread_cond_broadcast(&os->state_cond);
        pthread_mutex_unlock(&os->mutex);

        if (result != 0) {
            return -result;
        }
    }

    return reaped;
}

int proc_join_all(os_context_t *os) {
    int joined = 0;

    for (;;) {
        process_t *process = NULL;
        process_t *candidate;
        int result;

        pthread_mutex_lock(&os->mutex);
        for (candidate = os->process_list; candidate != NULL;
             candidate = candidate->next) {
            if (candidate->worker_started && !candidate->joined &&
                !candidate->reaping) {
                process = candidate;
                process->reaping = true;
                break;
            }
        }
        pthread_mutex_unlock(&os->mutex);

        if (process == NULL) {
            break;
        }

        result = pthread_join(process->thread, NULL);
        pthread_mutex_lock(&os->mutex);
        process->reaping = false;
        if (result != 0) {
            os_log_event_locked(os, "pthread_join final de P%d fallo: %s",
                                process->pid, strerror(result));
            pthread_mutex_unlock(&os->mutex);
            return -result;
        }
        process->joined = true;
        if (process->cond_initialized) {
            pthread_cond_destroy(&process->cond);
            process->cond_initialized = false;
        }
        joined++;
        pthread_mutex_unlock(&os->mutex);
    }

    return joined;
}
