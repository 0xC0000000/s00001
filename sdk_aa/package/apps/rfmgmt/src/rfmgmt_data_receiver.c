#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#include "utils.h"
#include "rfmgmt_config.h"
#include "rfmgmt_data_recorder.h"
#include "led_mgr.h"

#define CONNECT_TIMEOUT        5000  // Connect timeout, in millisecond
#define IFACE_ETH_NAME         "br-lan"
#define TAG                    "[receiver]"
#define RBUF_SIZE              256

typedef struct ReaderCmdPacket {
  u8 head; // packet header 0xA0
  u8 len; // bytes from address to the end
  u8 address; // reader address, for rs-485
  u8 cmd; // command code
  u8* p_cmd_para; // command parameter
  u8 check; // check code for all bytes except `check' self
} ReaderCmdPacketSt;

typedef struct ReaderResponsePacket {
  u8 head; // packet header 0xA0
  u8 len; // bytes from address to the end
  u8 address; // reader address, for rs-485
  u8* p_data; // data returned from reader
  u8 check; // check code for all bytes except `check' self
} ReaderResponsePacketSt;

typedef enum {
  CMD_RESET = 0x70,
  CMD_SET_BEEPER_MODE = 0x7A,
  CMD_REAL_TIME_INVENTORY = 0x89,
  CMD_FAST_SWITCH_ANT_INVENTORY = 0x8A,
  CMD_GET_INVENTORY_BUFFER = 0x90,
  CMD_GET_AND_RESET_INVENTORY_BUFFER = 0x91,

} READER_CMD_E;

struct ReceiverContext {
  char ip_addr[128];
  int port;
  pthread_t receiver_tid;
};

struct ReceiverContext g_receiver_context;

static u8 ComputeCheckSum(u8 *buf, u8 len) {
  u8 i, csum = 0;

  for (i = 0; i < len; i++) {
    csum += buf[i];
  }
  csum = (~csum) + 1;

  return csum;
}

static void PrepareSendCmd(ReaderCmdPacketSt* p_cmd_packet, u8* send_buf) {
  if (!p_cmd_packet || !send_buf) {
    return;
  }

  send_buf[0] = p_cmd_packet->head;
  send_buf[1] = p_cmd_packet->len;
  send_buf[2] = p_cmd_packet->address;
  send_buf[3] = p_cmd_packet->cmd;

  int len = p_cmd_packet->len;
  if (len > 3) {
    memcpy(send_buf + 4, p_cmd_packet->p_cmd_para, len - 3);
  }

  u8 checksum = 0;
  checksum = ComputeCheckSum(send_buf, len + 1);

  send_buf[len+1] = checksum;
}

static void DoRecordEpcData(u8* epc_buf) {
  if (epc_buf == NULL)
    return;

  u8 epc[12];
  memset(epc, 0, sizeof(epc));

  // skip empty epc
  if (!memcmp(epc, epc_buf, sizeof(epc)))
    return;

  memcpy(epc, epc_buf, sizeof(epc));

  char tmp[64] = "";
  HexToStr(epc, sizeof(epc), tmp);
  //_LOGD("%s - %s:  epc: %s", TAG, __func__, tmp);
  RecordEpcData(epc, sizeof(epc));
}

