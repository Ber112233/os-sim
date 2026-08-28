#include "monitor.h"

#include <ncurses.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MONITOR_MAX_ROWS (OS_MAX_ACTIVE_PROCESSES + OS_TERMINATED_HISTORY + 2)

typedef struct {
    int pid;
    process_state_t state;
    int total_time_ms;
    int remaining_time_ms;
    int executed_time_ms;
    bool completed_normally;
} process_snapshot_t;

typedef struct {
    scheduler_algorithm_t algorithm;
    int quantum_ms;
    int active_count;
    int total_created;
    int finished_count;
    int interrupted_count;
    int ready_count;
    int running_count;
    int running_pid;
    bool shutdown_requested;
    process_snapshot_t processes[MONITOR_MAX_ROWS];
    size_t process_count;
    char events[OS_EVENT_CAPACITY][OS_EVENT_TEXT_SIZE];
    size_t event_count;
} monitor_snapshot_t;

static void take_snapshot(os_context_t *os, monitor_snapshot_t *snapshot) {
    process_t *process;
    size_t index;

    memset(snapshot, 0, sizeof(*snapshot));
    pthread_mutex_lock(&os->mutex);
    snapshot->algorithm = os->algorithm;
    snapshot->quantum_ms = os->quantum_ms;
    snapshot->active_count = os->active_count;
    snapshot->total_created = os->total_created;
    snapshot->finished_count = os->finished_count;
    snapshot->interrupted_count = os->interrupted_count;
    snapshot->shutdown_requested = os->shutdown_requested;
    snapshot->running_pid = os->running_process == NULL
                                ? 0
                                : os->running_process->pid;

    for (process = os->process_list;
         process != NULL && snapshot->process_count < MONITOR_MAX_ROWS;
         process = process->next) {
        process_snapshot_t *row = &snapshot->processes[snapshot->process_count++];
        row->pid = process->pid;
        row->state = process->state;
        row->total_time_ms = process->total_time_ms;
        row->remaining_time_ms = process->remaining_time_ms;
        row->executed_time_ms = process->executed_time_ms;
        row->completed_normally = process->completed_normally;
        if (process->state == PROC_READY) {
            snapshot->ready_count++;
        } else if (process->state == PROC_RUNNING) {
            snapshot->running_count++;
        }
    }

    snapshot->event_count = os->event_count;
    for (index = 0; index < os->event_count; index++) {
        size_t source = (os->event_start + index) % OS_EVENT_CAPACITY;
        snprintf(snapshot->events[index], OS_EVENT_TEXT_SIZE, "%s",
                 os->events[source]);
    }
    pthread_mutex_unlock(&os->mutex);
}

static int color_for_state(process_state_t state) {
    switch (state) {
        case PROC_NEW:
            return 1;
        case PROC_READY:
            return 2;
        case PROC_RUNNING:
            return 3;
        case PROC_TERMINATED:
            return 4;
    }
    return 0;
}

static void draw_progress(int row, int column, int width,
                          const process_snapshot_t *process) {
    int completed;
    int index;

    if (width < 3 || process->total_time_ms <= 0) {
        return;
    }
    completed = process->executed_time_ms * width / process->total_time_ms;
    if (completed > width) {
        completed = width;
    }
    move(row, column);
    for (index = 0; index < width; index++) {
        addch(index < completed ? '#' : '.');
    }
}

