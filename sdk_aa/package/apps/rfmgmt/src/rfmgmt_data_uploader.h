#ifndef RFMGMT_DATA_SERVER_H_
#define RFMGMT_DATA_SERVER_H_

int CheckDataServerNetStatus();
int InitDataUploader();
void SetDataServerSyncHeartbeat(u8 status);
int CreateDataUploader();
u8 isDbUpdating();
void CheckRestoreUploaderSocket();
void CheckBackupDbIndex();

#endif  // RFMGMT_DATA_SERVER_H_
