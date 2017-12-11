#include "rfmgmt_config.h"

#include <string.h>
#include <stdlib.h>
#include <uci.h>
#include <arpa/inet.h>

#include "utils.h"
#include "rfmgmt_common.h"

#define DEFAULT_DATA_SRV_IP             "192.168.1.50"
#define DEFAULT_DATA_SRV_PORT           9000
#define DEFAULT_RFID_READER_IP          "192.168.1.50"
#define DEFAULT_RFID_READER_PORT        4001

#define UCI_SECTION_DATA_SERVER         "data_server"
#define UCI_SECTION_RFID_READER         "rfid_reader"
#define UCI_SECTION_LOG                 "log"

typedef struct {
  char rfid_reader_ip[128];
  u16 rfid_reader_port;
  char data_server_ip[128];
  u16 data_server_port;
  u16 heartbeat; //in seconds
  u8 upload_switch;
  u8 log_out;
  u8 log_level;
  u16 log_size; // kbytes
  struct uci_context* ctx;
} RfmgmtConfigSt;

static RfmgmtConfigSt g_rfmgmt_config;

static RfmgmtConfigSt* _cfg() {
  return &g_rfmgmt_config;
}

static void PrintCfgs() {
  _LOGD("cfg - rfid reader ip: %s", _cfg()->rfid_reader_ip);
  _LOGD("cfg - rfid reader port: %d", _cfg()->rfid_reader_port);
  _LOGD("cfg - data server ip: %s", _cfg()->data_server_ip);
  _LOGD("cfg - data server port: %d", _cfg()->data_server_port);
  _LOGD("cfg - log_out: %d", _cfg()->log_out);
  _LOGD("cfg - log_level: %d", _cfg()->log_level);
  _LOGD("cfg - log_size: %d", _cfg()->log_size);
}

// returns 1 on success
static u8 isValidIP(const char* ip) {
  struct sockaddr_in sa;
  int ret = inet_pton(AF_INET, ip, &(sa.sin_addr));
  return ret;
}

static int LoadDataServerCfg(struct uci_section* section) {
  struct uci_element *e;
  struct uci_option *o;

  uci_foreach_element(&section->options, e) {
    o = uci_to_option(e);

    _LOGD(" - %s, %s\n", o->e.name, o->v.string);
    if (!strncmp(o->e.name, "ipaddr", strlen("ipaddr"))) {
      if (isValidIP(o->v.string)) {
        strcpy(_cfg()->data_server_ip, o->v.string);
      } else {
        _LOGE(" invalid ip: %s\n", o->v.string);
        return -1;
      }
    } else if (!strncmp(o->e.name, "port", strlen("port"))) {
      _cfg()->data_server_port = atoi(o->v.string);
    }
  }

  return 0;
}

static int LoadRfidReaderCfg(struct uci_section* section) {
  struct uci_element *e;
  struct uci_option *o;

  uci_foreach_element(&section->options, e) {
    o = uci_to_option(e);

    _LOGD(" - %s, %s\n", o->e.name, o->v.string);
    if (!strncmp(o->e.name, "ipaddr", strlen("ipaddr"))) {
      if (isValidIP(o->v.string)) {
        strcpy(_cfg()->rfid_reader_ip, o->v.string);
      } else {
        _LOGE(" invalid ip: %s\n", o->v.string);
        return -1;
      }
    } else if (!strncmp(o->e.name, "port", strlen("port"))) {
      _cfg()->rfid_reader_port = atoi(o->v.string);
    }
  }

  return 0;
}

static int LoadLogCfg(struct uci_section* section) {
  struct uci_element *e;
  struct uci_option *o;

  uci_foreach_element(&section->options, e) {
    o = uci_to_option(e);

    _LOGD(" - %s, %s\n", o->e.name, o->v.string);
    if (!strncmp(o->e.name, "log_out", strlen("log_out"))) {
      _cfg()->log_out = atoi(o->v.string);
    } else if (!strncmp(o->e.name, "log_size", strlen("log_size"))) {
      _cfg()->log_size = atoi(o->v.string);
    } else if (!strncmp(o->e.name, "log_level", strlen("log_level"))) {
      _cfg()->log_level = atoi(o->v.string);
    }
  }
  return 0;
}

int LoadCfg() {
  memset(&g_rfmgmt_config, 0, sizeof(g_rfmgmt_config));

  struct uci_package* pkg = NULL;
  _cfg()->ctx = uci_alloc_context();
  if (!_cfg()->ctx) {
    _LOGE("uci_alloc_context failed.");
    return -1;
  }

  if (UCI_OK != uci_load(_cfg()->ctx, "rfmgmt", &pkg)) {
    uci_free_context(_cfg()->ctx);
    _cfg()->ctx = NULL;
    _LOGE("Failed to load /etc/config/rfmgmt");
    return -1;
  }

  struct uci_section* s;
  struct uci_element* e;
  uci_foreach_element(&pkg->sections, e) {
    s = uci_to_section(e);

    _LOGD("[cfg] section - %s\n", s->type);
    if (!strncmp(s->type, UCI_SECTION_DATA_SERVER,
                 strlen(UCI_SECTION_DATA_SERVER))) {
      if (LoadDataServerCfg(s) < 0) {
        return -1;
      }
    } else if (!strncmp(s->type, UCI_SECTION_RFID_READER,
                        strlen(UCI_SECTION_RFID_READER))) {
      if (LoadRfidReaderCfg(s) < 0) {
        return -1;
      }
    } else if (!strncmp(s->type, UCI_SECTION_LOG, strlen(UCI_SECTION_LOG))) {
      if (LoadLogCfg(s) < 0) {
        return -1;
      }
    }
  }

  uci_unload(_cfg()->ctx, pkg);
  uci_free_context(_cfg()->ctx);
  _cfg()->ctx = NULL;

  PrintCfgs();
  CsUpdateLogCfg();

  return 0;
}

void GetRfidReaderIP(char* buf, int buf_len) {
  int len = 0;

  memset(buf, 0, buf_len);

  len = strlen((char*)_cfg()->rfid_reader_ip);
  if (len > 0) {
    strncpy(buf, (char*)_cfg()->rfid_reader_ip, MIN(buf_len, len));
  } else {
    strcpy(buf, DEFAULT_RFID_READER_IP);
  }
}

u16 GetRfidReaderPort() {
  u16 port = DEFAULT_RFID_READER_PORT;

  if (_cfg()->rfid_reader_port) {
    port = _cfg()->rfid_reader_port;
  }
  return port;
}

void GetDataServerIP(char* buf, int buf_len) {
  int len = 0;

  memset(buf, 0, buf_len);

  len = strlen((char*)_cfg()->data_server_ip);
  if (len > 0) {
    strncpy(buf, (char*)_cfg()->data_server_ip, MIN(buf_len, len));
  } else {
    strcpy(buf, DEFAULT_DATA_SRV_IP);
  }
}

u16 GetDataServerPort() {
  u16 port = DEFAULT_DATA_SRV_PORT;

  if (_cfg()->data_server_port) {
    port = _cfg()->data_server_port;
  }
  return port;
}

u8 isCfgDataUploadAlwaysOn() {
  return _cfg()->upload_switch;
}

u8 GetLogLevel() {
  return _cfg()->log_level;
}

u8 GetLogOut() {
  return _cfg()->log_out;
}

u16 GetLogSize() {
  return _cfg()->log_size;
}
