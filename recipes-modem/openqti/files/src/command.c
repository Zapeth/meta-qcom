// SPDX-License-Identifier: MIT

#include "../inc/command.h"
#include "../inc/adspfw.h"
#include "../inc/audio.h"
#include "../inc/call.h"
#include "../inc/cell_broadcast.h"
#include "../inc/config.h"
#include "../inc/dms.h"
#include "../inc/ipc.h"
#include "../inc/logger.h"
#include "../inc/nas.h"
#include "../inc/proxy.h"
#include "../inc/scheduler.h"
#include "../inc/sms.h"
#include "../inc/tracking.h"
#include "../inc/wds.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

struct {
  bool is_unlocked;
  uint32_t unlock_time;
  uint8_t cmd_history[1024];
  uint16_t cmd_position;
  uint32_t last_cmd_timestamp;
  char user_name[32];
  char bot_name[32];
} cmd_runtime;

char *get_rt_modem_name() { return cmd_runtime.bot_name; }

char *get_rt_user_name() { return cmd_runtime.user_name; }

void add_to_history(uint8_t command_id) {
  if (cmd_runtime.cmd_position >= 1023) {
    cmd_runtime.cmd_position = 0;
  }
  cmd_runtime.cmd_history[cmd_runtime.cmd_position] = command_id;
  cmd_runtime.cmd_position++;
}

uint16_t find_cmd_history_match(uint8_t command_id) {
  uint16_t match = 0;
  uint16_t countdown;
  if (cmd_runtime.cmd_position > 5) {
    countdown = cmd_runtime.cmd_position - 5;
    for (uint16_t i = cmd_runtime.cmd_position; i >= countdown; i--) {
      if (cmd_runtime.cmd_history[i] == command_id) {
        match++;
      }
    }
  }
  return match;
}

void get_names() {
  get_modem_name(cmd_runtime.bot_name);
  get_user_name(cmd_runtime.user_name);
}
void set_cmd_runtime_defaults() {
  cmd_runtime.is_unlocked = false;
  cmd_runtime.unlock_time = 0;
  strncpy(cmd_runtime.user_name, "User",
          32); // FIXME: Allow user to set a custom name
  strncpy(cmd_runtime.bot_name, "Modem",
          32); // FIXME: Allow to change modem name
  get_names();
}

int get_uptime(uint8_t *output) {
  unsigned updays, uphours, upminutes;
  struct sysinfo info;
  struct tm *current_time;
  time_t current_secs;
  int bytes_written = 0;
  time(&current_secs);
  current_time = localtime(&current_secs);

  sysinfo(&info);

  bytes_written = snprintf((char *)output, MAX_MESSAGE_SIZE,
                           "%02u:%02u:%02u up ", current_time->tm_hour,
                           current_time->tm_min, current_time->tm_sec);
  updays = (unsigned)info.uptime / (unsigned)(60 * 60 * 24);
  if (updays)
    bytes_written += snprintf((char *)output + bytes_written,
                              MAX_MESSAGE_SIZE - bytes_written, "%u day%s, ",
                              updays, (updays != 1) ? "s" : "");
  upminutes = (unsigned)info.uptime / (unsigned)60;
  uphours = (upminutes / (unsigned)60) % (unsigned)24;
  upminutes %= 60;
  if (uphours)
    bytes_written += snprintf((char *)output + bytes_written,
                              MAX_MESSAGE_SIZE - bytes_written, "%2u:%02u",
                              uphours, upminutes);
  else
    bytes_written +=
        snprintf((char *)output + bytes_written,
                 MAX_MESSAGE_SIZE - bytes_written, "%u min", upminutes);

  return 0;
}

int get_load_avg(uint8_t *output) {
  int fd;
  fd = open("/proc/loadavg", O_RDONLY);
  if (fd < 0) {
    logger(MSG_ERROR, "%s: Cannot open load average \n", __func__);
    return 0;
  }
  lseek(fd, 0, SEEK_SET);
  if (read(fd, output, 64) <= 0) {
    logger(MSG_ERROR, "%s: Error reading PROCFS entry \n", __func__);
    close(fd);
    return 0;
  }

  close(fd);
  return 0;
}

int get_memory(uint8_t *output) {
  struct sysinfo info;
  sysinfo(&info);

  snprintf((char *)output, MAX_MESSAGE_SIZE,
           "Total:%luM\nFree:%luM\nShared:%luK\nBuffer:%luK\nProcs:%i\n",
           (info.totalram / 1024 / 1024), (info.freeram / 1024 / 1024),
           (info.sharedram / 1024), (info.bufferram / 1024), info.procs);

  return 0;
}

void set_custom_modem_name(uint8_t *command) {
  int strsz = 0;
  uint8_t *offset;
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  char name[32];
  offset = (uint8_t *)strstr((char *)command, partial_commands[0].cmd);
  if (offset == NULL) {
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE - strsz,
                     "Error setting my new name\n");
  } else {
    int ofs = (int)(offset - command) + strlen(partial_commands[0].cmd);
    if (strlen((char *)command) > ofs) {
      snprintf(name, 32, "%s", (char *)command + ofs);
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "My name is now %s\n",
                       name);
      set_modem_name(name);
      get_names();
    }
  }
  add_message_to_queue(reply, strsz);
  free(reply);
  reply = NULL;
}

void enable_service_debugging_for_service_id(uint8_t *command) {
  int strsz = 0;
  uint8_t *offset;
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  uint8_t service_id = 0;
  offset = (uint8_t *)strstr((char *)command, partial_commands[7].cmd);
  if (offset == NULL) {
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE - strsz,
                     "Error trying to find a matching ID\n");
  } else {
    int ofs = (int)(offset - command) + strlen(partial_commands[7].cmd);
    if (strlen((char *)command) > ofs) {
      service_id = atoi((char *)(command + ofs));
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                       "Enabling debugging of service id %u \n", service_id);
      enable_service_debugging(service_id);
    }
  }
  add_message_to_queue(reply, strsz);
  free(reply);
  reply = NULL;
}

void set_new_signal_tracking_mode(uint8_t *command) {
  int strsz = 0;
  uint8_t *offset;
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  uint8_t mode = 0;
  offset = (uint8_t *)strstr((char *)command, partial_commands[8].cmd);
  if (offset == NULL) {
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE - strsz,
                     "Error processing the command\n");
  } else {
    int ofs = (int)(offset - command) + strlen(partial_commands[8].cmd);
    if (strlen((char *)command) > ofs) {
      mode = atoi((char *)(command + ofs));
      if (mode < 4) {
        strsz =
            snprintf((char *)reply, MAX_MESSAGE_SIZE,
                     "Signal tracking mode: %u (%s)\n", mode, (command + ofs));
        set_signal_tracking_mode(mode);
      } else {
        strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                         "Available modes for signal tracking:\n0:Standalone learn\n1:Standalone enforce\n2:OpenCellID learn\n3:OpenCellID enforce\nYour selection: %u\n",
                         mode);
      }
    }
  }
  add_message_to_queue(reply, strsz);
  free(reply);
  reply = NULL;
}

void set_custom_user_name(uint8_t *command) {
  int strsz = 0;
  uint8_t *offset;
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  char name[32];
  offset = (uint8_t *)strstr((char *)command, partial_commands[1].cmd);
  if (offset == NULL) {
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE - strsz,
                     "Error setting your new name\n");
  } else {
    int ofs = (int)(offset - command) + strlen(partial_commands[1].cmd);
    if (strlen((char *)command) > ofs) {
      snprintf(name, 32, "%s", (char *)command + ofs);
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                       "I will call you %s from now on\n", name);
      set_user_name(name);
      get_names();
    }
  }
  add_message_to_queue(reply, strsz);
  free(reply);
  reply = NULL;
}

void delete_task(uint8_t *command) {
  int strsz = 0;
  uint8_t *offset;
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  int taskID;
  char command_args[64];
  offset = (uint8_t *)strstr((char *)command, partial_commands[5].cmd);
  if (offset == NULL) {
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE - strsz,
                     "Command mismatch!\n");
  } else {
    int ofs = (int)(offset - command) + strlen(partial_commands[5].cmd);
    if (strlen((char *)command) > ofs) {
      snprintf(command_args, 64, "%s", (char *)command + ofs);
      taskID = atoi((char *)command + ofs);
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "Removed task %i \n",
                       taskID);
      if (remove_task(taskID) < 0) {
        strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                         "Remove task %i failed: It doesn't exist!\n", taskID);
      }
    }
  }
  add_message_to_queue(reply, strsz);
  free(reply);
  reply = NULL;
}

