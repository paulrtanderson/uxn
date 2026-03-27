#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>

#include "../uxn.h"
#include "threads.h"
#include "../utils/mutex_array_list.h"
#include "../utils/cond_array_list.h"
#include "../utils/barrier_array_list.h"
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

typedef enum {
    CondInit_OK = 0,
    CondInit_InvalidAttributes,
    CondInit_SystemResources,
    CondInit_PermissionDenied,
} CondInitError;

typedef enum {
    CondTimedWait_OK = 0,
    CondTimedWait_Timeout,
    CondTimedWait_InvalidArgs,
    CondTimedWait_ClockFail,
} CondTimedWaitResult;

typedef enum {
    BarrierInit_OK = 0,
    BarrierInit_InvalidAttributes,
    BarrierInit_SystemResources,
    BarrierInit_PermissionDenied,
} BarrierInitError;

enum { STATUS_OK = 0, STATUS_ERROR = 1 };

/* CMD meanings
0 - Return the current thread index in RETURN
1 - Create a new thread
2 - Join a thread
3 - Create a mutex, returns handle in RETURN
4 - Destroy a mutex given handle in ARG_0
5 - Lock a mutex given handle in ARG_0
6 - Unlock a mutex given handle in ARG_0
7 - Try lock a mutex given handle in ARG_0, RETURN=1 if acquired else 0
8 - Create a condition variable, returns handle in RETURN
9 - Destroy a condition variable given handle in ARG_0
A - Wait on a condition variable (ARG_0=cond, ARG_1=mutex)
B - Signal one thread waiting on condition variable in ARG_0
C - Broadcast all threads waiting on condition variable in ARG_0
D - Timed wait on a condition variable (ARG_0=cond, ARG_1=mutex, ARG_2=timeout_ms)
E - Create a barrier (ARG_0=thread count), returns handle in RETURN
F - Destroy a barrier given handle in ARG_0
10 - Wait on a barrier given handle in ARG_0, RETURN=1 if serial thread else 0
*/
enum {
  CMD_SELF =          0x00,
  CMD_CREATE =        0x01,
  CMD_JOIN =          0x02,
  CMD_MUTEX_CREATE =  0x03,
  CMD_MUTEX_DESTROY = 0x04,
  CMD_MUTEX_LOCK =    0x05,
  CMD_MUTEX_UNLOCK =  0x06,
  CMD_MUTEX_TRYLOCK = 0x07,
  CMD_COND_CREATE =   0x08,
  CMD_COND_DESTROY =  0x09,
  CMD_COND_WAIT =     0x0A,
  CMD_COND_SIGNAL =   0x0B,
  CMD_COND_BROADCAST = 0x0C,
  CMD_COND_TIMEDWAIT = 0x0D,
  CMD_BARRIER_CREATE =  0x0E,
  CMD_BARRIER_DESTROY = 0x0F,
  CMD_BARRIER_WAIT =    0x10,
};
typedef struct {
  pthread_t thread_handle;
  atomic_int in_use;
  Uint16 arg_0;
  Uint16 arg_1;
  Uint16 result_value;
} ThreadRecord;

/* TODO: fix race condition if two threads try to initialised this*/
static MutexTable mutex_table = { NULL, 0 };
static bool mutex_table_initialized = false;

static CondTable cond_table = { NULL, 0 };
static bool cond_table_initialized = false;

static BarrierTable barrier_table = { NULL, 0 };
static bool barrier_table_initialized = false;

#define MAX_THREAD_COUNT 16
static ThreadRecord thread_records[MAX_THREAD_COUNT];

Uint8 *shared_ram_ptr;

static void print_thread_id(pthread_t th) {
  log_printf("thread_handle=%lu\n", (unsigned long)th);
}

static bool mutex_init_done = false;

static void initialize_mutexes() {
  if (!mutex_init_done) {
    /* Initialize mutex table here - safe because this runs before any threads are created */
    if (!mutex_table_initialized) {
      if (mutex_table_init(&mutex_table, 16) == 0) {
        mutex_table_initialized = true;
      }
    }
    
    mutex_init_done = true;

    /* first record is main thread */
    atomic_store(&thread_records[0].in_use, 1);
    thread_records[0].thread_handle = pthread_self();
  }
}

