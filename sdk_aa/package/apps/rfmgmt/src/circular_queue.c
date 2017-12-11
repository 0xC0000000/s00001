#include "circular_queue.h"

#include <stdio.h>
#include <string.h>

#include "utils.h"

#define TAG                       "[EPC_BUF_QUEUE]"

void InitializeQueue(CircularQueueSt *cq) {
  cq->valid_items = 0;
  cq->first = 0;
  cq->last = 0;
  cq->total_recv_num = 0;

  memset(cq->epcitems, 0, sizeof(cq->epcitems));
}

int isEmpty(CircularQueueSt *cq) {
  if (cq->valid_items == 0) {
    return 1;
  }

  return 0;
}

int PutEpcItem(CircularQueueSt *cq, struct _CheckEpcItem* epc_item) {
  if (cq->valid_items >= MAX_QUEUED_EPC_NUM) {
    _LOGI("%s - the epc buffer queue is full", TAG);
    return -1;
  } else {

    cq->valid_items++;
    cq->epcitems[cq->last] = *epc_item;
    cq->last = (cq->last + 1) % MAX_QUEUED_EPC_NUM;

    cq->total_recv_num++;
  }

  return 0;
}

int GetEpcItem(CircularQueueSt *cq, struct _CheckEpcItem* epc_item) {
  if (isEmpty(cq)) {
    _LOGI("%s - the epc buffer queue is empty", TAG);
    return -1;
  } else {
    *epc_item = cq->epcitems[cq->first];
    memset(&cq->epcitems[cq->first], 0, sizeof(struct _CheckEpcItem));
    cq->first = (cq->first + 1) % MAX_QUEUED_EPC_NUM;
    cq->valid_items--;
    return 0;
  }
}

int GetMultiEpcItem(CircularQueueSt *cq,
                    struct _CheckEpcItem *multiepcs, int num) {

  int i = 0;

  for (i = 0; i < num; i++) {
    if (isEmpty(cq))
      break;

    GetEpcItem(cq, &multiepcs[i]);
  }

  return i;
}


void PrintQueue(CircularQueueSt *cq) {
  int aux, aux1;

  aux = cq->first;
  aux1 = cq->valid_items;
  while (aux1 > 0) {
    _LOGD("Element #%d: (%s,%s,%s,%s)\n",
          aux, cq->epcitems[aux].id, cq->epcitems[aux].date,
          cq->epcitems[aux].upload_status, cq->epcitems[aux].value);
    aux = (aux + 1) % MAX_QUEUED_EPC_NUM;
    aux1--;
  }
}

int GetItemNum(CircularQueueSt *cq) {
  return cq->valid_items;
}