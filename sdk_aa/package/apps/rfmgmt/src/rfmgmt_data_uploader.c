#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>

#include "utils.h"
#include "db_process.h"
#include "rfmgmt_config.h"
#include "rfmgmt_common.h"
#include "rfmgmt_data_recorder.h"
#include "led_mgr.h"

#define TAG                         "[uploader]"

#define MAX_PING_COUNT              3
#define PING_INTERVAL               (3 * 1000) // in millisecond
#define DATA_SRV_NET_CHECK_INTERVAL (120 * 1000)
#define CONNECT_TIMEOUT             1000  // Connect timeout, in millisecond
#define MAX_EPC_NUM                 5000 // server max buff size is 16384 bytes
#define MAX_SEND_BUF_SIZE           MAX_EPC_NUM * 20 + 100
#define UPLOAD_NUM_FILE             "/tmp/upload_num"

#define IFACE_WLAN_NAME             "wlan0"


#pragma pack(1)
typedef struct SdserverMessageHeader {
  u16 mark; // message mark, 0xAA11 = send data, 0xBEBE = heartbeat
  u32 source_ip; // client ip
  u32 destin_ip; // server ip
  u32 msg_version; // message version = 0
  u32 msg_type; // message type = 0 reserved
  u32 msg_length; // message length = (num * sizeof(SdServerEpcSt))
} SdserverMessageHeaderSt;

#pragma pack()

// BE BE 10 00 00 7F 10 00 00 7F 00 00 00 00 00 00 00 00 00 00 00 00
// 10 00 00 7F 是127.0.0.1我本机的ip地址，小端发送

struct UploaderContext {
  char local_addr[128];
  char server_addr[128];
  int port;
  pthread_t uploader_tid;
  pthread_t checker_tid;
  pthread_mutex_t lock;
  u8 is_net_connect;
  u8 checker_quit_flag;
  u8 data_server_sync_heartbeat;
  int sockfd;
  int sock_connected;
  u8 is_db_updating;
  u8 is_uploader_alive;
  u8 can_backup_index;
  u32 total_upload_epc_num;
};

struct DetailEpcSt {
  char db_id[16];
  char db_date[64];
  SdserverEpcSt data_epc;
};

struct MultiEpcPacket {
  char db_name[128];
  int epc_num;
  struct DetailEpcSt detail_epcs[MAX_EPC_NUM];
};

static struct UploaderContext g_uploader_context;
static struct MultiEpcPacket g_multi_epc_packet;

static struct UploaderContext* _upcxt() {
  return &g_uploader_context;
}

static void SetDbUpdatingStatus(u8 status) {
  _upcxt()->is_db_updating = status;
}

u8 isDbUpdating() {
  return _upcxt()->is_db_updating;
}

static void SetUploaderAliveFlag(u8 status) {
  _upcxt()->is_uploader_alive = status;
}

void CheckRestoreUploaderSocket() {
  if (_upcxt()->is_uploader_alive == 0) {

    if (_upcxt()->sockfd) {
      shutdown(_upcxt()->sockfd, SHUT_RDWR);
      close(_upcxt()->sockfd);
      _upcxt()->sockfd = 0;
    }

    SetDbUpdatingStatus(FALSE);
  }
}

static void SetDataServerNetStatus(u8 status) {
  pthread_mutex_lock(&g_uploader_context.lock);
  _upcxt()->is_net_connect = status;
  pthread_mutex_unlock(&g_uploader_context.lock);
}

static u8 isDataServerCanConnect() {
  u8 status;

  pthread_mutex_lock(&g_uploader_context.lock);
  status = _upcxt()->is_net_connect;
  pthread_mutex_unlock(&g_uploader_context.lock);

  return status;
}

void SetDataServerSyncHeartbeat(u8 status) {
  pthread_mutex_lock(&g_uploader_context.lock);
  _upcxt()->data_server_sync_heartbeat = status;
  pthread_mutex_unlock(&g_uploader_context.lock);
}