void debug_gsm7_cb_message(uint8_t *command) {
  int strsz = 0;
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  uint8_t example_pkt1[] = {
      0x01, 0x73, 0x00, 0x80, 0x05, 0x01, 0x04, 0x02, 0x00, 0x01, 0x00, 0x67,
      0x00, 0x11, 0x60, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x58, 0x00,
      0x40, 0x00, 0x11, 0x1c, 0x00, 0x16, 0xc4, 0x64, 0x71, 0x0a, 0x4a, 0x4e,
      0xa9, 0xa0, 0x62, 0xd2, 0x59, 0x04, 0x15, 0xa5, 0x50, 0xe9, 0x53, 0x58,
      0x75, 0x1e, 0x15, 0xc4, 0xb4, 0x0b, 0x24, 0x93, 0xb9, 0x62, 0x31, 0x97,
      0x0c, 0x26, 0x93, 0x81, 0x5a, 0xa0, 0x98, 0x4d, 0x47, 0x9b, 0x81, 0xaa,
      0x68, 0x39, 0xa8, 0x05, 0x22, 0x86, 0xe7, 0x20, 0xa1, 0x70, 0x09, 0x2a,
      0xcb, 0xe1, 0xf2, 0xb7, 0x98, 0x0e, 0x22, 0x87, 0xe7, 0x20, 0x77, 0xb9,
      0x5e, 0x06, 0x21, 0xc3, 0x6e, 0x72, 0xbe, 0x75, 0x0d, 0xcb, 0xdd, 0xad,
      0x69, 0x7e, 0x4e, 0x07, 0x16, 0x01, 0x00, 0x00};
  uint8_t example_pkt2[] = {
      0x01, 0x73, 0x00, 0x80, 0x05, 0x01, 0x04, 0x03, 0x00, 0x01, 0x00, 0x67,
      0x00, 0x11, 0x60, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x58, 0x00,
      0x40, 0x00, 0x11, 0x1c, 0x00, 0x26, 0xe5, 0x36, 0x28, 0xed, 0x06, 0x25,
      0xd1, 0xf2, 0xb2, 0x1c, 0x24, 0x2d, 0x9f, 0xd3, 0x6f, 0xb7, 0x0b, 0x54,
      0x9c, 0x83, 0xc4, 0xe5, 0x39, 0xbd, 0x8c, 0xa6, 0x83, 0xd6, 0xe5, 0xb4,
      0xbb, 0x0c, 0x3a, 0x96, 0xcd, 0x61, 0xb4, 0xdc, 0x05, 0x12, 0xa6, 0xe9,
      0xf4, 0x32, 0x28, 0x7d, 0x76, 0xbf, 0xe5, 0xe9, 0xb2, 0xbc, 0xec, 0x06,
      0x4d, 0xd3, 0x65, 0x10, 0x39, 0x5d, 0x06, 0x39, 0xc3, 0x63, 0xb4, 0x3c,
      0x3d, 0x46, 0xd3, 0x5d, 0xa0, 0x66, 0x19, 0x2d, 0x07, 0x25, 0xdd, 0xe6,
      0xf7, 0x1c, 0x64, 0x06, 0x16, 0x01, 0x00, 0x00};
  uint8_t example_pkt3[] = {
      0x01, 0x73, 0x00, 0x80, 0x05, 0x01, 0x04, 0x04, 0x00, 0x01, 0x00, 0x67,
      0x00, 0x11, 0x60, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x58, 0x00,
      0x40, 0x00, 0x11, 0x1c, 0x00, 0x36, 0x69, 0x37, 0xb9, 0xec, 0x06, 0x4d,
      0xd3, 0x65, 0x50, 0xdd, 0x4d, 0x2f, 0xcb, 0x41, 0x68, 0x3a, 0x1d, 0xae,
      0x7b, 0xbd, 0xee, 0xf7, 0xbb, 0x4b, 0x2c, 0x5e, 0xbb, 0xc4, 0x75, 0x37,
      0xd9, 0x45, 0x2e, 0xbf, 0xc6, 0x65, 0x36, 0x5b, 0x2c, 0x7f, 0x87, 0xc9,
      0xe3, 0xf0, 0x9c, 0x0e, 0x12, 0x82, 0xa6, 0x6f, 0x36, 0x9b, 0x5e, 0x76,
      0x83, 0xa6, 0xe9, 0x32, 0x88, 0x9c, 0x2e, 0xcf, 0xcb, 0x20, 0x67, 0x78,
      0x8c, 0x96, 0xa7, 0xc7, 0x68, 0x3a, 0xa8, 0x2c, 0x47, 0x87, 0xd9, 0xf4,
      0xb2, 0x9b, 0x05, 0x02, 0x16, 0x01, 0x00, 0x00};
  uint8_t example_pkt4[] = {
      0x01, 0x73, 0x00, 0x80, 0x05, 0x01, 0x04, 0x05, 0x00, 0x01, 0x00, 0x67,
      0x00, 0x11, 0x60, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x58, 0x00,
      0x40, 0x00, 0x11, 0x1c, 0x00, 0x46, 0xf3, 0x37, 0xc8, 0x5e, 0x96, 0x9b,
      0xfd, 0x67, 0x3a, 0x28, 0x89, 0x96, 0x83, 0xa6, 0xed, 0xb0, 0x9c, 0x0e,
      0x47, 0xbf, 0xdd, 0x65, 0xd0, 0xf9, 0x6c, 0x76, 0x81, 0xdc, 0xe9, 0x31,
      0x9a, 0x0e, 0xf2, 0x8b, 0xcb, 0x72, 0x10, 0xb9, 0xec, 0x06, 0x85, 0xd7,
      0xf4, 0x7a, 0x99, 0xcd, 0x2e, 0xbb, 0x41, 0xd3, 0xb7, 0x99, 0x7e, 0x0f,
      0xcb, 0xcb, 0x73, 0x7a, 0xd8, 0x4d, 0x76, 0x81, 0xae, 0x69, 0x39, 0xa8,
      0xdc, 0x86, 0x9b, 0xcb, 0x68, 0x76, 0xd9, 0x0d, 0x22, 0x87, 0xd1, 0x65,
      0x39, 0x0b, 0x94, 0x04, 0x16, 0x01, 0x00, 0x00};
  uint8_t example_pkt5[] = {
      0x01, 0x73, 0x00, 0x80, 0x05, 0x01, 0x04, 0x06, 0x00, 0x01, 0x00, 0x67,
      0x00, 0x11, 0x60, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x58, 0x00,
      0x40, 0x00, 0x11, 0x1c, 0x00, 0x56, 0x68, 0x39, 0xa8, 0xe8, 0x26, 0x9f,
      0xcb, 0xf2, 0x3d, 0x1d, 0x14, 0xae, 0x9b, 0x41, 0xe4, 0xb2, 0x1b, 0x14,
      0x5e, 0xd3, 0xeb, 0x65, 0x36, 0xbb, 0xec, 0x06, 0x4d, 0xdf, 0x66, 0xfa,
      0x3d, 0x2c, 0x2f, 0xcf, 0xe9, 0x61, 0x37, 0x19, 0xa4, 0xaf, 0x83, 0xe0,
      0x72, 0xbf, 0xb9, 0xec, 0x76, 0x81, 0x84, 0xe5, 0x34, 0xc8, 0x28, 0x0f,
      0x9f, 0xcb, 0x6e, 0x10, 0x3a, 0x5d, 0x96, 0xeb, 0xeb, 0xa0, 0x7b, 0xd9,
      0x4d, 0x2e, 0xbb, 0x41, 0xd3, 0x74, 0x19, 0x34, 0x4f, 0x8f, 0xd1, 0x20,
      0x71, 0x9a, 0x4e, 0x07, 0x16, 0x01, 0x00, 0x00};
  uint8_t example_pkt6[] = {
      0x01, 0x3a, 0x00, 0x80, 0x05, 0x01, 0x04, 0x07, 0x00, 0x01, 0x00, 0x2e,
      0x00, 0x11, 0x27, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x1f, 0x00,
      0x40, 0x00, 0x11, 0x1c, 0x00, 0x66, 0x65, 0x50, 0xd8, 0x0d, 0x4a, 0xa2,
      0xe5, 0x65, 0x37, 0xe8, 0x58, 0x96, 0xef, 0xe9, 0x65, 0x74, 0x59, 0x3e,
      0xa7, 0x97, 0xd9, 0xec, 0xb2, 0xdc, 0xd5, 0x16, 0x01, 0x00, 0x00};
  strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "Dummy CB Message parse\n");
  add_message_to_queue(reply, strsz);
  check_cb_message(example_pkt1, sizeof(example_pkt1), 0, 0);
  check_cb_message(example_pkt2, sizeof(example_pkt2), 0, 0);
  check_cb_message(example_pkt3, sizeof(example_pkt3), 0, 0);
  check_cb_message(example_pkt4, sizeof(example_pkt4), 0, 0);
  check_cb_message(example_pkt5, sizeof(example_pkt5), 0, 0);
  check_cb_message(example_pkt6, sizeof(example_pkt6), 0, 0);
  free(reply);
  reply = NULL;
}

