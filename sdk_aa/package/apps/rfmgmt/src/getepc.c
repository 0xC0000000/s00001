#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "db_process.h"

static void usage() {
  fprintf(stderr, "usage: getepc [N|Y]\n");
}

int main(int argc, char *argv[]) {
  char value[MAX_TEXT_LENGTH];

  memset(value, 0, sizeof(value));

  if (argc > 2) {
    usage();
    return -1;
  }

  char group[4] = "";
  int check_count = 0;

  if (argc == 2) {
    if (!strncmp(argv[1], "Y", 1)) {
      strcpy(group, "Y");
    } else if (!strncmp(argv[1], "N", 1)) {
      strcpy(group, "N");
    } else if (!strncmp(argv[1], "X", 1)) {
      check_count = 1;
    } else if (!strncmp(argv[1], "AX", 2)) {
      printf("epcdata counts = %d\n", DbGetDbANum());
      return 0;
    } else if (!strncmp(argv[1], "BX", 2)) {
      printf("epcdata2 counts = %d\n", DbGetDbBNum());
      return 0;
    } else {
      usage();
      return -1;
    }
  }

  if (check_count) {
    printf("total EPC counts = %d\n", DbGetTotalNum());
    return 0;
  }

  struct _CheckEpcItem all;

  INIT_LIST_HEAD(&all.list);
  if (strlen(group) > 0) {
    DbGetGroup(group, &all);
  } else {
    DbGetGroup(NULL, &all);
  }

  struct list_head *pos, *q;
  struct _CheckEpcItem *tmp;

  list_for_each_safe(pos, q, &all.list) {
    tmp = list_entry(pos, struct _CheckEpcItem, list);
    fprintf(stderr, "%s: %s, %s, %s\n",
            tmp->id, tmp->upload_status, tmp->value, tmp->date);

    list_del(pos);
    free(tmp);
  }

  return 0;
}
