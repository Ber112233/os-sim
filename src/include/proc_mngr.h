#ifndef PROC_MNGR_H
#define PROC_MNGR_H

#include "os.h"

int proc_create(os_context_t *os);
int proc_reap_finished(os_context_t *os);
int proc_join_all(os_context_t *os);

#endif
