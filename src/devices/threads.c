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

#define LOGGING_ENABLED 0  /* Set to 0 to disable all logging */

#if LOGGING_ENABLED
#define log_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define log_printf(...) ((void)0)
#endif

/* CMD

*/


/*  Device: Threads
    Memory map: 0xd0–0xdf (16 bytes total)

    Layout:
      d0    CMD             Command register
      d1    STATUS          0=idle 2=ok 3=error
      d2–d3 PTR             Entry pointer (lo/hi)
      d4    ERRNO           Error code
      d5–d7 —               Reserved/padding
      d8–d9 RESULT          16‑bit result
      dA–dB OUT_THREAD      ID of the last created thread
      dC–dD —               Reserved/padding
      dE–dF TARGET_THREAD   ID of thread to join
*/

enum ThreadsPort {
  THREAD_THREAD_BASE         = 0xD0,

  THREAD_CMD               = 0xD0,
  THREAD_STATUS            = 0xD1,

  THREAD_PTR_LO            = 0xD2,
  THREAD_PTR_HI            = 0xD3,

  THREAD_ERRNO             = 0xD4,

  /* padding: 0xD5–0xD7 */

  THREAD_RESULT_LO         = 0xD8,
  THREAD_RESULT_HI         = 0xD9,

  THREAD_OUT_THREAD_LO     = 0xDA,
  THREAD_OUT_THREAD_HI     = 0xDB,

  /* padding: 0xDC–0xDD */

  THREAD_TARGET_THREAD_LO  = 0xDE,
  THREAD_TARGET_THREAD_HI  = 0xDF,

  THREAD_THREAD_END          = 0xDF
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
  Uint16 entry_address;
  Uint16 result_value;
  pthread_t thread_id;
  bool is_in_use;
  bool is_detached;
  bool is_finished;
  pthread_mutex_t thread_mutex;
} ThreadRecord;

#define MAX_THREAD_COUNT 8
static ThreadRecord thread_records[MAX_THREAD_COUNT];
pthread_mutex_t thread_record_mutex = PTHREAD_MUTEX_INITIALIZER;

static void print_thread_id(pthread_t th) {
  log_printf("thread_handle=%lu\n", (unsigned long)th);
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
static void *worker_thread_entry(void *args) {
  Uint8 thread_num = (Uint8)(uintptr_t)args;
  ThreadRecord* record = &thread_records[thread_num];
  log_printf("worker_thread_entry: thread_num=%d\n", thread_num);
  log_printf("worker_thread_entry: started\n");
  log_printf("worker_thread_entry: entry_address=0x%04x\n", record->entry_address);
  uxn = uxn_global; /* copy global uxn state to thread-local uxn state */
  log_printf("uxn ram ptr: %p\n", uxn.ram);
  uxn_eval(record->entry_address);

  /*log_printf("worker_thread_entry: top of stack: %d\n", uxn.wst.dat[uxn.wst.ptr - 1]);*/

  log_printf("worker_thread_entry: finished\n");
  Uint16 result = device_get16(THREAD_RESULT_LO);
  record->result_value = result;

  log_printf("worker_thread_entry: result_value=0x%04x\n", result);

  pthread_mutex_lock(&record->thread_mutex);
  record->is_finished = true;

  if (record->is_detached) {
      /* We are detached: free the slot for reuse */
      record->is_in_use = false;
      record->is_detached = false;
      record->is_finished = false;
  }

  pthread_mutex_unlock(&record->thread_mutex);
  return NULL;
}

/* when a CREATE command is received */
static void handle_create_command(void) {
  log_printf("handle_create_command: started\n");
  Uint16 entry_address = device_get16(THREAD_PTR_LO);
  Uint8 thread_id = find_first_free_thread_num();
  thread_records[thread_id].entry_address = entry_address;
  log_printf("handle_create_command: creating thread_id=%d\n", thread_id);
  log_printf("handle_create_command: entry_address=0x%04x\n", entry_address);
  pthread_create(&thread_records[thread_id].thread_handle, NULL,
               worker_thread_entry, (void *)(uintptr_t)thread_id);
  log_printf("handle_create_command: created ");
  print_thread_id(thread_records[thread_id].thread_handle);
  device_set16(THREAD_OUT_THREAD_LO, thread_id);
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

  device_set16(THREAD_RESULT_LO, record->result_value);


  log_printf("handle_join_command: joined with return value=0x%04x\n", record->result_value);
}

bool detach_thread(int i) {
    ThreadRecord *record = &thread_records[i];
    bool result = false;

    pthread_mutex_lock(&record->thread_mutex);

    if (!record->is_in_use)
        goto unlock;

    record->is_detached = true;
    pthread_detach(record->thread_id);

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
  if ((address & 0xf0) != 0xd0) return; /* bitwise AND to check high nibble is in the thread device range */
  if ((address & 0x0f) != 0x00) return; /* bitwise AND to check low nibble is 0x0 (only THREAD_CMD is writable) */

  switch (uxn.dev[THREAD_CMD]) {
  case CMD_CREATE:
    log_printf("threads_deo: CMD_CREATE\n");
    handle_create_command();
    break;
  case CMD_JOIN:
    log_printf("threads_deo: CMD_JOIN\n");
    Uint16 target_thread_id = device_get16(THREAD_TARGET_THREAD_LO);
    handle_join_command(target_thread_id);
    break;
  case CMD_DETACH:
    log_printf("threads_deo: CMD_DETACH\n");
    Uint16 detach_thread_id = device_get16(THREAD_TARGET_THREAD_LO);
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