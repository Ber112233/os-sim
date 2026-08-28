#include "worker.h"

#include "os.h"

#include <errno.h>
#include <time.h>

static void sleep_ms(int milliseconds) {
    struct timespec request;

    request.tv_sec = milliseconds / 1000;
    request.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    while (nanosleep(&request, &request) == -1 && errno == EINTR) {
    }
}

static void terminate_for_shutdown_locked(process_t *process) {
    os_context_t *os = process->os;

    if (process->state != PROC_TERMINATED) {
        process->state = PROC_TERMINATED;
    }
    if (process->counted_active) {
        process->counted_active = false;
        os->active_count--;
        os->interrupted_count++;
    }
    if (os->running_process == process) {
        os->running_process = NULL;
    }
    process->worker_exited = true;
    os_log_event_locked(os, "P%d detenido durante shutdown", process->pid);
    pthread_cond_broadcast(&os->state_cond);
}

void *worker_main(void *argument) {
    process_t *process = argument;
    os_context_t *os = process->os;

    for (;;) {
        int step_ms;

        pthread_mutex_lock(&os->mutex);
        while (!os->shutdown_requested && process->state != PROC_RUNNING) {
            pthread_cond_wait(&process->cond, &os->mutex);
        }

        if (os->shutdown_requested) {
            terminate_for_shutdown_locked(process);
            pthread_mutex_unlock(&os->mutex);
            return NULL;
        }

        step_ms = process->remaining_time_ms < OS_WORK_UNIT_MS
                      ? process->remaining_time_ms
                      : OS_WORK_UNIT_MS;
        pthread_mutex_unlock(&os->mutex);

        sleep_ms(step_ms);

        pthread_mutex_lock(&os->mutex);
        if (os->shutdown_requested) {
            terminate_for_shutdown_locked(process);
            pthread_mutex_unlock(&os->mutex);
            return NULL;
        }

        if (process->state == PROC_RUNNING) {
            process->remaining_time_ms -= step_ms;
            process->executed_time_ms += step_ms;

            if (process->remaining_time_ms <= 0) {
                process->remaining_time_ms = 0;
                process->executed_time_ms = process->total_time_ms;
                process->state = PROC_TERMINATED;
                process->completed_normally = true;
                process->worker_exited = true;
                if (process->counted_active) {
                    process->counted_active = false;
                    os->active_count--;
                    os->finished_count++;
                }
                if (os->running_process == process) {
                    os->running_process = NULL;
                }
                os_log_event_locked(os, "P%d: RUNNING -> TERMINATED",
                                    process->pid);
                pthread_cond_broadcast(&os->state_cond);
                pthread_mutex_unlock(&os->mutex);
                return NULL;
            }
        }
        pthread_mutex_unlock(&os->mutex);
    }
}
