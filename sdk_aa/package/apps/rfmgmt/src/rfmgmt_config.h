#ifndef RFMGMT_CONFIG_H_
#define RFMGMT_CONFIG_H_

#include "rfmgmt_define.h"

int LoadCfg();

void GetRfidReaderIP(char* buf, int buf_len);
u16 GetRfidReaderPort();
void GetDataServerIP(char* buf, int buf_len);
u16 GetDataServerPort();
u8 isCfgDataUploadAlwaysOn();
u8 GetLogLevel();
u8 GetLogOut();
u16 GetLogSize();

#endif /* RFMGMT_CONFIG_H_ */