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

ShellState shell_state = { "% " };
History history;

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

int main(void) {
    char *line = NULL;
    size_t n = 0;

    history_init(&history, 1000);
    // ignore interactive signals in the shell process
    signal(SIGINT, SIG_IGN); // 'ctrl-c'
    signal(SIGQUIT, SIG_IGN); // 'ctrl-\'
    signal(SIGTSTP, SIG_IGN); // 'ctrl-z'

    while (1) {
        printf("%s ", shell_state.prompt);
        fflush(stdout);

        ssize_t r = getline(&line, &n, stdin);
        if (r < 0) { putchar('\n'); break; }
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
