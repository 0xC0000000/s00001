#include "rfmgmt_data_recorder.h"

#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "utils.h"
#include "db_process.h"
#include "circular_queue.h"
#include "rfmgmt_common.h"
#include "rfmgmt_data_uploader.h"

#define TAG                       "[recorder]"
#define RECV_NUM_FILE             "/tmp/recv_num"
#define DB_INSERTED_NUM_FILE      "/tmp/inserted_num"
#define DB_EPC_PREPARED_FLAG_FILE "/storage/prepared_flag"

struct RecorderContext {
  u8 data_prepared;
  pthread_mutex_t lock;
  CircularQueueSt epc_buffer_queue;
  u32 total_inserted_epc_num;
};

static struct RecorderContext g_recorder_context;

static struct RecorderContext* _rectx() {
  return &g_recorder_context;
}

// check if TF mounted, if TF card mounted without
// EPC dir, create the EPC dir for TF
static int DetectMountStat(char* mount_point) {
  char cmd[256] = "";
  char buffer[128] = "";
  char cs_dir[128] = "";
  int ret = 0;

  sprintf(cmd,
          "for dname in  $(cat /proc/mounts | grep %s"\
          " | awk -F ' ' '{print $1}'); do ret=0; "\
          " test -e $dname && ret=1; done; echo $ret",
          mount_point);

  if (SystemCmd(cmd, buffer, sizeof(buffer)-1) < 0) {
    _LOGE("check sd mount info failed");
    return -1;
  }

  sprintf(cs_dir, "%s/%s", mount_point, STORAGE_DIR);

  // check if TF mounted
  if (atoi(buffer) == 1) {
    // check if dir exist
    ret = access(cs_dir, R_OK|W_OK);
    if (ret != 0 && strstr(mount_point, SD_MOUNT_PATH)) {
      // if TF card mounted && no EPC dir, create it
      sprintf(cmd, "mkdir -m 755 %s;sync", cs_dir);
      system(cmd);
    }

    _LOGD("mount folder [%s]", cs_dir);
    return 1;
  }

  return 0;
}

static u8 isRecorderStorageMounted() {
  if (DetectMountStat(SD_MOUNT_PATH) > 0)
    return 1;

  return 0;
}

static void CheckUnuploadedEpc() {
  _LOGD("%s - %s", TAG, __func__);

  if (isExistUnuploadedEpc()) {
    SetEpcDataPreparedFlag(TRUE);
  }
}

static void RecordTotalRecvNum(u32 num) {
  FILE *fp = NULL;

  fp = fopen(RECV_NUM_FILE, "w");
  if (fp) {
    fprintf(fp, "%u\n", num);
    fclose(fp);
  }
}

static void RecordTotalInsertedNum(u32 num) {
  FILE *fp = NULL;

  fp = fopen(DB_INSERTED_NUM_FILE, "w");
  if (fp) {
    fprintf(fp, "%u\n", num);
    fclose(fp);
  }
}


static void* DataRecorderThread() {
  _LOGD("%s - enter DataRecorderThread", TAG);

  int cnt = 0;
  const int MS_TIME = 500;
  const int LIVE_CNT = (3000 / MS_TIME);
  struct _CheckEpcItem multi_epcs[500];
  int inserted_num = 0;
  int epcnum = 0;
  int rnum = 0;
  int can_inserted_num;

  while (1) {

    if (!isEmpty(&_rectx()->epc_buffer_queue)) {
      epcnum = GetItemNum(&_rectx()->epc_buffer_queue);
      //_LOGD("%s - epcnum = %d, total received epcnum = %u",
      //      TAG, epcnum, _rectx()->epc_buffer_queue.total_recv_num);

      RecordTotalRecvNum(_rectx()->epc_buffer_queue.total_recv_num);

      can_inserted_num = GetCanInsertedNum(epcnum);
      //_LOGE("%s - %d -can inserted num: %d", TAG, epcnum, can_inserted_num);

      if (can_inserted_num > 0) {
        rnum = GetMultiEpcItem(&_rectx()->epc_buffer_queue,
                               multi_epcs, can_inserted_num);

        if (DbInsertMultiItems(multi_epcs, rnum, &inserted_num) == SQLITE_OK) {
          _rectx()->total_inserted_epc_num += inserted_num;
          RecordTotalInsertedNum(_rectx()->total_inserted_epc_num);
          SetEpcDataPreparedFlag(TRUE);
        } else {
          _LOGE("%s - DbInsertItem err", TAG);
        }
      }
    }

    // wait MS_TIME then Get RECV buffer
    _msleep(MS_TIME);
    if (cnt++ > LIVE_CNT) {
      cnt = 0;
      _LOGD("DataRecorderThread - beating");
    }
  }

  _LOGW("%s - DataRecorderThread exit!!", TAG);

  // if quit for some reason, restart it again.
  CsStartTimer(RFMGMT_TIMER_LOAD_DATA_RECORDER, 1000);
  return NULL;
}

