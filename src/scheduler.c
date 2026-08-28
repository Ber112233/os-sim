#include "scheduler.h"

#include "proc_list.h"
#include "proc_mngr.h"

#include <errno.h>
#include <stdbool.h>

static process_t *select_next_locked(os_context_t *os) {
    switch (os->algorithm) {
        case ALG_FCFS:
            return proc_list_select_fcfs_locked(os);
        case ALG_SJF:
            return proc_list_select_sjf_locked(os);
        case ALG_SRTF:
            return proc_list_select_srtf_locked(os);
        case ALG_RR:
            return proc_list_select_rr_locked(os);
    }
    return NULL;
}

static bool srtf_precedes(const process_t *candidate,
                          const process_t *running) {
    return candidate != NULL &&
           (candidate->remaining_time_ms < running->remaining_time_ms ||
            (candidate->remaining_time_ms == running->remaining_time_ms &&
             candidate->arrival_seq < running->arrival_seq));
}

static void dispatch_locked(os_context_t *os, process_t *process) {
    process->state = PROC_RUNNING;
    os->running_process = process;
    os_log_event_locked(os, "%s: P%d READY -> RUNNING",
                        os_algorithm_name(os->algorithm), process->pid);
    pthread_cond_signal(&process->cond);
    pthread_cond_broadcast(&os->state_cond);
}

static void preempt_locked(os_context_t *os, process_t *process,
                           const char *reason) {
    process->state = PROC_READY;
    process->ready_seq = ++os->next_ready_seq;
    os->running_process = NULL;
    os_log_event_locked(os, "%s: P%d RUNNING -> READY", reason, process->pid);
    pthread_cond_broadcast(&os->state_cond);
}

void *scheduler_main(void *argument) {
    os_context_t *os = argument;
    bool rr_deadline_active = false;
    process_t *rr_process = NULL;
    struct timespec rr_deadline;

    for (;;) {
        process_t *selected;
        int wait_result = 0;

        proc_reap_finished(os);

        pthread_mutex_lock(&os->mutex);
        if (os->shutdown_requested) {
            pthread_mutex_unlock(&os->mutex);
            break;
        }

        if (os->algorithm == ALG_SRTF && os->running_process != NULL) {
            selected = proc_list_select_srtf_locked(os);
            if (srtf_precedes(selected, os->running_process)) {
                process_t *previous = os->running_process;
                preempt_locked(os, previous, "SRTF");
                dispatch_locked(os, selected);
            }
        }

        if (os->running_process == NULL) {
            rr_deadline_active = false;
            rr_process = NULL;
            selected = select_next_locked(os);
            if (selected != NULL) {
                dispatch_locked(os, selected);
                if (os->algorithm == ALG_RR) {
                    os_timespec_after_ms(&rr_deadline, os->quantum_ms);
                    rr_deadline_active = true;
                    rr_process = selected;
                }
            }
        }

        if (os->running_process == NULL) {
            pthread_cond_wait(&os->state_cond, &os->mutex);
        } else if (os->algorithm == ALG_RR) {
            if (!rr_deadline_active || rr_process != os->running_process) {
                os_timespec_after_ms(&rr_deadline, os->quantum_ms);
                rr_deadline_active = true;
                rr_process = os->running_process;
            }
            wait_result = pthread_cond_timedwait(&os->state_cond, &os->mutex,
                                                 &rr_deadline);
            if (wait_result == ETIMEDOUT && !os->shutdown_requested &&
                os->running_process == rr_process &&
                rr_process->state == PROC_RUNNING) {
                preempt_locked(os, rr_process, "RR quantum");
                rr_deadline_active = false;
                rr_process = NULL;
            } else if (os->running_process != rr_process) {
                rr_deadline_active = false;
                rr_process = NULL;
            }
        } else {
            pthread_cond_wait(&os->state_cond, &os->mutex);
        }
        pthread_mutex_unlock(&os->mutex);
    }

    pthread_mutex_lock(&os->mutex);
    os_log_event_locked(os, "Scheduler finalizado");
    pthread_mutex_unlock(&os->mutex);
    return NULL;
}
