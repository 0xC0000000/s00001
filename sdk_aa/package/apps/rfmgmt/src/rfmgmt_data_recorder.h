#ifndef RFMGMT_DATA_RECORDER_H_
#define RFMGMT_DATA_RECORDER_H_

#include "rfmgmt_define.h"

#pragma pack(1)
typedef struct SdserverEpc {
  u64 time_stamp; // utc
  u8 epc[12]; // epc
} SdserverEpcSt;
#pragma pack()


int InitDataRecorder();
int RecordEpcData(u8* epc, u8 len);
void SetEpcDataPreparedFlag(u8 flag);
u8 isEpcDataPrepared();
int CreateDataRecorderThr();
void CheckRestoreSDCrash();

#endif  // RFMGMT_DATA_RECORDER_H_
