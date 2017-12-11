#include "db_process.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rfmgmt_config.h"
#include "rfmgmt_common.h"

#define MAX_DB_RECORDS_NUM      1000000
#define TAG                     "[db]"

struct DbContext {
  sqlite3 *db_handle; // SQLite db handle, if valid, then DB opened OK
  int commit_num;
  char db_name[256];
};

struct DbContext g_db_ctx;

static int DoRemountSD() {
  char cmd[256] = "";
  char buffer[128] = "";

  sprintf(cmd,
          "mount | grep %s | grep \"(ro\" >/dev/null; echo $?",
          SD_MOUNT_PATH);

  if (SystemCmd(cmd, buffer, sizeof(buffer)-1) < 0) {
    _LOGE("check sd mount RW failed");
    return -1;
  }

  // check if SD readonly now
  if (atoi(buffer) == 0) {
    sprintf(cmd, "mount -t vfat -o remount -rw /dev/sda1 /storage");
    memset(buffer, 0, sizeof(buffer));
    if (SystemCmd(cmd, buffer, sizeof(buffer)-1) < 0) {
      _LOGE("SD Remount failed");
      return -1;
    }

    _LOGD("SD READONLY! DO REMOUNT!!!");
  }

  return 0;
}

static void DoReplaceDb() {
  char cmd[128];

  memset(cmd, 0, sizeof(cmd));

  // store the crupt db
  memset(cmd, 0, sizeof(cmd));
  snprintf(cmd, sizeof(cmd), "dt=$(date +'%%Y%%m%%d_%%H%%M%%S'); mv %s %s/%s_${dt}_corrupt",
           g_db_ctx.db_name, OLD_DB_DIR, basename(g_db_ctx.db_name));
  system(cmd);

  // prepare a empty db using the same name
  memset(cmd, 0, sizeof(cmd));
  snprintf(cmd, sizeof(cmd), "cp %s %s; sync",
           DATABASE_FILE_DEFAULT, g_db_ctx.db_name);
  system(cmd);

  _LOGD("%s - db [%s] corrupt!!! Use the empty db now!", TAG, g_db_ctx.db_name);
}

static void SetEpcItem(struct _CheckEpcItem *ret,
                       const char *name, const char *value) {

  if (!ret || !name || !value) {
    return;
  }

  //_LOGD("%s - %s name = %s, value = %s", TAG, __func__, name, value);

  if (!strcmp(name, "id") || !strcmp(name, "max(id)")) {
    strcpy(ret->id, value);
  } else if (!strcmp(name, "upload_status")) {
    strcpy(ret->upload_status, value);
  } else if (!strcmp(name, "value")) {
    strcpy(ret->value, value);
  } else if (!strcmp(name, "date")) {
    strcpy(ret->date, value);
  }
}

static int Get(const char *file, const char *sql,
                struct _CheckEpcItem* epcitem) {
  int rc = 0;

  sqlite3 *db = NULL;
  sqlite3_stmt *stmt = NULL;

  if (!file || !sql) {
    _LOGE("%s %s:%d", TAG, __func__, __LINE__);
    goto out;
  }

  rc = sqlite3_open_v2(file, &db, SQLITE_OPEN_READONLY, NULL);
  if (rc) {
    _LOGE("%s %s:%d, Can't open database: %s", TAG,
          __func__, __LINE__, sqlite3_errmsg(db));
    goto out;
  }

  // prepare the SQL statement from the command line
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc) {
    _LOGE("%s %s:%d: %s, %d, %s %s", TAG, __func__, __LINE__,
          file, rc, sqlite3_errmsg(db), sql);
    goto out;
  }

  // execute the statement
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
    _LOGE("%s %s, %s rc=%d, in %s, %d\n", TAG,
          sql, sqlite3_errmsg(db), rc, __func__, __LINE__);
    goto out;
  }

  if (rc == SQLITE_ROW) {
    int col, cols;
    // results for this row
    cols = sqlite3_column_count(stmt);
    for (col = 0; col < cols; col++) {
      //_LOGD("AAA %s %s: %s\n", TAG, sqlite3_column_name(stmt, col),
      //      (const char *) sqlite3_column_text(stmt, col));
      SetEpcItem(epcitem, sqlite3_column_name(stmt, col),
                 (const char *) sqlite3_column_text(stmt, col));
    }
  }

