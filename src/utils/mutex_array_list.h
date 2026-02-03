#ifndef MUTEX_LIST_H
#define MUTEX_LIST_H

#include <stddef.h>
#include <pthread.h>

/* Valid handles are 0..65535 (Uint16 range) */
#define MT_HANDLE_MAX   65535UL

/* Error "handles" live above MT_HANDLE_MAX */
#define MT_ERR_NO_MEMORY  (MT_HANDLE_MAX + 1UL)
#define MT_ERR_INIT_FAIL  (MT_HANDLE_MAX + 2UL)
#define MT_ERR_BAD_HANDLE (MT_HANDLE_MAX + 3UL)

typedef struct {
    pthread_mutex_t **slots;
    size_t capacity;
    pthread_mutex_t lock;  /* protects this table from concurrent access */
} MutexTable;

/*
 * MutexTable is an opaque registry that maps small integer handles
 * (0..65535) to pthread_mutex_t pointers.
 *
 * The caller:
 *  - holds the MutexTable storage (typically static/global)
 *  - must call mutex_table_init() once before use
 */

/* Initialize the table with at least initial_capacity slots. */
int mutex_table_init(MutexTable *t, size_t initial_capacity);

/* Create a mutex; returns handle (<= MT_HANDLE_MAX) or MT_ERR_* on error. */
unsigned long mutex_table_create_mutex(MutexTable *t);

/* Destroy mutex by handle; returns 0 on success or MT_ERR_BAD_HANDLE. */
unsigned long mutex_table_destroy_mutex(MutexTable *t, unsigned long h);

/* Get the pthread_mutex_t* associated with handle, or NULL on error. */
pthread_mutex_t *mutex_table_get_mutex(MutexTable *t, unsigned long h);

#endif /* MUTEX_LIST_H */