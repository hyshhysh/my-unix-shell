#include "parser.h"
#include "executor.h"
#include "builtins.h"
#include "history.h"
#include "string.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

ShellState shell_state = { "% " };
History history;


/* ---------- Helpers ---------- */
static void reap_background_children(void) {
    int status;
    for (;;) {
        pid_t p = waitpid(-1, &status, WNOHANG);
        if (p > 0) {
            fprintf(stderr, "[background done pid %d]\n", (int)p);
            continue;
        }
        if (p == 0) break; // nothing to reap
        if (p < 0 && errno == EINTR) continue; // interrupted, try again
        break; // other errors
    }
}

static void enable_raw_mode(struct termios *orig_termios) {
    tcgetattr(STDIN_FILENO, orig_termios);
    struct termios raw = *orig_termios;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);   // no canonical mode, no echo
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void disable_raw_mode(const struct termios *orig_termios) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, orig_termios);
}

static ssize_t read_line_with_history(char **lineptr, size_t *n, const History *hist) {
    struct termios orig;
    enable_raw_mode(&orig);

    size_t cap = *n ? *n : 128;
    char *buf = *lineptr ? *lineptr : malloc(cap);
    size_t len = 0;

    ssize_t hist_index = (ssize_t)hist->count; // one past last entry
    const char *current = NULL;

    write(STDOUT_FILENO, shell_state.prompt, strlen(shell_state.prompt));

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            buf[len++] = '\0';
            write(STDOUT_FILENO, "\n", 1);
            break;
        } else if (c == 127 || c == '\b') {        // backspace
            if (len > 0) {
                len--;
                write(STDOUT_FILENO, "\b \b", 3);
            }
        } else if (c == 27) {                      // ESC sequence
            char seq[2];
            if (read(STDIN_FILENO, seq, 2) == 2) {
                if (seq[0] == '[' && seq[1] == 'A') { // Up arrow
                    if (hist->count > 0 && hist_index > 0) {
                        hist_index--;
                        current = hist->items[(hist->head + hist->capacity
                             - hist->count + (size_t)hist_index) % hist->capacity];
                        // clear line
                        for (size_t i = 0; i < len; i++) write(STDOUT_FILENO, "\b \b", 3);
                        len = strlen(current);
                        strncpy(buf, current, cap - 1);
                        buf[len] = '\0';
                        write(STDOUT_FILENO, current, len);
                    }
                } else if (seq[0] == '[' && seq[1] == 'B') { // Down arrow
                    if (hist_index < (ssize_t)hist->count - 1) {
                        hist_index++;
                        current = hist->items[(hist->head + hist->capacity
                             - hist->count + (size_t)hist_index) % hist->capacity];
                        for (size_t i = 0; i < len; i++) write(STDOUT_FILENO, "\b \b", 3);
                        len = strlen(current);
                        strncpy(buf, current, cap - 1);
                        buf[len] = '\0';
                        write(STDOUT_FILENO, current, len);
                    } else if (hist_index == (ssize_t)hist->count - 1) {
                        hist_index++;
                        for (size_t i = 0; i < len; i++) write(STDOUT_FILENO, "\b \b", 3);
                        len = 0;
                        buf[0] = '\0';
                    }
                }
            }
        } else {
            if (len + 1 >= cap) {
                cap *= 2;
                buf = realloc(buf, cap);
            }
            buf[len++] = c;
            write(STDOUT_FILENO, &c, 1);
        }
    }

    disable_raw_mode(&orig);
    *lineptr = buf;
    *n = cap;
    return (ssize_t)len;
}


/* ---------- Main logic ---------- */
int main(void) {
    char *line = NULL;
    size_t n = 0;

    history_init(&history, 1000);
    // ignore interactive signals in the shell process
    signal(SIGINT, SIG_IGN); // 'ctrl-c'
    signal(SIGQUIT, SIG_IGN); // 'ctrl-\'
    signal(SIGTSTP, SIG_IGN); // 'ctrl-z'

    while (1) {
        fflush(stdout);

        ssize_t r = read_line_with_history(&line, &n, &history);
        if (r <= 0) { putchar('\n'); break; }

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        reap_background_children(); // clean up finished & jobs

        // history expansion
        const char *to_parse = line;
        char *expanded = NULL;

        // Trim leading whitespace
        char *trim = line;
        while (*trim == ' ' || *trim == '\t') trim++;
        
        if (trim[0] == '!') {
            if (history_expand_bang(&history, trim, &expanded)) {
                printf("%s\n", expanded);  // echo the expanded command like Bash
                to_parse = expanded;
            } else {
                fprintf(stderr, "history: event not found: %s\n", trim + 1);
                free(expanded);
                continue;  // skip to next prompt
            }
        }

        // add the effective command line to history
        history_add(&history, to_parse);

        // parse the line into one or more jobs
        JobList list = parse_line(to_parse);

        // iterate through each job and execute
        for (size_t i = 0; i < list.count; i++) {
            Job *job = list.jobs[i];
            if (!job || job->num_cmds == 0) continue;

            // executor
            execute_job(job);
        }
        // free everything parsed from this line
        free_job_list(&list);

        free(expanded);
        reap_background_children(); // clean again after executing a line
    }

    free(line);
    history_free(&history);
    return 0;
}