void destroy_mutexes() {
  if (mutex_init_done) {
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
    int expected = 0;
    if (atomic_compare_exchange_strong(&thread_records[i].in_use, &expected, 1)) {
      return i;
    }
  }
  return (Uint8)-1;
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

  return NULL;
}



/* when a CREATE command is received */
static void handle_create_command(Uint16 entry_address, Uint16 arg_ptr) {
  Uint8 thread_id = find_first_free_thread_num();
  if (thread_id == (Uint8)-1) {
    log_printf("handle_create_command: no free thread slots\n");
    uxn.dev[THREAD_STATUS] = ThreadCreate_ThreadLimitReached;
    return;
  }

  thread_records[thread_id].arg_0 = entry_address;
  thread_records[thread_id].arg_1 = arg_ptr;
  thread_records[thread_id].result_value = 0;

  /* copy ram pointer from TLS to global variable */
  shared_ram_ptr = uxn.ram;

  int error = pthread_create(&thread_records[thread_id].thread_handle, NULL,
               worker_thread_entry, (void *)&thread_records[thread_id]);
  switch (error) {
    case 0:
      uxn.dev[THREAD_STATUS] = ThreadCreate_OK;
      break;
    case EAGAIN:
      uxn.dev[THREAD_STATUS] = ThreadCreate_SystemResources;
      break;
    case EINVAL:
      uxn.dev[THREAD_STATUS] = ThreadCreate_InvalidAttributes;
      break;
    case EPERM:
      uxn.dev[THREAD_STATUS] = ThreadCreate_PermissionDenied;
      break;
    default:
      break;
  }

  if (error != 0) {
    atomic_store(&thread_records[thread_id].in_use, 0);
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

  atomic_store(&record->in_use, 0);

  device_set16(RETURN_LO, record->result_value);

  log_printf("handle_join_command: joined with return value=0x%04x\n", record->result_value);
}

Uint8 get_current_thread_num(void) {
    pthread_t self = pthread_self();
    Uint8 i;

    for (i = 0; i < MAX_THREAD_COUNT; i++) {
      log_printf("get_current_thread_num: checking thread_num=%d\n", i);
        if (atomic_load(&thread_records[i].in_use)) {
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
  case CMD_SELF: {
    Uint8 thread_num = get_current_thread_num();

    log_printf("threads_deo: CMD_SELF\n");

    if (thread_num == (Uint8)-1) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      device_set16(RETURN_LO, 0);
    } else {
      uxn.dev[THREAD_STATUS] = STATUS_OK;
      device_set16(RETURN_LO, (Uint16)thread_num);
    }
    break;
  }
  case CMD_CREATE:
    log_printf("threads_deo: CMD_CREATE\n");
    Uint16 entry_address = device_get16(ARG_0_LO);
    Uint16 arg_ptr = device_get16(ARG_1_LO);
    log_printf("with args <entry_address=0x%04x, arg_ptr=0x%04x>\n", entry_address, arg_ptr);
    handle_create_command(entry_address, arg_ptr);
    break;
  case CMD_JOIN:
    log_printf("threads_deo: CMD_JOIN\n");
    Uint16 target_thread_id = device_get16(ARG_0_LO);
    handle_join_command(target_thread_id);
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
  case CMD_MUTEX_TRYLOCK: {
    Uint16 handle = device_get16(ARG_0_LO);
    pthread_mutex_t *m;
    int rc;

    log_printf("threads_deo: CMD_MUTEX_TRYLOCK\n");

    if (!mutex_table_initialized) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    m = mutex_table_get_mutex(&mutex_table, (unsigned long)handle);
    if (m == NULL) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    rc = pthread_mutex_trylock(m);
    if (rc == 0) {
      uxn.dev[THREAD_STATUS] = STATUS_OK;
      device_set16(RETURN_LO, 1); /* acquired */
    } else if (rc == EBUSY) {
      uxn.dev[THREAD_STATUS] = STATUS_OK;
      device_set16(RETURN_LO, 0); /* not acquired */
    } else {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    }
    break;
  }
  case CMD_COND_CREATE: {
    unsigned long h;
    int rc;

    log_printf("threads_deo: CMD_COND_CREATE\n");

    if (!cond_table_initialized) {
      rc = cond_table_init(&cond_table, 16);
      if (rc != 0) {
        uxn.dev[THREAD_STATUS] = CondInit_SystemResources;
        device_set16(RETURN_LO, 0);
        break;
      }
      cond_table_initialized = true;
    }

    h = cond_table_create_cond(&cond_table);
    if (h > CT_HANDLE_MAX) {
      /* Map all current creation errors to "system resources" */
      uxn.dev[THREAD_STATUS] = CondInit_SystemResources;
      device_set16(RETURN_LO, 0);
    } else {
      uxn.dev[THREAD_STATUS] = CondInit_OK;
      device_set16(RETURN_LO, (Uint16)h);
    }
    break;
  }
  case CMD_COND_DESTROY: {
    Uint16 handle = device_get16(ARG_0_LO);
    unsigned long rc;

    log_printf("threads_deo: CMD_COND_DESTROY\n");

    if (!cond_table_initialized) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    rc = cond_table_destroy_cond(&cond_table, (unsigned long)handle);
    if (rc == 0) {
      uxn.dev[THREAD_STATUS] = STATUS_OK;
    } else {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    }
    break;
  }
  case CMD_COND_WAIT: {
    Uint16 cond_handle = device_get16(ARG_0_LO);
    Uint16 mutex_handle = device_get16(ARG_1_LO);
    pthread_cond_t *c;
    pthread_mutex_t *m;
    int rc;

    log_printf("threads_deo: CMD_COND_WAIT cond=%d mutex=%d\n", cond_handle, mutex_handle);

    if (!cond_table_initialized || !mutex_table_initialized) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    c = cond_table_get_cond(&cond_table, (unsigned long)cond_handle);
    if (c == NULL) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    m = mutex_table_get_mutex(&mutex_table, (unsigned long)mutex_handle);
    if (m == NULL) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    rc = pthread_cond_wait(c, m);
    if (rc == 0)
      uxn.dev[THREAD_STATUS] = STATUS_OK;
    else
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    break;
  }
  case CMD_COND_SIGNAL: {
    Uint16 handle = device_get16(ARG_0_LO);
    pthread_cond_t *c;
    int rc;

    log_printf("threads_deo: CMD_COND_SIGNAL\n");

    if (!cond_table_initialized) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    c = cond_table_get_cond(&cond_table, (unsigned long)handle);
    if (c == NULL) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    rc = pthread_cond_signal(c);
    if (rc == 0)
      uxn.dev[THREAD_STATUS] = STATUS_OK;
    else
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    break;
  }
  case CMD_COND_BROADCAST: {
    Uint16 handle = device_get16(ARG_0_LO);
    pthread_cond_t *c;
    int rc;

    log_printf("threads_deo: CMD_COND_BROADCAST\n");

    if (!cond_table_initialized) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    c = cond_table_get_cond(&cond_table, (unsigned long)handle);
    if (c == NULL) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    rc = pthread_cond_broadcast(c);
    if (rc == 0)
      uxn.dev[THREAD_STATUS] = STATUS_OK;
    else
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    break;
  }
  case CMD_COND_TIMEDWAIT: {
    Uint16 cond_handle = device_get16(ARG_0_LO);
    Uint16 mutex_handle = device_get16(ARG_1_LO);
    Uint16 timeout_ms = device_get16(ARG_2_LO);
    pthread_cond_t *c;
    pthread_mutex_t *m;
    struct timespec ts;
    int rc;

    log_printf("threads_deo: CMD_COND_TIMEDWAIT cond=%d mutex=%d timeout_ms=%d\n",
               cond_handle, mutex_handle, timeout_ms);

    if (!cond_table_initialized || !mutex_table_initialized) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      device_set16(RETURN_LO, (Uint16)CondTimedWait_InvalidArgs);
      break;
    }

    c = cond_table_get_cond(&cond_table, (unsigned long)cond_handle);
    if (c == NULL) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      device_set16(RETURN_LO, (Uint16)CondTimedWait_InvalidArgs);
      break;
    }

    m = mutex_table_get_mutex(&mutex_table, (unsigned long)mutex_handle);
    if (m == NULL) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      device_set16(RETURN_LO, (Uint16)CondTimedWait_InvalidArgs);
      break;
    }

    /* Compute absolute deadline from relative ms timeout */
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      device_set16(RETURN_LO, (Uint16)CondTimedWait_ClockFail);
      break;
    }

    ts.tv_sec  += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
      ts.tv_sec  += 1;
      ts.tv_nsec -= 1000000000L;
    }

    rc = pthread_cond_timedwait(c, m, &ts);
    if (rc == 0) {
      uxn.dev[THREAD_STATUS] = STATUS_OK;
      device_set16(RETURN_LO, (Uint16)CondTimedWait_OK);
    } else if (rc == ETIMEDOUT) {
      uxn.dev[THREAD_STATUS] = STATUS_OK;
      device_set16(RETURN_LO, (Uint16)CondTimedWait_Timeout);
    } else {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      device_set16(RETURN_LO, (Uint16)CondTimedWait_InvalidArgs);
    }
    break;
  }
  case CMD_BARRIER_CREATE: {
    Uint16 count = device_get16(ARG_0_LO);
    unsigned long h;
    int rc;

    log_printf("threads_deo: CMD_BARRIER_CREATE count=%d\n", count);

    if (count == 0) {
      uxn.dev[THREAD_STATUS] = BarrierInit_InvalidAttributes;
      device_set16(RETURN_LO, 0);
      break;
    }

    if (!barrier_table_initialized) {
      rc = barrier_table_init(&barrier_table, 16);
      if (rc != 0) {
        uxn.dev[THREAD_STATUS] = BarrierInit_SystemResources;
        device_set16(RETURN_LO, 0);
        break;
      }
      barrier_table_initialized = true;
    }

    h = barrier_table_create_barrier(&barrier_table, (unsigned int)count);
    if (h > BT_HANDLE_MAX) {
      uxn.dev[THREAD_STATUS] = BarrierInit_SystemResources;
      device_set16(RETURN_LO, 0);
    } else {
      uxn.dev[THREAD_STATUS] = BarrierInit_OK;
      device_set16(RETURN_LO, (Uint16)h);
    }
    break;
  }
  case CMD_BARRIER_DESTROY: {
    Uint16 handle = device_get16(ARG_0_LO);
    unsigned long rc;

    log_printf("threads_deo: CMD_BARRIER_DESTROY handle=%d\n", handle);

    if (!barrier_table_initialized) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    rc = barrier_table_destroy_barrier(&barrier_table, (unsigned long)handle);
    if (rc == 0) {
      uxn.dev[THREAD_STATUS] = STATUS_OK;
    } else {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    }
    break;
  }
  case CMD_BARRIER_WAIT: {
    Uint16 handle = device_get16(ARG_0_LO);
    pthread_barrier_t *b;
    int rc;

    log_printf("threads_deo: CMD_BARRIER_WAIT handle=%d\n", handle);

    if (!barrier_table_initialized) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    b = barrier_table_get_barrier(&barrier_table, (unsigned long)handle);
    if (b == NULL) {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
      break;
    }

    rc = pthread_barrier_wait(b);
    if (rc == 0) {
      uxn.dev[THREAD_STATUS] = STATUS_OK;
      device_set16(RETURN_LO, 0);
    } else if (rc == PTHREAD_BARRIER_SERIAL_THREAD) {
      uxn.dev[THREAD_STATUS] = STATUS_OK;
      device_set16(RETURN_LO, 1);
    } else {
      uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    }
    break;
  }
  default:
    log_printf("threads_deo: Unknown command 0x%02x\n", uxn.dev[THREAD_CMD]);
    uxn.dev[THREAD_STATUS] = STATUS_ERROR;
    break;
  }
}