#ifndef BUILTINS_H
#define BUILTINS_H

#include "history.h"

#include <stdbool.h>

extern History history;

typedef struct {
    char prompt[256];
} ShellState;

bool bi_cd(char **argv);
bool bi_pwd(char **argv);
bool bi_prompt(ShellState *st, char **argv);
bool bi_exit(char **argv);
bool bi_history(char **argv); // stub

#endif // BUILTINS.H

