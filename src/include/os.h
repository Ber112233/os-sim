#ifndef OS_H
#define OS_H

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define OS_MAX_ACTIVE_PROCESSES 20
#define OS_TERMINATED_HISTORY 10
#define OS_EVENT_CAPACITY 10
#define OS_EVENT_TEXT_SIZE 160
#define OS_WORK_UNIT_MS 10
#define OS_MIN_QUANTUM_MS 50
#define OS_DEFAULT_QUANTUM_MS 500
#define OS_MONITOR_INTERVAL_MS 250

typedef enum {
    ALG_FCFS,
    ALG_SJF,
    ALG_SRTF,
    ALG_RR
} scheduler_algorithm_t;

typedef enum {
    PROC_NEW,
    PROC_READY,
    PROC_RUNNING,
    PROC_TERMINATED
} process_state_t;

struct os_context;

typedef struct process {
    int pid;
    pthread_t thread;
    process_state_t state;
    int total_time_ms;
    int remaining_time_ms;
    int executed_time_ms;
    unsigned long arrival_seq;
    unsigned long ready_seq;
    bool worker_started;
    bool worker_exited;
    bool joined;
    bool reaping;
    bool cond_initialized;
    bool counted_active;
    bool completed_normally;
    pthread_cond_t cond;
    struct os_context *os;
    struct process *next;
} process_t;

typedef struct os_context {
    process_t *process_list;
    process_t *running_process;
    scheduler_algorithm_t algorithm;
    int quantum_ms;
    int next_pid;
    unsigned long next_arrival_seq;
    unsigned long next_ready_seq;
    int active_count;
    int total_created;
    int finished_count;
    int interrupted_count;
    bool shutdown_requested;
    unsigned int rng_state;
    pthread_mutex_t mutex;
    pthread_cond_t state_cond;
    sigset_t signal_set;
    char events[OS_EVENT_CAPACITY][OS_EVENT_TEXT_SIZE];
    size_t event_start;
    size_t event_count;
    struct timespec started_at;
} os_context_t;

int os_context_init(os_context_t *os, scheduler_algorithm_t algorithm,
                    int quantum_ms);
void os_context_destroy(os_context_t *os);
void os_request_shutdown(os_context_t *os, const char *reason);
void os_log_event_locked(os_context_t *os, const char *format, ...);
const char *os_algorithm_name(scheduler_algorithm_t algorithm);
const char *os_process_state_name(process_state_t state);
void os_timespec_after_ms(struct timespec *deadline, int milliseconds);

#endif
