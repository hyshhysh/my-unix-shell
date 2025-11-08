#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include "executor.h"
#include "builtins.h"

ShellState shell_state = { "% "};

int main(void) {
    char *line = NULL;
    size_t n = 0;

    while (1) {
        printf("%s", shell_state.prompt);
        fflush(stdout);

        if (getline(&line, &n, stdin) < 0) {
            putchar('\n');
            break;  // EOF (Ctrl-D)
        }

        // parse the line into one or more jobs
        JobList list = parse_line(line);

        // iterate through each job and execute
        for (size_t i = 0; i < list.count; i++) {
            Job *job = list.jobs[i];
            if (!job || job->num_cmds == 0) continue;

            // executor stub
            execute_job(job);

            // later executor will:
            //  - set up pipes between job->commands
            //  - fork/exec each command
            //  - respect job->background and job->sequential flags
        }

        // free everything parsed from this line
        free_job_list(&list);
    }

    free(line);
    return 0;
}
