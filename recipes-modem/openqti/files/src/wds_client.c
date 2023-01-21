// SPDX-License-Identifier: MIT

#include <asm-generic/errno-base.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../inc/cell.h"
#include "../inc/config.h"
#include "../inc/devices.h"
#include "../inc/ipc.h"
#include "../inc/logger.h"
#include "../inc/qmi.h"
#include "../inc/wds.h"

struct {
  uint8_t mux_state;
  int pdp_session_handle; // we need this to stop the connection later
} wds_runtime;

void reset_wds_runtime() {
  wds_runtime.mux_state = 0;
  wds_runtime.pdp_session_handle = -1;
}

const char *get_wds_command(uint16_t msgid) {
  for (uint16_t i = 0;
       i < (sizeof(wds_svc_commands) / sizeof(wds_svc_commands[0])); i++) {
    if (wds_svc_commands[i].id == msgid) {
      return wds_svc_commands[i].cmd;
    }
  }
  return "WDS service: Unknown command\n";
}

int wds_bind_mux_data_port() {
  struct wds_bind_mux_data_port_request *request = NULL;

  if (is_internal_connect_enabled()) {
    logger(MSG_INFO, "%s: Bind mux data port request!\n", __func__);
  } else {
    logger(MSG_INFO, "%s: Internal connectivity support is not enabled\n",
           __func__);
    return -EINVAL;
  }
  request = (struct wds_bind_mux_data_port_request *)calloc(
      1, sizeof(struct wds_bind_mux_data_port_request));

  request->qmux.version = 0x01;
  request->qmux.packet_length =
      sizeof(struct wds_bind_mux_data_port_request) - sizeof(uint8_t);
  request->qmux.control = 0x00;
  request->qmux.service = QMI_SERVICE_WDS;
  //  request->qmux.instance_id = wds_runtime.instance_id;

  request->qmi.ctlid = QMI_REQUEST;
  //  request->qmi.transaction_id = wds_runtime.transaction_id;
  request->qmi.msgid = WDS_BIND_MUX_DATA_PORT;
  request->qmi.length = sizeof(struct wds_bind_mux_data_port_request) -
                        sizeof(struct qmux_packet) - sizeof(struct qmi_packet);

  request->peripheral_id.type = 0x10;
  request->peripheral_id.len = 0x08;
  request->peripheral_id.ep_type = htole32(DATA_EP_TYPE_BAM_DMUX);
  request->peripheral_id.interface_id = htole32(0);

  request->mux.type = 0x11;
  request->mux.len = 0x01;
  request->mux.mux_id = 42;

  add_pending_message(QMI_SERVICE_WDS, (uint8_t *)request,
                              sizeof(struct wds_bind_mux_data_port_request));
  free(request);
  return 0;
}

int wds_attempt_to_connect() {
  struct wds_start_network *request = NULL;
  if (is_internal_connect_enabled()) {
    logger(MSG_INFO, "%s: Autoconnect request!\n", __func__);
  } else {
    logger(MSG_INFO, "%s: Internal connectivity support is not enabled\n",
           __func__);
    return -EINVAL;
  }
  if (wds_runtime.mux_state == 0) {
    wds_bind_mux_data_port();
  } else if (wds_runtime.mux_state == 1) {
    logger(MSG_INFO, "%s: Waiting for mux ready...\n", __func__);
  } else {
    logger(MSG_INFO, "Mux was already setup\n");
  }

  request =
      (struct wds_start_network *)calloc(1, sizeof(struct wds_start_network));
  request->qmux.version = 0x01;
  request->qmux.packet_length =
      sizeof(struct wds_start_network) - sizeof(uint8_t);
  request->qmux.control = 0x00;
  request->qmux.service = QMI_SERVICE_WDS;
  //  request->qmux.instance_id = wds_runtime.instance_id;

  request->qmi.ctlid = QMI_REQUEST;
  //  request->qmi.transaction_id = wds_runtime.transaction_id;
  request->qmi.msgid = WDS_START_NETWORK;
  request->qmi.length = sizeof(struct wds_start_network) -
                        sizeof(struct qmux_packet) - sizeof(struct qmi_packet);

  request->apn.type = 0x14;
  request->apn.len = 8;
  memcpy(request->apn.apn, DEFAULT_APN_NAME, strlen(DEFAULT_APN_NAME));

  request->apn_type.type = 0x38;
  request->apn_type.len = 0x04;
  request->apn_type.value = htole32(WDS_APN_TYPE_INTERNET);

  request->ip_family.type = 0x19;
  request->ip_family.len = 0x01;
  request->ip_family.value = 4;

  request->profile.type = 0x31;
  request->profile.len = 0x01;
  request->profile.value = 2; // Let's try this one

  request->call_type.type = 0x35;
  request->call_type.len = 0x01;
  request->call_type.value = 1; // Let's try with embedded

  request->autoconnect.type = 0x33;
  request->autoconnect.len = 0x01;
  request->autoconnect.value = 0x01; // ON

  request->roaming_lock.type = 0x39;
  request->roaming_lock.len = 0x01;
  request->roaming_lock.value = 0x00; // OFF

  add_pending_message(QMI_SERVICE_WDS, (uint8_t *)request,
                              sizeof(struct wds_start_network));

  free(request);
  return 0;
}

void *init_internal_networking() {
  if (!is_internal_connect_enabled()) {
    logger(MSG_WARN, "%s: Internal networking is disabled\n", __func__);
    return NULL;
  }

  while (!get_network_type()) {
    logger(MSG_INFO, "[%s] Waiting for network to be ready...\n", __func__);
    sleep(10);
  }

  logger(MSG_INFO, "%s: Network is ready, try to connect\n", __func__);
  wds_attempt_to_connect();

  return NULL;
}

/*
 * Reroutes messages from the internal QMI client to the service
 */
int handle_incoming_wds_message(uint8_t *buf, size_t buf_len) {
  logger(MSG_INFO, "%s: Start\n", __func__);
  uint16_t qmi_err;

  switch (get_qmi_message_id(buf, buf_len)) {
  case WDS_BIND_MUX_DATA_PORT:
    if (get_qmi_message_type(buf, buf_len) == QMI_RESPONSE) {
      qmi_err = did_qmi_op_fail(buf, buf_len);
      if (qmi_err == QMI_RESULT_FAILURE) {
        logger(MSG_ERROR, "%s failed\n", __func__);
        wds_runtime.mux_state = 0;
      } else if (qmi_err == QMI_RESULT_SUCCESS) {
        logger(MSG_INFO, "%s succeeded\n", __func__);
        wds_runtime.mux_state = 2;
      } else if (qmi_err == QMI_RESULT_UNKNOWN) {
        logger(MSG_ERROR, "%s: QMI message didn't have an indication\n",
               __func__);
      }
    }
    break;
  case WDS_START_NETWORK:
    if (did_qmi_op_fail(buf, buf_len)) {
      logger(MSG_ERROR, "%s failed\n", __func__);
    }
    break;
  default:
    logger(MSG_INFO, "%s: Unhandled message for WDS: %.4x\n", __func__,
           get_qmi_message_id(buf, buf_len));
    break;
  }

  return 0;
}