out:
  // finalize the statement to release resources
  if (stmt)
    sqlite3_finalize(stmt);

  if (db)
    sqlite3_close(db);

  if (rc == SQLITE_ROW)
    rc = SQLITE_OK;

  return rc;
}

static int Update(const char* file, const char* sql) {
  sqlite3 *db = NULL;
  sqlite3_stmt *stmt = NULL;
  int rc;

  //_LOGD("%s %s:%d: sql: %s", TAG, __func__, __LINE__, sql);

  rc = sqlite3_open_v2(file, &db, SQLITE_OPEN_READWRITE, NULL);
  if (rc) {
    _LOGE("%s %s:%d, Can't open database: %s", TAG,
              __func__, __LINE__, sqlite3_errmsg(db));
    goto out;
  }

  // prepare the SQL statement from the command line
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
  if (rc) {
    _LOGE("%s %s:%d: %s, %d, %s %s", TAG, __func__, __LINE__,
          file, rc, sqlite3_errmsg(db), sql);
    goto out;
  }

  // execute the statement
  rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    _LOGE("%s %s, %s in %s, %d\n", TAG,
          sql, sqlite3_errmsg(db), __func__, __LINE__);
    goto out;
  }

out:
  // finalize the statement to release resources
  if (stmt)
    sqlite3_finalize(stmt);

  if (db)
    sqlite3_close(db);

  if (rc == SQLITE_DONE)
    rc = SQLITE_OK;

  return rc;
}

static int GetAll(const char *file, const char *select,
                  fp_set_column call, struct list_head *all) {
  int rc;
  sqlite3 *db = NULL;
  sqlite3_stmt *stmt = NULL;

  if (!file || !select || !call || !all) {
    _LOGE("[db] %s:%d\n", __func__, __LINE__);
    return -1;
  }

  rc = sqlite3_open_v2(file, &db, SQLITE_OPEN_READONLY, NULL);
  if (rc) {
    _LOGE("[db] %s:%d Can't open database: %s",
          __func__, __LINE__, sqlite3_errmsg(db));
    goto out;
  }

  // prepare the SQL statement from the command line
  rc = sqlite3_prepare_v2(db, select, -1, &stmt, 0);
  if (rc) {
    _LOGE("[db] SQL error: %d, %s", __LINE__, sqlite3_errmsg(db));
    goto out;
  }

  // execute the statement
  do {
    rc = sqlite3_step(stmt);
    switch (rc) {
      case SQLITE_DONE:
        rc = SQLITE_OK;
        break;
      case SQLITE_ROW: // indicates that another row of output is available
        if (call(stmt, all) < 0) {
          goto out;
        }
        break;
      default:
        _LOGE("[db] SQL error: %d, %s", __LINE__, sqlite3_errmsg(db));
        goto out;
    }
  } while (rc == SQLITE_ROW);

out:
  // finalize the statement to release resources
  if (stmt)
    sqlite3_finalize(stmt);

  if (db)
    sqlite3_close(db);

  return rc;
}

static int GetAllRepeat(const char *file, const char *select,
                 fp_set_column call, struct list_head *all) {
  int i, rc;

  for (i = 0; i < 100; i++) {
    rc = GetAll(file, select, call, all);
    if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) {
      break;
    }

    _msleep(100);
  }

  if (rc)
    _LOGE("%s Error after repeat %d: %s, rc %d", TAG, i, select, rc);

  return rc;
}

static int GetRepeat(const char* file, const char* sql,
                     struct _CheckEpcItem* epcitem) {
  int i, rc;

  for (i = 0; i < 100; i++) {
    rc = Get(file, sql, epcitem);
    if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) {
      break;
    }

    _msleep(100);
  }

  if (rc && rc != SQLITE_DONE)
    _LOGE("%s Error after repeat %d: %s, rc %d", TAG, i, sql, rc);

  return rc;
}

