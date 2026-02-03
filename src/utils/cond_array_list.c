#include "cond_array_list.h"

#include <stdlib.h>

int cond_table_init(CondTable *t, size_t initial_capacity) {
    size_t i;

    if (pthread_mutex_init(&t->lock, NULL) != 0)
        return -1;

    t->slots =
        (pthread_cond_t **)malloc(initial_capacity *
                                  sizeof(pthread_cond_t *));
    if (t->slots == NULL) {
        pthread_mutex_destroy(&t->lock);
        return -1;
    }

    t->capacity = initial_capacity;

    for (i = 0; i < t->capacity; i++)
        t->slots[i] = NULL;

    return 0;
}

/* must be called with t->lock held */
static unsigned long cond_table_create_cond_locked(CondTable *t) {
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
    return cond_table_create_cond_locked(t);
}

unsigned long cond_table_create_cond(CondTable *t) {
    unsigned long result;

    pthread_mutex_lock(&t->lock);
    result = cond_table_create_cond_locked(t);
    pthread_mutex_unlock(&t->lock);

    return result;
}

unsigned long cond_table_destroy_cond(CondTable *t, unsigned long h) {
    unsigned long result;

    pthread_mutex_lock(&t->lock);

    if (h >= t->capacity) {
        pthread_mutex_unlock(&t->lock);
        return CT_ERR_BAD_HANDLE;
    }
    if (t->slots[h] == NULL) {
        pthread_mutex_unlock(&t->lock);
        return CT_ERR_BAD_HANDLE;
    }

    pthread_cond_destroy(t->slots[h]);
    free(t->slots[h]);
    t->slots[h] = NULL;
    result = 0;

    pthread_mutex_unlock(&t->lock);
    return result;
}

pthread_cond_t *cond_table_get_cond(CondTable *t, unsigned long h) {
    pthread_cond_t *result;

    pthread_mutex_lock(&t->lock);

    if (h >= t->capacity) {
        pthread_mutex_unlock(&t->lock);
        return NULL;
    }
    result = t->slots[h];

    pthread_mutex_unlock(&t->lock);
    return result;
}
