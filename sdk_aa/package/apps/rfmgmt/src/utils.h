#ifndef UTILS_H_
#define UTILS_H_

#include <pthread.h>

#include "rfmgmt_define.h"

#define CS_TIMER_INVALID       ((timer_t)0xFFFFFFFF)

enum {
  CS_MSG_TYPE_TIMER = 0,
  CS_MSG_TYPE_EVENT,
  CS_MSG_TYPE_DATA,
  CS_MSG_TYPE_STAT,
  CS_MSG_TYPE_EXIT,

  __CS_MSG_TYPE_MAX
};

typedef enum {
  LOG_DISABLE = 0,
  LOG_FILE,
  LOG_PRINTF
} CsLogModeE;

typedef struct _cs_message_ {
  u32 msgType;
  u32 msgID;
  union {
    u32 status;
    u32 size;
  };
  struct _cs_message_ *pNext;
} CsMsgSt;

typedef struct {
  CsMsgSt* pHead;
  CsMsgSt* pTail;
  pthread_mutex_t mutex;
  pthread_cond_t  cond;
  u32 timeout;  // in second
} CsMsgQueueSt;

CsMsgQueueSt* CsInitMsg(u32 timeout /*in second, 0: no timeout*/);
void CsDestroyMsg(CsMsgQueueSt* queue);
void CsPutMsg(CsMsgSt* curMsg, CsMsgQueueSt* queue);
CsMsgSt* CsGetMsg(CsMsgQueueSt* queue);

b8 CsPostEvent(u32 eventID, void* pParam, u32 paramSize, CsMsgQueueSt* queue);
b8 CsPostStat(u32 objID, u32 stat, CsMsgQueueSt* queue);
b8 CsPostData(u32 dataID, void* pData, u32 size, CsMsgQueueSt* queue);
b8 CsPostExit(CsMsgQueueSt* queue);

extern CsMsgQueueSt* g_cs_main_queue;
#define CsSendData(id, data, size) CsPostData(id, data, size, g_cs_main_queue)

void CsInitTimer(u32 max_count, CsMsgQueueSt* queue);
void CsDestroyTimer();
void CsStartTimer(u32 timerID, u32 interval);
void CsStopTimer(u32 timerID);
u32  CsReadLeftTimer(u32 timerID);

void _msleep(long long msec);
u32 CsGetMs(void);  //get millisecond

s32 CsGetRandom(void);

void CsFree(void* pPtr);

void CsLogInit(char* log_name, char* log_tag);
void CsUpdateLogCfg();
void CsLogOut(u32 level, const char* fmt, ...);

int GetFileSize(const char* fullname);
int isFileExist(const char* fullname);
void HexToStr(const u8* buf, int buflen, char* out);
int HexStrToBin(const char* pHexString, u8* pBinArray);
int SystemCmd(char *cmd , char *ret, int ret_size);

enum {
  LOG_LOW = 0,
  LOG_MIDDLE,
  LOG_HIGH,
  LOG_ALWAYS,
};

#define _LOG(level, ...)   CsLogOut(level, __VA_ARGS__)
#define _LOGD(...)         CsLogOut(LOG_LOW, __VA_ARGS__)
#define _LOGI(...)         CsLogOut(LOG_MIDDLE, __VA_ARGS__)
#define _LOGW(...)         CsLogOut(LOG_HIGH, __VA_ARGS__)
#define _LOGE(...)         CsLogOut(LOG_ALWAYS, __VA_ARGS__)

#endif  // UTILS_H_

