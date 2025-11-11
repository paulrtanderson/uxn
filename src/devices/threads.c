#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "../uxn.h"
#include "threads.h"
#include "system.h"

#if LOGGING_ENABLED
#define log_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define log_printf(...) ((void)0)
#endif


/*  Device: Threads
    Memory map: 0xd0–0xdf (16 bytes total)
    Layout:
    0xD0:       CMD (write-only)
    0xD1:       STATUS (read-only)
    0xD2-0xD3:  ARG_0 (16 bits)
    0xD4-0xD5:  ARG_1 (16 bits)
    0xD6-0xD7:  ARG_2 (16 bits)
    0xD8-0xD9:  RETURN (16 bits) (read-only)
    0xDA:       ERRNO (read-only)
*/

enum ThreadsPort {
  THREAD_THREAD_BASE         = 0xD0,

  THREAD_CMD               = 0xD0,
  THREAD_STATUS            = 0xD1,

  ARG_0_LO            = 0xD2,
  ARG_0_HI            = 0xD3,

  ARG_1_LO            = 0xD4,
  ARG_1_HI            = 0xD5,

  ARG_2_LO            = 0xD6,
  ARG_2_HI            = 0xD7,

  RETURN_LO           = 0xD8,
  RETURN_HI           = 0xD9,

  THREAD_ERRNO             = 0xDA,
};

enum { STATUS_IDLE = 0, STATUS_OK = 2, STATUS_ERROR = 3 };


/* CMD meanings
 1 - Create a new thread
 2 - Join a thread
 3 - Detach a thread
*/
enum { CMD_CREATE = 0x01, CMD_JOIN = 0x02, CMD_DETACH = 0x03 };

typedef struct {
  pthread_t thread_handle;
  pthread_mutex_t thread_mutex;
  Uint16 entry_address;
  Uint16 result_value;
  bool is_in_use;
  bool is_detached;
  bool is_finished;
} ThreadRecord;

#define MAX_THREAD_COUNT 8
static ThreadRecord thread_records[MAX_THREAD_COUNT];

Uint8 *shared_ram_ptr;

static void print_thread_id(pthread_t th) {
  log_printf("thread_handle=%lu\n", (unsigned long)th);
}

static bool mutex_init_done = false;

static void initialize_mutexes() {
  if (!mutex_init_done) {
    int i;
    for (i = 0; i < MAX_THREAD_COUNT; i++) {
      pthread_mutex_init(&thread_records[i].thread_mutex, NULL);
    }
    mutex_init_done = true;
  }
}

/* built in macro helpers */
/* read 16 bits from thread memory starting at low_address */
static Uint16 device_get16(Uint16 low_address) {
  return PEEK2(&uxn.dev[low_address]);
}
/* write 16 bits to thread ram starting at low_address */
static void device_set16(Uint16 low_address, Uint16 value) {
  POKE2(&uxn.dev[low_address], value);
}

/* Find a free record slot */
static Uint8 find_first_free_thread_num(void) {
  Uint8 i;
  for (i = 0; i < MAX_THREAD_COUNT; i++) {
    /* Non blocking check for free slot because if something has the mutex it must be in use */
    if (pthread_mutex_trylock(&thread_records[i].thread_mutex) == 0) {
      if (!thread_records[i].is_in_use) {
        thread_records[i].is_in_use = true;
        pthread_mutex_unlock(&thread_records[i].thread_mutex);
        return i;
      }
      pthread_mutex_unlock(&thread_records[i].thread_mutex);
    }
  }
  return -1;
}

/* get a thread record by id */
static ThreadRecord *get_thread_record(Uint16 thread_id) {
  if (thread_id >= MAX_THREAD_COUNT) return NULL;
  /*if (!thread_records[thread_id].is_in_use) return NULL;*/
  return &thread_records[thread_id];
}

/* Thread starts evaluating uxn code here */
static void *worker_thread_entry(void *p_worker_thread_args) {
  ThreadRecord *p_record = (ThreadRecord *)p_worker_thread_args;

  /* copy ram pointer from global variable to TLS */
  uxn.ram = shared_ram_ptr;

  log_printf("worker_thread_entry: thread_num=%d\n", (int)(p_record - thread_records));
  log_printf("worker_thread_entry: entry_address=0x%04x\n", p_record->entry_address);
  log_printf("uxn ram ptr: %p\n", uxn.ram);
  uxn_eval(p_record->entry_address);

  /*log_printf("worker_thread_entry: top of stack: %d\n", uxn.wst.dat[uxn.wst.ptr - 1]);*/

  log_printf("worker_thread_entry: finished\n");
  Uint16 result = device_get16(RETURN_LO);
  p_record->result_value = result;

  log_printf("worker_thread_entry: result_value=0x%04x\n", result);

  pthread_mutex_lock(&p_record->thread_mutex);
  p_record->is_finished = true;

  if (p_record->is_detached) {
      /* We are detached: free the slot for reuse */
      p_record->is_in_use = false;
      p_record->is_detached = false;
      p_record->is_finished = false;
  }

  pthread_mutex_unlock(&p_record->thread_mutex);
  return NULL;
}



