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
#include <fcntl.h>

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
            glob_t g;
            memset(&g, 0, sizeof g);
            
            int r = glob(arg, 0, NULL, &g);
            if (r == 0 && g.gl_pathc > 0) {
                // append all matches
                for (size_t j = 0; j < g.gl_pathc; j++) {
                    if (newsize + 1 >= newcap) {
                        newcap = newcap ? newcap * 2 : 8;
                        newargv = realloc(newargv, newcap * sizeof *newargv);
                    }
                    newargv[newsize++] = strdup(g.gl_pathv[j]);
                }
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

    // NULL terminate the new argv
    if (newsize + 1 >= newcap) {
        newcap = newcap ? newcap + 1 : 1;
        newargv = realloc(newargv, newcap * sizeof *newargv);
    }
    newargv[newsize] = NULL;

    // free old argv strings and array, swap in the new one
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
        
        /* ---------- I/O redirection ---------- */
        if (cmd->input_file) {
            int fd = open(cmd->input_file, O_RDONLY);
            if (fd < 0) {
                perror(cmd->input_file);
                _exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if (cmd->output_file) {
            int fd = open(cmd->output_file,
                O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror(cmd->output_file);
                _exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        if (cmd->error_file) {
            int fd = open(cmd->error_file,
                O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) {
                perror(cmd->error_file);
                _exit(1);
            }
            dup2(fd, STDERR_FILENO);
            close(fd);
        }


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
            if (errno == EINTR) continue;
            perror("waitpid");
            return -1;
        }

        // If child was terminated by signal, print newline
        if (WIFSIGNALED(status)) {
            write(STDOUT_FILENO, "\n", 1);
        }

        break;
    }

    return 0;

}

/* ---------- Public entry point ---------- */
int execute_job(const Job *job) {
    if (job->num_cmds > 1) {
        size_t num_pipes = job->num_cmds > 0 ? job->num_cmds - 1 : 0;
        int pipes[num_pipes][2];

        // create pipes
        for (size_t i = 0; i < num_pipes; i++) {
            if (pipe(pipes[i]) < 0) {
                perror("pipe");
                return -1;
            }
        }

        pid_t pids[job->num_cmds];

        for (size_t i =0; i < job->num_cmds; i++) {
            Command *cmd = &job->commands[i];
            expand_wildcards(cmd);

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                return -1;
            }

            if (pid == 0) {
                /* ---------- child ---------- */
                signal(SIGINT, SIG_DFL);
                signal(SIGQUIT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);

                // connect input
                if (i > 0) {
                    dup2(pipes[i-1][0], STDIN_FILENO);
                }
                // connect output
                if (i < num_pipes) {
                    dup2(pipes[i][1], STDOUT_FILENO);
                }
                
                // close all pipe ends
                for (size_t j = 0; j < num_pipes; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);   
                }

                // apply redirections
                if (cmd->input_file) {
                    int fd = open(cmd->input_file, O_RDONLY);
                    if (fd < 0) {
                        perror(cmd->input_file);
                        _exit(1);
                    }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
                if (cmd->output_file) {
                    int fd = open(cmd->output_file,
                        O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd < 0) {
                        perror(cmd->output_file);
                        _exit(1);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                if (cmd->error_file) {
                    int fd = open(cmd->error_file, 
                        O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd < 0) {
                        perror(cmd->error_file);
                        _exit(1);
                    }
                    dup2(fd, STDERR_FILENO);
                    close(fd);
                }

                execvp(cmd->argv[0], cmd->argv);
                perror("execvp");
                _exit(127);
            }

            /* ---------- parent ---------- */
            pids[i] = pid;

            // close ends not needed in parent
            if (i > 0) close(pipes[i-1][0]);
            if (i < num_pipes) close(pipes[i][1]);
        }

        // parent closes any remaining pipe read ends
        if (num_pipes > 0) close(pipes[num_pipes-1][0]);

        // wait unless background
        if (!job->background) {
            for (size_t i = 0; i < job->num_cmds; i++) {
                int status;
                waitpid(pids[i], &status, 0);
            }
        } else {
            printf("[background pipeline started]\n");
        }

        return 0;

    }

    // Single command job
    return run_single_command(&job->commands[0], job->background);
}