void debug_ucs2_cb_message(uint8_t *command) {
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  uint8_t pkt1[] = {
      0x01, 0x73, 0x00, 0x80, 0x05, 0x01, 0x04, 0x03, 0x00, 0x01, 0x00, 0x67,
      0x00, 0x11, 0x60, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x58, 0x00,
      0x63, 0x40, 0x00, 0x32, 0x59, 0x14, 0x00, 0x20, 0x04, 0x1f, 0x04, 0x40,
      0x04, 0x3e, 0x04, 0x42, 0x04, 0x4f, 0x04, 0x33, 0x04, 0x3e, 0x04, 0x3c,
      0x00, 0x20, 0x04, 0x34, 0x04, 0x3d, 0x04, 0x4f, 0x00, 0x20, 0x04, 0x54,
      0x00, 0x20, 0x04, 0x32, 0x04, 0x38, 0x04, 0x41, 0x04, 0x3e, 0x04, 0x3a,
      0x04, 0x30, 0x00, 0x20, 0x04, 0x56, 0x04, 0x3c, 0x04, 0x3e, 0x04, 0x32,
      0x04, 0x56, 0x04, 0x40, 0x04, 0x3d, 0x04, 0x56, 0x04, 0x41, 0x04, 0x42,
      0x04, 0x4c, 0x00, 0x20, 0x04, 0x40, 0x04, 0x30, 0x04, 0x3a, 0x04, 0x35,
      0x04, 0x42, 0x04, 0x3d, 0x16, 0x01, 0x00, 0x00};
  uint8_t pkt2[] = {
      0x01, 0x73, 0x00, 0x80, 0x05, 0x01, 0x04, 0x04, 0x00, 0x01, 0x00, 0x67,
      0x00, 0x11, 0x60, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x58, 0x00,
      0x63, 0x40, 0x00, 0x32, 0x59, 0x24, 0x04, 0x38, 0x04, 0x45, 0x00, 0x20,
      0x04, 0x43, 0x04, 0x34, 0x04, 0x30, 0x04, 0x40, 0x04, 0x56, 0x04, 0x32,
      0x00, 0x20, 0x04, 0x3f, 0x04, 0x3e, 0x00, 0x20, 0x04, 0x42, 0x04, 0x35,
      0x04, 0x40, 0x04, 0x38, 0x04, 0x42, 0x04, 0x3e, 0x04, 0x40, 0x04, 0x56,
      0x04, 0x57, 0x00, 0x20, 0x04, 0x23, 0x04, 0x3a, 0x04, 0x40, 0x04, 0x30,
      0x04, 0x57, 0x04, 0x3d, 0x04, 0x38, 0x00, 0x2e, 0x00, 0x20, 0x04, 0x17,
      0x04, 0x30, 0x04, 0x3b, 0x04, 0x38, 0x04, 0x48, 0x04, 0x30, 0x04, 0x39,
      0x04, 0x42, 0x04, 0x35, 0x16, 0x01, 0x00, 0x00};
  uint8_t pkt3[] = {
      0x01, 0x73, 0x00, 0x80, 0x05, 0x01, 0x04, 0x05, 0x00, 0x01, 0x00, 0x67,
      0x00, 0x11, 0x60, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x58, 0x00,
      0x63, 0x40, 0x00, 0x32, 0x59, 0x34, 0x04, 0x41, 0x04, 0x4c, 0x00, 0x20,
      0x04, 0x32, 0x00, 0x20, 0x04, 0x43, 0x04, 0x3a, 0x04, 0x40, 0x04, 0x38,
      0x04, 0x42, 0x04, 0x42, 0x04, 0x4f, 0x04, 0x45, 0x00, 0x20, 0x04, 0x37,
      0x04, 0x30, 0x04, 0x40, 0x04, 0x30, 0x04, 0x34, 0x04, 0x38, 0x00, 0x20,
      0x04, 0x12, 0x04, 0x30, 0x04, 0x48, 0x04, 0x3e, 0x04, 0x57, 0x00, 0x20,
      0x04, 0x31, 0x04, 0x35, 0x04, 0x37, 0x04, 0x3f, 0x04, 0x35, 0x04, 0x3a,
      0x04, 0x38, 0x00, 0x2e, 0x00, 0x20, 0x04, 0x1d, 0x04, 0x35, 0x00, 0x20,
      0x04, 0x3d, 0x04, 0x35, 0x16, 0x01, 0x00, 0x00};
  uint8_t pkt4[] = {
      0x01, 0x72, 0x00, 0x80, 0x05, 0x01, 0x04, 0x06, 0x00, 0x01, 0x00, 0x66,
      0x00, 0x11, 0x5f, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x07, 0x57, 0x00,
      0x63, 0x40, 0x00, 0x32, 0x59, 0x44, 0x04, 0x45, 0x04, 0x42, 0x04, 0x43,
      0x04, 0x39, 0x04, 0x42, 0x04, 0x35, 0x00, 0x20, 0x04, 0x41, 0x04, 0x38,
      0x04, 0x33, 0x04, 0x3d, 0x04, 0x30, 0x04, 0x3b, 0x04, 0x30, 0x04, 0x3c,
      0x04, 0x38, 0x00, 0x20, 0x00, 0x22, 0x04, 0x1f, 0x04, 0x3e, 0x04, 0x32,
      0x04, 0x56, 0x04, 0x42, 0x04, 0x40, 0x04, 0x4f, 0x04, 0x3d, 0x04, 0x30,
      0x00, 0x20, 0x04, 0x42, 0x04, 0x40, 0x04, 0x38, 0x04, 0x32, 0x04, 0x3e,
      0x04, 0x33, 0x04, 0x30, 0x00, 0x22, 0x00, 0x2e, 0x00, 0x0d, 0x00, 0x0d,
      0x00, 0x0d, 0x00, 0x16, 0x01, 0x00, 0x00};

  check_cb_message(pkt1, sizeof(pkt1), 0, 0);
  check_cb_message(pkt2, sizeof(pkt2), 0, 0);
  check_cb_message(pkt3, sizeof(pkt3), 0, 0);
  check_cb_message(pkt4, sizeof(pkt4), 0, 0);

  free(reply);
  reply = NULL;
}

void dump_signal_report() {
  int strsz = 0;
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  struct nas_report report = get_current_cell_report();
  if (report.mcc == 0) {
    strsz = snprintf(
        (char *)reply, MAX_MESSAGE_SIZE,
        "Serving cell report has not been retrieved yet or is invalid\n");
    add_message_to_queue(reply, strsz);
    free(reply);
    reply = NULL;
    return;
  }

  strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "MCC: %i MNC: %i\n",
                   report.mcc, report.mnc);
  switch (report.type_of_service) {
  case OCID_RADIO_GSM:
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "Current Network: GSM\n");
    break;
  case OCID_RADIO_UMTS:
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "Current Network: UMTS\n");
    break;
  case OCID_RADIO_LTE:
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "Current Network: LTE\n");
    break;
  case OCID_RADIO_NR:
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "Current Network: 5G\n");
    break;
  }
  strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                    "Cell: %.8x\n", report.cell_id);
  strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                    "Location Area Code: %.4x\n", report.lac);
  strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                    "(E/)ARFCN %u\n", report.arfcn);
  strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                    "SRX Level (min) %i dBm\n", report.srx_level_min);
  strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                    "SRX Level (max) %i dBm\n", report.srx_level_max);
  strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                    "RX Level (min) %u\n", report.rx_level_min);
  strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                    "RX Level (max) %u\n", report.rx_level_max);
  strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                    "PSC (umts) %.4x\n", report.psc);
  strsz +=
      snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz, "Verified? %s",
               report.opencellid_verified == 1 ? "Yes" : "No");
  add_message_to_queue(reply, strsz);
}

