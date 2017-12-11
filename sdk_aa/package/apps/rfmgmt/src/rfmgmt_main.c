#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include "utils.h"
#include "rfmgmt_common.h"
#include "rfmgmt_config.h"
#include "rfmgmt_data_uploader.h"
#include "rfmgmt_data_receiver.h"
#include "rfmgmt_data_recorder.h"
#include "db_process.h"
#include "led_mgr.h"

CsMsgQueueSt* g_cs_main_queue;

static void FreeCaches(void) {
  system("sync; echo 3 > /proc/sys/vm/drop_caches");
}

static void CheckFreeSpace(const char* fpath) {
  char cmd[512] = "";
  int max_space_percent = 90;

  sprintf(cmd, "free_size=$(df %s | grep -v Filesystem |"\
          " awk -F ' ' '{print $5}' | tr -d '%%');"\
          " if [ $free_size -ge %d ];then rm -rf %s/*; fi",
          fpath, max_space_percent, fpath);
  system(cmd);

  _LOGD("[%s] - check free space done", fpath);
}

static void CheckFreeSDSpace() {
  char cmd[512] = "";
  int max_space_percent = 80;

  sprintf(cmd, "free_size=$(df %s | grep -v Filesystem |"\
          " awk -F ' ' '{print $5}' | tr -d '%%');"\
          " if [ $free_size -ge %d ];then rm -rf %s/*; fi",
          OLD_DB_DIR, max_space_percent, SD_LOGS_DIR);
  system(cmd);

  _LOGD("check free SD space done");
}

static void* CheckLogsThread() {
  char fname[128] = "";
  char fpath[128] = "/var/log";
  char cmd[512] = "";

  DIR *dp = NULL;
  struct dirent* dirp = NULL;

  CheckFreeSpace(fpath);
  CheckFreeSDSpace();

  if ((dp = opendir(fpath)) == NULL) {
    _LOGE("opendir failed %s %s", fpath, strerror(errno));
    return NULL;
  }

  while ((dirp = readdir(dp)) != NULL) {
    if (strncmp(dirp->d_name, "cs-", 3) != 0 ||
        strstr(dirp->d_name, "tar.gz") != NULL) {
      continue;
    }

    u16 max_fsize = GetLogSize();
    sprintf(fname, "%s/%s", fpath, dirp->d_name);
    if (GetFileSize(fname) >= max_fsize*1024) {
      memset(cmd, 0, sizeof(cmd));
      sprintf(cmd,"dt=$(date +'%%Y%%m%%d_%%H%%M%%S'); cd %s;"\
              " mv %s %s_back; tar -czf %s_${dt}.tar.gz %s_back; rm *_back;"\
              " mv *.tar.gz %s/",
              fpath, dirp->d_name, dirp->d_name, dirp->d_name,
              dirp->d_name, SD_LOGS_DIR);
      system(cmd);
    }
  }

  closedir(dp);
  _LOGD("check logs done");

  return NULL;
}

static void CheckLogs(void) {
  pthread_attr_t attr;
  pthread_t check_log;

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 32*1024);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  if (pthread_create(&check_log, &attr, CheckLogsThread, NULL)) {
    _LOGE("create check log thread failed");
    return;
  }
}

static void ProcTimer(u32 timer_id) {
  switch (timer_id) {
    case RFMGMT_TIMER_FREE_CACHE:
      _LOGD("proc timer - free cache");
      FreeCaches();
      CsStartTimer(RFMGMT_TIMER_FREE_CACHE, 300*1000);
      break;

    case RFMGMT_TIMER_CHECK_LOGS:
      _LOGD("proc timer - check logs");
      CheckLogs();
      CsStartTimer(RFMGMT_TIMER_CHECK_LOGS, 600*1000);
      break;

    case RFMGMT_TIMER_DATA_SERVER_SYNC_HEARTBEAT:
      _LOGD("proc timer - enable data server sync heartbeat");
      SetDataServerSyncHeartbeat(TRUE);
      CsStartTimer(RFMGMT_TIMER_DATA_SERVER_SYNC_HEARTBEAT, 10*1000);
      break;

    case RFMGMT_TIMER_CHECK_NET_ALIVE:
      _LOGD("proc timer - check data server alive");
      CheckDataServerNetStatus();
      CsStartTimer(RFMGMT_TIMER_CHECK_NET_ALIVE, 5*1000);
      break;

    case RFMGMT_TIMER_LOAD_DATA_UPLOADER:
      _LOGD("proc timer - START data uploader!!");
      CreateDataUploader();
      break;

    case RFMGMT_TIMER_CHECK_UPLOAD_HANG:
      _LOGD("proc timer - CHECK IF uploader blocked");
      CheckRestoreUploaderSocket();
      break;

    case RFMGMT_TIMER_LOAD_DATA_RECORDER:
      _LOGD("proc timer - START data recorder!!");
      CreateDataRecorderThr();
      break;

    case RFMGMT_TIMER_CHECK_SD_CRASH:
      _LOGD("proc timer - check sd crash");
      CheckRestoreSDCrash();
      CsStartTimer(RFMGMT_TIMER_CHECK_SD_CRASH, 30*1000);
      break;

    case RFMGMT_TIMER_SAVE_INDEX_FILE:
      _LOGD("proc timer - set backup db_index flag");
      CheckBackupDbIndex();
      CsStartTimer(RFMGMT_TIMER_SAVE_INDEX_FILE, 180*1000);
      break;
  }
}

int RfmgmtInit() {
  CsLogInit("cs-rfmgmt.log", "RFMGMT");

  _LOGD("%s - %s - %s\n", RFMGMT_VERSION, __DATE__, __TIME__);

  g_cs_main_queue = CsInitMsg(0);
  CsInitTimer(__RFMGMT_TIMER_MAX, g_cs_main_queue);

  if (LoadCfg() < 0) {
    _LOGE("load cfg err.\n");
    return -1;
  }

  if (InitLed() < 0) {
    _LOGE("init led err.\n");
  }

  if (InitDataReceiver() < 0) {
    _LOGE("init data receiver err.\n");
    return -2;
  }

  if (InitDataRecorder() < 0) {
    _LOGE("init data recorder err.\n");
    return -3;
  }

  if (InitDataUploader() < 0) {
    _LOGE("init data uploader err.\n");
    return -4;
  }

  CsStartTimer(RFMGMT_TIMER_FREE_CACHE, 300*1000);
  CsStartTimer(RFMGMT_TIMER_CHECK_LOGS, 10*1000);
  CsStartTimer(RFMGMT_TIMER_CHECK_SD_CRASH, 3*1000);
  CsStartTimer(RFMGMT_TIMER_SAVE_INDEX_FILE, 10*1000);

  _LOGD("rfmgmt init done");

  return 0;
}

void RfmgmtMainLoop() {
  CsMsgSt *pMsg;

  _LOGD("Entering rfmgmt main loop ...");

  while (1) {
    pMsg = CsGetMsg(g_cs_main_queue);

    if (pMsg) {
      //_LOGD("Handle MSG: type=%u id=%u", pMsg->msgType, pMsg->msgID);

      switch (pMsg->msgType) {
        case CS_MSG_TYPE_TIMER:
          ProcTimer(pMsg->msgID);
          break;
      }
      CsFree(pMsg);
    }
  }

  _LOGD("rfmgmt exit!!!");

  CsDestroyMsg(g_cs_main_queue);
  CsDestroyTimer();
}

int main(int argc, char *argv[])
{
  if (RfmgmtInit() < 0) {
    _LOGD("rfmgmt init failed!!!");
    return -1;
  }

  RfmgmtMainLoop();

  return 0;
}
