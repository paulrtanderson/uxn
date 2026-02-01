#ifndef COND_LIST_H
#define COND_LIST_H

#include <stddef.h>
#include <pthread.h>

/* Valid handles are 0..65535 (Uint16 range) */
#define CT_HANDLE_MAX   65535UL

/* Error "handles" live above CT_HANDLE_MAX */
#define CT_ERR_NO_MEMORY  (CT_HANDLE_MAX + 1UL)
#define CT_ERR_INIT_FAIL  (CT_HANDLE_MAX + 2UL)
#define CT_ERR_BAD_HANDLE (CT_HANDLE_MAX + 3UL)

typedef struct {
    pthread_cond_t **slots;
    size_t capacity;
} CondTable;

/*
 * CondTable is an opaque registry that maps small integer handles
 * (0..65535) to pthread_cond_t pointers.
 *
 * The caller:
 *  - holds the CondTable storage (typically static/global)
 *  - must call cond_table_init() once before use
 */

/* Initialize the table with at least initial_capacity slots. */
int cond_table_init(CondTable *t, size_t initial_capacity);

/* Create a condition variable; returns handle (<= CT_HANDLE_MAX) or CT_ERR_* on error. */
unsigned long cond_table_create_cond(CondTable *t);

/* Destroy condition variable by handle; returns 0 on success or CT_ERR_BAD_HANDLE. */
unsigned long cond_table_destroy_cond(CondTable *t, unsigned long h);

/* Get the pthread_cond_t* associated with handle, or NULL on error. */
pthread_cond_t *cond_table_get_cond(CondTable *t, unsigned long h);

#endif /* COND_LIST_H */
