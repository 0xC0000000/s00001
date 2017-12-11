#include "utils.h"

#include <stdio.h>
#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <dirent.h>

#include "rfmgmt_config.h"
#include "db_process.h"

#define CS_SIG_TIMER            SIGRTMIN+1
#define MAX_LOG_LINE_LEN        256

typedef struct {
  u32 max_count;
  pthread_mutex_t mutex;
  CsMsgQueueSt *queue;
  timer_t *timers;
} CsTimerSt;

typedef struct {
  int mode;
  u32 level;
  u16 size;
  FILE *fp;
  char *file_name;
  struct timespec base_time;
  char tag[32];
  pthread_mutex_t lock_mutex;
  char log_buffer[1024];
} CsLogInfoSt;

static CsTimerSt g_cs_timer;
static CsLogInfoSt g_cs_log = {.mode = LOG_DISABLE};

static b8 CsPostTimer(u32 timerID, CsMsgQueueSt* queue);
static void CsRemoveMsg(CsMsgSt* msg, CsMsgQueueSt* queue);

static void CsSetTimespecRelative(struct timespec* p_ts, long long msec) {
  struct timeval tv;

  gettimeofday(&tv, (struct timezone*) NULL);

  /* what's really funny about this is that I know
     pthread_cond_timedwait just turns around and makes this
     a relative time again */
  p_ts->tv_sec = tv.tv_sec + (msec / 1000);
  p_ts->tv_nsec = (tv.tv_usec + (msec % 1000) * 1000L ) * 1000L;
}

static void* CsTimerThread(void* arg) {
  sigset_t waitset;
  siginfo_t info;
  pthread_t ppid = pthread_self();

  pthread_detach(ppid);

  sigemptyset(&waitset);
  sigaddset(&waitset, CS_SIG_TIMER);
  while (1) {
    if (sigwaitinfo(&waitset, &info) != -1) {
      u32 timerID = (u32)info.si_value.sival_int;

      pthread_mutex_lock(&g_cs_timer.mutex);
      timer_delete(g_cs_timer.timers[timerID]);
      g_cs_timer.timers[timerID] = CS_TIMER_INVALID;
      pthread_mutex_unlock(&g_cs_timer.mutex);

      CsPostTimer(timerID, g_cs_timer.queue);
    }
  }

  return NULL;
}

void CsInitTimer(u32 max_count, CsMsgQueueSt* queue) {
  sigset_t bset, oset;
  pthread_t ppid;
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 256*1024);

  g_cs_timer.max_count = max_count;
  g_cs_timer.timers = malloc(max_count * sizeof(timer_t));

  // CS_TIMER_INVALID
  memset(g_cs_timer.timers, 0xff, max_count*sizeof(timer_t));

  pthread_mutex_init(&g_cs_timer.mutex, NULL);

  sigemptyset(&bset);
  sigaddset(&bset, CS_SIG_TIMER);
  if (pthread_sigmask(SIG_BLOCK, &bset, &oset) != 0) {
     _LOGE("timer init fail!");
    return;
  }

  g_cs_timer.queue = queue;
  pthread_create(&ppid, &attr, CsTimerThread, NULL);
  pthread_attr_destroy(&attr);
}

void CsDestroyTimer(void) {
  u32 i;

  for (i = 0; i < g_cs_timer.max_count; i++) {
    if (g_cs_timer.timers[i] != CS_TIMER_INVALID) {
      timer_delete(g_cs_timer.timers[i]);
    }
  }

  pthread_mutex_destroy(&g_cs_timer.mutex);

  CsFree(g_cs_timer.timers);
}

