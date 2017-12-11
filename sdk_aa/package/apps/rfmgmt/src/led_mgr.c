#include "led_mgr.h"

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "utils.h"

#define UPLOAD_STATUS_LED_PIN       15
#define RF_READER_BROKEN_LED_PIN    3
#define RF_READER_RESET_PIN         0

#define TAG                    "[LED]"

static int GpioExport(int pin) {
  char buffer[64];
  int len;
  int fd;

  fd = open("/sys/class/gpio/export", O_WRONLY);
  if (fd < 0) {
    _LOGE("failed to open export for writing!");
    return -1;
  }

  len = snprintf(buffer, sizeof(buffer), "%d", pin);
  if (write(fd, buffer, len) < 0) {
    _LOGE("failed to export gpio %d", pin);
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

static int GpioUnexport(int pin) {
  char buffer[64];
  int len;
  int fd;

  fd = open("/sys/class/gpio/unexport", O_WRONLY);
  if (fd < 0) {
    _LOGE("failed to open unexport for writing!");
    return -1;
  }

  len = snprintf(buffer, sizeof(buffer), "%d", pin);
  if (write(fd, buffer, len) < 0) {
    _LOGE("failed to unexport gpio %d", pin);
    close(fd);
    return -1;
  }

  close(fd);
  return 0;
}

// dir: 0 for input, 1 for output
static int GpioSetDirection(int pin, int dir) {
  char path[64];
  int fd;

  snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", pin);
  fd = open(path, O_WRONLY);
  if (fd < 0) {
    _LOGE("failed to open gpio direction for writing!");
    return -1;
  }

  if (dir == 0) {
    if (write(fd, "in", 2) < 0) {
      _LOGE("failed to set direction in for %d", pin);
      close(fd);
      return -1;
    }
  } else {
    if (write(fd, "out", 3) < 0) {
      _LOGE("failed to set direction out for %d", pin);
      close(fd);
      return -1;
    }
  }

  close(fd);
  return 0;
}

// val: 0 for LOW, 1 for HIGH
static int GpioSetValue(int pin, int val) {
  char path[64];
  int fd;

  snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
  fd = open(path, O_WRONLY);
  if (fd < 0) {
    _LOGE("failed to open gpio value for writing!");
    return -1;
  }

  if (val == 0) {
    if (write(fd, "0", 1) < 0) {
      _LOGE("failed to set value LOW for %d", pin);
      close(fd);
      return -1;
    }
  } else {
    if (write(fd, "1", 1) < 0) {
      _LOGE("failed to set value HIGH for %d", pin);
      close(fd);
      return -1;
    }
  }

  close(fd);
  return 0;
}

static int GpioReadValue(int pin) {
  char path[64];
  char value_str[4] = "";
  int fd;

  snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", pin);
  fd = open(path, O_RDONLY);
  if (fd < 0) {
    _LOGE("failed to open gpio value for reading!");
    return -1;
  }

  if (read(fd, value_str, sizeof(value_str)) < 0) {
    _LOGE("failed to read value for pin %d", pin);
    close(fd);
    return -1;
  }

  close(fd);
  return (atoi(value_str));
}

int InitLed() {
  // init upload status led
  if (GpioExport(UPLOAD_STATUS_LED_PIN) < 0)
    return -1;
  if (GpioSetDirection(UPLOAD_STATUS_LED_PIN, 1) < 0)
    return -1;

  if (GpioExport(RF_READER_BROKEN_LED_PIN) < 0)
    return -1;
  if (GpioSetDirection(RF_READER_BROKEN_LED_PIN, 1) < 0)
    return -1;

  if (GpioExport(RF_READER_RESET_PIN) < 0)
    return -1;
  if (GpioSetDirection(RF_READER_RESET_PIN, 1) < 0)
    return -1;
  GpioSetValue(RF_READER_RESET_PIN, 1);

  return 0;
}

void BlinkUploadStatusLed() {
  static int val = 0;

  val = (val == 0) ? 1 : 0;
  GpioSetValue(UPLOAD_STATUS_LED_PIN, val);
}

void TurnOffUploadStatusLed() {
  GpioSetValue(UPLOAD_STATUS_LED_PIN, 0);
}

void TurnOnRfReaderErrLed() {
  _LOGD("%s - Reader ERR ON", TAG);
  GpioSetValue(RF_READER_BROKEN_LED_PIN, 1);
}

void TurnOffRfReaderErrLed() {
  _LOGD("%s - Reader ERR OFF", TAG);
  GpioSetValue(RF_READER_BROKEN_LED_PIN, 0);
}

void ResetReader() {
  // pull low 3 seconds leads to the reader reset
  _LOGD("Do reader reset!");
  GpioSetValue(RF_READER_RESET_PIN, 0);
  sleep(3);
  GpioSetValue(RF_READER_RESET_PIN, 1);
  sleep(5);
}