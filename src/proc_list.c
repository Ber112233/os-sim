#include "include/proc_list.h"

#include <stdlib.h>

void proc_list_add_locked(os_context_t *os, process_t *process) {
    process_t **link = &os->process_list;

    while (*link != NULL) {
        link = &(*link)->next;
    }
    *link = process;
    process->next = NULL;
}

void proc_list_remove_locked(os_context_t *os, process_t *process) {
    process_t **link = &os->process_list;

    while (*link != NULL && *link != process) {
        link = &(*link)->next;
    }
    if (*link == process) {
        *link = process->next;
        process->next = NULL;
    }
}

static bool earlier_arrival(const process_t *candidate,
                            const process_t *selected) {
    return selected == NULL || candidate->arrival_seq < selected->arrival_seq;
}

process_t *proc_list_select_fcfs_locked(const os_context_t *os) {
    process_t *selected = NULL;
    process_t *process;

    for (process = os->process_list; process != NULL; process = process->next) {
        if (process->state == PROC_READY &&
            earlier_arrival(process, selected)) {
            selected = process;
        }
    }
    return selected;
}

process_t *proc_list_select_sjf_locked(const os_context_t *os) {
    process_t *selected = NULL;
    process_t *process;

    for (process = os->process_list; process != NULL; process = process->next) {
        if (process->state != PROC_READY) {
            continue;
        }
        if (selected == NULL || process->total_time_ms < selected->total_time_ms ||
            (process->total_time_ms == selected->total_time_ms &&
             process->arrival_seq < selected->arrival_seq)) {
            selected = process;
        }
    }
    return selected;
}

process_t *proc_list_select_srtf_locked(const os_context_t *os) {
    process_t *selected = NULL;
    process_t *process;

    for (process = os->process_list; process != NULL; process = process->next) {
        if (process->state != PROC_READY) {
            continue;
        }
        if (selected == NULL ||
            process->remaining_time_ms < selected->remaining_time_ms ||
            (process->remaining_time_ms == selected->remaining_time_ms &&
             process->arrival_seq < selected->arrival_seq)) {
            selected = process;
        }
    }
    return selected;
}

process_t *proc_list_select_rr_locked(const os_context_t *os) {
    process_t *selected = NULL;
    process_t *process;

    for (process = os->process_list; process != NULL; process = process->next) {
        if (process->state != PROC_READY) {
            continue;
        }
        if (selected == NULL || process->ready_seq < selected->ready_seq) {
            selected = process;
        }
    }
    return selected;
}

void proc_list_prune_history_locked(os_context_t *os) {
    size_t history_count = 0;
    process_t *process;

    for (process = os->process_list; process != NULL; process = process->next) {
        if (process->state == PROC_TERMINATED && process->joined) {
            history_count++;
        }
    }

    while (history_count > OS_TERMINATED_HISTORY) {
        process_t *oldest = NULL;

        for (process = os->process_list; process != NULL; process = process->next) {
            if (process->state == PROC_TERMINATED && process->joined) {
                oldest = process;
                break;
            }
        }
        if (oldest == NULL) {
            break;
        }
        proc_list_remove_locked(os, oldest);
        if (oldest->cond_initialized) {
            pthread_cond_destroy(&oldest->cond);
        }
        free(oldest);
        history_count--;
    }
}

void proc_list_destroy(os_context_t *os) {
    process_t *process = os->process_list;

    while (process != NULL) {
        process_t *next = process->next;
        if (process->cond_initialized) {
            pthread_cond_destroy(&process->cond);
        }
        free(process);
        process = next;
    }
    os->process_list = NULL;
    os->running_process = NULL;
}
