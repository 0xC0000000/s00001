#ifndef CFG_UTILS_H_
#define CFG_UTILS_H_

#include "sqlite3.h"
#include "rfmgmt_config.h"
#include "utils.h"
#include "list.h"

#define MAX_NAME_LEN            64
#define MAX_TEXT_LENGTH         256

#define SD_MOUNT_PATH           "/storage"
#define STORAGE_DIR             "EPC_DATA"
#define FULL_TF_MOUNT_DIR       SD_MOUNT_PATH"/"STORAGE_DIR"/"
#define OLD_DB_DIR              "/storage/histo"
#define SD_LOGS_DIR             "/storage/logs"

#define DB_INDEX_FILE_NAME      "/tmp/db_index"
#define DB_INDEX_BK_FILE_NAME   "/tmp/db_index_bk"
#define DB_INDEX_STORED_NAME    "/root/db_index"

#define DATABASE_FILE_DEFAULT   "/etc/database/epcdata.db"
#define DATABASE_FILE           "/storage/EPC_DATA/epcdata.db"
#define DATABASE_FILE2          "/storage/EPC_DATA/epcdata2.db"
//#define DATABASE_FILE           "/tmp/epcdata.db"
#define DATEBASE_RB_JOURNAL     "/storage/EPC_DATA/epcdata.db-journal"
#define DATEBASE_RB_JOURNAL2    "/storage/EPC_DATA/epcdata2.db-journal"

struct _CheckEpcItem {
  char id[16];
  char upload_status[4];
  char value[64];
  char date[16];

  struct list_head list;
};

typedef int (*fp_set_column)(sqlite3_stmt *stmt, struct list_head *all);
typedef int (*fp_set_col_v2)(sqlite3_stmt *stmt, void *arg);

int InitDb();
int GetCanInsertedNum(int num);
int DbGetTotalNum();
int DbGetDbANum();
int DbGetDbBNum();
int DbInsertMultiItems(const struct _CheckEpcItem* multiepcs,
                       int num, int* inserted_num);
int DbGetGroup(const char* group, struct _CheckEpcItem* all);
int DbGetUnuploadedRows(struct _CheckEpcItem *all, int rows);
int DbGetUnuploadedRows1(struct _CheckEpcItem *all, const char* db_name,
                         const char* max_id, const char* time, int rows);
int GetOriginalDate(struct _CheckEpcItem *epcitem);

int KeepDbUploadedInfo(const char* db_name, const char* upload_id,
                        const char* datebuf);
int GetDbUploadedInfo(char* db_name, char *item_id, char* item_date);
int isExistUnuploadedEpc();

#endif /* CFG_UTILS_H_ */