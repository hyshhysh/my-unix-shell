#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include "executor.h"

int main(void) {
    char *line = NULL;
    size_t n = 0;

    while (1) {
        printf("%% ");
        fflush(stdout);
        if (getline(&line, &n, stdin) < 0) {
            putchar('\n');
            break; // EOF
        }

        Job *job = parse_line(line);
        if (!job) continue;

        execute_job(job);
        free_job(job);
    }

    free(line);
    return 0;
}