void *delayed_shutdown() {
  sleep(5);
  reboot(0x4321fedc);
  return NULL;
}

void *delayed_reboot() {
  sleep(5);
  reboot(0x01234567);
  return NULL;
}

void *schedule_call(void *cmd) {
  int strsz = 0;
  uint8_t *offset;
  uint8_t *command = (uint8_t *)cmd;
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  logger(MSG_WARN, "SCH: %s -> %s \n", cmd, command);

  int delaysec;
  char tmpbuf[10];
  char *secoffset;
  offset = (uint8_t *)strstr((char *)command, partial_commands[2].cmd);
  if (offset == NULL) {
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE - strsz,
                     "Error reading the command\n");
    add_message_to_queue(reply, strsz);
  } else {
    int ofs = (int)(offset - command) + strlen(partial_commands[2].cmd);
    snprintf(tmpbuf, 10, "%s", (char *)command + ofs);
    delaysec = strtol(tmpbuf, &secoffset, 10);
    if (delaysec > 0) {
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                       "I will call you back in %i seconds\n", delaysec);
      add_message_to_queue(reply, strsz);
      sleep(delaysec);
      logger(MSG_INFO, "Calling you now!\n");
      set_pending_call_flag(true);
    } else {
      strsz = snprintf(
          (char *)reply, MAX_MESSAGE_SIZE,
          "Please tell me in how many seconds you want me to call you, %s\n",
          cmd_runtime.user_name);
      add_message_to_queue(reply, strsz);
    }
  }
  free(reply);
  reply = NULL;
  command = NULL;
  return NULL;
}

void render_gsm_signal_data() {
  int strsz = 0;
  char *network_types[] = {"Unknown", "CDMA",  "EVDO",  "AMPS", "GSM",
                           "UMTS",    "Error", "Error", "LTE"};

  uint8_t *reply = calloc(256, sizeof(unsigned char));
  strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                    "Network type: ");
  if (get_network_type() >= 0x00 && get_network_type() <= 0x08) {
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz, "%s\n",
                      network_types[get_network_type()]);
  } else {
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "Unknown (0x%.2x)\n", get_network_type());
  }
  strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                    "Signal strength: -%i dBm\n", (int)get_signal_strength());
  add_message_to_queue(reply, strsz);
  free(reply);
  reply = NULL;
}

/* Syntax:
 * Remind me in X[hours]:[optional minutes] [optional description spoken by tts]
 * Remind me at X[hours]:[optional minutes] [optional description spoken by tts]
 * Examples:
 *  remind me at 5 do some stuff
 *  remind me at 5:05 do some stuff
 *  remind me at 15:05 do some stuff
 *  remind me in 1 do some stuff
 *  remind me in 99 do some stuff
 *
 */
void schedule_reminder(uint8_t *command) {
  uint8_t *offset_command;
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  int strsz = 0;
  char temp_str[160];
  char reminder_text[160] = {0};
  char current_word[160] = {0};
  int markers[128] = {0};
  int phrase_size = 1;
  int start = 0;
  int end = 0;
  char sep[] = " ";
  struct task_p scheduler_task;
  scheduler_task.time.mode = SCHED_MODE_TIME_AT; // 0 at, 1 in
  /* Initial command check */
  offset_command = (uint8_t *)strstr((char *)command, partial_commands[3].cmd);
  if (offset_command == NULL) {
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE - strsz,
                     "Command mismatch!\n");
    add_message_to_queue(reply, strsz);
    free(reply);
    reply = NULL;
    return;
  }

  strcpy(temp_str, (char *)command);
  int init_size = strlen(temp_str);
  char *ptr = strtok(temp_str, sep);
  while (ptr != NULL) {
    logger(MSG_INFO, "%s: '%s'\n", __func__, ptr);
    ptr = strtok(NULL, sep);
  }

  for (int i = 0; i < init_size; i++) {
    if (temp_str[i] == 0) {
      markers[phrase_size] = i;
      phrase_size++;
    }
  }

  logger(MSG_INFO, "%s: Total words in command: %i\n", __func__, phrase_size);
  for (int i = 0; i < phrase_size; i++) {
    start = markers[i];
    if (i + 1 >= phrase_size) {
      end = init_size;
    } else {
      end = markers[i + 1];
    }
    // So we don't pick the null byte separating the word
    if (i > 0) {
      start++;
    }
    // Copy this token
    memset(current_word, 0, 160);
    memcpy(current_word, temp_str + start, (end - start));
    // current_word[strlen(current_word)] = '\0';

    /*
     * remind me [at|in] 00[:00] [Text chunk]
     *  ^      ^   ^^^^  ^^  ^^     ^^^^^
     *  0      1     2   ?3 3OPT     4
     */

    logger(MSG_INFO, "Current word: %s\n", current_word);
    switch (i) {
    case 0:
      if (strstr(current_word, "remind") == NULL) {
        logger(MSG_ERROR, "%s: First word is not remind, %s \n", __func__,
               current_word);
        strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                         "First word in command is wrong\n");
        add_message_to_queue(reply, strsz);
        free(reply);
        reply = NULL;
        return;
      } else {
        logger(MSG_INFO, "%s: Word ok: %s\n", __func__, current_word);
      }
      break;
    case 1:
      if (strstr(current_word, "me") == NULL) {
        logger(MSG_ERROR, "%s: Second word is not me, %s \n", __func__,
               current_word);
        strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                         "Second word in command is wrong\n");
        add_message_to_queue(reply, strsz);
        free(reply);
        reply = NULL;
        return;
      }
      break;
    case 2:
      if (strstr(current_word, "at") != NULL) {
        logger(MSG_INFO, "%s: Remind you AT \n", __func__);
      } else if (strstr((char *)command, "in") != NULL) {
        logger(MSG_INFO, "%s: Remind you IN \n", __func__);
        scheduler_task.time.mode = SCHED_MODE_TIME_COUNTDOWN;
      } else {
        logger(MSG_ERROR,
               "%s: Don't know when you want me to remind you: %s \n", __func__,
               current_word);
        strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                         "Do you want me to remind you *at* a specific time or "
                         "*in* some time from now\n");
        add_message_to_queue(reply, strsz);
        free(reply);
        reply = NULL;
        return;
      }
      break;
    case 3:
      if (strchr(current_word, ':') != NULL) {
        logger(MSG_INFO, "%s: Time has minutes", __func__);
        char *offset = strchr((char *)current_word, ':');
        scheduler_task.time.hh = get_int_from_str(current_word, 0);
        scheduler_task.time.mm = get_int_from_str(offset, 1);

        if (scheduler_task.time.mode == SCHED_MODE_TIME_AT) {
          if (scheduler_task.time.hh > 23 && scheduler_task.time.mm > 59) {
            strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                             "Ha ha, very funny. Please give me a time value I "
                             "can work with if you want me to do anything \n");
          } else if (scheduler_task.time.hh > 23 ||
                     scheduler_task.time.mm > 59) {
            if (scheduler_task.time.hh > 23)
              strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                               "I might not be very smart, but I don't think "
                               "there's enough hours in a day \n");
            if (scheduler_task.time.mm > 59)
              strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                               "I might not be very smart, but I don't think "
                               "there's enough minutes in an hour \n");
            add_message_to_queue(reply, strsz);
            free(reply);
            reply = NULL;
            return;
          }
        }
      } else {
        logger(MSG_INFO, "%s:  Only hours have been specified\n", __func__);
        if (strlen(current_word) > 2) {
          logger(MSG_WARN, "%s: How long do you want me to wait? %s\n",
                 __func__, current_word);
          strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                           "I can't wait for a task that long... %s\n",
                           current_word);
          add_message_to_queue(reply, strsz);
          free(reply);
          reply = NULL;
          return;
        } else {
          scheduler_task.time.hh = atoi(current_word);
          scheduler_task.time.mm = 0;
          if (scheduler_task.time.mode)
            logger(MSG_WARN, "%s: Waiting for %i hours\n", __func__,
                   scheduler_task.time.hh);
          else
            logger(MSG_WARN, "%s: Waiting until %i to call you back\n",
                   __func__, scheduler_task.time.hh);
        }
      }
      break;
    case 4:
      memcpy(reminder_text, command + start, (strlen((char *)command) - start));
      logger(MSG_INFO, "%s: Reminder has the following text: %s\n", __func__,
             reminder_text);
      if (scheduler_task.time.mode == SCHED_MODE_TIME_AT) {
        strsz = snprintf(
            (char *)reply, MAX_MESSAGE_SIZE, " Remind you at %.2i:%.2i.\n%s\n",
            scheduler_task.time.hh, scheduler_task.time.mm, reminder_text);
      } else if (scheduler_task.time.mode == SCHED_MODE_TIME_COUNTDOWN) {
        strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                         " Remind you in %i hours and %i minutes\n%s\n",
                         scheduler_task.time.hh, scheduler_task.time.mm,
                         reminder_text);
      }
      break;
    }
  }
  scheduler_task.type = TASK_TYPE_CALL;
  scheduler_task.status = STATUS_PENDING;
  scheduler_task.param = 0;
  scheduler_task.time.mode = scheduler_task.time.mode;
  strncpy(scheduler_task.arguments, reminder_text, MAX_MESSAGE_SIZE);
  if (add_task(scheduler_task) < 0) {
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                     " Can't add reminder, my task queue is full!\n");
  }
  add_message_to_queue(reply, strsz);
  free(reply);
  reply = NULL;
}