void CsStartTimer(u32 timerID, u32 timeMSec) {
  struct sigevent se;
  struct itimerspec ts;
  CsMsgSt msg;

  if (timerID >= g_cs_timer.max_count || timeMSec == 0) {
    return;
  }

  msg.msgID = timerID;
  msg.msgType = CS_MSG_TYPE_TIMER;
  CsRemoveMsg(&msg, g_cs_timer.queue);

  pthread_mutex_lock(&g_cs_timer.mutex);

  if (g_cs_timer.timers[timerID] == CS_TIMER_INVALID) {
    memset(&se, 0, sizeof(se));
    se.sigev_notify = SIGEV_SIGNAL;
    se.sigev_signo = CS_SIG_TIMER;
    se.sigev_value.sival_int = timerID;

    if (timer_create(CLOCK_REALTIME, &se, &g_cs_timer.timers[timerID]) < 0) {
      _LOGE("timer create failed\n");
      pthread_mutex_unlock(&g_cs_timer.mutex);
      return;
    }
  }

  ts.it_value.tv_sec = timeMSec / 1000;
  ts.it_value.tv_nsec = (timeMSec % 1000) * 1000000;
  ts.it_interval.tv_sec = 0;
  ts.it_interval.tv_nsec = 0;
  if (timer_settime(g_cs_timer.timers[timerID], 0, &ts, NULL) < 0) {
    _LOGE("timer set failed\n");
  }
  pthread_mutex_unlock(&g_cs_timer.mutex);
}

u32 CsReadLeftTimer(u32 timerID) {
  u32 left = 0;
  struct itimerspec ts;

  pthread_mutex_lock(&g_cs_timer.mutex);

  if (g_cs_timer.timers[timerID] != CS_TIMER_INVALID) {
    timer_gettime(g_cs_timer.timers[timerID], &ts);
    left = ts.it_value.tv_sec * 1000 + ts.it_value.tv_nsec / 1000000;
  }

  pthread_mutex_unlock(&g_cs_timer.mutex);

  return left;
}

void CsStopTimer(u32 timerID) {
  CsMsgSt msg;

  if (timerID >= g_cs_timer.max_count) {
    return;
  }

  pthread_mutex_lock(&g_cs_timer.mutex);

  if (g_cs_timer.timers[timerID] != CS_TIMER_INVALID) {
    struct itimerspec ts;

    ts.it_value.tv_sec = 0;
    ts.it_value.tv_nsec = 0;
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;
    timer_settime(g_cs_timer.timers[timerID], 0, &ts, NULL);
    timer_delete(g_cs_timer.timers[timerID]);
  }
  g_cs_timer.timers[timerID] = CS_TIMER_INVALID;
  pthread_mutex_unlock(&g_cs_timer.mutex);

  msg.msgID = timerID;
  msg.msgType = CS_MSG_TYPE_TIMER;
  CsRemoveMsg(&msg, g_cs_timer.queue);
}

CsMsgQueueSt* CsInitMsg(u32 timeout) {
  CsMsgQueueSt* queue;

  queue = malloc(sizeof(CsMsgQueueSt));
  if (queue == NULL) {
    return NULL;
  }
  memset(queue, 0, sizeof(CsMsgQueueSt));

  pthread_mutex_init(&queue->mutex, NULL);
  pthread_cond_init(&queue->cond, NULL);
  queue->timeout = timeout;

  return queue;
}

void CsDestroyMsg(CsMsgQueueSt* queue) {
  CsMsgSt* curMsg;

  if (queue == NULL) {
    return;
  }

  while (queue->pHead) {
    curMsg = queue->pHead;
    queue->pHead = queue->pHead->pNext;
    CsFree(curMsg);
  }

  pthread_mutex_destroy(&queue->mutex);
  pthread_cond_destroy(&queue->cond);

  CsFree(queue);
}

static void CsRemoveMsg(CsMsgSt* msg, CsMsgQueueSt* queue) {
  CsMsgSt *curMsg, *preMsg = NULL, *freeMsg;

  if (queue == NULL || queue->pHead == NULL) {
    return;
  }

  pthread_mutex_lock(&queue->mutex);

  curMsg = queue->pHead;

  while (curMsg) {
    if ((curMsg->msgType == msg->msgType) && (curMsg->msgID == msg->msgID)) {
      freeMsg = curMsg;

      if (curMsg->pNext == NULL) {
        queue->pTail = preMsg;
      }

      if (preMsg == NULL) {
        queue->pHead = curMsg->pNext;
      } else {
        preMsg->pNext = curMsg->pNext;
      }

      curMsg = curMsg->pNext;
      free(freeMsg);
    } else {
      preMsg = curMsg;
      curMsg = curMsg->pNext;
    }
  }
  pthread_mutex_unlock(&queue->mutex);
}

