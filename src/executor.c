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
#include <glob.h>

extern ShellState shell_state;

/* ---------- Helpers ---------- */

// Returns 1 if argv[0] is a builtin command to handle in the parent
static int is_builtin(const char *name) {
    if (!name) return 0;
    return (
        strcmp(name, "cd") == 0 ||
        strcmp(name, "pwd") == 0 ||
        strcmp(name, "prompt") == 0 ||
        strcmp(name, "exit") == 0 ||
        strcmp(name, "history") == 0
    );
}

// Run the built in: return 0 if success
static int run_builtin(char **argv) {
    if (strcmp(argv[0], "cd") == 0) return bi_cd(argv);
    if (strcmp(argv[0], "pwd") == 0) return bi_pwd(argv);
    if (strcmp(argv[0], "prompt") == 0) return bi_prompt(&shell_state, argv);
    if (strcmp(argv[0], "exit") == 0) return bi_exit(argv);
    if (strcmp(argv[0], "history") == 0) return bi_history(argv);
    return 0;
}

/* ---------- Wildcard patterns (* or ?) in cmd->argv using glob(3) ---------- */
static void expand_wildcards(Command *cmd) {
    if (!cmd || !cmd->argv) return;

    // temporary dynamic list for expanded arguments
    size_t newcap = 0, newsize = 0;
    char **newargv = NULL;

    for (size_t i = 0; cmd->argv[i]; i++) {
        char *arg = cmd->argv[i];

        // check if argument contains * or ?
        if (strpbrk(arg, "*?")) {
            glob_t g = {0};
            int r = glob(arg, 0, NULL, &g);

            if (r == 0) {
                // append all matches
                for (size_t j = 0; j < g.gl_pathc; j++) {
                    if (newsize + 1 >= newcap) {
                        newcap = newcap ? newcap * 2 : 8;
                        newargv = realloc(newargv, newcap * sizeof *newargv);
                    }
                    newargv[newsize++] = strdup(arg);
                }
                globfree(&g);
            } else {
                // no matches: keep the original token
                if (newsize + 1 >= newcap) {
                    newcap = newcap ? newcap * 2 : 8;
                    newargv = realloc(newargv, newcap * sizeof *newargv);
                }
                newargv[newsize++] = strdup(arg);
            }
            globfree(&g);
        } else {
            // normal argument (no wildcard)
            if (newsize + 1 >= newcap) {
                newcap = newcap ? newcap * 2 : 8;
                newargv = realloc(newargv, newcap * sizeof *newargv);
            }
            newargv[newsize++] = strdup(arg);
        }
    }

    // NULL terminate
    if (newsize + 1 >= newcap) {
        newcap++;
        newargv = realloc(newargv, newcap * sizeof *newargv);
    }
    newargv[newsize] = NULL;

    // free old argv
    for (size_t i = 0; cmd->argv[i]; i++) free(cmd->argv[i]);
    free(cmd->argv);

    cmd->argv = newargv;
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

    // Expand any * or ? in arguments
    expand_wildcards((Command *)cmd);

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

