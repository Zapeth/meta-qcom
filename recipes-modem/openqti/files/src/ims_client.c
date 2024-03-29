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

#include "../inc/config.h"
#include "../inc/devices.h"
#include "../inc/ims.h"
#include "../inc/ipc.h"
#include "../inc/logger.h"
#include "../inc/qmi.h"

#define DEBUG_IMS 1

const char *get_ims_command(uint16_t msgid) {
  for (uint16_t i = 0;
       i < (sizeof(ims_svc_commands) / sizeof(ims_svc_commands[0])); i++) {
    if (ims_svc_commands[i].id == msgid) {
      return ims_svc_commands[i].cmd;
    }
  }
  return "IMSD: Unknown command\n";
}
/*
int imsd_request_read_file(uint8_t *path) {
  size_t pkt_len = sizeof(struct qmux_packet) + sizeof(struct qmi_packet) +
                   sizeof(struct modemfs_path) + strlen((char*)path);
  uint8_t *pkt = malloc(pkt_len);
  memset(pkt, 0, pkt_len);
  if (build_qmux_header(pkt, pkt_len, 0x00, QMI_SERVICE_IMS, 0) < 0) {
    logger(MSG_ERROR, "%s: Error adding the qmux header\n", __func__);
    free(pkt);
    return -EINVAL;
  }
  if (build_qmi_header(pkt, pkt_len, QMI_REQUEST, 0, MODEM_FS_READ) < 0) {
    logger(MSG_ERROR, "%s: Error adding the qmi header\n", __func__);
    free(pkt);
    return -EINVAL;
  }
  size_t curr_offset = sizeof(struct qmux_packet) + sizeof(struct qmi_packet);
  struct modemfs_path *filepath = (struct modemfs_path *)(pkt + curr_offset);
  filepath->id = 0x01;
  filepath->len = strlen((char*)path);
  memcpy(filepath->path, path, filepath->len);


  add_pending_message(QMI_SERVICE_IMS, (uint8_t *)pkt, pkt_len);

  free(pkt);
    return 0;
}
*/
/*
int imsd_demo_read() {
    char demopath[]= "/nv/item_files/modem/mmode/sms_domain_pref";
    imsd_request_read_file((uint8_t *)demopath);
  return 0;
}
*/
/*
int imsd_request_config_list() {
  size_t pkt_len = sizeof(struct qmux_packet) + sizeof(struct qmi_packet) +
                   sizeof(struct imsd_indication_key) + sizeof(struct
imsd_setting_type); uint8_t *pkt = malloc(pkt_len); memset(pkt, 0, pkt_len); if
(build_qmux_header(pkt, pkt_len, 0x00, QMI_SERVICE_PDC, 0) < 0) {
    logger(MSG_ERROR, "%s: Error adding the qmux header\n", __func__);
    free(pkt);
    return -EINVAL;
  }
  if (build_qmi_header(pkt, pkt_len, QMI_REQUEST, 0,
                       PERSISTENT_DEVICE_CONFIG_LIST_CONFIGS) < 0) {
    logger(MSG_ERROR, "%s: Error adding the qmi header\n", __func__);
    free(pkt);
    return -EINVAL;
  }
  size_t curr_offset = sizeof(struct qmux_packet) + sizeof(struct qmi_packet);
  struct imsd_indication_key *key = (struct imsd_indication_key *)(pkt +
curr_offset); key->id = 0x10; key->len = sizeof(uint32_t); key->key =
0xdeadbeef;

  curr_offset += sizeof(struct imsd_indication_key);
  struct imsd_setting_type *config_type = (struct imsd_setting_type *)(pkt +
curr_offset); config_type->id = 0x11; config_type->len = sizeof(uint32_t);
  config_type->data = PDC_CONFIG_SW;

  add_pending_message(QMI_SERVICE_PDC, (uint8_t *)pkt, pkt_len);

  free(pkt);
  return 0;
}*/

int ims_request_sip_config() {
  size_t pkt_len = sizeof(struct qmux_packet) + sizeof(struct qmi_packet);
  uint8_t *pkt = malloc(pkt_len);
  memset(pkt, 0, pkt_len);
  if (build_qmux_header(pkt, pkt_len, 0x00, QMI_SERVICE_IMS_SETTINGS, 0) < 0) {
    logger(MSG_ERROR, "%s: Error adding the qmux header\n", __func__);
    free(pkt);
    return -EINVAL;
  }
  if (build_qmi_header(pkt, pkt_len, QMI_REQUEST, 0,
                       QMI_IMS_SETTINGS_GET_SIP_CONFIG) < 0) {
    logger(MSG_ERROR, "%s: Error adding the qmi header\n", __func__);
    free(pkt);
    return -EINVAL;
  }
  size_t curr_offset = sizeof(struct qmux_packet) + sizeof(struct qmi_packet);
  add_pending_message(QMI_SERVICE_IMS_SETTINGS, (uint8_t *)pkt, pkt_len);

  free(pkt);
  return 0;
}


