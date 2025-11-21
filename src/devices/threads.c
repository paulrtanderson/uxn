#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "../uxn.h"
#include "threads.h"
#include "../utils/mutex_array_list.h"
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
    0xD1:       ERRNO (read-only)
    0xD2-0xD3:  ARG_0 (16 bits)
    0xD4-0xD5:  ARG_1 (16 bits)
    0xD6-0xD7:  ARG_2 (16 bits)
    0xD8-0xD9:  RETURN (16 bits) (read-only)
    0xDA:       THREAD_USELOCALSTORAGEINDEX
*/

enum ThreadsPort {
  THREAD_THREAD_BASE          = 0xD0,

  THREAD_CMD                  = 0xD0,
  THREAD_STATUS               = 0xD1,

  ARG_0_LO                    = 0xD2,
  ARG_0_HI                    = 0xD3,

  ARG_1_LO                    = 0xD4,
  ARG_1_HI                    = 0xD5,

  ARG_2_LO                    = 0xD6,
  ARG_2_HI                    = 0xD7,

  RETURN_LO                   = 0xD8,
  RETURN_HI                   = 0xD9,

  THREAD_USELOCALSTORAGEINDEX = 0xDA,
};

typedef enum {
    ThreadCreate_OK = 0,
    ThreadCreate_SystemResources,
    ThreadCreate_InvalidAttributes,
    ThreadCreate_PermissionDenied,
    ThreadCreate_ATTR_INIT_OUT_OF_MEMORY,
    ThreadCreate_ThreadLimitReached,
} ThreadCreateError;

typedef enum {
    ThreadJoin_OK = 0,
    ThreadJoin_Deadlock,
    ThreadJoin_NotJoinable,
    ThreadJoin_NotFound,
} ThreadJoinError;

typedef enum {
    MutexInit_OK = 0,
    MutexInit_InvalidAttributes,
    MutexInit_SystemResources,
    MutexInit_PermissionDenied,
} MutexInitError;

enum { STATUS_OK = 0, STATUS_ERROR = 1 };

/* CMD meanings
1 - Create a new thread
2 - Join a thread
3 - Detach a thread
4 - Create a mutex, returns handle in RETURN
5 - Destroy a mutex given handle in ARG_0
6 - Lock a mutex given handle in ARG_0
7 - Unlock a mutex given handle in ARG_0
8 - Use local storage index
*/
enum {
  CMD_CREATE =        0x01,
  CMD_JOIN =          0x02,
  CMD_DETACH =        0x03,
  CMD_MUTEX_CREATE =  0x04,
  CMD_MUTEX_DESTROY = 0x05,
  CMD_MUTEX_LOCK =    0x06,
  CMD_MUTEX_UNLOCK =  0x07,
  CMD_USELOCALSTORAGEINDEX = 0x08,
};
typedef struct {
  pthread_t thread_handle;
  pthread_mutex_t thread_mutex;
  Uint16 arg_0;
  Uint16 arg_1;
  Uint16 arg_2;
  Uint16 result_value;
  bool is_in_use;
  bool is_detached;
  bool is_finished;
} ThreadRecord;

static MutexTable mutex_table = { NULL, 0 };
static bool mutex_table_initialized = false;

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
    
    /* Initialize mutex table here - safe because this runs before any threads are created */
    if (!mutex_table_initialized) {
      if (mutex_table_init(&mutex_table, 16) == 0) {
        mutex_table_initialized = true;
      }
    }
    
    mutex_init_done = true;

    /* first record is main thread */
    thread_records[0].is_in_use = true;
    thread_records[0].thread_handle = pthread_self();
  }
}