static int UpdateRepeat(const char* file, const char* sql) {
  int i, rc;

  for (i = 0; i < 100; i++) {
    rc = Update(file, sql);
    if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) {
      break;
    }

    _msleep(100);
  }

  if (rc)
    _LOGE("%s Error after repeat %d: rc %d", TAG, i, rc);

  return rc;
}

static int Execute(const char* file, const char* sql) {
  int rc;
  char *errorMsg;
  sqlite3 *db = NULL;

  rc = sqlite3_open(file, &db);
  if (rc) {
    _LOGE("%s %s:%d Can't open database: %s",
          TAG, __func__, __LINE__, sqlite3_errmsg(db));
    goto out;
  }

  rc = sqlite3_exec(db, sql, NULL, NULL, &errorMsg);
  if (rc != SQLITE_OK) {
    _LOGW("%s warning %s:%d %s", TAG, sql, rc, errorMsg);
    goto out;
  }

out:
  if (db)
    sqlite3_close(db);

  return rc;
}


static int ExecuteRepeat(const char* file, const char* sql) {
  int i, rc;

  for (i = 0; i < 100; i++) {
    rc = Execute(file, sql);
    if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) {
      break;
    }

    _msleep(100);
  }

  if (rc)
    _LOGE("[db] Error after repeat %d: %s, rc %d", i, sql, rc);

  return rc;
}

static int ExecuteFast(sqlite3 *db, const char* sql) {
  int rc = 0;
  char *errorMsg = NULL;

  if (!db)
    return -1;

  rc = sqlite3_exec(db, sql, NULL, NULL, &errorMsg);
  if (rc != SQLITE_OK) {
    _LOGW("%s warning %s:%d %s", TAG, sql, rc, errorMsg);
  }

  return rc;
}

static int ExecuteRepeatFast(sqlite3 *db, const char* sql) {
  int i, rc;

  if (!db)
    return -1;

  //_LOGD("%s - %s", TAG, __func__);

  for (i = 0; i < 100; i++) {
    rc = ExecuteFast(db, sql);
    if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) {
      break;
    }

    _msleep(100);
  }

  if (rc)
    _LOGE("%s -ERF Error after repeat %d: %s, rc %d", TAG, i, sql, rc);

  return rc;
}

static int DbSetColumn(sqlite3_stmt *stmt, struct list_head *all) {
  int col, cols;

  if (!stmt || !all) {
    _LOGE("[db] %s:%d", __func__, __LINE__);
    return -1;
  }

  struct _CheckEpcItem *tmp = calloc(1, sizeof(struct _CheckEpcItem));
  if (!tmp) {
    _LOGE("[db] %s:%d", __func__, __LINE__);
    return -1;
  }

  // results for this row
  cols = sqlite3_column_count(stmt);
  for (col = 0; col < cols; col++) {
    SetEpcItem(tmp, sqlite3_column_name(stmt, col),
                  (const char *) sqlite3_column_text(stmt, col));
  }

  list_add_tail(&(tmp->list), all);
  return 0;
}

static int GetMaxId(const char* db_name) {
  struct _CheckEpcItem epcitem;

  char sql[MAX_TEXT_LENGTH];

  memset(sql, 0, sizeof(sql));
  snprintf(sql, sizeof(sql), "select max(id) from datas");

  int rc;

  rc = GetRepeat(db_name, sql, &epcitem);
  if (rc == 0) {
    return atoi(epcitem.id);
  } else if (rc == SQLITE_CORRUPT) {
    _LOGE("%s - DB corrupt for [%s]", TAG, sql);
    DoReplaceDb();
  }

  return -1;
}

int GetOriginalDate(struct _CheckEpcItem *epcitem) {
  if (epcitem == NULL)
    return -1;

  //_LOGD("%s - %s", TAG, __func__);

  char sql[MAX_TEXT_LENGTH];
  memset(sql, 0, sizeof(sql));

  snprintf(sql, sizeof(sql),
           "select * from datas order by date asc limit 1");

  int rc;
  rc = GetRepeat(g_db_ctx.db_name, sql, epcitem);
  if (rc == 0) {
    //strcpy(org_date, epcitem.date);
    return 0;
  }

  _LOGE("%s - %s get original date err", TAG, __func__);
  return -1;

}