int ims_get_subscription() {
  size_t pkt_len = sizeof(struct qmux_packet) + sizeof(struct qmi_packet);
  uint8_t *pkt = malloc(pkt_len);
  logger(MSG_INFO, "%s: start\n", __func__);
  memset(pkt, 0, pkt_len);
  if (build_qmux_header(pkt, pkt_len, 0x00, QMI_SERVICE_IMS_SETTINGS, 0) < 0) {
    logger(MSG_ERROR, "%s: Error adding the qmux header\n", __func__);
    free(pkt);
    return -EINVAL;
  }
  if (build_qmi_header(pkt, pkt_len, QMI_REQUEST, 0,
                       QMI_IMS_GET_ACTIVE_SUBSCRIPTION_STATUS) < 0) {
    logger(MSG_ERROR, "%s: Error adding the qmi header\n", __func__);
    free(pkt);
    return -EINVAL;
  }
  size_t curr_offset = sizeof(struct qmux_packet) + sizeof(struct qmi_packet);
  add_pending_message(QMI_SERVICE_IMS_SETTINGS, (uint8_t *)pkt, pkt_len);

  free(pkt);
  return 0;
}

//QMI_IMS_GET_ACTIVE_SUBSCRIPTION_STATUS
int ims_process_config_response(uint8_t *buf, size_t buf_len) {
  /* TLVS here: 0x10, 0x11, 0x12 (we discard this one since it's the same as
   * 0x10 but encoded in gsm7)*/
  uint8_t tlvs[] = {
      IMS_GET_SETTINGS_RESPONSE,
      IMS_GET_SETTINGS_SIP_LOCAL_PORT,
      IMS_GET_SETTINGS_SIP_REGISTRATION_TIMER,
      IMS_GET_SETTINGS_SIP_SUBSCRIBE_TIMER,
      IMS_GET_SETTINGS_SIP_RTT_MS,
      IMS_GET_SETTINGS_SIP_RETRANSMIT_INTERVAL_MS,
      IMS_GET_SETTINGS_SIP_NON_INVITE_TRANSACTION_TIMEOUT_MS,
      IMS_GET_SETTINGS_SIP_IS_SIGCOMP_ENABLED,
      IMS_GET_SETTINGS_SIP_WAIT_TIME_REQUEST_RETRANSMIT_MS,
      IMS_GET_SETTINGS_SIP_WAIT_TIME_NON_INVITE_REQUEST_MS,
      IMS_GET_SETTINGS_SIP_KEEPALIVE_ENABLED,
      IMS_GET_SETTINGS_SIP_NAT_RTO_TIMER,
      IMS_GET_SETTINGS_SIP_TIMER_OPERATOR_MODE,
  };
  for (uint8_t i = 0; i < 13; i++) {
    struct qmi_generic_uint8_t_tlv *u8tlv;
    struct qmi_generic_uint16_t_tlv *u16tlv;
    struct qmi_generic_uint32_t_tlv *u32tlv;

    int offset = get_tlv_offset_by_id(buf, buf_len, tlvs[i]);
    if (offset > 0) {
      switch (tlvs[i]) {
      case IMS_GET_SETTINGS_RESPONSE:
        u8tlv = (struct qmi_generic_uint8_t_tlv *)(buf + offset);
        logger(MSG_INFO, "%s: Param: IMS_GET_SETTINGS_RESPONSE: %u\n", __func__, u8tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_LOCAL_PORT:
        u16tlv = (struct qmi_generic_uint16_t_tlv *)(buf + offset);

        logger(MSG_INFO, "%s: Param: IMS_GET_SETTINGS_SIP_LOCAL_PORT: %u\n",
               __func__, u16tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_REGISTRATION_TIMER:
        u32tlv = (struct qmi_generic_uint32_t_tlv *)(buf + offset);

        logger(MSG_INFO, "%s: Param: IMS_GET_SETTINGS_SIP_REGISTRATION_TIMER %u\n",
               __func__, u32tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_SUBSCRIBE_TIMER:
        u32tlv = (struct qmi_generic_uint32_t_tlv *)(buf + offset);
        logger(MSG_INFO, "%s: Param: IMS_GET_SETTINGS_SIP_SUBSCRIBE_TIMER %u\n",
               __func__, u32tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_RTT_MS:
        u32tlv = (struct qmi_generic_uint32_t_tlv *)(buf + offset);
        logger(MSG_INFO, "%s: Param: IMS_GET_SETTINGS_SIP_RTT_MS: %u\n", __func__, u32tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_RETRANSMIT_INTERVAL_MS:
        u32tlv = (struct qmi_generic_uint32_t_tlv *)(buf + offset);

        logger(MSG_INFO,
               "%s: Param: IMS_GET_SETTINGS_SIP_RETRANSMIT_INTERVAL_MS: %u\n",
               __func__, u32tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_NON_INVITE_TRANSACTION_TIMEOUT_MS:
        u16tlv = (struct qmi_generic_uint16_t_tlv *)(buf + offset);
        logger(MSG_INFO,
               "%s: Param: "
               "IMS_GET_SETTINGS_SIP_NON_INVITE_TRANSACTION_TIMEOUT_MS: %u\n",
               __func__, u16tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_IS_SIGCOMP_ENABLED:
        u8tlv = (struct qmi_generic_uint8_t_tlv *)(buf + offset);
        logger(MSG_INFO, "%s: Param: IMS_GET_SETTINGS_SIP_IS_SIGCOMP_ENABLED: %u\n",
               __func__, u8tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_WAIT_TIME_REQUEST_RETRANSMIT_MS:
        u16tlv = (struct qmi_generic_uint16_t_tlv *)(buf + offset);
        logger(
            MSG_INFO,
            "%s: Param: IMS_GET_SETTINGS_SIP_WAIT_TIME_REQUEST_RETRANSMIT_MS: %u\n",
            __func__, u16tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_WAIT_TIME_NON_INVITE_REQUEST_MS:
        u32tlv = (struct qmi_generic_uint32_t_tlv *)(buf + offset);
        logger(
            MSG_INFO,
            "%s: Param: IMS_GET_SETTINGS_SIP_WAIT_TIME_NON_INVITE_REQUEST_MS: %u\n",
            __func__, u32tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_KEEPALIVE_ENABLED:
        u8tlv = (struct qmi_generic_uint8_t_tlv *)(buf + offset);
        logger(MSG_INFO, "%s: Param: IMS_GET_SETTINGS_SIP_KEEPALIVE_ENABLED: %u\n",
               __func__, u8tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_NAT_RTO_TIMER:
        u32tlv = (struct qmi_generic_uint32_t_tlv *)(buf + offset);
        logger(MSG_INFO, "%s: Param: IMS_GET_SETTINGS_SIP_NAT_RTO_TIMER: %u\n",
               __func__, u32tlv->data);
        break;
      case IMS_GET_SETTINGS_SIP_TIMER_OPERATOR_MODE:
        u32tlv = (struct qmi_generic_uint32_t_tlv *)(buf + offset);
        logger(MSG_INFO,
               "%s: Param: IMS_GET_SETTINGS_SIP_TIMER_OPERATOR_MODE: %u\n",
               __func__, u32tlv->data);
        break;
      }
    }
  }

  return 0;
}

int ims_process_active_subscription_status(uint8_t *buf, size_t buf_len) {
  uint8_t tlvs[] = {
	  IMS_ACTIVE_SUBSCRIPTION_PRIMARY,
	  IMS_ACTIVE_SUBSCRIPTION_SECONDARY,
	  IMS_ACTIVE_SUBSCRIPTION_TERTIARY,
  };
  for (uint8_t i = 0; i < 13; i++) {
    struct subscription_status_indication *subscription;
    int offset = get_tlv_offset_by_id(buf, buf_len, tlvs[i]);
    if (offset > 0) {
      switch (tlvs[i]) {
      case IMS_ACTIVE_SUBSCRIPTION_PRIMARY:
        subscription = (struct subscription_status_indication *)(buf + offset);
        logger(MSG_INFO, "%s: Param: IMS_ACTIVE_SUBSCRIPTION_PRIMARY:%i\n", __func__, subscription->state);
        break;
      case IMS_ACTIVE_SUBSCRIPTION_SECONDARY:
        subscription = (struct subscription_status_indication *)(buf + offset);
        logger(MSG_INFO, "%s: Param: IMS_ACTIVE_SUBSCRIPTION_SECONDARY:%i\n", __func__, subscription->state);
        break;
      case IMS_ACTIVE_SUBSCRIPTION_TERTIARY:
        subscription = (struct subscription_status_indication *)(buf + offset);
        logger(MSG_INFO, "%s: Param: IMS_ACTIVE_SUBSCRIPTION_TERTIARY:%i\n", __func__, subscription->state);
        break;
      }
    }
  }

  return 0;
}
 /*
  * Before we can use (or step over) hexagon's IMS service
  * We need a data session. So we're going to build it like
  * the WDS service
  *
  *
  */

/*
 * Reroutes messages from the internal QMI client to the service
 */
int handle_incoming_ims_message(uint8_t *buf, size_t buf_len) {
  logger(MSG_INFO, "%s: Start: Message from the IMS Service: %.4x | %s\n",
         __func__, get_qmi_message_id(buf, buf_len),
         get_ims_command(get_qmi_message_id(buf, buf_len)));

#ifdef DEBUG_IMS
  pretty_print_qmi_pkt("IMS: Baseband --> Host", buf, buf_len);
#endif

  switch (get_qmi_message_id(buf, buf_len)) {
    case QMI_IMS_SETTINGS_GET_SIP_CONFIG:
        ims_process_config_response(buf, buf_len);
        break;
    case QMI_IMS_GET_ACTIVE_SUBSCRIPTION_STATUS:
        ims_process_active_subscription_status(buf, buf_len);
        break;
  default:
    logger(MSG_INFO, "%s: Unhandled message for the IMS Service: %.4x | %s\n",
           __func__, get_qmi_message_id(buf, buf_len),
           get_ims_command(get_qmi_message_id(buf, buf_len)));
    break;
  }

  return 0;
}

void *register_to_ims_service() {
  ims_request_sip_config();
  ims_get_subscription();
  logger(MSG_INFO, "%s finished!\n", __func__);
  return NULL;
}