void CsPutMsg(CsMsgSt* curMsg, CsMsgQueueSt* queue) {
  if (queue == NULL || curMsg == NULL) {
    return;
  }

  pthread_mutex_lock(&queue->mutex);

  curMsg->pNext = NULL;
  if (queue->pHead == NULL) {
    queue->pHead = curMsg;
    queue->pTail = curMsg;
  } else {
    queue->pTail->pNext = curMsg;
    queue->pTail = curMsg;
  }

  pthread_cond_signal(&queue->cond);
  pthread_mutex_unlock(&queue->mutex);
}

CsMsgSt* CsGetMsg(CsMsgQueueSt* queue) {
  CsMsgSt* curMsg = NULL;

  if (queue == NULL) {
    return NULL;
  }

  pthread_mutex_lock(&queue->mutex);

  // if no msg, block
  if (queue->pHead == NULL) {
    if (queue->timeout != 0) {
      struct timespec ts;

      CsSetTimespecRelative(&ts, queue->timeout);
      pthread_cond_timedwait(&queue->cond, &queue->mutex, &ts);
    } else {
      // block-->unlock-->wait() return-->lock
      pthread_cond_wait(&queue->cond, &queue->mutex);
    }
  }

  if (queue->pHead) {
    curMsg = queue->pHead;
    queue->pHead = queue->pHead->pNext;
    curMsg->pNext = NULL;
  }

  pthread_mutex_unlock(&queue->mutex);

  return curMsg;
}

b8 CsPostEvent(u32 eventID, void* pParam, u32 paramSize, CsMsgQueueSt* queue) {
  if (queue == NULL) {
    return FALSE;
  }

  CsMsgSt* curMsg = malloc(sizeof(CsMsgSt) + paramSize);
  if (curMsg == NULL) {
    return FALSE;
  }

  u8* pParamPtr = (u8*) curMsg + sizeof(CsMsgSt);

  memset(curMsg, 0, sizeof(CsMsgSt));
  curMsg->msgType = CS_MSG_TYPE_EVENT;
  curMsg->msgID = eventID;
  curMsg->size = paramSize;
  if (paramSize) {
    memcpy(pParamPtr, pParam, paramSize);
  }

  CsPutMsg(curMsg, queue);
  return TRUE;
}

b8 CsPostStat(u32 objID, u32 stat, CsMsgQueueSt* queue) {
  if (queue == NULL) {
    return FALSE;
  }

  CsMsgSt* curMsg = malloc(sizeof(CsMsgSt));
  if (curMsg == NULL) {
    return FALSE;
  }

  memset(curMsg, 0, sizeof(CsMsgSt));
  curMsg->msgType = CS_MSG_TYPE_STAT;
  curMsg->msgID = objID;
  curMsg->status = stat;

  CsPutMsg(curMsg, queue);
  return TRUE;
}

b8 CsPostExit(CsMsgQueueSt* queue) {
  if (queue == NULL) {
    return FALSE;
  }

  CsMsgSt* curMsg = malloc(sizeof(CsMsgSt));
  if (curMsg == NULL) {
    return FALSE;
  }

  memset(curMsg, 0, sizeof(CsMsgSt));
  curMsg->msgType = CS_MSG_TYPE_EXIT;
  curMsg->msgID = 0;
  curMsg->status = 0;

  CsPutMsg(curMsg, queue);
  return TRUE;
}

static b8 CsPostTimer(u32 timerID, CsMsgQueueSt *queue) {
  if (queue == NULL) {
    return FALSE;
  }

  CsMsgSt *curMsg = malloc(sizeof(CsMsgSt));
  if (curMsg == NULL) {
    return FALSE;
  }

  memset(curMsg, 0, sizeof(CsMsgSt));
  curMsg->msgType = CS_MSG_TYPE_TIMER;
  curMsg->msgID = timerID;

  CsPutMsg(curMsg, queue);
  return TRUE;
}

