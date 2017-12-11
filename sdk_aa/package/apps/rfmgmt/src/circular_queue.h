#ifndef CIRCULAR_QUEUE_H_
#define CIRCULAR_QUEUE_H_

#include "db_process.h"
#include "rfmgmt_define.h"

#define MAX_QUEUED_EPC_NUM      500

typedef struct CircularQueue {
  int first;
  int last;
  int valid_items;
  u32 total_recv_num;
  struct _CheckEpcItem epcitems[MAX_QUEUED_EPC_NUM];
} CircularQueueSt;

void InitializeQueue(CircularQueueSt *cq);
int isEmpty(CircularQueueSt *cq);
int GetItemNum(CircularQueueSt *cq);
int PutEpcItem(CircularQueueSt *cq, struct _CheckEpcItem* epc_item);
int GetEpcItem(CircularQueueSt *cq, struct _CheckEpcItem* epc_item);
void PrintQueue(CircularQueueSt *cq);
int GetMultiEpcItem(CircularQueueSt *cq,
                    struct _CheckEpcItem *multiepcs, int num);

#endif /* CIRCULAR_QUEUE_H_ */