static int isDbOk(void) {
  if (!isFileExist(DATABASE_FILE)) {
    return 0;
  }

  if (!isFileExist(DATABASE_FILE2)) {
    return 0;
  }

  if (!GetFileSize(DATABASE_FILE)) {
    return 0;
  }

  if (!GetFileSize(DATABASE_FILE2)) {
    return 0;
  }

  return 1;
}

static int DbInsertItem(sqlite3 *db, const struct _CheckEpcItem* epcitem) {

  if (!db)
    return -1;

  char sql[MAX_TEXT_LENGTH];
  memset(sql, 0, sizeof(sql));

  snprintf(sql, sizeof(sql),
           "insert into datas values (NULL, 'N', '%s', '%s')",
           epcitem->value, epcitem->date);

  //_LOGD("%s: %s", TAG, sql);
  int ret = 0;

  // check if SD readonly
  ret = ExecuteRepeatFast(db, sql);
  if (ret == SQLITE_READONLY) {
    DoRemountSD();
  }

  return ret;
}

static int SwitchDb() {
  char cmd[MAX_TEXT_LENGTH];

  if (strcmp(g_db_ctx.db_name, DATABASE_FILE) == 0) {
    strcpy(g_db_ctx.db_name, DATABASE_FILE2);
  } else {
    strcpy(g_db_ctx.db_name, DATABASE_FILE);
  }

  // store the old db
  memset(cmd, 0, sizeof(cmd));
  snprintf(cmd, sizeof(cmd), "dt=$(date +'%%Y%%m%%d_%%H%%M%%S'); mv %s %s/%s_$dt",
           g_db_ctx.db_name, OLD_DB_DIR, basename(g_db_ctx.db_name));
  system(cmd);

  // prepare a empty db using the same name
  memset(cmd, 0, sizeof(cmd));
  snprintf(cmd, sizeof(cmd), "cp %s %s; sync",
           DATABASE_FILE_DEFAULT, g_db_ctx.db_name);
  system(cmd);

  _LOGD("%s - switch db to %s", TAG, g_db_ctx.db_name);

  return 0;
}

int DbInsertMultiItems(const struct _CheckEpcItem* multiepcs,
                       int num, int* inserted_num) {
  int rc = 0;
  sqlite3 *db = NULL;
  char *errorMsg = NULL;
  int inserted_n = 0;

  //_LOGD("%s - %s", TAG, __func__);

  rc = sqlite3_open_v2(g_db_ctx.db_name, &db, SQLITE_OPEN_READWRITE, NULL);
  if (rc) {
    _LOGE("%s %s:%d, Can't open database: %s", TAG,
              __func__, __LINE__, sqlite3_errmsg(db));
    return -1;
  }

  rc = sqlite3_exec(db, "begin;", NULL, NULL, &errorMsg);
  if (rc != SQLITE_OK) {
    _LOGW("%s warning %s:%d %s", TAG, "begin;", rc, errorMsg);
    goto out;
  }

  int i = 0;
  for (i = 0; i < num; i++) {
    rc = DbInsertItem(db, &multiepcs[i]);
    if (rc == 0) {
      inserted_n++;
    } else if (rc == SQLITE_CORRUPT) {
      goto out;
    }
  }

  rc = sqlite3_exec(db, "commit;", NULL, NULL, &errorMsg);
  if (rc != SQLITE_OK) {
    _LOGW("%s warning %s:%d %s", TAG, "commit;", rc, errorMsg);
    goto out;
  }

  _LOGD("%s - %s: Do Commit (%d epcs)", TAG, __func__, inserted_n);

out:
  sqlite3_close(db);

  *inserted_num = inserted_n;

  if (rc == SQLITE_CORRUPT) {
    _LOGE("%s - DB corrupt for inserting", TAG);
    DoReplaceDb();
  }

  return rc;
}