// sample
//   err: a0  4  1 90 38 93
//    ok: a0 19  1 90  0  1 10 30  0 e2  0 30 98 88 19  0 72 21 40 36 9e 4d d4 4b 30  7 e0
// A01901 900001103000 E2003098881900722140369E4DD45BB46DE6
static int ParseResponseCmd(ReaderResponsePacketSt* respkt) {
  u8 cmd;
  int ret = 0;
  u8 index = 0;

  cmd = respkt->p_data[index];
  switch (cmd) {
    case CMD_GET_INVENTORY_BUFFER:
      if (respkt->len != 0x04) {
        index += 6; // skip tagcount(2) + datalen(1) + pc(2)
        DoRecordEpcData(respkt->p_data + index);
      } else {
        ret = -1;
      }
      break;

    case CMD_REAL_TIME_INVENTORY:
      if (respkt->len != 0x0A) {
        index += 4; // skip FreqAnt(1) + pc(2)
        DoRecordEpcData(respkt->p_data + index);
      } else {
        ret = -1;
      }
      break;

    case CMD_FAST_SWITCH_ANT_INVENTORY:
      //_LOGD("%s - %s len = %d", TAG, __func__, respkt->len);
      if (respkt->len == 0x04) {
        _LOGE("%s - %s fast switch ant cmd err", TAG, __func__);
        ret = -1;
      } else if (respkt->len == 0x05) {
        _LOGD("%s - %s Antena (%d) not connected!",
              TAG, __func__, respkt->p_data[1]);
        ret = -1;
      } else if (respkt->len == 0x0A) {
        //int epc_n = 0;
        //memcpy(&epc_n, respkt->p_data+1, 3);
        //_LOGD("%s - %s read (%d) epcs from reader", TAG, __func__, epc_n);
      } else {
        if (respkt->len > 18) {
          // compute epc number
          //int epc_n = (respkt->len - 7) / 12;
          //_LOGD("%s - %s have (%d) epcs", TAG, __func__, epc_n);

          index += 4; // skip FreqAnt(1) + pc(2)
          DoRecordEpcData(respkt->p_data+index);
        } else {
          _LOGE("%s - %s len(%d) err", TAG, __func__, respkt->len);
        }
      }
      break;

    case CMD_SET_BEEPER_MODE:
      if (respkt->len == 0x04) {
        u8 errcode = respkt->p_data[1];
        _LOGD("%s - %s:  set beeper ret = %d", TAG, __func__, errcode);
      } else {
        ret = -1;
      }
      break;
  }

  return ret;
}

// get each epc and pack it with rtc time
// prepared for data recorder
static int ParseResponsePacket(u8* buf, int buflen) {
  ReaderResponsePacketSt response_pkt;
  int index = 0;

  if (buf == NULL || buflen < 4 || buflen > RBUF_SIZE) {
    _LOGE("%s %s invalid para.", TAG, __func__);
    return -1;
  }

  response_pkt.head = buf[index];

  index += 1;
  response_pkt.len = buf[index];

  index += 1;
  response_pkt.address = buf[index];

  index += 1;
  response_pkt.p_data = buf + index;
  u8 datalen = response_pkt.len - 2;

  index += datalen;
  response_pkt.check = buf[index];

  u8 csum = ComputeCheckSum(buf, index);
  if (csum != response_pkt.check) {
    _LOGE("%s response packet checksum error", TAG);
    return -1;
  }

  //char tmp[64] = "";
  //HexToStr(response_pkt.p_data, datalen, tmp);
  //_LOGD("%s - %s:  p_data: %s", TAG, __func__, tmp);
  ParseResponseCmd(&response_pkt);

  return 0;
}

static int GetCmdResponse(int sockfd, char* hexstr, int len) {
  int rbytes = 0;
  u8 rbuf[RBUF_SIZE];
  u8 head = 0;
  u8 pktlen = 0;

  memset(rbuf, 0, sizeof(rbuf));
  memset(hexstr, 0, len);

  while (head != 0xa0) {
    rbytes = recv(sockfd, &head, 1, 0);
    if (rbytes != 1) {
      return -1;
    }

    if (rbytes == 1 && head == 0xa0) {
      rbuf[0] = head;
      break;
    }
  }

  errno = 0;
  rbytes = recv(sockfd, &pktlen, 1, 0);
  if (rbytes == 1) {
    rbuf[1] = pktlen;
    if (pktlen + 2 > RBUF_SIZE) {
      _LOGE("%s - %s ptklen = %d (larger than bufsize)",
            TAG, __func__, pktlen+2);
      return 1;
    }

    rbytes = recv(sockfd, rbuf + 2, pktlen, 0);
    if (rbytes == pktlen) {
      if (ParseResponsePacket(rbuf, pktlen+2) < 0)
        return 2;

      HexToStr(rbuf, pktlen+2, hexstr);

      // A01901900001103000E2003098881900722140369E4DD45BB46DE6
      //_LOGD("%s recv: %s", TAG, hexstr);
      return 0;
    } else {
      _LOGD("%s - rbytes = %d pktlen = %d, errno = %d",
            TAG, rbytes, pktlen, errno);
    }
  } else {
    _LOGD("%s - rbytes = %d, errno = %d", TAG, rbytes, errno);
  }

  // e.g. rbytes = 13 pktlen = 19
  if (errno == 0)
    return 3;

  return -1;
}