/* Syntax:
 * Wake me up in X[hours]:[optional minutes]
 * Wake me up at X[hours]:[optional minutes]
 * Examples:
 *  wake me up at 5
 *  wake me up at 5:05
 *  wake me up at 15:05
 *  wake me up in 1
 *  wake me up in 99
 *
 */
void schedule_wakeup(uint8_t *command) {
  uint8_t *offset_command;
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  int strsz = 0;
  char temp_str[160];
  char current_word[160] = {0};
  int markers[128] = {0};
  int phrase_size = 1;
  int start = 0;
  int end = 0;
  char sep[] = " ";
  struct task_p scheduler_task;
  scheduler_task.time.mode = SCHED_MODE_TIME_AT; // 0 at, 1 in
  /* Initial command check */
  offset_command = (uint8_t *)strstr((char *)command, partial_commands[4].cmd);
  if (offset_command == NULL) {
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE - strsz,
                     "Command mismatch!\n");
    add_message_to_queue(reply, strsz);
    free(reply);
    reply = NULL;
    return;
  }

  strcpy(temp_str, (char *)command);
  int init_size = strlen(temp_str);
  char *ptr = strtok(temp_str, sep);
  while (ptr != NULL) {
    logger(MSG_INFO, "%s: '%s'\n", __func__, ptr);
    ptr = strtok(NULL, sep);
  }

  for (int i = 0; i < init_size; i++) {
    if (temp_str[i] == 0) {
      markers[phrase_size] = i;
      phrase_size++;
    }
  }

  logger(MSG_INFO, "%s: Total words in command: %i\n", __func__, phrase_size);
  for (int i = 0; i < phrase_size; i++) {
    start = markers[i];
    if (i + 1 >= phrase_size) {
      end = init_size;
    } else {
      end = markers[i + 1];
    }
    // So we don't pick the null byte separating the word
    if (i > 0) {
      start++;
    }
    // Copy this token
    memset(current_word, 0, 160);
    memcpy(current_word, temp_str + start, (end - start));
    // current_word[strlen(current_word)] = '\0';

    /*
     * wake me up [at|in] 00[:00]
     *  ^    ^  ^   ^^^^  ^^  ^^
     *  0    1  2     3   ?4 3OPT
     */

    logger(MSG_INFO, "Current word: %s\n", current_word);
    switch (i) {
    case 0:
      if (strstr(current_word, "wake") == NULL) {
        logger(MSG_ERROR, "%s: First word is not wake, %s \n", __func__,
               current_word);
        strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                         "First word in command is wrong\n");
        add_message_to_queue(reply, strsz);
        free(reply);
        reply = NULL;
        return;
      } else {
        logger(MSG_INFO, "%s: Word ok: %s\n", __func__, current_word);
      }
      break;
    case 1:
      if (strstr(current_word, "me") == NULL) {
        logger(MSG_ERROR, "%s: Second word is not me, %s \n", __func__,
               current_word);
        strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                         "Second word in command is wrong\n");
        add_message_to_queue(reply, strsz);
        free(reply);
        reply = NULL;
        return;
      }
      break;
    case 2:
      if (strstr(current_word, "up") == NULL) {
        logger(MSG_ERROR, "%s: Third word is not up, %s \n", __func__,
               current_word);
        strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                         "Second word in command is wrong\n");
        add_message_to_queue(reply, strsz);
        free(reply);
        reply = NULL;
        return;
      }
      break;
    case 3:
      if (strstr(current_word, "at") != NULL) {
        logger(MSG_INFO, "%s: Wake you AT \n", __func__);
      } else if (strstr((char *)command, "in") != NULL) {
        logger(MSG_INFO, "%s: Wake you IN \n", __func__);
        scheduler_task.time.mode = SCHED_MODE_TIME_COUNTDOWN;
      } else {
        logger(MSG_ERROR,
               "%s: Don't know when you want me to wake you up: %s \n",
               __func__, current_word);
        strsz =
            snprintf((char *)reply, MAX_MESSAGE_SIZE,
                     "Do you want me to wake you up *at* a specific time or "
                     "*in* some time from now?\n");
        add_message_to_queue(reply, strsz);
        free(reply);
        reply = NULL;
        return;
      }
      break;
    case 4:
      if (strchr(current_word, ':') != NULL) {
        logger(MSG_INFO, "%s: Time has minutes", __func__);
        char *offset = strchr((char *)current_word, ':');
        scheduler_task.time.hh = get_int_from_str(current_word, 0);
        scheduler_task.time.mm = get_int_from_str(offset, 1);

        if (offset - ((char *)current_word) > 2 || strlen(offset) > 3) {
          logger(MSG_WARN, "%s: How long do you want me to wait? %s\n",
                 __func__, current_word);
          if (offset - ((char *)current_word) > 2)
            strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                             "I can't wait for a task that long... %s\n",
                             current_word);
          if (strlen(offset) > 3)
            strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                             "Add another hour rather than putting 100 or more "
                             "minutes... %s\n",
                             current_word);
          add_message_to_queue(reply, strsz);
          free(reply);
          reply = NULL;
          return;
        } else if (scheduler_task.time.mode == SCHED_MODE_TIME_AT) {
          if (scheduler_task.time.hh > 23 && scheduler_task.time.mm > 59) {
            strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                             "Ha ha, very funny. Please give me a time value I "
                             "can work with if you want me to do anything \n");
          } else if (scheduler_task.time.hh > 23 ||
                     scheduler_task.time.mm > 59) {
            if (scheduler_task.time.hh > 23)
              strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                               "I might not be very smart, but I don't think "
                               "there's enough hours in a day \n");
            if (scheduler_task.time.mm > 59)
              strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                               "I might not be very smart, but I don't think "
                               "there's enough minutes in an hour \n");
            add_message_to_queue(reply, strsz);
            free(reply);
            reply = NULL;
            return;
          } else {
            strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                             "Waiting until %i:%02i to call you back\n",
                             scheduler_task.time.hh, scheduler_task.time.mm);
          }
        } else {
          strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                           "Waiting for %i hours and %i minutes to call you "
                           "back\n",
                           scheduler_task.time.hh, scheduler_task.time.mm);
        }
      } else {
        logger(MSG_INFO, "%s:  Only hours have been specified\n", __func__);
        if (strlen(current_word) > 2) {
          logger(MSG_WARN, "%s: How long do you want me to wait? %s\n",
                 __func__, current_word);
          strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                           "I can't wait for a task that long... %s\n",
                           current_word);
          add_message_to_queue(reply, strsz);
          free(reply);
          reply = NULL;
          return;
        } else {
          scheduler_task.time.hh = atoi(current_word);
          scheduler_task.time.mm = 0;
          if (scheduler_task.time.mode) {
            logger(MSG_WARN, "%s: Waiting for %i hours\n", __func__,
                   scheduler_task.time.hh);
            strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                             "Waiting for %i hours to call you back\n",
                             scheduler_task.time.hh);
          } else {
            if (scheduler_task.time.hh > 24) {
              strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                               "I might not be very smart, but I don't think "
                               "there's enough hours in a day \n");
              add_message_to_queue(reply, strsz);
              free(reply);
              reply = NULL;
              return;
            } else {
              logger(MSG_WARN, "%s: Waiting until %i to call you back\n",
                     __func__, scheduler_task.time.hh);
              strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                               "Waiting until %i to call you back\n",
                               scheduler_task.time.hh);
            }
          }
        }
      }
      break;
    }
  }
  scheduler_task.type = TASK_TYPE_CALL;
  scheduler_task.status = STATUS_PENDING;
  scheduler_task.param = 0;
  scheduler_task.time.mode = scheduler_task.time.mode;
  snprintf(scheduler_task.arguments, MAX_MESSAGE_SIZE,
           "It's time to wakeup, %s", cmd_runtime.user_name);
  if (add_task(scheduler_task) < 0) {
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                     " Can't schedule wakeup, my task queue is full!\n");
  }
  add_message_to_queue(reply, strsz);
  free(reply);
  reply = NULL;
}