/* when a CREATE command is received */
static void handle_create_command(Uint16 entry_address, Uint16 arg_ptr, Uint8 flags) {
  Uint8 thread_id = find_first_free_thread_num();
  thread_records[thread_id].entry_address = entry_address;

  /* copy ram pointer from TLS to global variable */
  shared_ram_ptr = uxn.ram;

  pthread_attr_t attr;
  pthread_attr_t *attr_ptr = NULL;
  thread_records[thread_id].is_detached = false;

  /* only flag is detach for now */
  if (flags == 1) {
    attr_ptr = &attr;
    pthread_attr_init(attr_ptr);
    pthread_attr_setdetachstate(attr_ptr, PTHREAD_CREATE_DETACHED);

    thread_records[thread_id].is_detached = true; 
  }

  pthread_create(&thread_records[thread_id].thread_handle, attr_ptr,
               worker_thread_entry, (void *)&thread_records[thread_id]);
  if (attr_ptr) pthread_attr_destroy(attr_ptr);
  
  log_printf("handle_create_command: created thread_id=%d\n", thread_id);
  print_thread_id(thread_records[thread_id].thread_handle);
  device_set16(RETURN_LO, thread_id);
}

/* when a JOIN command is received */
static void handle_join_command(Uint16 thread_num) {
  log_printf("handle_join_command: started\n");
  ThreadRecord *record = get_thread_record(thread_num);

  log_printf("handle_join_command: thread_num=%d\n", thread_num);
  print_thread_id(record->thread_handle);
  pthread_join(record->thread_handle, NULL);
  record->is_in_use = false;
  record->is_detached = false;
  record->is_finished = false;

  device_set16(RETURN_LO, record->result_value);


  log_printf("handle_join_command: joined with return value=0x%04x\n", record->result_value);
}

bool detach_thread(int i) {
    ThreadRecord *record = &thread_records[i];
    bool result = false;

    pthread_mutex_lock(&record->thread_mutex);

    if (!record->is_in_use)
        goto unlock;

    record->is_detached = true;
    pthread_detach(record->thread_handle);

    if (record->is_finished) {
        record->is_in_use = false;
        record->is_detached = false;
        record->is_finished = false;
    }

    result = true;

unlock:
    pthread_mutex_unlock(&record->thread_mutex);
    return result;
}

/* Device write entry */
void threads_deo(Uint8 address) {
  if ((address & 0xf0) != THREAD_CMD) return; /* bitwise AND to check high nibble is in the thread device range */
  if ((address & 0x0f) != 0x00) return; /* bitwise AND to check low nibble is 0x0 (only THREAD_CMD has writable side affects) */

  initialize_mutexes();

  switch (uxn.dev[THREAD_CMD]) {
  case CMD_CREATE:
    log_printf("threads_deo: CMD_CREATE\n");
    Uint16 entry_address = device_get16(ARG_0_LO);
    Uint16 arg_ptr = device_get16(ARG_1_LO);
    Uint8 flag = uxn.dev[ARG_2_LO];
    log_printf("with args <entry_address=0x%04x, arg_ptr=0x%04x, flags=0x%02x>\n", entry_address, arg_ptr, flag);
    handle_create_command(entry_address, arg_ptr, flag);
    break;
  case CMD_JOIN:
    log_printf("threads_deo: CMD_JOIN\n");
    Uint16 target_thread_id = device_get16(ARG_0_LO);
    handle_join_command(target_thread_id);
    break;
  case CMD_DETACH:
    log_printf("threads_deo: CMD_DETACH\n");
    Uint16 detach_thread_id = device_get16(ARG_0_LO);
    if (detach_thread(detach_thread_id)) {
        uxn.dev[THREAD_STATUS] = STATUS_OK;
    } else {
        uxn.dev[THREAD_STATUS] = STATUS_ERROR;
        uxn.dev[THREAD_ERRNO] = EINVAL;
    }
    break;
  default:
    log_printf("threads_deo: Unknown command 0x%02x\n", uxn.dev[THREAD_CMD]);
    uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    uxn.dev[THREAD_ERRNO] = EINVAL;
    break;
  }
}