#ifndef HISTORY_H
#define HISTORY_H

#include <stddef.h>
#include <stdbool.h>

/* Simple ring buffer history.
* Stores up to `capacity`.
* Indexing exposed is 1-based like in Bash
*/

typedef struct {
    char **items;   // array of strdup lines
    size_t capacity; // max slots
    size_t count;   // number of stored entries (<= capacity)
    size_t head;    // next insertion index
    size_t base;    // 1-based number of oldest entry
} History;

void history_init(History *h, size_t capacity);
void history_free(History *h);

void history_add(History *h, const char *line);
void history_print(const History *h);

/* Bang expansions.
* !! - last entry
* !N - entry N
* !prefix - most recent entry starting with prefix
* On failure, returns false.
*/
bool history_expand_bang(const History *h, const char *in, char **out);

#endif // HISTORY.H