// get all if group is NULL
static int GetGroup(const char *file, const char *group, struct list_head *all) {
  char sql[MAX_TEXT_LENGTH];
  memset(sql, 0, sizeof(sql));

  if (group) {
    snprintf(sql, sizeof(sql),
             "select * from datas where upload_status = '%s'", group);
  } else {
    snprintf(sql, sizeof(sql), "select * from datas");
  }

  return GetAllRepeat(file, sql, DbSetColumn, all);
}

int DbGetGroup(const char *group, struct _CheckEpcItem *all) {
  return GetGroup(g_db_ctx.db_name, group, &all->list);
}

int GetCanInsertedNum(int num) {
  int current_item_num = GetMaxId(g_db_ctx.db_name);
  if (current_item_num < 0) {
    return -1;
  }

  if (current_item_num >= MAX_DB_RECORDS_NUM) {
    _LOGD("%s - reached max", TAG);
    if (SwitchDb() == 0) {
      current_item_num = GetMaxId(g_db_ctx.db_name);
    } else {
      _LOGE("%s - %s switch db err", TAG, __func__);
      return -1;
    }
  }

  if (current_item_num + num > MAX_DB_RECORDS_NUM) {
    _LOGD("%s - %s current item num: %d, new num: %d",
          TAG, __func__, current_item_num, num);
    return (MAX_DB_RECORDS_NUM - current_item_num);
  }

  return num;
}

int DbGetTotalNum() {
  u32 total_num = 0;
  total_num = GetMaxId(DATABASE_FILE);
  total_num += GetMaxId(DATABASE_FILE2);
  return total_num;
}

int DbGetDbANum() {
  return GetMaxId(DATABASE_FILE);
}

int DbGetDbBNum() {
  return GetMaxId(DATABASE_FILE2);
}

int DbGetUnuploadedRows(struct _CheckEpcItem *all, int rows) {
  if (all == NULL || rows < 1)
    return -1;

  char sql[MAX_TEXT_LENGTH];
  memset(sql, 0, sizeof(sql));

  snprintf(sql, sizeof(sql),
           "select * from datas where upload_status = 'N' LIMIT %d", rows);

  return GetAllRepeat(g_db_ctx.db_name, sql, DbSetColumn, &all->list);
}

int DbGetUnuploadedRows1(struct _CheckEpcItem *all, const char* db_name,
                         const char* max_id, const char* time, int rows) {
  if (all == NULL || db_name == NULL ||
      max_id == NULL || time == NULL || rows < 1 )
    return -1;

  char sql[MAX_TEXT_LENGTH];
  memset(sql, 0, sizeof(sql));

  snprintf(sql, sizeof(sql),
           "select * from datas where id > %s LIMIT %d", max_id, rows);

  return GetAllRepeat(db_name, sql, DbSetColumn, &all->list);
}

static int DbGetNextUnuploadedItem(const char* db_name, const char* max_id,
                                   const char* time,
                                   struct _CheckEpcItem *epcitem) {
  if (db_name == NULL || max_id == NULL || time == NULL ||
      epcitem == NULL)
    return -1;

  char sql[MAX_TEXT_LENGTH];
  memset(sql, 0, sizeof(sql));

  snprintf(sql, sizeof(sql),
           "select * from datas where id > %s and date >= %s LIMIT 1",
           max_id, time);

  return GetRepeat(db_name, sql, epcitem);
}

