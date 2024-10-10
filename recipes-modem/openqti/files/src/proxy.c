// SPDX-License-Identifier: MIT

#include "proxy.h"
#include "atfwd.h"
#include "audio.h"
#include "call.h"
#include "config.h"
#include "devices.h"
#include "helpers.h"
#include "ipc.h"
#include "logger.h"
#include "openqti.h"
#include "qmi.h"
#include "sms.h"
#include "tracking.h"
#include "nas.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <unistd.h>

struct {
  int is_usb_suspended;
  uint8_t is_service_debugging_enabled;
  uint8_t debug_service_id;
  struct pkt_stats rmnet_packet_stats;
  struct pkt_stats gps_packet_stats;
} proxy_rt;

void proxy_rt_reset() {
  proxy_rt.is_usb_suspended = 0;
  proxy_rt.is_service_debugging_enabled = 0;
  proxy_rt.debug_service_id = 0;
}

void enable_service_debugging(uint8_t service_id) {
  proxy_rt.debug_service_id = service_id;
  proxy_rt.is_service_debugging_enabled = 1;
}

void disable_service_debugging() {
  proxy_rt.is_service_debugging_enabled = 0;
  proxy_rt.debug_service_id = 0;
}
struct pkt_stats get_rmnet_stats() {
  return proxy_rt.rmnet_packet_stats;
}

struct pkt_stats get_gps_stats() {
  return proxy_rt.gps_packet_stats;
}

int get_transceiver_suspend_state() {
  int fd, val = 0;
  char readval[6];
  proxy_rt.is_usb_suspended = 0;
  fd = open("/sys/devices/78d9000.usb/msm_hsusb/isr_suspend_state", O_RDONLY);
  if (fd < 0) {
    logger(MSG_ERROR, "%s: Cannot open USB state \n", __func__);
    return proxy_rt.is_usb_suspended;
  }
  lseek(fd, 0, SEEK_SET);
  if (read(fd, &readval, 1) <= 0) {
    logger(MSG_ERROR, "%s: Error reading USB Sysfs entry \n", __func__);
    close(fd);
    return proxy_rt.is_usb_suspended; // return last state
  }
  val = strtol(readval, NULL, 10);

  if (val > 0 && proxy_rt.is_usb_suspended == 0) {
    proxy_rt.is_usb_suspended =
        1; // USB is suspended, stop trying to transfer data
           // system("echo mem > /sys/power/state");
  } else if (val == 0 && proxy_rt.is_usb_suspended == 1) {
    usleep(100000);                // Allow time to finish wakeup
    proxy_rt.is_usb_suspended = 0; // Then allow transfers again
  }
  close(fd);
  return proxy_rt.is_usb_suspended;
}

/*
 * GPS does funky stuff when enabled and suspended
 *  When GPS is turned on and the pinephone is suspended
 *  there's a buffer that starts filling up. Once it's full
 *  it triggers a kernel panic that brings the whole thing
 *  down.
 *  Since we don't want to kill user's GPS session, but at
 *  the same time we don't want the modem to die, we just
 *  allow the buffer to slowly drain of NMEA messages but
 *  we ignore them without sending them to the USB port.
 *  When phone wakes up again (assuming something was
 *  using it), GPS is still active and won't need resyncing
 */
void find_and_set_current_sms_memory_index(uint8_t *buf, int len) {
  char *pos;
  uint8_t val;
  if (strstr((char *)buf, "+CMTI: \"ME\",") != NULL) {
    logger(MSG_WARN, "%s CMTI Report: %s\n", __func__, buf);
    pos = strstr((char *)buf, ",");
    if (pos != NULL) {
      val = atoi(pos);
      logger(MSG_WARN, "%s: Memory index: %u\n", __func__, val);
      set_pending_messages_in_adsp(val);
    }
  }
  pos = NULL;
}