b8 CsPostData(u32 dataID, void* pData, u32 size, CsMsgQueueSt* queue) {
  if (queue == NULL) {
    return FALSE;
  }

  CsMsgSt* curMsg = malloc(sizeof(CsMsgSt) + size);
  if (curMsg == NULL) {
    return FALSE;
  }

  u8* pDataPtr = (u8*) curMsg + sizeof(CsMsgSt);

  memset(curMsg, 0, sizeof(CsMsgSt));
  curMsg->msgType = CS_MSG_TYPE_DATA;
  curMsg->msgID = dataID;
  curMsg->size = size;
  if (size) {
    memcpy(pDataPtr, pData, size);
  }

  CsPutMsg(curMsg, queue);
  return TRUE;
}

void _msleep(long long msec) {
  struct timespec ts;
  int err;

  ts.tv_sec = (msec / 1000);
  ts.tv_nsec = (msec % 1000) * 1000 * 1000;

  do {
    err = nanosleep (&ts, &ts);
  } while (err < 0 && errno == EINTR);
}

u32 CsGetMs(void) {
  struct timespec tp;

  clock_gettime(CLOCK_MONOTONIC, &tp);
  return (u32)(tp.tv_sec * 1000 + tp.tv_nsec / 1000000);
}

s32 CsGetRandom(void) {
  struct timespec tp;

  clock_gettime(CLOCK_MONOTONIC, &tp);
  srand(tp.tv_nsec);

  return rand();
}

void CsFree(void* pPtr) {
  if (pPtr) {
    free(pPtr);
    pPtr = NULL;
  }
}

static u32 CsLogGenHeader(char* start_with) {
  struct timespec tp;
  struct tm* newtime;
  time_t long_time;
  u32 count;

  time(&long_time);
  newtime = localtime(&long_time);

  clock_gettime(CLOCK_MONOTONIC, &tp);
  count = sprintf(g_cs_log.log_buffer,
                  "[%s]%04d-%02d-%02d %02d:%02d:%02d(%06d,%03d) - %s",
                  g_cs_log.tag, newtime->tm_year + 1900, newtime->tm_mon + 1,
                  newtime->tm_mday, newtime->tm_hour, newtime->tm_min,
                  newtime->tm_sec,
                  (int)(tp.tv_sec - g_cs_log.base_time.tv_sec),
                  (int)(tp.tv_nsec/1000000), start_with);
  return count;
}

static void CsLogToFile() {
  // locked in caller.

  if (g_cs_log.mode == LOG_PRINTF) {
    printf("%s", g_cs_log.log_buffer);
  } else if (g_cs_log.fp) { // LOG_FILE
    if (access(g_cs_log.file_name, F_OK) != 0) {
      char* cmd;
      if (g_cs_log.fp != NULL)
        fclose(g_cs_log.fp);

      g_cs_log.fp = fopen(g_cs_log.file_name, "a");
      // give read permission to all user
      asprintf(&cmd, "chmod a+r %s", g_cs_log.file_name);
      system(cmd);
      free(cmd);
    }

    fprintf(g_cs_log.fp, "%s", g_cs_log.log_buffer);
    fflush(g_cs_log.fp);
  }
}

void CsLogOut(u32 level, const char* fmt, ...) {
  if (g_cs_log.mode == LOG_DISABLE)
    return;

  if (level >= g_cs_log.level) {
    va_list ap;
    char* out_buf = g_cs_log.log_buffer;

    // avoid muti-thread log conflict
    pthread_mutex_lock(&g_cs_log.lock_mutex);

    out_buf += CsLogGenHeader("");

    va_start(ap, fmt);
    vsnprintf(out_buf, (sizeof(g_cs_log.log_buffer) - 20), fmt, ap);
    strcat(out_buf, "\n\0");
    CsLogToFile();
    va_end(ap);

    pthread_mutex_unlock(&g_cs_log.lock_mutex);
  }
}

void CsUpdateLogCfg() {
  g_cs_log.mode = GetLogOut();
  g_cs_log.level = GetLogLevel();
  g_cs_log.size = GetLogSize();
}