static int DbGetEarliestItemInfo(char* db_name,
                                    struct _CheckEpcItem* epcitem) {
  if (db_name == NULL || epcitem == NULL)
    return -1;

  char sql[MAX_TEXT_LENGTH];

  memset(sql, 0, sizeof(sql));
  snprintf(sql, sizeof(sql),
           "select * from datas where id=1");

  struct _CheckEpcItem tmp_item;
  memset(&tmp_item, 0, sizeof(tmp_item));

  int rc;
  // check first DB
  rc = GetRepeat(DATABASE_FILE, sql, &tmp_item);
  if (rc != 0) {
    _LOGE("%s - %s select db1 err rc = %d", TAG, __func__, rc);
  } else {
    *epcitem = tmp_item;

    // check second DB
    memset(&tmp_item, 0, sizeof(tmp_item));
    rc = GetRepeat(DATABASE_FILE2, sql, &tmp_item);
    if (rc == 0) {
      // chose the earliest item
      if (strcmp(tmp_item.date, epcitem->date) < 0) {
        *epcitem = tmp_item;
        strcpy(db_name, DATABASE_FILE2);
      } else {
        strcpy(db_name, DATABASE_FILE);
      }
    } else if (rc == SQLITE_DONE) {
      // db2 is empty
      strcpy(db_name, DATABASE_FILE);
      rc = 0;
    } else {
      _LOGE("%s - %s select db2 err rc = %d", TAG, __func__, rc);
    }
  }

  return rc;
}

static int DbGetFirstItemInfo(const char* db_name,
                              struct _CheckEpcItem* epcitem) {
  if (db_name == NULL || epcitem == NULL)
    return -1;

  char sql[MAX_TEXT_LENGTH];

  memset(sql, 0, sizeof(sql));
  snprintf(sql, sizeof(sql),
           "select * from datas where id=1");

  struct _CheckEpcItem tmp_item;
  memset(&tmp_item, 0, sizeof(tmp_item));

  int rc;
  rc = GetRepeat(db_name, sql, &tmp_item);
  if (rc != 0) {
    _LOGE("%s - %s select db1 err rc = %d", TAG, __func__, rc);
  } else {
    *epcitem = tmp_item;
  }

  return rc;
}

int isExistUnuploadedEpc() {
  _LOGD("%s - %s", TAG, __func__);

  struct _CheckEpcItem one_item;
  char db_name[128];
  char item_date[128];
  char item_id[128];

  memset(db_name, 0, sizeof(db_name));
  memset(item_id, 0, sizeof(item_id));
  memset(item_date, 0, sizeof(item_date));

  if (GetDbUploadedInfo(db_name, item_id, item_date) == 0) {
    _LOGD("%s - %s name = %s, id = %s, date = %s",
          TAG, __func__, db_name, item_id, item_date);

    int ret = 0;
    ret = DbGetNextUnuploadedItem(db_name, item_id, item_date, &one_item);
    if (ret == 0) {
      return 1;
    } else if (ret == SQLITE_DONE) {
      _LOGD("%s - %s no unuploaded item now", TAG, __func__);
      return 0;
    } else {
      _LOGE("%s - %s DbGetNextUnuploadedItem err ret = %d", TAG, __func__, ret);
    }
  } else {
    _LOGE("%s - %s GetDbUploadedIndex err", TAG, __func__);
  }

  return 0;
}

int KeepDbUploadedInfo(const char* db_name,
                       const char* upload_id, const char* datebuf) {
  if (db_name == NULL || datebuf == NULL || upload_id == NULL)
    return -1;

  int uid = 0;
  uid = atoi(upload_id);
  _LOGD("%s - %s upload id = %d", TAG, __func__, uid);

  char new_db_name[128];
  FILE *fp = NULL;

  fp = fopen(DB_INDEX_FILE_NAME, "w");
  if (fp) {
    if (uid >= MAX_DB_RECORDS_NUM) {
      _LOGD("%s - %s upload ID reached max now, record another DB index now",
            TAG, __func__);

      memset(new_db_name, 0, sizeof(new_db_name));
      if (strcmp(db_name, DATABASE_FILE) == 0)
        strcpy(new_db_name, DATABASE_FILE2);
      else
        strcpy(new_db_name, DATABASE_FILE);

      fprintf(fp, "%s\n", new_db_name);
      fprintf(fp, "%s\n", "0");

      struct _CheckEpcItem first_item;
      memset(&first_item, 0, sizeof(first_item));
      if (DbGetFirstItemInfo(new_db_name, &first_item) == 0) {
        fprintf(fp, "%s\n", first_item.date);
      } else {
        _LOGE("%s - %s DbGetFirstItemInfo err", TAG, __func__);
        // use another db's last item date
        fprintf(fp, "%s\n", datebuf);
      }

    } else {
      fprintf(fp, "%s\n", db_name);
      fprintf(fp, "%s\n", upload_id);
      fprintf(fp, "%s\n", datebuf);
    }

    fclose(fp);
    return 0;
  }

  return -1;
}