void CheckBackupDbIndex() {
  char cmd[256] = "";
  char buffer[128] = "";

  if (_upcxt()->can_backup_index) {

    if (!isFileExist(DB_INDEX_FILE_NAME)) {
      _LOGD("%s: %s, there is no db_index yet", TAG, __func__);
      return;
    }

    if (!isFileExist(DB_INDEX_STORED_NAME)) {
      _LOGD("%s: %s, no db_index in flash, do backup", TAG, __func__);
      sprintf(cmd, "cp %s %s", DB_INDEX_FILE_NAME, DB_INDEX_STORED_NAME);
      system(cmd);
      return;
    }

    sprintf(cmd, "%s", "check_index");
    if (SystemCmd(cmd, buffer, sizeof(buffer)-1) < 0) {
      _LOGE("check_index failed");
      return;
    }

    if (atoi(buffer) == 0) {
      // if not same do backup
      sprintf(cmd, "cp %s %s", DB_INDEX_FILE_NAME, DB_INDEX_STORED_NAME);
      system(cmd);

      _LOGD("%s: %s, do backup index", TAG, __func__);
    } else {
      _LOGD("%s: %s, db_index identical", TAG, __func__);
    }
  }
}

static u8 isDataServerSyncHeartbeatReached() {
  u8 status;

  pthread_mutex_lock(&g_uploader_context.lock);
  status = _upcxt()->data_server_sync_heartbeat;
  pthread_mutex_unlock(&g_uploader_context.lock);

  return status;
}

