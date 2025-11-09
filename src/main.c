#include "parser.h"
#include "executor.h"
#include "builtins.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

ShellState shell_state = { "% " };

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

    // ignore interactive signals in the shell process
    signal(SIGINT, SIG_IGN); // 'ctrl-c'
    signal(SIGQUIT, SIG_IGN); // 'ctrl-\'
    signal(SIGTSTP, SIG_IGN); // 'ctrl-z'

    while (1) {
        printf("%s ", shell_state.prompt);
        fflush(stdout);

        if (getline(&line, &n, stdin) < 0) {
            putchar('\n');
            break;  // EOF (Ctrl-D)
        }

        reap_background_children(); // clean up finished & jobs

        // parse the line into one or more jobs
        JobList list = parse_line(line);

        // iterate through each job and execute
        for (size_t i = 0; i < list.count; i++) {
            Job *job = list.jobs[i];
            if (!job || job->num_cmds == 0) continue;

            // executor
            execute_job(job);
        }
        // free everything parsed from this line
        free_job_list(&list);

        reap_background_children(); // clean again after executing a line
    }

    free(line);
    return 0;
}
