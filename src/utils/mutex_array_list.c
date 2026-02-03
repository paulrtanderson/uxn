#include "mutex_array_list.h"

#include <stdlib.h>

int mutex_table_init(MutexTable *t, size_t initial_capacity) {
    size_t i;

    if (pthread_mutex_init(&t->lock, NULL) != 0)
        return -1;

    t->slots =
        (pthread_mutex_t **)malloc(initial_capacity *
                                   sizeof(pthread_mutex_t *));
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
static unsigned long mutex_table_create_mutex_locked(MutexTable *t) {
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
    return mutex_table_create_mutex_locked(t);
}

unsigned long mutex_table_create_mutex(MutexTable *t) {
    unsigned long result;

    pthread_mutex_lock(&t->lock);
    result = mutex_table_create_mutex_locked(t);
    pthread_mutex_unlock(&t->lock);

    return result;
}

unsigned long mutex_table_destroy_mutex(MutexTable *t, unsigned long h) {
    unsigned long result;

    pthread_mutex_lock(&t->lock);

    if (h >= t->capacity) {
        pthread_mutex_unlock(&t->lock);
        return MT_ERR_BAD_HANDLE;
    }
    if (t->slots[h] == NULL) {
        pthread_mutex_unlock(&t->lock);
        return MT_ERR_BAD_HANDLE;
    }

    pthread_mutex_destroy(t->slots[h]);
    free(t->slots[h]);
    t->slots[h] = NULL;
    result = 0;

    pthread_mutex_unlock(&t->lock);
    return result;
}

pthread_mutex_t *mutex_table_get_mutex(MutexTable *t, unsigned long h) {
    pthread_mutex_t *result;

    pthread_mutex_lock(&t->lock);

    if (h >= t->capacity) {
        pthread_mutex_unlock(&t->lock);
        return NULL;
    }
    result = t->slots[h];

    pthread_mutex_unlock(&t->lock);
    return result;
}