#include "proc_list.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require_selection(const char *name, const process_t *actual,
                              const process_t *expected) {
    if (actual != expected) {
        fprintf(stderr, "FALLO %s: se esperaba P%d y se obtuvo P%d\n", name,
                expected == NULL ? 0 : expected->pid,
                actual == NULL ? 0 : actual->pid);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    os_context_t os;
    process_t first;
    process_t second;
    process_t third;

    memset(&os, 0, sizeof(os));
    memset(&first, 0, sizeof(first));
    memset(&second, 0, sizeof(second));
    memset(&third, 0, sizeof(third));

    first.pid = 1;
    first.state = PROC_READY;
    first.total_time_ms = 800;
    first.remaining_time_ms = 800;
    first.arrival_seq = 1;
    first.ready_seq = 4;
    first.next = &second;

    second.pid = 2;
    second.state = PROC_READY;
    second.total_time_ms = 300;
    second.remaining_time_ms = 600;
    second.arrival_seq = 2;
    second.ready_seq = 2;
    second.next = &third;

    third.pid = 3;
    third.state = PROC_READY;
    third.total_time_ms = 500;
    third.remaining_time_ms = 200;
    third.arrival_seq = 3;
    third.ready_seq = 3;

    os.process_list = &first;
    require_selection("FCFS", proc_list_select_fcfs_locked(&os), &first);
    require_selection("SJF", proc_list_select_sjf_locked(&os), &second);
    require_selection("SRTF", proc_list_select_srtf_locked(&os), &third);
    require_selection("RR", proc_list_select_rr_locked(&os), &second);

    first.total_time_ms = second.total_time_ms;
    first.remaining_time_ms = third.remaining_time_ms;
    require_selection("SJF tie", proc_list_select_sjf_locked(&os), &first);
    require_selection("SRTF tie", proc_list_select_srtf_locked(&os), &first);

    puts("OK: criterios FCFS/SJF/SRTF/RR");
    return EXIT_SUCCESS;
}
