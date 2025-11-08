#include "parser.h"
#include "shelltypes.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ---------- Utilities ---------- */
static void strip_trailing_newline(char *s) {
    if (!s) return;
    size_t n = strlen(s);
    if (n > 0 && s[n -1] == '\n') s[n - 1] = '\0';
}

typedef struct {
    char **data;
    size_t size;
    size_t cap;
} StrVec;

static void sv_init(StrVec *v) {
    v->data = NULL;
    v->size = 0;
    v->cap = 0;
}

static int sv_reserve(StrVec *v, size_t new_cap) {
    if (new_cap <= v->cap) return 1;
    size_t cap = v->cap ? v->cap : 8;
    while (cap < new_cap) cap *= 2;
    char **tmp = (char**)realloc(v->data, cap * sizeof *tmp);
    if (!tmp) return 0;
    v->data = tmp;
    v->cap = cap;
    return 1;
}

static int sv_push(StrVec *v, char *s) {
    if (!sv_reserve(v, v->size + 1)) return 0;
    v->data[v->size++] = s;
    return 1;
}

static void sv_free(StrVec *v) {
    if (v->data) {
        for (size_t i = 0; i < v->size; i++) {
            free(v->data[i]);
        }
        free(v->data);
    }
    v->data = NULL;
    v->size = v->cap = 0;
}

/* ---------- Tokenizer ---------- */

static int is_one_char_special(char c) {
    return (c == '|' || c == ';' || c == '&' || c == '<' || c == '>');
}

/* Tokenize with shell specials as separate tokens.
 * Handles | ; & < > and 2>
*/
static void tokenize_with_specials(char *buf, StrVec *out) {
    sv_init(out);
    char *p = buf;

    while (*p) {
        // skip leading whitespace
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        char *start = p;

        // handle specials
        if (p[0] == '2' && p[1] == '>') {
            // 2>
            p += 2;
        }
        else if (is_one_char_special(*p)) {
            p++;
        }
        else {
            // normal word
            while (*p && *p != ' ' && *p != '\t' && !is_one_char_special(*p)) {
                if (p[0] == '2' && p[1] == '>') break;
                p++;
            }
        }

        // Extract token substring and push copy
        char saved = *p;
        *p = '\0'; // terminate token
        
        // Push a duplicate string
        char *token_copy = strdup(start);
        if (token_copy) {
            sv_push(out, token_copy);
        }
        
        *p = saved; // restore char to continue

        if (*p == '\0') break;
    }
}


/* ---------- Job builders and cleaner ---------- */

static Command make_empty_command(void) {
    Command c;
    c.argv = NULL;
    c.input_file = NULL;
    c.output_file = NULL;
    c.error_file = NULL;
    return c;
}

static Command make_command_from_argv(StrVec *argv) {
    Command cmd = make_empty_command();
    cmd.argv = calloc(argv->size + 1, sizeof *cmd.argv);
    for (size_t i = 0; i < argv->size; i++) {
        cmd.argv[i] = argv->data[i]; //transfer ownership
        argv->data[i] = NULL;
    }
    cmd.argv[argv->size] = NULL;
    return cmd;
}

static void free_command(Command *c) {
    if (!c) return;
    if (c->argv) {
        for (size_t i = 0; c->argv[i]; i++)
            free(c->argv[i]);
        free(c->argv);
    }
    free(c->input_file);
    free(c->output_file);
    free(c->error_file);
}


void free_job(Job *job) {
    if (!job) return;
    if (job->commands) {
        for (size_t i = 0; i < job->num_cmds; i++) {
            free_command(&job->commands[i]);
        }
        free(job->commands);
    }
    free(job);
}

void free_job_list(JobList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) {
        free_job(list->jobs[i]);
    }
    free(list->jobs);
    list->jobs = NULL;
    list->count = 0;
}


