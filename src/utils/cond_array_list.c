#include "cond_array_list.h"

#include <stdlib.h>

int cond_table_init(CondTable *t, size_t initial_capacity) {
    size_t i;

    t->slots =
        (pthread_cond_t **)malloc(initial_capacity *
                                  sizeof(pthread_cond_t *));
    if (t->slots == NULL)
        return -1;

    t->capacity = initial_capacity;

    for (i = 0; i < t->capacity; i++)
        t->slots[i] = NULL;

    return 0;
}

unsigned long cond_table_create_cond(CondTable *t) {
    size_t i;
    pthread_cond_t *c;

    /* find empty slot */
    for (i = 0; i < t->capacity; i++) {
        if (t->slots[i] == NULL) {
            c = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
            if (c == NULL)
                return CT_ERR_NO_MEMORY;

            if (pthread_cond_init(c, NULL) != 0) {
                free(c);
                return CT_ERR_INIT_FAIL;
            }

            t->slots[i] = c;
            return (unsigned long)i;
        }
    }

    /* grow table */
    {
        size_t old_capacity = t->capacity;
        size_t new_capacity = old_capacity * 2;
        pthread_cond_t **new_slots;

        new_slots = (pthread_cond_t **)realloc(
            t->slots, new_capacity * sizeof(pthread_cond_t *));
        if (new_slots == NULL)
            return CT_ERR_NO_MEMORY;

        t->slots = new_slots;
        t->capacity = new_capacity;

        for (i = old_capacity; i < new_capacity; i++)
            t->slots[i] = NULL;
    }

    /* retry after growing */
    return cond_table_create_cond(t);
}

unsigned long cond_table_destroy_cond(CondTable *t, unsigned long h) {
    if (h >= t->capacity)
        return CT_ERR_BAD_HANDLE;
    if (t->slots[h] == NULL)
        return CT_ERR_BAD_HANDLE;

    pthread_cond_destroy(t->slots[h]);
    free(t->slots[h]);
    t->slots[h] = NULL;

    return 0;
}

pthread_cond_t *cond_table_get_cond(CondTable *t, unsigned long h) {
    if (h >= t->capacity)
        return NULL;
    return t->slots[h];
}