void *gps_proxy() {
  struct node_pair *nodes;
  nodes = calloc(1, sizeof(struct node_pair));
  int ret;
  fd_set readfds;
  uint8_t buf[MAX_PACKET_SIZE];
  struct timeval tv;
  logger(MSG_INFO, "%s: Initialize GPS proxy thread.\n", __func__);

  nodes->node1.fd = -1;
  nodes->node2.fd = -1;

  while (1) {
    FD_ZERO(&readfds);
    memset(buf, 0, sizeof(buf));

    if (nodes->node1.fd < 0) {
      nodes->node1.fd = open(SMD_GPS, O_RDWR);
      if (nodes->node1.fd < 0) {
        logger(MSG_ERROR, "%s: Error opening %s \n", __func__, SMD_GPS);
      }
    }

    if (!get_transceiver_suspend_state() && nodes->node2.fd < 0) {
      nodes->node2.fd = open(USB_GPS, O_RDWR);
      if (nodes->node2.fd < 0) {
        logger(MSG_ERROR, "%s: Error opening %s \n", __func__, USB_GPS);
      }
    } else if (nodes->node2.fd < 0) {
      logger(MSG_WARN, "%s: Not trying to open USB GPS \n", __func__);
    }

    FD_SET(nodes->node1.fd, &readfds);
    if (nodes->node2.fd >= 0) {
      FD_SET(nodes->node2.fd, &readfds);
    }

    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    select(MAX_FD, &readfds, NULL, NULL, &tv);
    if (FD_ISSET(nodes->node1.fd, &readfds)) {
      ret = read(nodes->node1.fd, &buf, MAX_PACKET_SIZE);
      if (ret > 0) {
        dump_packet("GPS_SMD-->USB", buf, ret);
        // CMTI Initial check:
        /*       if (strstr((char*)buf, "+CMTI: \"ME\",") != NULL) {
                 logger(MSG_WARN, "CMTI Report: %s", buf);
                 find_and_set_current_sms_memory_index(buf, ret);
               }*/
        if (!get_transceiver_suspend_state() && nodes->node2.fd >= 0) {
          proxy_rt.gps_packet_stats.allowed++;
          ret = write(nodes->node2.fd, buf, ret);
          if (ret == 0) {
            proxy_rt.gps_packet_stats.failed++;
            logger(MSG_ERROR, "%s: [GPS_TRACK Failed to write to USB\n",
                   __func__);
          }
        } else {
          proxy_rt.gps_packet_stats.discarded++;
        }
      } else {
        proxy_rt.gps_packet_stats.empty++;
        logger(MSG_WARN, "%s: Closing at the ADSP side \n", __func__);
        close(nodes->node1.fd);
        nodes->node1.fd = -1;
      }
    } else if (!get_transceiver_suspend_state() && nodes->node2.fd >= 0 &&
               FD_ISSET(nodes->node2.fd, &readfds)) {

      ret = read(nodes->node2.fd, &buf, MAX_PACKET_SIZE);
      if (ret > 0) {
        proxy_rt.gps_packet_stats.allowed++;
        dump_packet("GPS_SMD<--USB", buf, ret);
        ret = write(nodes->node1.fd, buf, ret);
        if (ret == 0) {
          proxy_rt.gps_packet_stats.failed++;
          logger(MSG_ERROR, "%s: Failed to write to the ADSP\n", __func__);
        }
      } else {
        proxy_rt.gps_packet_stats.empty++;
        logger(MSG_ERROR, "%s: Closing at the USB side \n", __func__);
        nodes->allow_exit = true;
        close(nodes->node2.fd);
        nodes->node2.fd = -1;
      }
    }
  }
  free(nodes);
}