/* ---------- Parser ---------- */
JobList parse_line(const char *line_in) {
    JobList list  = {0}; // structure holding all parsed jobs
    if (!line_in) return list;

    // make a working copy of the input for modifications
    char *buf = strdup(line_in);
    if (!buf) {
        perror("strdup");
        return list;
    }
    strip_trailing_newline(buf);

    // 1. tokenize the line
    StrVec tokens;
    tokenize_with_specials(buf, &tokens);
    // no tokens if empty line
    if (tokens.size == 0) {
        sv_free(&tokens);
        free(buf);
        return list;
    }

    // 2. setup variables for the current job
    Job *current_job = calloc(1, sizeof *current_job);
    current_job->commands = NULL;
    current_job->num_cmds = 0;
    current_job->background = false;
    current_job->sequential = false;

    // temporary holders for the current command being built
    StrVec argv;
    sv_init(&argv);

    char *input_file = NULL;
    char *output_file = NULL;
    char *error_file = NULL;

    // 3. main token scan loop
    for (size_t i = 0; i < tokens.size; i++) {
        char *t = tokens.data[i];

        // job separator (; and &)
        if (strcmp(t, ";") == 0 || strcmp(t, "&") == 0) {
            // flush any pending argv into a command
            if (argv.size > 0) {
                Command cmd = make_command_from_argv(&argv);
                cmd.input_file  = input_file;
                cmd.output_file = output_file;
                cmd.error_file  = error_file;
                input_file = output_file = error_file = NULL;

                current_job->commands = realloc(
                    current_job->commands, 
                    (current_job->num_cmds + 1) * sizeof *current_job->commands);
                current_job->commands[current_job->num_cmds++] = cmd;
            }
            sv_free(&argv);

            // mark job type
            current_job->background = (strcmp(t, "&") == 0);
            current_job->sequential = (strcmp(t, ";") == 0);

            // store this job in the list
            list.jobs = realloc(list.jobs, (list.count + 1) *sizeof *list.jobs);
            list.jobs[list.count++] = current_job;

            // start a fresh job
            current_job = calloc(1, sizeof *current_job);
            current_job->commands = NULL;
            current_job->num_cmds = 0;
            current_job->background = false;
            current_job->sequential = false;
            continue;
        }

        // pipeline split (|)
        if (strcmp(t, "|") == 0) {
            Command cmd = make_command_from_argv(&argv);
            cmd.input_file = input_file;
            cmd.output_file = output_file;
            cmd.error_file = error_file;
            input_file = output_file = error_file = NULL;

            current_job->commands = realloc(
                current_job->commands,
                (current_job->num_cmds +1) * sizeof *current_job->commands);
            current_job->commands[current_job->num_cmds++] = cmd;

            sv_free(&argv);
            sv_init(&argv);
            continue;
        }

        // redirections (<, >, 2>)
        if (strcmp(t, "<") == 0 && i + 1 < tokens.size) {
            free(input_file);
            input_file = strdup(tokens.data[++i]);
            continue;
        }
        if (strcmp(t, ">") == 0 && i + 1 < tokens.size) {
            free(output_file);
            output_file = strdup(tokens.data[++i]);
            continue;
        }
        if (strcmp(t, "2>") == 0 && i + 1 < tokens.size) {
            free(error_file);
            error_file = strdup(tokens.data[++i]);
            continue;
        }

        // normal word argument
        sv_push(&argv, strdup(t));
    }

    // 4. finalize the last command and job
    if (argv.size > 0) {
        Command cmd = make_command_from_argv(&argv);
        cmd.input_file = input_file;
        cmd.output_file = output_file;
        cmd.error_file = error_file;
        input_file = output_file = error_file = NULL;

        current_job->commands = realloc(
            current_job->commands,
            (current_job->num_cmds + 1) * sizeof *current_job->commands);
        current_job->commands[current_job->num_cmds++] = cmd;
    }
    sv_free(&argv);
    free(input_file);
    free(output_file);
    free(error_file);

    if (current_job->num_cmds > 0) {
        list.jobs = realloc(list.jobs, (list.count + 1) * sizeof *list.jobs);
        list.jobs[list.count++] = current_job;
    } else {
        free(current_job);
    }

    // 5. cleanup
    sv_free(&tokens);
    free(buf);

    return list;
}