static void draw_snapshot(const monitor_snapshot_t *snapshot) {
    int terminal_rows;
    int terminal_columns;
    int row = 0;
    size_t index;

    erase();
    getmaxyx(stdscr, terminal_rows, terminal_columns);
    if (terminal_rows < 8 || terminal_columns < 70) {
        mvaddnstr(0, 0, "Terminal demasiado pequena (minimo 70x8)",
                  terminal_columns - 1);
        refresh();
        return;
    }

    attron(A_BOLD);
    mvaddnstr(row++, 0, "OS PROCESS SCHEDULER SIMULATOR", terminal_columns - 1);
    attroff(A_BOLD);
    mvprintw(row++, 0, "PID OS: %ld   Algorithm: %s   Quantum: ", (long)getpid(),
             os_algorithm_name(snapshot->algorithm));
    if (snapshot->algorithm == ALG_RR) {
        printw("%d ms", snapshot->quantum_ms);
    } else {
        printw("N/A");
    }
    mvprintw(row, 0,
             "Active: %d/%d  Ready: %d  Running: %d  Finished: %d  CPU: ",
             snapshot->active_count, OS_MAX_ACTIVE_PROCESSES,
             snapshot->ready_count, snapshot->running_count,
             snapshot->finished_count);
    if (snapshot->running_pid == 0) {
        printw("idle");
    } else {
        printw("P%d", snapshot->running_pid);
    }
    row++;
    mvhline(row++, 0, '-', terminal_columns - 1);
    mvaddnstr(row++, 0,
              "PID   STATE        TOTAL    REMAINING  EXECUTED   PROGRESS",
              terminal_columns - 1);

    for (index = 0; index < snapshot->process_count && row < terminal_rows - 4;
         index++, row++) {
        const process_snapshot_t *process = &snapshot->processes[index];
        int progress_column = 53;
        int progress_width = terminal_columns - progress_column - 1;
        int color = color_for_state(process->state);

        if (progress_width > 20) {
            progress_width = 20;
        }
        if (has_colors()) {
            attron(COLOR_PAIR(color));
        }
        mvprintw(row, 0, "P%-4d %-12s %6.2fs   %6.2fs    %6.2fs", process->pid,
                 os_process_state_name(process->state),
                 process->total_time_ms / 1000.0,
                 process->remaining_time_ms / 1000.0,
                 process->executed_time_ms / 1000.0);
        if (has_colors()) {
            attroff(COLOR_PAIR(color));
        }
        if (progress_width > 2) {
            draw_progress(row, progress_column, progress_width, process);
        }
    }

    if (row < terminal_rows - 2) {
        mvhline(row++, 0, '-', terminal_columns - 1);
        attron(A_BOLD);
        mvaddnstr(row++, 0, "EVENTS", terminal_columns - 1);
        attroff(A_BOLD);
    }
    index = snapshot->event_count;
    while (index > 0 && row < terminal_rows - 1) {
        index--;
        mvaddnstr(row++, 0, snapshot->events[index], terminal_columns - 1);
    }
    if (row < terminal_rows) {
        mvaddnstr(terminal_rows - 1, 0,
                  "Signals: USR1=new process | INT/TERM=shutdown",
                  terminal_columns - 1);
    }
    refresh();
}

static bool terminal_supports_ui(void) {
    const char *term = getenv("TERM");
    return isatty(STDOUT_FILENO) && term != NULL && strcmp(term, "dumb") != 0;
}

void *monitor_main(void *argument) {
    os_context_t *os = argument;
    bool ui_enabled = terminal_supports_ui();
    bool curses_started = false;
    SCREEN *screen = NULL;

    if (ui_enabled && (screen = newterm(NULL, stdout, stdin)) != NULL) {
        set_term(screen);
        curses_started = true;
        cbreak();
        noecho();
        curs_set(0);
        if (has_colors()) {
            start_color();
            use_default_colors();
            init_pair(1, COLOR_YELLOW, -1);
            init_pair(2, COLOR_BLUE, -1);
            init_pair(3, COLOR_GREEN, -1);
            init_pair(4, COLOR_RED, -1);
        }
    }

    for (;;) {
        monitor_snapshot_t snapshot;
        struct timespec deadline;

        take_snapshot(os, &snapshot);
        if (curses_started) {
            draw_snapshot(&snapshot);
        }
        if (snapshot.shutdown_requested) {
            break;
        }

        os_timespec_after_ms(&deadline, OS_MONITOR_INTERVAL_MS);
        pthread_mutex_lock(&os->mutex);
        if (!os->shutdown_requested) {
            pthread_cond_timedwait(&os->state_cond, &os->mutex, &deadline);
        }
        pthread_mutex_unlock(&os->mutex);
    }

    if (curses_started) {
        endwin();
        delscreen(screen);
    }
    return NULL;
}