uint8_t process_simulated_packet(uint8_t source, int adspfd, int usbfd) {
  /* Messaging */
  if (is_message_pending() && get_notification_source() == MSG_INTERNAL) {
    process_message_queue(usbfd);
    return 0;
  }
  if (is_message_pending() && get_notification_source() == MSG_EXTERNAL) {
    // we trigger a notification only
    logger(MSG_WARN, "%s: Generating artificial message notification\n",
           __func__);
    do_inject_notification(usbfd);
    set_pending_notification_source(MSG_NONE);
    set_notif_pending(false);
    return 0;
  }
  if (is_stuck_message_retrieve_pending()) {
    retrieve_and_delete(adspfd, usbfd);
    return 0;
  }

  /* Calling */
  if (get_call_pending()) {
    logger(MSG_WARN, "%s Clearing flag and creating a call\n", __func__);
    set_pending_call_flag(false);
    start_simulated_call(usbfd);
    return 0;
  }

  if (get_call_simulation_mode()) {
    notify_simulated_call(usbfd);
  }

  if (at_debug_cb_message_requested()) {
    send_cb_message_to_modemmanager(usbfd, 0);
  }

  if (at_debug_random_cb_message_requested()) {
    send_cb_message_to_modemmanager(usbfd, -1);
  }
  if (at_debug_stream_cb_message_requested()) {
    send_cb_message_to_modemmanager(usbfd, -2);
  }

  return 0;
}

uint8_t process_wms_packet(void *bytes, size_t len, int adspfd, int usbfd) {
  int needs_rerouting = 0;
  if (is_message_pending() && get_notification_source() == MSG_INTERNAL) {
    logger(MSG_DEBUG, "%s: We need to do stuff\n", __func__);
    notify_wms_event(bytes, len, usbfd);
    needs_rerouting = 1;
  }
  return needs_rerouting;
}

/* Node1 -> RMNET , Node2 -> SMD */
/*
 *  process_packet()
 *    Looks at the QMI message to get the service type
 *    and if needed, moves the message somewhere else
 *    for further processing
 */
