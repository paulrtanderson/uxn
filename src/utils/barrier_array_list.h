#ifndef BARRIER_LIST_H
#define BARRIER_LIST_H

#include <stddef.h>
#include <pthread.h>

/* Valid handles are 0..65535 (Uint16 range) */
#define BT_HANDLE_MAX   65535UL

/* Error "handles" live above BT_HANDLE_MAX */
#define BT_ERR_NO_MEMORY  (BT_HANDLE_MAX + 1UL)
#define BT_ERR_INIT_FAIL  (BT_HANDLE_MAX + 2UL)
#define BT_ERR_BAD_HANDLE (BT_HANDLE_MAX + 3UL)

typedef struct {
    pthread_barrier_t **slots;
    size_t capacity;
    pthread_mutex_t lock;  /* protects this table from concurrent access */
} BarrierTable;

/* Initialize the table with at least initial_capacity slots. */
int barrier_table_init(BarrierTable *t, size_t initial_capacity);

/* Create a barrier with the given thread count; returns handle (<= BT_HANDLE_MAX) or BT_ERR_* on error. */
unsigned long barrier_table_create_barrier(BarrierTable *t, unsigned int count);

/* Destroy barrier by handle; returns 0 on success or BT_ERR_BAD_HANDLE. */
unsigned long barrier_table_destroy_barrier(BarrierTable *t, unsigned long h);

/* Get the pthread_barrier_t* associated with handle, or NULL on error. */
pthread_barrier_t *barrier_table_get_barrier(BarrierTable *t, unsigned long h);

#endif /* BARRIER_LIST_H */