static int isEpcItemReplaced(const char* db_name, const char *item_id,
                             const char* item_date) {

  if (db_name == NULL || item_date == NULL || item_id == NULL)
    return -1;

  //_LOGD("%s - %s", TAG, __func__);

  char sql[MAX_TEXT_LENGTH];

  memset(sql, 0, sizeof(sql));
  snprintf(sql, sizeof(sql),
           "select * from datas where id=%s", item_id);

  struct _CheckEpcItem tmp_item;
  memset(&tmp_item, 0, sizeof(tmp_item));

  int rc;
  rc = GetRepeat(db_name, sql, &tmp_item);
  if (rc == 0) {
    if (strcmp(tmp_item.date, item_date) != 0) {
      return 1;
    }
  } else if (rc == SQLITE_DONE){
    _LOGE("%s - %s id(%s) item not exist, it should be replaced!!",
          TAG, __func__, item_id);
    return 1;
  } else {
    _LOGE("%s - %s err rc = %d", TAG, __func__, rc);
    return -1;
  }

  return 0;
}

int KeepFirstUnuploadedIndex() {
  struct _CheckEpcItem epcitem;
  char name_buf[128];
  char prev_id_buf[20];

  memset(&epcitem, 0, sizeof(epcitem));
  memset(name_buf, 0, sizeof(name_buf));

  _LOGD("%s - %s", TAG, __func__);
  if (DbGetEarliestItemInfo(name_buf, &epcitem) == 0) {
    _LOGD("%s - %s db = %s, id = %s, date = %s",
          TAG, __func__, name_buf, epcitem.id, epcitem.date);
    int prev_id = atoi(epcitem.id);
    if (prev_id > 0)
      prev_id--;

    memset(prev_id_buf, 0, sizeof(prev_id_buf));
    sprintf(prev_id_buf, "%d", prev_id);
    KeepDbUploadedInfo(name_buf, prev_id_buf, epcitem.date);
    return 0;
  }

  return -1;
}

static void GetStrimStr(char* strbuf, int buf_len, FILE *fp) {
  if (strbuf == NULL || fp == NULL)
    return;

  char* find = NULL;

  memset(strbuf, 0, buf_len);
  if (fgets(strbuf, buf_len-1, fp) == NULL) {
    _LOGE("%s - %s ERR endofile or err for fgets", TAG, __func__);
    return;
  }

  find = strchr(strbuf, '\n');
  if (find)
    *find = '\0';
}

int GetDbUploadedIndex(char* db_name, char *max_id, char* datebuf) {
  if (db_name == NULL || max_id == NULL || datebuf == NULL)
    return -1;

  _LOGD("%s - %s", TAG, __func__);

  if (access(DB_INDEX_FILE_NAME, F_OK) != 0) {
    if (KeepFirstUnuploadedIndex() < 0) {
      _LOGE("%s - %s err KeepFirstUnuploadedIndex", TAG, __func__);
      return -1;
    }
  }

  static int valid_get_num = 0;
  FILE *fp = NULL;
  int err_flag = 0;
  char instr[128] = "";
  fp = fopen(DB_INDEX_FILE_NAME, "r");
  if (fp) {
    GetStrimStr(instr, sizeof(instr), fp);
    if (instr[0] == '\0') {
      _LOGE("%s - %s err - no db_name", TAG, __func__);
      err_flag = 1;
    } else {
      strcpy(db_name, instr);
    }

    GetStrimStr(instr, sizeof(instr), fp);
    if (instr[0] == '\0') {
      _LOGE("%s - %s err - no max_id", TAG, __func__);
      err_flag = 1;
    } else {
      strcpy(max_id, instr);
    }

    GetStrimStr(instr, sizeof(instr), fp);
    if (instr[0] == '\0') {
      _LOGE("%s - %s err - no date", TAG, __func__);
      err_flag = 1;
    } else {
      strcpy(datebuf, instr);
    }

    _LOGD("%s - %s dbname = %s, id = %s, date = %s",
          TAG, __func__, db_name, max_id, datebuf);
    fclose(fp);
  }

  if (err_flag == 0) {
    valid_get_num++;
    // nearly 10*5000 = 50000 epc uploaded
    if (valid_get_num >= 10) {
      valid_get_num = 0;

      // backup current db_index file
      char cmd[64];
      snprintf(cmd, sizeof(cmd), "cp %s %s",
               DB_INDEX_FILE_NAME, DB_INDEX_BK_FILE_NAME);
      system(cmd);
      _LOGD("%s - %s backup db_index file", TAG, __func__);
    }
  } else {
    // the db_index file empty now, recover by using the backup file
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "cp %s %s",
             DB_INDEX_BK_FILE_NAME, DB_INDEX_FILE_NAME);
    system(cmd);
    _LOGD("%s - %s restore db_index file", TAG, __func__);
  }
  return err_flag;
}

