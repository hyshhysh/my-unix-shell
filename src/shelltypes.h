#ifndef SHELLTYPES_H
#define SHELLTYPES_H

#include <stdbool.h>>
#include <stddef.h>

// Single command in a pipeline
typedef struct {
    char **argv;            // null-terminated argument list
    char *input_file;       // "<" redirection
    char *output_file;      // ">" redirection
    char *error_file;       // "2>" redirection
} Command;

// A full job which may include multiple commands
typedef struct {
    Command *commands;      // array of commands
    size_t num_cmds;        // number of commands
    bool background;        // ends with &
    bool sequential;        // ends with ;
} Job;


#endif SHELLTYPES_H