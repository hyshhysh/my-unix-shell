#include "history.h"
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char*)malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

static int is_blank_line(const char *s) {
    if (!s) return 1;
    while (*s) {
        if (!isspace((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}
 
void history_init(History *h, size_t capacity) {
    h->items = (char**)calloc(capacity, sizeof *h->items);
    h->capacity = capacity;
    h->count = 0;
    h->head = 0;
    h->base = 1; // first number
}

void history_free(History *h){
    if (!h) return;
    if (h->items) {
        for (size_t i = 0; i < h->capacity; i++) {
            free(h->items[i]);
        }
        free(h->items);
    }
    h->items = NULL;
    h->capacity = h->count = h->head = 0;
    h->base = 1;
}

void history_add(History *h, const char *line) {
    if (!h || !h->items) return;
    if (!line || is_blank_line(line)) return;

    // store a copy without the trailing newline if present
    size_t n = strlen(line);
    while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) n--;
    char *copy = (char*)malloc(n + 1);
    if (!copy) return;
    memcpy(copy, line, n);
    copy[n] = '\0';

    // overwrite at head
    free(h->items[h->head]);
    h->items[h->head] = copy;

    h->head = (h->head + 1) % h->capacity;
    if (h->count < h->capacity) {
        h->count++;
    } else {
        // wrapped, visible starting num increments
        h->base++;
    }
}

void history_print(const History *h) {
    if (!h || h->count == 0) return;

    for (size_t i = 0; i < h->count; i++) {
        size_t idx = (h->head + h->capacity - h->count + i) % h->capacity;
        printf("%5zu  %s\n", h->base + i, h->items[idx] ? h->items[idx] : "");
    }
}

// helper to fetch by 1-based number N
static const char *history_get_by_number(const History *h, size_t N) {
    if (!h || h->count == 0) return NULL;
    //valid range [h->base, h->base + h->count - 1]
    if (N < h->base || N >= h->base + h->count) return NULL;
    size_t offset = N - h->base;
    size_t idx = (h->head + h->capacity - h->count + offset) % h->capacity;
    return h->items[idx];
}

static const char *history_last(const History *h) {
    if (!h || h->count == 0) return NULL;
    size_t last_idx = (h->head + h->capacity - 1) % h->capacity;
    return h->items[last_idx];
}

static const char *history_search_prefix(const History *h, const char *prefix) {
    if (!h || h->count == 0 || !prefix || !*prefix) return NULL;

    // search from newest to oldest
    for (size_t i = 0; i < h->count; i++) {
        size_t idx = (h->head + h->capacity - 1 - i) % h->capacity;
        const char *line = h->items[idx];
        if (line && strncmp(line, prefix, strlen(prefix)) == 0) {
            return line;
        }
    }
    return NULL;
}

bool history_expand_bang(const History *h, const char *in, char **out) {
    if (!h || !in || !out) return false;
    if (in[0] != '!') return false; // not a bang form
    
    // !!
    if (strcmp(in, "!!") == 0) {
        const char *line = history_last(h);
        if (!line) return false;
        *out = xstrdup(line);
        return *out != NULL;
    }

    //!N
    if (in[1] && isdigit((unsigned char)in[1])) {
        char *endp = NULL;
        long val = strtol(in + 1, &endp, 10);
        if (endp && *endp == '\0' && val > 0) {
            const char *line = history_get_by_number(h, (size_t)val);
            if (!line) return false;
            *out = xstrdup(line);
            return *out != NULL;
        }
        return false;
    }

    //!prefix
    if (in[1]) {
        const char *line = history_search_prefix(h, in + 1);
        if (!line) return false;
        *out = xstrdup(line);
        return *out != NULL;
    }
    return false;
}

