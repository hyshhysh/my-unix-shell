#include "parser.h"
#include <stdlib.h>
#include<stdio.h>

Job *parse_line(const char *line) {
    (void)line;
    // dummy job for executor testing purposes
    return NULL;
}

void free_job(Job *job) {
    (void)job;
}