#ifndef PROC_LIST_H
#define PROC_LIST_H

#include "os.h"

/* Every function in this module must be called with os->mutex held. */
void proc_list_add_locked(os_context_t *os, process_t *process);
void proc_list_remove_locked(os_context_t *os, process_t *process);
process_t *proc_list_select_fcfs_locked(const os_context_t *os);
process_t *proc_list_select_sjf_locked(const os_context_t *os);
process_t *proc_list_select_srtf_locked(const os_context_t *os);
process_t *proc_list_select_rr_locked(const os_context_t *os);
void proc_list_prune_history_locked(os_context_t *os);
void proc_list_destroy(os_context_t *os);

#endif
