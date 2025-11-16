#include "mutex_array_list.h"

#include <stdlib.h>

int mutex_table_init(MutexTable *t, size_t initial_capacity) {
    size_t i;

    t->slots =
        (pthread_mutex_t **)malloc(initial_capacity *
                                   sizeof(pthread_mutex_t *));
    if (t->slots == NULL)
        return -1;

    t->capacity = initial_capacity;

    for (i = 0; i < t->capacity; i++)
        t->slots[i] = NULL;

    return 0;
}

unsigned long mutex_table_create_mutex(MutexTable *t) {
    size_t i;
    pthread_mutex_t *m;

    /* find empty slot */
    for (i = 0; i < t->capacity; i++) {
        if (t->slots[i] == NULL) {
            m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
            if (m == NULL)
                return MT_ERR_NO_MEMORY;

            if (pthread_mutex_init(m, NULL) != 0) {
                free(m);
                return MT_ERR_INIT_FAIL;
            }

            t->slots[i] = m;
            return (unsigned long)i;
        }
    }

    /* grow table */
    {
        size_t old_capacity = t->capacity;
        size_t new_capacity = old_capacity * 2;
        pthread_mutex_t **new_slots;

        new_slots = (pthread_mutex_t **)realloc(
            t->slots, new_capacity * sizeof(pthread_mutex_t *));
        if (new_slots == NULL)
            return MT_ERR_NO_MEMORY;

        t->slots = new_slots;
        t->capacity = new_capacity;

        for (i = old_capacity; i < new_capacity; i++)
            t->slots[i] = NULL;
    }

    /* retry after growing */
    return mutex_table_create_mutex(t);
}

unsigned long mutex_table_destroy_mutex(MutexTable *t, unsigned long h) {
    if (h >= t->capacity)
        return MT_ERR_BAD_HANDLE;
    if (t->slots[h] == NULL)
        return MT_ERR_BAD_HANDLE;

    pthread_mutex_destroy(t->slots[h]);
    free(t->slots[h]);
    t->slots[h] = NULL;

    return 0;
}

pthread_mutex_t *mutex_table_get_mutex(MutexTable *t, unsigned long h) {
    if (h >= t->capacity)
        return NULL;
    return t->slots[h];
}