void suspend_call_notifications(uint8_t *command) {
  uint8_t *offset_command;
  uint8_t *reply = calloc(256, sizeof(unsigned char));
  int strsz = 0;
  char temp_str[160];
  char current_word[160] = {0};
  int markers[128] = {0};
  int phrase_size = 1;
  int start = 0;
  int end = 0;
  char sep[] = " ";
  struct task_p scheduler_task;
  scheduler_task.time.mode = SCHED_MODE_TIME_COUNTDOWN; // 0 at, 1 in
  /* Initial command check */
  offset_command = (uint8_t *)strstr((char *)command, partial_commands[6].cmd);
  if (offset_command == NULL) {
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE - strsz,
                     "Command mismatch!\n");
    add_message_to_queue(reply, strsz);
    free(reply);
    reply = NULL;
    return;
  }

  strcpy(temp_str, (char *)command);
  int init_size = strlen(temp_str);
  char *ptr = strtok(temp_str, sep);
  while (ptr != NULL) {
    logger(MSG_INFO, "%s: '%s'\n", __func__, ptr);
    ptr = strtok(NULL, sep);
  }

  for (int i = 0; i < init_size; i++) {
    if (temp_str[i] == 0) {
      markers[phrase_size] = i;
      phrase_size++;
    }
  }

  logger(MSG_DEBUG, "%s: Total words in command: %i\n", __func__, phrase_size);
  for (int i = 0; i < phrase_size; i++) {
    start = markers[i];
    if (i + 1 >= phrase_size) {
      end = init_size;
    } else {
      end = markers[i + 1];
    }
    // So we don't pick the null byte separating the word
    if (i > 0) {
      start++;
    }
    // Copy this token
    memset(current_word, 0, 160);
    memcpy(current_word, temp_str + start, (end - start));
    // current_word[strlen(current_word)] = '\0';

    /*
     * leave me alone for 00[:00]
     *  ^    ^  ^      ^  ^^  ^^
     *  0    1  2      3   4  5OPT
     */

    switch (i) {
    case 0: /* Fall through */
    case 1:
    case 2:
      logger(MSG_DEBUG, "Current word in pos %i: %s\n", i, current_word);
      break;
    case 3:
      if (strchr(current_word, ':') != NULL) {
        logger(MSG_DEBUG, "%s: Time has minutes", __func__);
        char *offset = strchr((char *)current_word, ':');
        scheduler_task.time.hh = get_int_from_str(current_word, 0);
        scheduler_task.time.mm = get_int_from_str(offset, 1);

        if (offset - ((char *)current_word) > 2 || strlen(offset) > 3) {
          logger(MSG_WARN, "%s: How long do you want me to wait? %s\n",
                 __func__, current_word);
          if (offset - ((char *)current_word) > 2)
            strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                             "I can't wait for a task that long... %s\n",
                             current_word);
          if (strlen(offset) > 3)
            strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                             "Add another hour rather than putting 100 or more "
                             "minutes... %s\n",
                             current_word);
          add_message_to_queue(reply, strsz);
          free(reply);
          reply = NULL;
          return;
        } else {
          strsz =
              snprintf((char *)reply, MAX_MESSAGE_SIZE,
                       "Blocking incoming calls for %i hours and %i minutes\n",
                       scheduler_task.time.hh, scheduler_task.time.mm);
        }
      } else {
        logger(MSG_INFO, "%s:  Only hours have been specified\n", __func__);
        if (strlen(current_word) > 2) {
          logger(MSG_WARN, "%s: How long do you want me to wait? %s\n",
                 __func__, current_word);
          strsz =
              snprintf((char *)reply, MAX_MESSAGE_SIZE,
                       "I can't keep DND on _that_ long... %s\n", current_word);
          add_message_to_queue(reply, strsz);
          free(reply);
          reply = NULL;
          return;
        } else {
          scheduler_task.time.hh = atoi(current_word);
          scheduler_task.time.mm = 0;
          if (scheduler_task.time.hh > 0) {
            strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                             "Blocking incoming calls for %i hour(s)\n",
                             scheduler_task.time.hh);

          } else {
            strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                             "%i hours is not a valid time! Please specify the "
                             "number of hours\n",
                             scheduler_task.time.hh);
            add_message_to_queue(reply, strsz);
            free(reply);
            reply = NULL;
            return;
          }
        }
      }
      break;
    }
  }

  set_do_not_disturb(true);
  scheduler_task.type = TASK_TYPE_DND_CLEAR;
  scheduler_task.status = STATUS_PENDING;
  scheduler_task.param = 0;
  scheduler_task.time.mode = scheduler_task.time.mode;
  snprintf(scheduler_task.arguments, MAX_MESSAGE_SIZE,
           "Hi %s, as requested, I'm disabling DND now!",
           cmd_runtime.user_name);
  remove_all_tasks_by_type(
      TASK_TYPE_DND_CLEAR); // We clear all previous timers set
  if (add_task(scheduler_task) < 0) {
    set_do_not_disturb(false);
    strsz = snprintf(
        (char *)reply, MAX_MESSAGE_SIZE,
        "Can't schedule DND disable, so keeping it off! (too many tasks)\n");
  }
  add_message_to_queue(reply, strsz);
  free(reply);
  reply = NULL;
}

void set_cb_broadcast(bool en) {
  char *response = malloc(128 * sizeof(char));
  uint8_t *reply = calloc(160, sizeof(unsigned char));
  int strsz;
  int cmd_ret;
  if (en) {
    cmd_ret = send_at_command(CB_ENABLE_AT_CMD, sizeof(CB_ENABLE_AT_CMD),
                              response, 128);
    if (cmd_ret < 0 || (strstr(response, "OK") == NULL)) {
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                       "Failed to enable cell broadcasting messages\n");
    } else {
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                       "Enabling Cell broadcast messages\n");
    }
  } else {
    cmd_ret = send_at_command(CB_DISABLE_AT_CMD, sizeof(CB_DISABLE_AT_CMD),
                              response, 128);
    if (cmd_ret < 0 || (strstr(response, "OK") == NULL)) {
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                       "Failed to disable cell broadcasting messages\n");
    } else {
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                       "Disabling Cell broadcast messages\n");
    }
  }
  add_message_to_queue(reply, strsz);

  free(response);
  free(reply);
  response = NULL;
  reply = NULL;
}

