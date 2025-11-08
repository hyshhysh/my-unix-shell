#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

bool bi_cd(char **argv) {
    const char *target = argv[1];
    if (!target) {
        target = getenv("HOME");
    }
    if (chdir(target) != 0) {
        perror("cd");
    }
    return true; // handled
}

bool bi_pwd(char **argv) {
    (void)argv;
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof buf)) {
        puts(buf);
    } else {
        perror("pwd");
    }
    return true;
}

bool bi_prompt(ShellState *st, char **argv) {
    if (!argv[1]) {
        fprintf(stderr, "usage: prompt NEWPROMPT\n");
        return true;
    }
    strncpy(st->prompt, argv[1], sizeof(st->prompt)-1);
    st->prompt[sizeof(st->prompt)-1] = '\0';
    return true;
}

bool bi_exit(char **argv) {
    (void)argv;
    exit(0);
}

bool bi_history(char **argv) {
    (void)argv;
    fprintf(stderr, "[history not yet implemented]\n");
    return true;
}