void CheckRestoreSDCrash() {
  char cmd[256] = "";
  char buffer[128] = "";
  int ret_val = 0;

  sprintf(cmd,
          "dmesg | tail | grep \"sda] Device not ready\">/dev/null && echo 1 || echo 0");

  if (SystemCmd(cmd, buffer, sizeof(buffer)-1) < 0) {
    _LOGE("check sd crash 1 failed");
    return;
  }

  ret_val = atoi(buffer);
  if (ret_val == 0) {
    sprintf(cmd,
            "dmesg | tail | grep \"(sda1): Directory bread\">/dev/null && echo 1 || echo 0");

    if (SystemCmd(cmd, buffer, sizeof(buffer)-1) < 0) {
      _LOGE("check sd crash 2 failed");
      return;
    }

    ret_val = atoi(buffer);
  }

  if (ret_val == 1) {
    _LOGE("do reboot to restore from sd crash");

    sprintf(cmd, "dt=$(date +'%%Y%%m%%d_%%H%%M%%S'); echo ${dt} - sd crash reboot > /root/reboot_info");
    system(cmd);

    system("sync");
    sleep(1);
    system("reboot");
  }
}

int CreateDataRecorderThr() {
  pthread_t recorder_tid;
  pthread_attr_t attr128;

  pthread_attr_init(&attr128);
  pthread_attr_setstacksize(&attr128, 128*1024);
  pthread_attr_setdetachstate(&attr128, PTHREAD_CREATE_DETACHED);

  if (pthread_create(&recorder_tid, &attr128, DataRecorderThread, NULL) < 0) {
    _LOGE("data uploader thread create err!");
    return -1;
  }
  pthread_attr_destroy(&attr128);

  return 0;
}

int RecordEpcData(u8* epc, u8 len) {
  SdserverEpcSt sd_epc;
  struct _CheckEpcItem epcitem;

  memset(&sd_epc, 0, sizeof(sd_epc));
  memset(&epcitem, 0, sizeof(epcitem));

  sd_epc.time_stamp = time(NULL);
  memcpy(sd_epc.epc, epc, len);

  // 1995758632
  //_LOGD("%s sd_epc.time_stamp: %ld", TAG, sd_epc.time_stamp);
  HexToStr((u8*)&sd_epc.time_stamp, sizeof(sd_epc.time_stamp), epcitem.value);

  // 0000000059757C33
  //_LOGD("%s epcitem.value1: %s", TAG, epcitem.value);
  HexToStr((u8*)sd_epc.epc, sizeof(sd_epc.epc), epcitem.value+16);

  // 00000000E2003098881900722140369E
  //_LOGD("%s epcitem.value2: %s", TAG, epcitem.value);

  epcitem.value[40] = '\0';

  time_t now = time(NULL);
  sprintf(epcitem.date, "%ld", now);

  // 00000000E2003098881900722140369E, date: 1500868744
  //_LOGD("%s packed epc: %s, date: %s", TAG, epcitem.value, epcitem.date);

  PutEpcItem(&_rectx()->epc_buffer_queue, &epcitem);

  return 0;
}

u8 isEpcDataPrepared() {
  int flag;
  pthread_mutex_lock(&(_rectx()->lock));
  flag = _rectx()->data_prepared;
  pthread_mutex_unlock(&(_rectx()->lock));

  return flag;
}

void SetEpcDataPreparedFlag(u8 flag) {
  pthread_mutex_lock(&(_rectx()->lock));
  _rectx()->data_prepared = flag;
  pthread_mutex_unlock(&(_rectx()->lock));

  //_LOGD("%s - %s data_prepared = %d", TAG, __func__, flag);
}

int InitDataRecorder() {
  memset(&g_recorder_context, 0, sizeof(g_recorder_context));
  pthread_mutex_init(&g_recorder_context.lock, NULL);

  if (!isRecorderStorageMounted()) {
    _LOGE("there is no tf flash mounted.");
    return -1;
  }

  if (InitDb() < 0) {
    _LOGE("init db failed.");
    return -1;
  }

  InitializeQueue(&g_recorder_context.epc_buffer_queue);
  RecordTotalRecvNum(0);
  RecordTotalInsertedNum(0);

  CheckUnuploadedEpc();
  CsStartTimer(RFMGMT_TIMER_LOAD_DATA_RECORDER, 1000);

  return 0;
}
