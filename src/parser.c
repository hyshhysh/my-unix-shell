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

static void free_command(Command *c) {
    if (!c) return;
    if (c->argv) {
        for (size_t i = 0; c->argv[i]; i++) {
            free(c->argv[i]); // free each strdup string
        }
        free(c->argv);
        c->argv = NULL;
    }
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

/* ---------- Parser ---------- */
Job *parse_line(const char *line_in) {
    if (!line_in) return NULL;

    // 1. Copy the input for modifying
    char *buf = strdup(line_in);
    if (!buf) {
        perror("strdup");
        return NULL;
    }
    strip_trailing_newline(buf);

    // 2. Tokenize by whitespace
    StrVec tokens;
    tokenize_with_specials(buf, &tokens);

    // 3. No tokens if empty line
    if (tokens.size == 0) {
        sv_free(&tokens);
        free(buf);
        return NULL;
    }

    // 4. Build a Command with argv[] from tokens
    Command cmd = make_empty_command();

    // Allocate argv with +1 for NULL terminator
    cmd.argv = (char**)calloc(tokens.size+1, sizeof *cmd.argv);
    if (!cmd.argv) {
        perror("calloc argv");
        sv_free(&tokens);
        free(buf);
        return NULL;
    }

    // Copy token strings into argv for job
    for (size_t i = 0; i < tokens.size; i++) {
        cmd.argv[i] = strdup(tokens.data[i]);
        if (!cmd.argv[i]) {
            perror("strdup argv");
            // clean partial argv
            for (size_t k = 0; k < i; k++) free(cmd.argv[k]);
            free(cmd.argv);
            sv_free(&tokens);
            free(buf);
            return NULL;
        }
    }
    cmd.argv[tokens.size] = NULL;

    // debug print
    for (size_t i = 0; i < tokens.size; i++) {
        fprintf(stderr, "[tok %zu] \"%s\"\n", i, tokens.data[i]);
    }


    // 5. Build a single command Job
    Job *job = (Job*)calloc(1, sizeof *job);
    if (!job) {
        perror("calloc job");
        free_command(&cmd);
        sv_free(&tokens);
        free(buf);
        return NULL;
    }

    job->commands = (Command*)calloc(1, sizeof *job->commands);
    if (!job->commands) {
        perror("calloc job->commands");
        free(job);
        free_command(&cmd);
        sv_free(&tokens);
        free(buf);
        return NULL;
    }
    job->commands[0] = cmd;
    job->num_cmds = 1;
    job->background = false;
    job->sequential = false;

    // 6. Cleanup temp tokenizer storage
    sv_free(&tokens);
    free(buf);

    return job;

}





