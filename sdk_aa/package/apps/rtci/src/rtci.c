#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#define RTC_DEV_NAME       "/dev/sds1302"

struct datetime {
  unsigned char year;
  unsigned char month;
  unsigned char mday;
  unsigned char hour;
  unsigned char min;
  unsigned char sec;
  unsigned char wday;
};

static void usage(const char* appname) {
  printf("Usage: \nread rtc date:\n\t%s\n\nset rtc date:\n\t"\
         "%s -s year-month-day hour:minute:second\n\n\t"\
         "sample:\n\t%s\n\t%s -s 2017-08-01 20:18:00\n",
         appname, appname, appname, appname);
}

static int set_datetime(int fd, struct datetime* dt, const char* date_str) {
  int limit[6][2] = {{1980, 2079}, {1, 12}, {1, 31}, {0, 23}, {0, 59}, {0, 59}};
  int value[6];
  char setbuf[64];

  memset(value, 0, sizeof(value));
  memset(setbuf, 0, sizeof(setbuf));

  strncpy(setbuf, date_str, sizeof(setbuf)-1);

  char *p = setbuf;
  value[0] = strtoul(p, &p, 10);
  if (*p != '-')
    return -1;

  int quit = 0;
  int i = 0;
  for (i = 1; i < 6; i++) {
    value[i] = strtoul(p+1, &p, 10);
    if (i < 2)
      quit = (*p == '-') ? 0 : 1;
    else if (i == 2)
      quit = (*p == ' ') ? 0 : 1;
    else if (i > 2 && i < 5)
      quit = (*p == ':') ? 0 : 1;
    else if (i == 5)
      quit = (*p == '\0') ? 0 : 1;

    if (quit)
      return -1;
  }

  // valid check
  for (i = 0; i < 6; i++) {
    printf("%d ", value[i]);
    if ((value[i] < limit[i][0]) || value[i] > limit[i][1])
      return -1;
  }
  printf("\n");

  dt->year = value[0] - 2000;
  dt->month = value[1];
  dt->mday = value[2];
  dt->hour = value[3];
  dt->min = value[4];
  dt->sec = value[5];

  printf("%d-%d-%d %d:%d:%d\n",
         dt->year, dt->month, dt->mday, dt->hour, dt->min, dt->sec);

  ssize_t len;
  len = write(fd, dt, sizeof(struct datetime));
  if (len != sizeof(struct datetime)) {
    printf("write err\n");
    return -1;
  }

  return 0;
}

int main(int argc, char *argv[])
{
  int fd = open(RTC_DEV_NAME, O_RDWR);
  if (fd < 0) {
    printf("open %s failed\n", RTC_DEV_NAME);
    return -1;
  }

  ssize_t len;
  struct datetime tm;
  memset(&tm, 0, sizeof(tm));

  if (argc == 1) {
    len = read(fd, &tm, sizeof(tm));
    if (len != sizeof(tm)) {
      printf("read err\n");
      return -1;
    }

    char time_buf[64];
    memset(time_buf, 0, sizeof(time_buf));
    snprintf(time_buf, sizeof(time_buf)-1, "%d-%02d-%02d %02d:%02d:%02d",
             tm.year+2000, tm.month, tm.mday, tm.hour, tm.min, tm.sec);
    printf("%s\n", time_buf);
    return 0;
  }

  if (argc != 3 && memcmp(argv[1], "-s", 2) != 0) {
    usage(argv[0]);
    return -1;
  }

  if (set_datetime(fd, &tm, argv[2]) < 0) {
    usage(argv[0]);
    return -1;
  }

  return 0;
}