static int SendAndRecv(int sockfd, ReaderCmdPacketSt *p_cmd_packet) {
  if (!p_cmd_packet)
    return -1;

  u8 send_buf[128];
  memset(send_buf, 0, sizeof(send_buf));
  PrepareSendCmd(p_cmd_packet, send_buf);

  int send_len = 0;
  send_len = p_cmd_packet->len + 2;

  int sbytes = 0;
  sbytes = send(sockfd, send_buf, send_len, 0);
  if (sbytes < 0) {
    _LOGE("%s send error: %s", TAG, strerror(errno));
    return -1;
  }

  char hexstr[RBUF_SIZE];
  int ret = 0;
  ret = GetCmdResponse(sockfd, hexstr, sizeof(hexstr));
  if (ret < 0) {
    _LOGW("%s get cmd response failed!", TAG);
    return -1;
  }

  return 0;
}

static int DoCmdReset(int sockfd) {
  ReaderCmdPacketSt cmd_packet;

  memset(&cmd_packet, 0, sizeof(cmd_packet));
  cmd_packet.head = 0xA0;
  cmd_packet.len = 0x03;
  cmd_packet.address = 0x01;
  cmd_packet.cmd = CMD_RESET;
  cmd_packet.p_cmd_para = NULL;

  return SendAndRecv(sockfd, &cmd_packet);
}

static int DoCmdRealTimeInventory(int sockfd) {
  ReaderCmdPacketSt cmd_packet;

  u8 repeat = 0xFF;
  memset(&cmd_packet, 0, sizeof(cmd_packet));
  cmd_packet.head = 0xA0;
  cmd_packet.len = 0x04;
  cmd_packet.address = 0x01;
  cmd_packet.cmd = CMD_REAL_TIME_INVENTORY;
  cmd_packet.p_cmd_para = &repeat;

  return SendAndRecv(sockfd, &cmd_packet);
}

static int DoCmdFastSwitchAntInventory(int sockfd) {
  ReaderCmdPacketSt cmd_packet;

  memset(&cmd_packet, 0, sizeof(cmd_packet));

  u8 cmd_para_buf[10];
  memset(cmd_para_buf, 0, sizeof(cmd_para_buf));
  cmd_para_buf[0] = 0x00; // Ant 0
  cmd_para_buf[1] = 0x01; // Ant 0 loop times
  cmd_para_buf[2] = 0x01; // Ant 1
  cmd_para_buf[3] = 0x01; // Ant 1 loop times
  cmd_para_buf[4] = 0x02; // Ant 2
  cmd_para_buf[5] = 0x01; // Ant 2 loop times
  cmd_para_buf[6] = 0x03; // Ant 3
  cmd_para_buf[7] = 0x01; // Ant 3 loop times
  cmd_para_buf[8] = 0x00; // the sleep time (ms) for switch Ant
  cmd_para_buf[9] = 0x0A; // the loop times for Ant 0 -> 3 switch

  // fill the cmd_packet
  cmd_packet.head = 0xA0;
  cmd_packet.len = 0x0D;
  cmd_packet.address = 0x01;
  cmd_packet.cmd = CMD_FAST_SWITCH_ANT_INVENTORY;
  cmd_packet.p_cmd_para = cmd_para_buf;

  return SendAndRecv(sockfd, &cmd_packet);
}

static int DoCmdGetInventoryBuffer(int sockfd) {
  ReaderCmdPacketSt cmd_packet;

  memset(&cmd_packet, 0, sizeof(cmd_packet));
  cmd_packet.head = 0xA0;
  cmd_packet.len = 0x03;
  cmd_packet.address = 0x01;
  cmd_packet.cmd = CMD_GET_INVENTORY_BUFFER;
  cmd_packet.p_cmd_para = NULL;

  return SendAndRecv(sockfd, &cmd_packet);
}

static int DoCmdSetBeeperMode(int sockfd) {
  ReaderCmdPacketSt cmd_packet;

  u8 mode = 0x00;
  memset(&cmd_packet, 0, sizeof(cmd_packet));
  cmd_packet.head = 0xA0;
  cmd_packet.len = 0x04;
  cmd_packet.address = 0x01;
  cmd_packet.cmd = CMD_SET_BEEPER_MODE;
  cmd_packet.p_cmd_para = &mode;

  return SendAndRecv(sockfd, &cmd_packet);
}