void CsLogInit(char* log_name, char* log_tag) {

  memset(&g_cs_log, 0, sizeof(g_cs_log));

  pthread_mutex_init(&g_cs_log.lock_mutex, NULL);

  clock_gettime(CLOCK_MONOTONIC, &g_cs_log.base_time);

  g_cs_log.level = LOG_LOW;
  g_cs_log.mode = LOG_FILE;

  if (g_cs_log.mode == LOG_FILE) {
    char* cmd = NULL;
    asprintf(&g_cs_log.file_name, "%s/%s", "/var/log", log_name);

    g_cs_log.fp = fopen(g_cs_log.file_name, "a");
    // give read permission to all user
    asprintf(&cmd, "chmod ouag+r %s", g_cs_log.file_name);
    system(cmd);
    free(cmd);
  }

  memcpy(g_cs_log.tag, log_tag, MIN(strlen(log_tag), 31));

  CsLogOut(LOG_LOW, "LOG init");
}

int GetFileSize(const char* fullname) {
  struct stat st;

  if (stat(fullname, &st)) {
    if (errno == ENOENT) {
      _LOGE("get file size error %s", strerror(errno));
      return 0;
    }
  }

  return st.st_size;
}

int isFileExist(const char* fullname) {
  struct stat st;

  if (stat(fullname, &st)) {
    if (errno == ENOENT) {
      return 0;
    }
  }

  return 1;
}

void HexToStr(const u8* buf, int buflen, char* out) {
  const char hex[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
    'A', 'B', 'C', 'D', 'E', 'F',
  };

  int i = 0;
  int j = 0;
  for (i = 0; i < buflen; i++) {
    char ch = buf[i];
    out[j++] = hex[(ch & 0xF0) >> 4];
    out[j++] = hex[ch & 0xF];
  }
}

// change hex characters to hex bytes
// e.g. 14 characters is a hex represantation of 7 bytes
int HexStrToBin(const char* pHexString, u8* pBinArray) {
  int i = 0;
  int o = 0;

  while (pHexString[i] != 0x00) {
    switch (pHexString[i]) {
      case '0': pBinArray[o] = 0x00; break;
      case '1': pBinArray[o] = 0x10; break;
      case '2': pBinArray[o] = 0x20; break;
      case '3': pBinArray[o] = 0x30; break;
      case '4': pBinArray[o] = 0x40; break;
      case '5': pBinArray[o] = 0x50; break;
      case '6': pBinArray[o] = 0x60; break;
      case '7': pBinArray[o] = 0x70; break;
      case '8': pBinArray[o] = 0x80; break;
      case '9': pBinArray[o] = 0x90; break;
      case 'A':
      case 'a': pBinArray[o] = 0xa0; break;
      case 'B':
      case 'b': pBinArray[o] = 0xb0; break;
      case 'C':
      case 'c': pBinArray[o] = 0xc0; break;
      case 'D':
      case 'd': pBinArray[o] = 0xd0; break;
      case 'E':
      case 'e': pBinArray[o] = 0xe0; break;
      case 'F':
      case 'f': pBinArray[o] = 0xf0; break;
      default:
        return -1;
    }

    if (pHexString[i+1] == 0x00)
      return -1;

    switch (pHexString[i+1]) {
      case '0': pBinArray[o] |= 0x00; break;
      case '1': pBinArray[o] |= 0x01; break;
      case '2': pBinArray[o] |= 0x02; break;
      case '3': pBinArray[o] |= 0x03; break;
      case '4': pBinArray[o] |= 0x04; break;
      case '5': pBinArray[o] |= 0x05; break;
      case '6': pBinArray[o] |= 0x06; break;
      case '7': pBinArray[o] |= 0x07; break;
      case '8': pBinArray[o] |= 0x08; break;
      case '9': pBinArray[o] |= 0x09; break;
      case 'A':
      case 'a': pBinArray[o] |= 0x0a; break;
      case 'B':
      case 'b': pBinArray[o] |= 0x0b; break;
      case 'C':
      case 'c': pBinArray[o] |= 0x0c; break;
      case 'D':
      case 'd': pBinArray[o] |= 0x0d; break;
      case 'E':
      case 'e': pBinArray[o] |= 0x0e; break;
      case 'F':
      case 'f': pBinArray[o] |= 0x0f; break;
      default:
        return -1;
    }

    i += 2;
    o++;
  }

  return o;
}

int SystemCmd(char *cmd , char *ret, int ret_size) {
  FILE *fd = popen(cmd, "r");
  if (fd == NULL)
    return -1;

  fgets(ret, ret_size, fd);
  pclose(fd);
  return 0;
}