uint8_t process_packet(uint8_t source, uint8_t *pkt, size_t pkt_size,
                       int adspfd, int usbfd) {
  struct qmux_packet *qmux_header;

  // By default everything should just go to its place
  int action = PACKET_PASS_TRHU;
  if (source == FROM_HOST) {
    logger(MSG_DEBUG, "%s: New packet from HOST of %i bytes\n", __func__,
           pkt_size);
  } else {
    logger(MSG_DEBUG, "%s: New packet from ADSP of %i bytes\n", __func__,
           pkt_size);
  }
  dump_packet(source == FROM_HOST ? "HOST->SMD" : "HOST<-SMD", pkt, pkt_size);

  if (pkt_size == 0) {   // Port was closed
    return PACKET_EMPTY; // Abort processing
  }

  /*
   * There are two different types of QMI packets, and we don't know
   * which one we're handling right now, so cast both and then check
   */

  /*
   * Message needs to have a QMUX header and at least a 6 byte QMI header
   * (control) Or QMUX header + 7 Byte QMI header (service). If it's less than
   * that we stop here
   */

  if (pkt_size < (sizeof(struct qmux_packet) + sizeof(struct ctl_qmi_packet))) {
    logger(MSG_ERROR, "%s: Message too small\n", __func__);
    return PACKET_EMPTY;
  }

  qmux_header = (struct qmux_packet *)pkt;
  /* If we have enough for a "service" QMI message we will inspect things
   * further down
   */

  logger(MSG_DEBUG, "[New QMI message] Service: %s (%i bytes)\n",
         get_service_name(qmux_header->service), pkt_size);

  if (proxy_rt.is_service_debugging_enabled &&
      get_qmux_service_id(pkt, pkt_size) == proxy_rt.debug_service_id) {
    pretty_print_qmi_pkt(source == FROM_HOST ? "Host --> Baseband"
                                             : "Baseband --> Host",
                         pkt, pkt_size);
  }
  /* In the future we can use this as a router inside the application.
   * For now we only do some simple tasks depending on service, so no
   * need to do too much
   */
  switch (get_qmux_service_id(pkt, pkt_size)) {
  case 0: // Control packet with no service
    logger(
        MSG_DEBUG, "%s Control message, Command: %s\n", __func__,
        get_ctl_command(get_control_message_id(
            pkt, pkt_size))); // reroute to the tracker for further inspection
    if (get_control_message_id(pkt, pkt_size) == CONTROL_CLIENT_REGISTER_REQ ||
        get_control_message_id(pkt, pkt_size) == CONTROL_CLIENT_RELEASE_REQ) {
      track_client_count(pkt, source, pkt_size, adspfd, usbfd);
    }
    break;
  case 3:
    logger(MSG_DEBUG, "%s: Network Access Service\n", __func__);
    if (get_call_simulation_mode() && get_qmi_message_id(pkt, pkt_size) == NAS_GET_SIGNAL_INFO) { // 0x004f == GET_SIGNAL_REPORT
        logger(MSG_INFO, "%s: Skip signal level reporting while in call\n",
               __func__);
        action = PACKET_BYPASS;
    }
    break;
  /* Here we'll trap messages to the Modem */
  case 5: // Message for the WMS service
    action = PACKET_FORCED_PT;
    logger(MSG_DEBUG, "%s WMS Packet\n", __func__);
    if (check_wms_message(source, pkt, pkt_size, adspfd, usbfd)) {
      action = PACKET_BYPASS; // We bypass response
    } else if (check_wms_indication_message(pkt, pkt_size, adspfd, usbfd)) {
      action = PACKET_FORCED_PT;
    } else if (check_cb_message(pkt, pkt_size, adspfd, usbfd)) {
      action = PACKET_FORCED_PT;
    } else if (get_current_host_app() == HOST_USES_MODEMMANAGER &&
              is_sms_list_all_bypass_enabled() && 
              check_wms_list_all_messages(source, pkt, pkt_size, adspfd, usbfd)) {
      action = PACKET_BYPASS;
    } else if (source == FROM_HOST &&
               process_wms_packet(pkt, pkt_size, adspfd, usbfd)) {
      action = PACKET_BYPASS; // We bypass response
    }

    break;

  /* Here we'll handle in call audio and simulated voicecalls */
  /* REMEMBER: 0x002e -> Call indication
              0x0024 -> All Call information */
  case 9: // Voice service
    action = call_service_handler(source, pkt, pkt_size, adspfd, usbfd);
    break;

  case 16: // Location service
    logger(MSG_DEBUG, "%s Location service packet, MSG ID = %.4x \n", __func__,
           get_qmi_message_id(pkt, pkt_size));
    proxy_rt.gps_packet_stats.other++;
    break;

  default:
    break;
  }
  return action; // 1 == Pass through
}

/*
 *  is_inject_needed
 *    Notifies rmnet_proxy if there's pending data to push to the host
 *
 */

uint8_t is_inject_needed() {
  if (is_message_pending() && get_notification_source() == MSG_INTERNAL) {
    logger(MSG_DEBUG, "%s: Internal generated message\n", __func__);
    return 1;
  } else if (is_message_pending() &&
             get_notification_source() == MSG_EXTERNAL) {
    logger(MSG_DEBUG, "%s: Pending external message\n", __func__);
    return 1;
  } else if (get_call_pending()) {
    logger(MSG_DEBUG, "%s: Simulated call pending\n", __func__);
    return 1;
  } else if (get_call_simulation_mode()) {
    logger(MSG_DEBUG, "%s: In simulated call\n", __func__);
    return 1;
  } else if (at_debug_cb_message_requested() ||
             at_debug_random_cb_message_requested() ||
             at_debug_stream_cb_message_requested()) {
    logger(MSG_INFO, "We're going to fake some Cell Broadcast messages now");
    return 1;
  } else if (is_stuck_message_retrieve_pending()) {
    logger(MSG_INFO, "%s: We have pending messages to get\n", __func__);
    return 1;
  }

  return 0;
}

/*
 *  rmnet_proxy
 *    Moves QMI messages between the host and the baseband firmware
 *    It also handles routing to internal (simulated) call and message
 *    functions.
 */