void destroy_mutexes() {
  if (mutex_init_done) {
    int i;
    for (i = 0; i < MAX_THREAD_COUNT; i++) {
      pthread_mutex_destroy(&thread_records[i].thread_mutex);
    }
    mutex_init_done = false;
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

/* Thread starts evaluating uxn code here */
static void *worker_thread_entry(void *p_worker_thread_args) {
  ThreadRecord *p_record = (ThreadRecord *)p_worker_thread_args;

  /* copy ram pointer from global variable to TLS */
  uxn.ram = shared_ram_ptr;

  device_set16(RETURN_LO, p_record->arg_1); /* set arg1 as ram pointer to arg data */

  log_printf("worker_thread_entry: thread_num=%d\n", (int)(p_record - thread_records));
  log_printf("worker_thread_entry: entry_address=0x%04x\n", p_record->arg_0);
  log_printf("uxn ram ptr: %p\n", uxn.ram);

  uxn_eval(p_record->arg_0);

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
  if (thread_id == (Uint8)-1) {
    log_printf("handle_create_command: no free thread slots\n");
    uxn.dev[THREAD_STATUS] = ThreadCreate_ThreadLimitReached;
    return;
  }

  thread_records[thread_id].arg_0 = entry_address;
  thread_records[thread_id].arg_1 = arg_ptr;
  thread_records[thread_id].arg_2 = flags;
  thread_records[thread_id].result_value = 0;


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

  int error = pthread_create(&thread_records[thread_id].thread_handle, attr_ptr,
               worker_thread_entry, (void *)&thread_records[thread_id]);
  if (attr_ptr) pthread_attr_destroy(attr_ptr);
  switch (error) {
    case 0:
      uxn.dev[THREAD_STATUS] = ThreadCreate_OK;
      break;
    case EAGAIN:
      uxn.dev[THREAD_STATUS] = ThreadCreate_SystemResources;
    case EINVAL:
      uxn.dev[THREAD_STATUS] = ThreadCreate_InvalidAttributes;
    case EPERM:
      uxn.dev[THREAD_STATUS] = ThreadCreate_PermissionDenied;
    default:
      thread_records[thread_id].is_in_use = false;
      log_printf("handle_create_command: pthread_create failed with error=%d\n", error);
      return;
  }

  log_printf("handle_create_command: created thread_id=%d\n", thread_id);
  print_thread_id(thread_records[thread_id].thread_handle);
  device_set16(RETURN_LO, thread_id);
}

/* when a JOIN command is received */
static void handle_join_command(Uint16 thread_num) {
  if (thread_num >= MAX_THREAD_COUNT) {
    log_printf("handle_join_command: invalid thread_num=%d\n", thread_num);
    uxn.dev[THREAD_STATUS] = ThreadJoin_NotFound;
    return;
  }
  ThreadRecord *record = &thread_records[thread_num];

  log_printf("handle_join_command: thread_num=%d\n", thread_num);
  print_thread_id(record->thread_handle);
  int error = pthread_join(record->thread_handle, NULL);
  switch (error) {
    case 0: 
      uxn.dev[THREAD_STATUS] = ThreadCreate_OK;
      break;
    case EDEADLK:
      uxn.dev[THREAD_STATUS] = ThreadJoin_Deadlock;
      return;
    case EINVAL:
      uxn.dev[THREAD_STATUS] = ThreadJoin_NotJoinable;
      return;
    case ESRCH:
      uxn.dev[THREAD_STATUS] = ThreadJoin_NotFound;
      return;
  }

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

Uint8 get_current_thread_num(void) {
    pthread_t self = pthread_self();
    Uint8 i;

    for (i = 0; i < MAX_THREAD_COUNT; i++) {
      log_printf("get_current_thread_num: checking thread_num=%d\n", i);
        if (thread_records[i].is_in_use) {
            if (pthread_equal(thread_records[i].thread_handle, self)) {
              log_printf("get_current_thread_num: found current thread at thread_num=%d\n", i);
                return i;
            }
        }
    }

    return (Uint8)-1;
}

/* Device write entry */
void threads_deo(Uint8 address) {
  if ((address & 0xf0) != THREAD_CMD) return; /* bitwise AND to check high nibble is in the thread device range */
  if ((address & 0x0f) != 0x00 && address != THREAD_USELOCALSTORAGEINDEX) return; /* bitwise AND to check address (low nibble is 0x0 or is THREAD_USELOCALSTORAGEINDEX) (only THREAD_CMD and THREAD_USELOCALSTORAGEINDEX have writable side affects) */

  initialize_mutexes();

  if (address == THREAD_USELOCALSTORAGEINDEX) {      /* max int will set to current thread num otherwise just use cmd val */
    log_printf("threads_deo: CMD_USELOCALSTORAGEINDEX with value=0x%02x\n", uxn.dev[THREAD_USELOCALSTORAGEINDEX]);
      if (uxn.dev[THREAD_USELOCALSTORAGEINDEX] == (Uint8)-1) {
          Uint8 thread_num = get_current_thread_num();
          log_printf("threads_deo: setting to current thread number %d\n", thread_num);
          uxn.dev[THREAD_USELOCALSTORAGEINDEX] = thread_num;
      }
      return;
  }

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
    }
    break;
  case CMD_MUTEX_CREATE: {
    unsigned long h;
    int rc;

    log_printf("threads_deo: CMD_MUTEX_CREATE\n");

    if (!mutex_table_initialized) {
      rc = mutex_table_init(&mutex_table, 16);
      if (rc != 0) {
        uxn.dev[THREAD_STATUS] = MutexInit_SystemResources;
        device_set16(RETURN_LO, 0);
        break;
      }
      mutex_table_initialized = true;
    }

    h = mutex_table_create_mutex(&mutex_table);
    if (h > MT_HANDLE_MAX) {
      /* Map all current creation errors to "system resources" */
      uxn.dev[THREAD_STATUS] = MutexInit_SystemResources;
      device_set16(RETURN_LO, 0);
    } else {
      uxn.dev[THREAD_STATUS] = MutexInit_OK;
      device_set16(RETURN_LO, (Uint16)h);
    }
    break;
  }
  case CMD_MUTEX_DESTROY: {
    Uint16 handle = device_get16(ARG_0_LO);
    unsigned long rc;

    log_printf("threads_deo: CMD_MUTEX_DESTROY\n");

    if (!mutex_table_initialized) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    rc = mutex_table_destroy_mutex(&mutex_table, (unsigned long)handle);
    if (rc == 0) {
      uxn.dev[THREAD_STATUS] = STATUS_OK;
    } else {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    }
    break;
  }
  case CMD_MUTEX_LOCK: {
    Uint16 handle = device_get16(ARG_0_LO);
    pthread_mutex_t *m;
    int rc;

    log_printf("threads_deo: CMD_MUTEX_LOCK\n");

    if (!mutex_table_initialized) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    m = mutex_table_get_mutex(&mutex_table, (unsigned long)handle);
    if (m == NULL) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    rc = pthread_mutex_lock(m);
    if (rc == 0)
      uxn.dev[THREAD_STATUS] = STATUS_OK;
    else
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    break;
  }
  case CMD_MUTEX_UNLOCK: {
    Uint16 handle = device_get16(ARG_0_LO);
    pthread_mutex_t *m;
    int rc;

    log_printf("threads_deo: CMD_MUTEX_UNLOCK\n");

    if (!mutex_table_initialized) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    m = mutex_table_get_mutex(&mutex_table, (unsigned long)handle);
    if (m == NULL) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    rc = pthread_mutex_unlock(m);
    if (rc == 0)
      uxn.dev[THREAD_STATUS] = STATUS_OK;
    else
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    break;
  }
  default:
    log_printf("threads_deo: Unknown command 0x%02x\n", uxn.dev[THREAD_CMD]);
    uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    break;
  }
}