static int GetLocalIp(char* ipbuf) {
  int getip_sock;
  struct ifreq temp;
  struct sockaddr_in *myaddr;

  if (!ipbuf)
    return -1;

  if ((getip_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    _LOGE("%s: %s, socket err", TAG, __func__);
    return -1;
  }

  strcpy(temp.ifr_name, "wlan0");

  int ret;
  ret = ioctl(getip_sock, SIOCGIFADDR, &temp);
  close(getip_sock);

  if (ret < 0) {
    _LOGE("%s: %s, ioctl err", TAG, __func__);
    return -1;
  }

  myaddr = (struct sockaddr_in *)&(temp.ifr_addr);
  strcpy(ipbuf, inet_ntoa(myaddr->sin_addr));

  _LOGD("%s: local ip: %s", TAG, ipbuf);

  return 0;
}

static void HexDump(const u8* buf, int buflen) {
  if (!buf)
    return;

  int i;
  for (i = 0; i < buflen; i++) {
    printf("%x ", buf[i]);
  }
  printf("\n");
}

static int CheckDataServerResponse(const u8* sndbuf,
                                   const u8* rcvbuf, int len) {
  if (!sndbuf || !rcvbuf)
    return -1;

  //rheader->mark = ntohs(rheader->mark);
  //rheader->source_ip = ntohl(rheader->source_ip);
  //rheader->destin_ip = ntohl(rheader->destin_ip);

  return memcmp(sndbuf, rcvbuf, len);
}

static int PrepareMultiEpcPacket(SdserverMessageHeaderSt* msg_header,
                                 u8* outbuf, int* outlen) {
  if (!msg_header || !outbuf || !outlen)
    return -1;

  //_LOGE("%s - %s", TAG, __func__);

  struct _CheckEpcItem all;

  INIT_LIST_HEAD(&all.list);

  char db_name[128];
  char db_index_buf[128];
  char db_id[128];

  memset(db_name, 0, sizeof(db_name));
  memset(db_id, 0, sizeof(db_id));
  memset(db_index_buf, 0, sizeof(db_index_buf));
  if (GetDbUploadedInfo(db_name, db_id, db_index_buf) < 0) {
    _LOGE("%s - %s GetDbUploadedIndex err", TAG, __func__);
    return -1;
  }

  if (DbGetUnuploadedRows1(&all, db_name, db_id,
                           db_index_buf, MAX_EPC_NUM) < 0) {
    _LOGE("%s - %s DbGetUnuploadedRows1 err", TAG, __func__);
    return -1;
  }

  struct list_head *pos, *q;
  struct _CheckEpcItem *tmp;

  int epc_num = 0;
  memset(&g_multi_epc_packet, 0, sizeof(g_multi_epc_packet));
  strcpy(g_multi_epc_packet.db_name, db_name);

  // prepare multi epc
  list_for_each_safe(pos, q, &all.list) {
    tmp = list_entry(pos, struct _CheckEpcItem, list);
    //_LOGD("%s - %s: %s, %s, %s\n", TAG,
    //      tmp->id, tmp->upload_status, tmp->value, tmp->date);

    u8 binbuf[20];
    memset(binbuf, 0, sizeof(binbuf));

    if (tmp->value[0] != '\0' && tmp->value[0] != ' ') {
      strcpy(g_multi_epc_packet.detail_epcs[epc_num].db_id, tmp->id);
      strcpy(g_multi_epc_packet.detail_epcs[epc_num].db_date, tmp->date);

      HexStrToBin(tmp->value, binbuf);

      memcpy(&g_multi_epc_packet.detail_epcs[epc_num].data_epc.time_stamp,
             binbuf, 8);
      memcpy(&g_multi_epc_packet.detail_epcs[epc_num].data_epc.epc,
             binbuf+8, 12);

      epc_num++;
    }

    list_del(pos);
    free(tmp);
  }

  _LOGD("%s - %s, epc-num: %d", TAG, __func__, epc_num);

  if (epc_num == 0) {
    _LOGE("no epc data available");
    SetEpcDataPreparedFlag(FALSE);
    return -1;
  }

  g_multi_epc_packet.epc_num = epc_num;

  msg_header->mark = htons(0xAA11);
  msg_header->source_ip = htonl(inet_addr(_upcxt()->local_addr));
  msg_header->destin_ip = htonl(inet_addr(_upcxt()->server_addr));
  msg_header->msg_length = sizeof(SdserverEpcSt)*g_multi_epc_packet.epc_num;

  memcpy(outbuf, msg_header, sizeof(SdserverMessageHeaderSt));

  int index = 0;
  for (index = 0; index < epc_num; index++) {
    memcpy(outbuf+sizeof(SdserverMessageHeaderSt)+index*sizeof(SdserverEpcSt),
           &g_multi_epc_packet.detail_epcs[index].data_epc,
           sizeof(SdserverEpcSt));
  }

  *outlen = sizeof(SdserverMessageHeaderSt) +
      sizeof(SdserverEpcSt)*g_multi_epc_packet.epc_num;

  return 0;
}

static void PrepareHeartbeatPacket(SdserverMessageHeaderSt *msg_header,
                                   u8* outbuf, int* outlen) {

  if (!msg_header)
    return;

  msg_header->mark = htons(0xBEBE);
  msg_header->source_ip = htonl(inet_addr(_upcxt()->local_addr));
  msg_header->destin_ip = htonl(inet_addr(_upcxt()->server_addr));

  int index = 0;
  memcpy(outbuf + index, &msg_header->mark, 2);
  index += 2;
  memcpy(outbuf + index, &msg_header->source_ip, 4);
  index += 4;
  memcpy(outbuf + index, &msg_header->destin_ip, 4);

  *outlen = sizeof(SdserverMessageHeaderSt);
}

int SetDbRecordsStatusUploaded1() {

  int ret = 0;

  _LOGD("%s - %s db_name = %s, item_id = %s, item_date = %s", TAG, __func__,
        g_multi_epc_packet.db_name,
        g_multi_epc_packet.detail_epcs[g_multi_epc_packet.epc_num-1].db_id,
        g_multi_epc_packet.detail_epcs[g_multi_epc_packet.epc_num-1].db_date);

  _upcxt()->can_backup_index = 0;

  ret = KeepDbUploadedInfo(
      g_multi_epc_packet.db_name,
      g_multi_epc_packet.detail_epcs[g_multi_epc_packet.epc_num-1].db_id,
      g_multi_epc_packet.detail_epcs[g_multi_epc_packet.epc_num-1].db_date);

  _upcxt()->can_backup_index = 1;

  return ret;
}


// try to connect to the specific server port
// return 0 if connect successfully, else return -1
static int DoConnectDataServer(char* url, int port) {
  int fd = 0;
  int flag = 0;
  int rc = 0;
  struct sockaddr_in servaddr;
  struct timeval timeout;

  _LOGD("%s - %s", TAG, __func__);

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  // use wifi iface to upload data
  setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, IFACE_WLAN_NAME,
             strlen(IFACE_WLAN_NAME)+1);

  // Set the socket to be non-blocking
  flag = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flag|O_NONBLOCK);

  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(url);
  servaddr.sin_port = htons(port);

  int connect_result = -1;
  if (connect(fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == 0) {
    connect_result = 0;
  } else {
    if (errno == EINPROGRESS) {
      fd_set wset;
      FD_ZERO(&wset);
      FD_SET(fd, &wset);
      timeout.tv_sec = 0;
      timeout.tv_usec = CONNECT_TIMEOUT*1000;
      rc = select(fd+1, NULL, &wset, NULL, &timeout);
      if (rc > 0) {
        int err = -1;

        socklen_t len = sizeof(err);
        if (FD_ISSET(fd, &wset)) {
          if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0) {
            _LOGD("%s - select check, err = %d.", TAG, err);
            if (err == 0) {
              connect_result = 0;
            }
          }
        }
      }
    }
  }

  // set back to default
  fcntl(fd, F_SETFL, flag);

  close(fd);

  return connect_result;
}

