#include "os.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int os_context_init(os_context_t *os, scheduler_algorithm_t algorithm,
                    int quantum_ms) {
    pthread_condattr_t attributes;
    int result;

    memset(os, 0, sizeof(*os));
    os->algorithm = algorithm;
    os->quantum_ms = quantum_ms;
    os->next_pid = 1;
    os->rng_state = (unsigned int)time(NULL) ^ (unsigned int)getpid();
    clock_gettime(CLOCK_MONOTONIC, &os->started_at);

    result = pthread_mutex_init(&os->mutex, NULL);
    if (result != 0) {
        return result;
    }

    result = pthread_condattr_init(&attributes);
    if (result != 0) {
        pthread_mutex_destroy(&os->mutex);
        return result;
    }

    result = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (result == 0) {
        result = pthread_cond_init(&os->state_cond, &attributes);
    }
    pthread_condattr_destroy(&attributes);

    if (result != 0) {
        pthread_mutex_destroy(&os->mutex);
        return result;
    }

    return 0;
}

void os_context_destroy(os_context_t *os) {
    pthread_cond_destroy(&os->state_cond);
    pthread_mutex_destroy(&os->mutex);
}

void os_request_shutdown(os_context_t *os, const char *reason) {
    process_t *process;

    pthread_mutex_lock(&os->mutex);
    if (!os->shutdown_requested) {
        os->shutdown_requested = true;
        os_log_event_locked(os, "%s", reason);
    }

    pthread_cond_broadcast(&os->state_cond);
    for (process = os->process_list; process != NULL; process = process->next) {
        if (process->cond_initialized) {
            pthread_cond_broadcast(&process->cond);
        }
    }
    pthread_mutex_unlock(&os->mutex);
}

void os_log_event_locked(os_context_t *os, const char *format, ...) {
    struct timespec now;
    char message[OS_EVENT_TEXT_SIZE - 24];
    double elapsed;
    size_t index;
    va_list arguments;

    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed = (double)(now.tv_sec - os->started_at.tv_sec) +
              (double)(now.tv_nsec - os->started_at.tv_nsec) / 1000000000.0;

    if (os->event_count < OS_EVENT_CAPACITY) {
        index = (os->event_start + os->event_count) % OS_EVENT_CAPACITY;
        os->event_count++;
    } else {
        index = os->event_start;
        os->event_start = (os->event_start + 1U) % OS_EVENT_CAPACITY;
    }

    snprintf(os->events[index], OS_EVENT_TEXT_SIZE, "[%6.2fs] %s", elapsed,
             message);
}

const char *os_algorithm_name(scheduler_algorithm_t algorithm) {
    switch (algorithm) {
        case ALG_FCFS:
            return "FCFS";
        case ALG_SJF:
            return "SJF";
        case ALG_SRTF:
            return "SRTF";
        case ALG_RR:
            return "RR";
    }
    return "UNKNOWN";
}

const char *os_process_state_name(process_state_t state) {
    switch (state) {
        case PROC_NEW:
            return "NEW";
        case PROC_READY:
            return "READY";
        case PROC_RUNNING:
            return "RUNNING";
        case PROC_TERMINATED:
            return "TERMINATED";
    }
    return "UNKNOWN";
}

void os_timespec_after_ms(struct timespec *deadline, int milliseconds) {
    clock_gettime(CLOCK_MONOTONIC, deadline);
    deadline->tv_sec += milliseconds / 1000;
    deadline->tv_nsec += (long)(milliseconds % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}
