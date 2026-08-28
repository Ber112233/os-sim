#include "sig_handler.h"

#include "proc_mngr.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

void *signal_thread_main(void *argument) {
    os_context_t *os = argument;

    for (;;) {
        int received_signal = 0;
        int result = sigwait(&os->signal_set, &received_signal);

        if (result != 0) {
            char reason[OS_EVENT_TEXT_SIZE];
            snprintf(reason, sizeof(reason), "sigwait fallo: %s",
                     strerror(result));
            os_request_shutdown(os, reason);
            break;
        }

        if (received_signal == SIGUSR1) {
            proc_create(os);
        } else if (received_signal == SIGINT) {
            os_request_shutdown(os, "SIGINT -> apagado ordenado");
            break;
        } else if (received_signal == SIGTERM) {
            os_request_shutdown(os, "SIGTERM -> apagado ordenado");
            break;
        }
    }

    return NULL;
}