static void* DataServerCheckerThread(void* arg) {
  int ping_count = 0;
  u32 timestamp = 0;
  u32 timestamp_now = 0;
  int is_connected_to_data_server = 0;

  _LOGD("%s - data server checker thread: url=%s, port=%d", TAG,
        _upcxt()->server_addr, _upcxt()->port);

  if (!isEpcDataPrepared()) {
    _LOGD("%s - %s no epc prepared now", TAG, __func__);
    _upcxt()->checker_tid = 0;
    return NULL;
  }

  if (isDbUpdating()) {
    _LOGD("%s - %s epc is uploading", TAG, __func__);
    _upcxt()->checker_tid = 0;
    return NULL;
  }

  do {
    timestamp_now = CsGetMs();
    if (timestamp == 0 || timestamp_now - timestamp > PING_INTERVAL) {
      if (DoConnectDataServer(_upcxt()->server_addr, _upcxt()->port) == 0) {
        is_connected_to_data_server = 1;
      }
      ping_count++;
      timestamp_now = CsGetMs();
      timestamp = timestamp_now;
    }

    if (is_connected_to_data_server || _upcxt()->checker_quit_flag) {
      break;
    } else {
      _msleep(100);
    }
  } while (ping_count < MAX_PING_COUNT);

  _LOGD("data server checker thread: join quit[%d], connected[%d]",
        _upcxt()->checker_quit_flag, is_connected_to_data_server);

  if (_upcxt()->checker_quit_flag == 0) {
    if (is_connected_to_data_server == 0) {
      SetDataServerNetStatus(FALSE);
    } else {
      SetDataServerNetStatus(TRUE);
    }

    // UpdateLed();
  }

  // checker quit, clear the tid
  _upcxt()->checker_tid = 0;

  return NULL;
}

static int StartDataServerChecker() {
  pthread_attr_t attr;
  int ret = 0;
  int err;
  void *status;

  if (_upcxt()->checker_tid) {
    _upcxt()->checker_quit_flag = 1;
    pthread_join(_upcxt()->checker_tid, &status);

    _LOGD("%s - StartDataServerChecker: cancel existing checker", TAG);
    _upcxt()->checker_quit_flag = 0;
    _upcxt()->checker_tid = 0;
  }

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 64*1024);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  err = pthread_create(&(_upcxt()->checker_tid), &attr,
                       DataServerCheckerThread, NULL);
  if (err) {
    _LOGE("Failed starting DataServerCheckerThread: %s", strerror(err));
    ret = -1;
  }
  pthread_attr_destroy(&attr);

  return ret;
}

