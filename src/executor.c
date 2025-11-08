#include "executor.h"
#include "shelltypes.h"
#include "builtins.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---------- Helpers ---------- */

// Returns 1 if argv[0] is a builtin command to handle in the parent
static int is_builtin(const char *name) {
    if (!name) return 0;
    if (strcmp(name, "cd") == 0) return 1;
    if (strcmp(name, "pwd") == 0) return 1;
    if (strcmp(name, "prompt") == 0) return 1;
    if (strcmp(name, "exit") == 0) return 1;
    if (strcmp(name, "history") == 0) return 1;
    return 0;
}

// Run the built in: return 0 if success
static int run_builtin(char **argv) {
    (void)argv;
    fprintf(stderr, "[executor] built ins not impleemented yet\n");
    return 0;
}

/* ---------- Core: run a single command ---------- */
static int run_single_command(const Command *cmd, int background) {
    if (!cmd || !cmd->argv || !cmd->argv[0]) {
        // Empty command - nothing to do
        return 0;
    }

    // If built in, handle in parent (no fork)
    if (is_builtin(cmd->argv[0])) {
        return run_builtin(cmd->argv);
    }

    // Fork a child to run external program
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* ---------- Child process ---------- */
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        
        // Redirection and pipes to be implemented
        // Execute program (search PATH, inherit environment)
        execvp(cmd->argv[0], cmd->argv);

        // if execvp returns, it failed
        perror("execvp");
        _exit(127); // 127 is conventional command not found/exec failed
    }

    /* ---------- Parent (shell) ---------- */
    if (background) {
        // don't wait, print PID to show background job
        printf("[background pid %d]\n", (int)pid);
        fflush(stdout);
        return 0;
    }

    // Foreground: wait for the child to finish
    int status = 0;
    for (;;) {
        pid_t w = waitpid(pid, &status, 0);
        if (w < 0) {
            if (errno == EINTR) continue; // interrupted by a signal - retry
            perror("waitpid");
            return -1;
        }
        break;
    }
    return 0;

}

/* ---------- Public entry point ---------- */
int execute_job(const Job *job) {
    if (!job || job->num_cmds == 0) return 0;

    if (job->num_cmds > 1) {
        // Pipelines to be implemented
        fprintf(stderr, "[executor] pipelines not implemented yet.\n");
        // only run first command for now to test
        return run_single_command(&job->commands[0], job->background);
    }

    // Singlle command job
    return run_single_command(&job->commands[0], job->background);
}