uint8_t parse_command(uint8_t *command) {
  int ret = 0;
  uint16_t i, random;
  FILE *fp;
  int cmd_id = -1;
  int strsz = 0;
  struct pkt_stats packet_stats;
  pthread_t disposable_thread;
  char lowercase_cmd[160];
  uint8_t *tmpbuf = calloc(MAX_MESSAGE_SIZE, sizeof(unsigned char));
  uint8_t *reply = calloc(MAX_MESSAGE_SIZE, sizeof(unsigned char));
  srand(time(NULL));
  for (i = 0; i < command[i]; i++) {
    lowercase_cmd[i] = tolower(command[i]);
  }
  lowercase_cmd[strlen((char *)command)] = '\0';
  /* Static commands */
  for (i = 0; i < (sizeof(bot_commands) / sizeof(bot_commands[0])); i++) {
    if ((strcmp((char *)command, bot_commands[i].cmd) == 0) ||
        (strcmp(lowercase_cmd, bot_commands[i].cmd) == 0)) {
      logger(MSG_DEBUG, "%s: Match! %s\n", __func__, bot_commands[i].cmd);
      cmd_id = bot_commands[i].id;
    }
  }

  /* Commands with arguments */
  if (cmd_id == -1) {
    for (i = 0; i < (sizeof(partial_commands) / sizeof(partial_commands[0]));
         i++) {
      if ((strstr((char *)command, partial_commands[i].cmd) != NULL) ||
          (strstr((char *)lowercase_cmd, partial_commands[i].cmd) != NULL)) {
        cmd_id = partial_commands[i].id;
      }
    }
  }
  ret = find_cmd_history_match(cmd_id);
  if (ret >= 5) {
    logger(MSG_WARN, "You're pissing me off\n");
    random = rand() % 10;
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz, "%s\n",
                      repeated_cmd[random].answer);
  }
  switch (cmd_id) {
  case -1:
    logger(MSG_INFO, "%s: Nothing to do\n", __func__);
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "Command not found: %s\n", command);
    add_message_to_queue(reply, strsz);
    break;
  case 0:
    strsz +=
        snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz, "%s %s\n",
                 bot_commands[cmd_id].cmd_text, cmd_runtime.bot_name);
    add_message_to_queue(reply, strsz);
    break;
  case 1:
    if (get_uptime(tmpbuf) == 0) {
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "Hi %s, %s:\n %s\n", cmd_runtime.user_name,
                        bot_commands[cmd_id].cmd_text, tmpbuf);
    } else {
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "Error getting the uptime\n");
    }
    add_message_to_queue(reply, strsz);
    break;
  case 2:
    if (get_load_avg(tmpbuf) == 0) {
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "Hi %s, %s:\n %s\n", cmd_runtime.user_name,
                        bot_commands[cmd_id].cmd_text, tmpbuf);
    } else {
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "Error getting laodavg\n");
    }
    add_message_to_queue(reply, strsz);
    break;
  case 3:
    strsz +=
        snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                 "FW Ver:%s\n"
                 "ADSP Ver:%s\n"
                 "Serial:%s\n"
                 "HW Rev:%s\n"
                 "Rev:%s\n"
                 "Model:%s\n",
                 RELEASE_VER, known_adsp_fw[read_adsp_version()].fwstring,
                 dms_get_modem_modem_serial_num(), dms_get_modem_modem_hw_rev(),
                 dms_get_modem_revision(), dms_get_modem_modem_model());
    if (strsz > 159) {
      strsz = 159;
    }
    add_message_to_queue(reply, strsz);
    break;
  case 4:
    strsz +=
        snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                 "USB Suspend state: %i\n", get_transceiver_suspend_state());
    add_message_to_queue(reply, strsz);
    break;
  case 5:
    if (get_memory(tmpbuf) == 0) {
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "Memory stats:\n%s\n", tmpbuf);
    } else {
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "Error getting laodavg\n");
    }
    add_message_to_queue(reply, strsz);
    break;
  case 6:
    packet_stats = get_rmnet_stats();
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "RMNET IF stats:\nBypassed: "
                      "%i\nEmpty:%i\nDiscarded:%i\nFailed:%i\nAllowed:%i",
                      packet_stats.bypassed, packet_stats.empty,
                      packet_stats.discarded, packet_stats.failed,
                      packet_stats.allowed);
    add_message_to_queue(reply, strsz);
    break;
  case 7:
    packet_stats = get_gps_stats();
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "GPS IF stats:\nBypassed: "
                      "%i\nEmpty:%i\nDiscarded:%i\nFailed:%i\nAllowed:%"
                      "i\nQMI Location svc.: %i",
                      packet_stats.bypassed, packet_stats.empty,
                      packet_stats.discarded, packet_stats.failed,
                      packet_stats.allowed, packet_stats.other);
    add_message_to_queue(reply, strsz);
    break;
  case 8:
    strsz = 0;
    snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
             "Help: Static commands\n");
    add_message_to_queue(reply, strsz);
    strsz = 0;
    for (i = 0; i < (sizeof(bot_commands) / sizeof(bot_commands[0])); i++) {
      if (strlen(bot_commands[i].cmd) + (3 * sizeof(uint8_t)) +
              strlen(bot_commands[i].help) + strsz >
          MAX_MESSAGE_SIZE) {
        add_message_to_queue(reply, strsz);
        strsz = 0;
      }
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "%s: %s\n", bot_commands[i].cmd, bot_commands[i].help);
    }
    add_message_to_queue(reply, strsz);
    strsz = 0;
    snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
             "Help: Commands with arguments\n");
    add_message_to_queue(reply, strsz);
    strsz = 0;
    for (i = 0; i < (sizeof(partial_commands) / sizeof(partial_commands[0]));
         i++) {
      if (strlen(partial_commands[i].cmd) + (5 * sizeof(uint8_t)) +
              strlen(partial_commands[i].help) + strsz >
          MAX_MESSAGE_SIZE) {
        add_message_to_queue(reply, strsz);
        strsz = 0;
      }
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "%s x: %s\n", partial_commands[i].cmd,
                        partial_commands[i].help);
    }
    add_message_to_queue(reply, strsz);
    break;
  case 9:
    strsz += snprintf(
        (char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
        "Blocking USB suspend until reboot or until you tell me otherwise!\n");
    set_suspend_inhibit(true);
    add_message_to_queue(reply, strsz);
    break;
  case 10:
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "Allowing USB to suspend again\n");
    set_suspend_inhibit(false);
    add_message_to_queue(reply, strsz);
    break;
  case 11:
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "Turning ADB *ON*\n");
    store_adb_setting(true);
    restart_usb_stack();
    add_message_to_queue(reply, strsz);
    break;
  case 12:
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "Turning ADB *OFF*\n");
    store_adb_setting(false);
    restart_usb_stack();
    add_message_to_queue(reply, strsz);
    break;
  case 13:
    for (i = 0; i < cmd_runtime.cmd_position; i++) {
      if (strsz < 160) {
        strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                          "%i ", cmd_runtime.cmd_history[i]);
      }
    }
    add_message_to_queue(reply, strsz);
    break;
  case 14:
    fp = fopen(get_openqti_logfile(), "r");
    if (fp == NULL) {
      logger(MSG_ERROR, "%s: Error opening file \n", __func__);
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "Error opening file\n");
    } else {
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "OpenQTI Log\n");
      add_message_to_queue(reply, strsz);
      if (ret > (MAX_MESSAGE_SIZE * QUEUE_SIZE)) {
        fseek(fp, (ret - (MAX_MESSAGE_SIZE * QUEUE_SIZE)), SEEK_SET);
      } else {
        fseek(fp, 0L, SEEK_SET);
      }
      do {
        memset(reply, 0, MAX_MESSAGE_SIZE);
        ret = fread(reply, 1, MAX_MESSAGE_SIZE - 2, fp);
        if (ret > 0) {
          add_message_to_queue(reply, ret);
        }
      } while (ret > 0);
      fclose(fp);
    }
    break;
  case 15:
    fp = fopen("/var/log/messages", "r");
    if (fp == NULL) {
      logger(MSG_ERROR, "%s: Error opening file \n", __func__);
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "Error opening file\n");
    } else {
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "DMESG:\n");
      add_message_to_queue(reply, strsz);
      fseek(fp, 0L, SEEK_END);
      ret = ftell(fp);
      if (ret > (MAX_MESSAGE_SIZE * QUEUE_SIZE)) {
        fseek(fp, (ret - (MAX_MESSAGE_SIZE * QUEUE_SIZE)), SEEK_SET);
      } else {
        fseek(fp, 0L, SEEK_SET);
      }
      do {
        memset(reply, 0, MAX_MESSAGE_SIZE);
        ret = fread(reply, 1, MAX_MESSAGE_SIZE - 2, fp);
        if (ret > 0) {
          add_message_to_queue(reply, ret);
        }
      } while (ret > 0);
      fclose(fp);
    }
    break;

  case 16:
    strsz +=
        snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz, "%s: %i\n",
                 bot_commands[cmd_id].cmd_text, get_dirty_reconnects());
    add_message_to_queue(reply, strsz);
    break;
  case 17:
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz, "%s\n",
                      bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    set_pending_call_flag(true);
    break;
  case 18:
    strsz +=
        snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz, "%s %s\n",
                 bot_commands[cmd_id].cmd_text, cmd_runtime.user_name);
    add_message_to_queue(reply, strsz);
    break;
  case 19:
    pthread_create(&disposable_thread, NULL, &delayed_shutdown, NULL);
    strsz +=
        snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz, "%s %s!\n",
                 bot_commands[cmd_id].cmd_text, cmd_runtime.user_name);
    add_message_to_queue(reply, strsz);
    break;
  case 20:
    render_gsm_signal_data();
    break;
  case 21:
    pthread_create(&disposable_thread, NULL, &delayed_reboot, NULL);
    strsz +=
        snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz, "%s %s!\n",
                 bot_commands[cmd_id].cmd_text, cmd_runtime.user_name);
    add_message_to_queue(reply, strsz);
    break;
  case 22:
    dump_signal_report();
    break;
  case 23:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    enable_signal_tracking(true);
    break;
  case 24:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    enable_signal_tracking(false);
    break;
  case 25:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    set_persistent_logging(true);
    break;
  case 26:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    set_persistent_logging(false);
    break;
  case 27:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    set_sms_logging(true);
    break;
  case 28:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    set_sms_logging(false);
    break;
  case 29: /* List pending tasks */
    dump_pending_tasks();
    break;
  case 30:
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "Hi, my name is %s and I'm at version %s\n",
                      cmd_runtime.bot_name, RELEASE_VER);
    add_message_to_queue(reply, strsz);
    strsz = snprintf(
        (char *)reply, MAX_MESSAGE_SIZE,
        "    .-.     .-.\n .****. .****.\n.*****.*****. Thank\n  .*********.   "
        " You\n    .*******.\n     .*****.\n       .***.\n          *\n");
    add_message_to_queue(reply, strsz);
    strsz = snprintf(
        (char *)reply, MAX_MESSAGE_SIZE,
        "Thank you for using me!\n And, especially, for all of you...");
    add_message_to_queue(reply, strsz);
    fp = fopen("/usr/share/thank_you/thankyou.txt", "r");
    if (fp != NULL) {
      size_t len = 0;
      char *line;
      memset(reply, 0, MAX_MESSAGE_SIZE);
      strsz = 0;
      i = 0;
      while ((ret = getline(&line, &len, fp)) != -1) {
        strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz, "%s",
                          line);
        if (i > 15 || strsz > 140) {
          add_message_to_queue(reply, strsz);
          memset(reply, 0, MAX_MESSAGE_SIZE);
          strsz = 0;
          i = 0;
        }
        i++;
      }
      if (strsz > 0) {
        add_message_to_queue(reply, strsz);
      }
      fclose(fp);
      if (line) {
        free(line);
      }
    }
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                     "Thank you for supporting my development and improving me "
                     "every day, I wouldn't have a purpose without you all!");
    add_message_to_queue(reply, strsz);
    break;
  case 31:
    set_cb_broadcast(true);
    break;
  case 32:
    set_cb_broadcast(false);
    break;
  case 33:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    enable_call_waiting_autohangup(2);
    break;
  case 34:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    enable_call_waiting_autohangup(1);
    break;
  case 35:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    enable_call_waiting_autohangup(0);
    break;
  case 36:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    set_custom_alert_tone(true); // enable in runtime
    break;
  case 37:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    set_custom_alert_tone(false); // enable in runtime
    break;
  case 38:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    if (!use_persistent_logging()) {
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "WARNING: Saving to ram, will be lost on reboot.\n");
    }
    add_message_to_queue(reply, strsz);

    strsz = snprintf(
        (char *)reply, MAX_MESSAGE_SIZE,
        "NOTICE: Call recording can be forbidden in some jurisdictions. Please "
        "check https://en.wikipedia.org/wiki/Telephone_call_recording_laws for "
        "more details\n");
    add_message_to_queue(reply, strsz);
    set_automatic_call_recording(2);
    break;
  case 39:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    if (!use_persistent_logging()) {
      strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                        "WARNING: Saving to ram, will be lost on reboot.\n");
    }
    add_message_to_queue(reply, strsz);
    strsz = snprintf(
        (char *)reply, MAX_MESSAGE_SIZE,
        "NOTICE: Call recording can be forbidden in some jurisdictions. Please "
        "check https://en.wikipedia.org/wiki/Telephone_call_recording_laws for "
        "more details\n");
    add_message_to_queue(reply, strsz);
    set_automatic_call_recording(1);
    break;
  case 40:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    set_automatic_call_recording(0);
    break;
  case 41:
    if (record_current_call() == 0) {
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                       bot_commands[cmd_id].cmd_text);
      if (!use_persistent_logging()) {
        strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                          "WARNING: Saving to ram, will be lost on reboot.\n");
      }
    } else
      strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE,
                       "There is no call in progress!\n");
    add_message_to_queue(reply, strsz);
    break;
  case 42:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    record_next_call(true);
    break;
  case 43:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    record_next_call(false);
    break;
  case 44:
    debug_gsm7_cb_message(command);
    break;
  case 45:
    debug_ucs2_cb_message(command);
    break;
  case 46: // enable smdcntl0
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    set_internal_connectivity(true);
    init_internal_networking();
    break;
  case 47: // disable smdcntl0
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    set_internal_connectivity(false);
    break;
  case 48:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    set_do_not_disturb(false);
    remove_all_tasks_by_type(TASK_TYPE_DND_CLEAR);
    break;

  case 49:
    strsz = snprintf((char *)reply, MAX_MESSAGE_SIZE, "%s\n",
                     bot_commands[cmd_id].cmd_text);
    add_message_to_queue(reply, strsz);
    disable_service_debugging();
    break;
  case 50:
    strsz = 0;
    snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
             "Known QMI Services\n");
    add_message_to_queue(reply, strsz);
    strsz = 0;
    for (i = 0; i < (sizeof(qmi_services) / sizeof(qmi_services[0])); i++) {
      if (strlen(qmi_services[i].name) + (3 * sizeof(uint8_t)) +
              strlen(qmi_services[i].name) + strsz >
          MAX_MESSAGE_SIZE) {
        add_message_to_queue(reply, strsz);
        strsz = 0;
      }
      strsz +=
          snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz, "%u: %s\n",
                   qmi_services[i].service, qmi_services[i].name);
    }
    add_message_to_queue(reply, strsz);
    break;
  case 100:
    set_custom_modem_name(command);
    break;
  case 101:
    set_custom_user_name(command);
    break;
  case 102:
    pthread_create(&disposable_thread, NULL, &schedule_call, command);
    sleep(2); // our string gets wiped out before we have a chance
    break;
  case 103:
    schedule_reminder(command);
    break;
  case 104:
    schedule_wakeup(command);
    break;
  case 105: /* Delete task %i */
    delete_task(command);
    break;
  case 106: /* Leave me alone [not implemented yet] */
    suspend_call_notifications(command);
    break;
  case 107:
    enable_service_debugging_for_service_id(command);
    break;
  case 108:
    set_new_signal_tracking_mode(command);
    break;
  default:
    strsz += snprintf((char *)reply + strsz, MAX_MESSAGE_SIZE - strsz,
                      "Invalid command id %i\n", cmd_id);
    logger(MSG_INFO, "%s: Unknown command %i\n", __func__, cmd_id);
    break;
  }

  add_to_history(cmd_id);

  free(tmpbuf);
  free(reply);

  tmpbuf = NULL;
  reply = NULL;
  return ret;
}