int CheckDataServerNetStatus() {
  _LOGD("%s", __func__);
  StartDataServerChecker();
  return 0;
}

static int DoDataUpload(SdserverMessageHeaderSt *snd_msg_header,
                        const u8* send_buf, int buf_len) {
  struct sockaddr_in servaddr;

  _LOGD("%s - %s, sockfd = %d", TAG, __func__, _upcxt()->sockfd);

  if (_upcxt()->sockfd == 0) {
    _upcxt()->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (_upcxt()->sockfd < 0) {
      _upcxt()->sockfd = 0;
      return -1;
    }

    // use wifi iface to upload data
    setsockopt(_upcxt()->sockfd, SOL_SOCKET, SO_BINDTODEVICE, IFACE_WLAN_NAME,
               strlen(IFACE_WLAN_NAME)+1);

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(_upcxt()->server_addr);
    servaddr.sin_port = htons(_upcxt()->port);
  }

  if (_upcxt()->sock_connected == 0) {
    if (connect(_upcxt()->sockfd,
                (struct sockaddr*)&servaddr, sizeof(servaddr)) == 0) {
      _upcxt()->sock_connected = 1;
    } else {
      _upcxt()->sock_connected = 0;
    }
  }

  _LOGD("%s - serverip: %s, port: %d, connected = %d",
        TAG, _upcxt()->server_addr, _upcxt()->port, _upcxt()->sock_connected);

  if (_upcxt()->sock_connected) {
    //HexDump(send_buf, buf_len);
    SetUploaderAliveFlag(FALSE);
    CsStartTimer(RFMGMT_TIMER_CHECK_UPLOAD_HANG, 10*1000);
    SetDbUpdatingStatus(TRUE);

    int sbytes = 0;
    sbytes = send(_upcxt()->sockfd, send_buf, buf_len, 0);
    if (sbytes != buf_len) {
      _LOGE("%s send error: %s", TAG, strerror(errno));
      _upcxt()->sock_connected = 0;
    }

    _LOGD("%s send %d", TAG, sbytes);

    u8 rbuf[128];
    int rbytes = 0;
    int rlen = sizeof(SdserverMessageHeaderSt);
    memset(rbuf, 0, sizeof(rbuf));

    rbytes = recv(_upcxt()->sockfd, rbuf, rlen, 0);

    CsStopTimer(RFMGMT_TIMER_CHECK_UPLOAD_HANG);
    SetUploaderAliveFlag(TRUE);
    SetDbUpdatingStatus(FALSE);

    if (rbytes != rlen) {
      _LOGE("%s recv error: %d", TAG, rbytes);
      _upcxt()->sock_connected = 0;
    } else {
      _LOGD("%s recv %d", TAG, rbytes);
      //HexDump(rbuf, rlen);
      if (CheckDataServerResponse(send_buf, rbuf, rlen)) {
        _LOGE("%s recv: header not same!", TAG);
        return -1;
      }
    }
  }

  if (_upcxt()->sock_connected == 0) {
    _LOGD("%s fd closed", TAG);

    if (_upcxt()->sockfd) {
      close(_upcxt()->sockfd);
      _upcxt()->sockfd = 0;
    }

    SetDataServerNetStatus(FALSE);

    return -1;
  }

  return 0;
}

static void SendHeartBeat() {
  _LOGD("%s - send heartbeat frame", TAG);

  u8 send_buf[128];
  int send_len = 0;
  SdserverMessageHeaderSt msg_header;

  memset(&msg_header, 0, sizeof(msg_header));
  memset(send_buf, 0, sizeof(send_buf));
  PrepareHeartbeatPacket(&msg_header, send_buf, &send_len);
  if (DoDataUpload(&msg_header, send_buf, send_len) < 0) {
    _LOGD("%s - send heartbeat frame err", TAG);
  }
}

