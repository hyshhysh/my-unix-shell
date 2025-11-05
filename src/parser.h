#ifndef PARSER_H
#define PARSER_H
#include "shelltypes.h"

Job *parse_line(const char *line);
void free_job(Job *job);

#endif PARSER_H