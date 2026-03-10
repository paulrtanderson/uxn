#include "barrier_array_list.h"

#include <stdlib.h>

int barrier_table_init(BarrierTable *t, size_t initial_capacity) {
    size_t i;

    if (pthread_mutex_init(&t->lock, NULL) != 0)
        return -1;

    t->slots =
        (pthread_barrier_t **)malloc(initial_capacity *
                                     sizeof(pthread_barrier_t *));
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
static unsigned long barrier_table_create_barrier_locked(BarrierTable *t,
                                                         unsigned int count) {
    size_t i;
    pthread_barrier_t *b;

    /* find empty slot */
    for (i = 0; i < t->capacity; i++) {
        if (t->slots[i] == NULL) {
            b = (pthread_barrier_t *)malloc(sizeof(pthread_barrier_t));
            if (b == NULL)
                return BT_ERR_NO_MEMORY;

            if (pthread_barrier_init(b, NULL, count) != 0) {
                free(b);
                return BT_ERR_INIT_FAIL;
            }

            t->slots[i] = b;
            return (unsigned long)i;
        }
    }

    /* grow table */
    {
        size_t old_capacity = t->capacity;
        size_t new_capacity = old_capacity * 2;
        pthread_barrier_t **new_slots;

        new_slots = (pthread_barrier_t **)realloc(
            t->slots, new_capacity * sizeof(pthread_barrier_t *));
        if (new_slots == NULL)
            return BT_ERR_NO_MEMORY;

        t->slots = new_slots;
        t->capacity = new_capacity;

        for (i = old_capacity; i < new_capacity; i++)
            t->slots[i] = NULL;
    }

    /* retry after growing */
    return barrier_table_create_barrier_locked(t, count);
}

unsigned long barrier_table_create_barrier(BarrierTable *t,
                                           unsigned int count) {
    unsigned long result;

    pthread_mutex_lock(&t->lock);
    result = barrier_table_create_barrier_locked(t, count);
    pthread_mutex_unlock(&t->lock);

    return result;
}

unsigned long barrier_table_destroy_barrier(BarrierTable *t, unsigned long h) {
    unsigned long result;

    pthread_mutex_lock(&t->lock);

    if (h >= t->capacity) {
        pthread_mutex_unlock(&t->lock);
        return BT_ERR_BAD_HANDLE;
    }
    if (t->slots[h] == NULL) {
        pthread_mutex_unlock(&t->lock);
        return BT_ERR_BAD_HANDLE;
    }

    pthread_barrier_destroy(t->slots[h]);
    free(t->slots[h]);
    t->slots[h] = NULL;
    result = 0;

    pthread_mutex_unlock(&t->lock);
    return result;
}

pthread_barrier_t *barrier_table_get_barrier(BarrierTable *t, unsigned long h) {
    pthread_barrier_t *result;

    pthread_mutex_lock(&t->lock);

    if (h >= t->capacity) {
        pthread_mutex_unlock(&t->lock);
        return NULL;
    }
    result = t->slots[h];

    pthread_mutex_unlock(&t->lock);
    return result;
}
