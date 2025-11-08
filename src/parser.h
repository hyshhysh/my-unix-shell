#ifndef PARSER_H
#define PARSER_H
#include "shelltypes.h"

typedef struct Joblist {
    Job **jobs;
    size_t count;
} JobList;

JobList parse_line(const char *line);
void free_job_list(JobList *list);

#endif // PARSER_H