static void RecordTotalUploadNum(u32 num) {
  FILE *fp = NULL;

  fp = fopen(UPLOAD_NUM_FILE, "w");
  if (fp) {
    fprintf(fp, "%u\n", num);
    fclose(fp);
  }
}

static void SendEpcData() {
  SdserverMessageHeaderSt msg_header;
  u8 send_buf[MAX_SEND_BUF_SIZE];
  int send_len = 0;

  //_LOGD("%s - %s", TAG, __func__);

  memset(&msg_header, 0, sizeof(msg_header));
  memset(send_buf, 0, sizeof(send_buf));

  if (PrepareMultiEpcPacket(&msg_header, send_buf, &send_len) == 0) {
    if (DoDataUpload(&msg_header, send_buf, send_len) == 0) {
      SetDbRecordsStatusUploaded1();
      BlinkUploadStatusLed();

      // check if last packet
      if (g_multi_epc_packet.epc_num < MAX_EPC_NUM) {
        if (!isExistUnuploadedEpc()) {
          SetEpcDataPreparedFlag(FALSE);
          TurnOffUploadStatusLed();
        }
      }

      _upcxt()->total_upload_epc_num += g_multi_epc_packet.epc_num;
      _LOGD("%s - total upload epc (%u)", TAG, _upcxt()->total_upload_epc_num);
      RecordTotalUploadNum(_upcxt()->total_upload_epc_num);
    }
    if (!isEpcDataPrepared())
      SetDbUpdatingStatus(FALSE);
  }
}

static void* DataUploaderThread() {
  _LOGD("%s - enter DataUploaderThread", TAG);

  _LOGD("%s data uploader thread: url=%s, port=%d", TAG,
        _upcxt()->server_addr, _upcxt()->port);

  SetDbUpdatingStatus(FALSE);

  while (1) {
    if (isDataServerCanConnect()) {
      if (isDataServerSyncHeartbeatReached()) {
        SetDataServerSyncHeartbeat(FALSE);
        SendHeartBeat();
      }

      if (isEpcDataPrepared()) {
        SendEpcData();
      }
    }
  }

  _LOGW("%s - DataUploaderThread exit!!", TAG);
  if (_upcxt()->sockfd) {
    close(_upcxt()->sockfd);
    _upcxt()->sockfd = 0;
  }

  // if quit for some reason, restart it again.
  CsStartTimer(RFMGMT_TIMER_LOAD_DATA_UPLOADER, 1000);
  return NULL;
}

int CreateDataUploader() {
  pthread_t uploader_tid;
  pthread_attr_t attr128;
  int ret = 0;

  pthread_attr_init(&attr128);
  pthread_attr_setstacksize(&attr128, 128*1024);
  pthread_attr_setdetachstate(&attr128, PTHREAD_CREATE_DETACHED);

  if (pthread_create(&uploader_tid, &attr128, DataUploaderThread, NULL) < 0) {
    _LOGE("data uploader thread create err!");
    ret = -1;
  }
  pthread_attr_destroy(&attr128);

  _upcxt()->uploader_tid = uploader_tid;

  return ret;
}

int InitDataUploader() {
  memset(&g_uploader_context, 0, sizeof(g_uploader_context));

  pthread_mutex_init(&g_uploader_context.lock, NULL);

  GetDataServerIP(_upcxt()->server_addr, 128);
  _upcxt()->port = GetDataServerPort();

  GetLocalIp(_upcxt()->local_addr);

  _upcxt()->can_backup_index = 1;

  signal(SIGPIPE, SIG_IGN);

  RecordTotalUploadNum(0);

  CsStartTimer(RFMGMT_TIMER_LOAD_DATA_UPLOADER, 1000);
  CsStartTimer(RFMGMT_TIMER_DATA_SERVER_SYNC_HEARTBEAT, 3*1000);
  CsStartTimer(RFMGMT_TIMER_CHECK_NET_ALIVE, 1*1000);

  return 0;
}