void *rmnet_proxy(void *node_data) {
  struct node_pair *nodes = (struct node_pair *)node_data;
  uint8_t sourcefd, targetfd;
  size_t bytes_read, bytes_written;
  int8_t source;
  fd_set readfds;
  uint8_t buf[MAX_PACKET_SIZE];
  struct timeval tv;

  logger(MSG_INFO, "%s: Initialize RMNET proxy thread.\n", __func__);

  while (1) {
    source = -1;
    sourcefd = -1;
    targetfd = -1;
    FD_ZERO(&readfds);
    memset(buf, 0, sizeof(buf));
    FD_SET(nodes->node2.fd, &readfds); // Always add ADSP
    FD_SET(nodes->node1.fd, &readfds); // Testing: add usb always too

    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    select(MAX_FD, &readfds, NULL, NULL, &tv);
    if (FD_ISSET(nodes->node2.fd, &readfds)) {
      source = FROM_DSP;
      sourcefd = nodes->node2.fd;
      targetfd = nodes->node1.fd;
    } else if (FD_ISSET(nodes->node1.fd, &readfds)) {
      source = FROM_HOST;
      sourcefd = nodes->node1.fd;
      targetfd = nodes->node2.fd;
    } else if (is_inject_needed()) {
      source = FROM_OPENQTI;
      logger(MSG_DEBUG,
             "%s: OpenQTI needs to take over communication between the host "
             "and the baseband \n",
             __func__);
      process_simulated_packet(source, nodes->node2.fd, nodes->node1.fd);
    }

    /* We've set it all up, now we do the work */
    if (source == FROM_HOST || source == FROM_DSP) {
      bytes_read = read(sourcefd, &buf, MAX_PACKET_SIZE);
      switch (process_packet(source, buf, bytes_read, nodes->node2.fd,
                             nodes->node1.fd)) {
      case PACKET_EMPTY:
        logger(MSG_WARN, "%s Empty packet on %s, (device closed?)\n", __func__,
               (source == FROM_HOST ? "HOST" : "ADSP"));
        proxy_rt.rmnet_packet_stats.empty++;
        break;
      case PACKET_PASS_TRHU:
        logger(MSG_DEBUG, "%s Pass through\n", __func__); // MSG_DEBUG
        if (!get_transceiver_suspend_state() || source == FROM_HOST) {
          proxy_rt.rmnet_packet_stats.allowed++;
          bytes_written = write(targetfd, buf, bytes_read);
          if (bytes_written < 1) {
            logger(MSG_WARN, "%s Error writing to %s\n", __func__,
                   (source == FROM_HOST ? "ADSP" : "HOST"));
            proxy_rt.rmnet_packet_stats.failed++;
          }
        } else {
          proxy_rt.rmnet_packet_stats.discarded++;
          logger(MSG_DEBUG, "%s Data discarded from %s to %s\n", __func__,
                 (source == FROM_HOST ? "HOST" : "ADSP"),
                 (source == FROM_HOST ? "ADSP" : "HOST"));
        }
        break;
      case PACKET_FORCED_PT:
        logger(MSG_DEBUG, "%s Force pass through\n", __func__); // MSG_DEBUG
        proxy_rt.rmnet_packet_stats.allowed++;
        bytes_written = write(targetfd, buf, bytes_read);
        if (bytes_written < 1) {
          logger(MSG_WARN, "%s [FPT] Error writing to %s\n", __func__,
                 (source == FROM_HOST ? "ADSP" : "HOST"));
          proxy_rt.rmnet_packet_stats.failed++;
        }
        break;
      case PACKET_BYPASS:
        proxy_rt.rmnet_packet_stats.bypassed++;
        logger(MSG_DEBUG, "%s Packet bypassed\n", __func__);
        break;

      default:
        logger(MSG_WARN, "%s Default case\n", __func__);
        break;
      }
    }
  } // end of infinite loop

  return NULL;
}