int GetDbUploadedInfo(char* db_name, char *item_id, char* item_date) {
  if (db_name == NULL || item_id == NULL || item_date == NULL)
    return -1;

  _LOGD("%s - %s", TAG, __func__);

  if (GetDbUploadedIndex(db_name, item_id, item_date) != 0) {
    _LOGE("%s 1GetDbUploadedIndex err", TAG);
    return -1;
  }

  int prev_id = 0;
  char prev_id_buf[20];
  prev_id = atoi(item_id);
  if (prev_id == 0)
    prev_id = 1;

  sprintf(prev_id_buf, "%d", prev_id);

  int ret = 0;
  ret = isEpcItemReplaced(db_name, prev_id_buf, item_date);
  if (ret < 0)
    return -1;

  if (ret == 1) {
    if (KeepFirstUnuploadedIndex() != 0) {
      _LOGE("%s KeepFirstUnuploadedIndex err", TAG);
      return -1;
    }

    if (GetDbUploadedIndex(db_name, item_id, item_date) != 0) {
      _LOGE("%s 2GetDbUploadedIndex err", TAG);
      return -1;
    }
  }

  return 0;
}

static void GetCurrentDbName() {
  int max_id = 0;
  max_id = GetMaxId(DATABASE_FILE);

  if (max_id >= MAX_DB_RECORDS_NUM) {
    strcpy(g_db_ctx.db_name, DATABASE_FILE2);
  } else {
    strcpy(g_db_ctx.db_name, DATABASE_FILE);
  }

  _LOGD("%s - %s current DB name: %s", TAG, __func__, g_db_ctx.db_name);
}

int InitDb() {
  char cmd[128];

  memset(cmd, 0, sizeof(cmd));
  memset(&g_db_ctx, 0, sizeof(g_db_ctx));

  if (!isDbOk() && isFileExist(DATABASE_FILE_DEFAULT)) {
    snprintf(cmd, sizeof(cmd), "cp %s %s; cp %s %s; sync",
             DATABASE_FILE_DEFAULT, DATABASE_FILE,
             DATABASE_FILE_DEFAULT, DATABASE_FILE2);
    system(cmd);
  }

  if (isFileExist(DATEBASE_RB_JOURNAL)) {
    snprintf(cmd, sizeof(cmd), "rm %s; sync", DATEBASE_RB_JOURNAL);
    system(cmd);
  }

  if (isFileExist(DATEBASE_RB_JOURNAL2)) {
    snprintf(cmd, sizeof(cmd), "rm %s; sync", DATEBASE_RB_JOURNAL2);
    system(cmd);
  }

  if (isFileExist(DB_INDEX_STORED_NAME)) {
    snprintf(cmd, sizeof(cmd), "cp %s %s",
             DB_INDEX_STORED_NAME, DB_INDEX_FILE_NAME);
    system(cmd);
  }

  GetCurrentDbName();

  return 0;
}