static int DoConnectRfidReader() {
  struct in_addr addr_ip;

  if (inet_aton(g_receiver_context.ip_addr, &addr_ip) == 0) {
    _LOGE("%s invalid ip: %s", TAG, g_receiver_context.ip_addr);
    return -1;
  }

  int sockfd = 0;
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd <= 0) {
    _LOGE("%s socket failed: %s", TAG, strerror(sockfd));
    return -1;
  }

  // use br-lan iface to communicate with rfid reader
  setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, IFACE_ETH_NAME,
             strlen(IFACE_ETH_NAME)+1);

  //int flag = 0;
  // Set the socket to be non-blocking
  //flag = fcntl(sockfd, F_GETFL, 0);
  //fcntl(sockfd, F_SETFL, flag|O_NONBLOCK);

  // set rx, tx timeout
  struct timeval timeo;
  timeo.tv_sec = 5;
  timeo.tv_usec = 0;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                (char*)&timeo, sizeof(timeo)) < 0) {
    _LOGE("%s - setsockopt err", TAG);
  }

  if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO,
                 (char*)&timeo, sizeof(timeo)) < 0) {
    _LOGE("%s - setsockopt err", TAG);
  }

  struct sockaddr_in peer_addr;
  peer_addr.sin_family = AF_INET;
  peer_addr.sin_addr.s_addr = inet_addr(g_receiver_context.ip_addr);
  peer_addr.sin_port = htons(g_receiver_context.port);

  int ret;
  int rc = 0;
  int connect_result = -1;
  struct timeval timeout;
  fd_set r;

  _LOGD("%s before connect.", TAG);
  ret = connect(sockfd, (struct sockaddr *)&peer_addr, sizeof(peer_addr));
  if (ret == 0) {
    connect_result = 0;
  } else {
    if (errno == EINPROGRESS) {
      errno = 0; // set back
      FD_ZERO(&r);
      FD_SET(sockfd, &r);
      timeout.tv_sec = 0;
      timeout.tv_usec = CONNECT_TIMEOUT*1000;
      rc = select(sockfd+1, NULL, &r, NULL, &timeout);
      if (rc > 0)
        connect_result = 0;
    }
  }

  if (connect_result < 0) {
    _LOGE("%s connect failed", TAG);
    close(sockfd);

    // if rc == 0; timeout expires
    if (rc == 0) {
      TurnOnRfReaderErrLed();
      ResetReader();
    }

    return -1;
  }

  TurnOffRfReaderErrLed();
  _LOGD("%s connect done.", TAG);

  return sockfd;
}

static void* DataReceiverThr() {
  _LOGD("enter data receiver thread");

  int cnt = 0;
  int sockfd = 0;
  const int MS_TIME = 50;
  const int LIVE_CNT = (3000 / MS_TIME);

  sockfd = DoConnectRfidReader();
  //if (sockfd)
  //  DoCmdSetBeeperMode(sockfd);

  while (1) {
    if (sockfd <= 0) {
      _msleep(3000);
      sockfd = DoConnectRfidReader();
    }

    if (sockfd > 0) {
      if (DoCmdFastSwitchAntInventory(sockfd) < 0) {
        _LOGD("%s - DoCmdFastSwitchAntInventory err", TAG);
        close(sockfd);
        sockfd = 0;
      }
    }

    _msleep(MS_TIME);
    if (cnt++ > LIVE_CNT) {
      cnt = 0;
      _LOGD("%s - beating", TAG);
    }
  }

  _LOGW("data receiver thread exit!");
  return NULL;
}

static int CreateDataReceiverThread() {
  pthread_t receiver_tid;
  pthread_attr_t attr128;
  int ret = 0;

  pthread_attr_init(&attr128);
  pthread_attr_setstacksize(&attr128, 128*1024);
  pthread_attr_setdetachstate(&attr128, PTHREAD_CREATE_DETACHED);

  if (pthread_create(&receiver_tid, &attr128, DataReceiverThr, NULL) < 0) {
    _LOGE("data receiver thread create err!");
    ret = -1;
  }
  pthread_attr_destroy(&attr128);

  g_receiver_context.receiver_tid = receiver_tid;
  return ret;
}

static void GetReiverCfg() {
  GetRfidReaderIP(g_receiver_context.ip_addr,
                  sizeof(g_receiver_context.ip_addr));
  g_receiver_context.port = GetRfidReaderPort();
}

int InitDataReceiver() {
  memset(&g_receiver_context, 0, sizeof(g_receiver_context));

  GetReiverCfg();

  if (CreateDataReceiverThread() < 0) {
    return -1;
  }

  return 0;
}