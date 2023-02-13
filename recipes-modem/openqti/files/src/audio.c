#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../inc/audio.h"
#include "../inc/call.h"
#include "../inc/config.h"
#include "../inc/devices.h"
#include "../inc/helpers.h"
#include "../inc/logger.h"
#include "../inc/space_mon.h"

struct mixer *mixer;
struct pcm *pcm_tx;
struct pcm *pcm_rx;

/*  Audio runtime state:
 *    current_call_state: IDLE / CIRCUITSWITCH / VOLTE
 *    sampling_rate: 8000 / 16000 / 48000
 *    output_device: I2S / USB
 */

struct {
  uint8_t current_call_state;
  uint8_t sampling_rate;
  uint8_t output_device;
  uint8_t is_muted;
  uint8_t is_alerting;
  struct call_data calls[MAX_ACTIVE_CALLS];
  uint8_t current_active_call_id;
  uint8_t is_recording;
  uint8_t record_next_call;
} audio_runtime_state;

void set_audio_runtime_default() {
  audio_runtime_state.current_call_state = CALL_STATUS_IDLE;
  audio_runtime_state.sampling_rate = 0;
  audio_runtime_state.output_device = AUDIO_MODE_I2S;
  audio_runtime_state.is_muted = 0;
  audio_runtime_state.is_alerting = 0;
  audio_runtime_state.is_recording = 0;
  audio_runtime_state.record_next_call = 0;
  audio_runtime_state.current_active_call_id = 0;
}

int use_external_codec() {
  int fd;
  fd = open(EXTERNAL_CODEC_DETECT_PATH, O_RDONLY);
  if (fd < 0) {
    logger(MSG_INFO, "%s: RT5616 codec not detected \n", __func__);
    return 0;
  }
  close(fd);
  return 1;
}

void set_audio_mute(bool mute) {
  if (audio_runtime_state.current_call_state != CALL_STATUS_IDLE) {

    if (!mixer) {
      logger(MSG_ERROR, "error opening mixer! %s:\n", strerror(errno));
      mixer = mixer_open(SND_CTL);
      if (!mixer) {
        logger(MSG_ERROR, "error opening mixer! %s:\n", strerror(errno));
        return;
      }
    }

    if (mute) {
      logger(MSG_INFO, "%s: Muting microphone... \n", __func__);

      audio_runtime_state.is_muted = 1;
      switch (audio_runtime_state.output_device) {
      case AUDIO_MODE_I2S: // I2S Audio
        // We close all the mixers
        if (audio_runtime_state.current_call_state == 1) {
          set_mixer_ctl(mixer, RXCTL_VOICE, 0); // Capture
        } else if (audio_runtime_state.current_call_state == 2) {
          set_mixer_ctl(mixer, RXCTL_VOLTE, 0); // Capture
        }
        break;
      case AUDIO_MODE_USB: // USB Audio
        // We close all the mixers
        if (audio_runtime_state.current_call_state == 1) {
          set_mixer_ctl(mixer, AFERX_VOICE, 0); // Capture
        } else if (audio_runtime_state.current_call_state == 2) {
          set_mixer_ctl(mixer, AFERX_VOLTE, 0); // Capture
        }
        break;
      }
    } else {
      logger(MSG_INFO, "%s: Enabling microphone... \n", __func__);

      audio_runtime_state.is_muted = 0;
      switch (audio_runtime_state.output_device) {
      case AUDIO_MODE_I2S: // I2S Audio
        // We close all the mixers
        if (audio_runtime_state.current_call_state == 1) {
          set_mixer_ctl(mixer, RXCTL_VOICE, 1); // Capture
        } else if (audio_runtime_state.current_call_state == 2) {
          set_mixer_ctl(mixer, RXCTL_VOLTE, 1); // Capture
        }
        break;
      case AUDIO_MODE_USB: // USB Audio
        // We close all the mixers
        if (audio_runtime_state.current_call_state == 1) {
          set_mixer_ctl(mixer, AFERX_VOICE, 1); // Capture
        } else if (audio_runtime_state.current_call_state == 2) {
          set_mixer_ctl(mixer, AFERX_VOLTE, 1); // Capture
        }
        break;
      }
    }

  } else {
    audio_runtime_state.is_muted = 0;
    logger(MSG_WARN, "%s: Can't mute audio when there's no call in progress\n",
           __func__);
  }
}

void set_multimedia_mixer() {
  if (use_external_codec()) {
    set_mixer_ctl(mixer, AUX_PCM_MODE, 0);
    set_mixer_ctl(mixer, SEC_AUXPCM_MODE, 0);
    set_mixer_ctl(mixer, AUX_PCM_SAMPLERATE, 1);
  } else {
    set_mixer_ctl(mixer, AUX_PCM_MODE, 1);
    set_mixer_ctl(mixer, SEC_AUXPCM_MODE, 1);
    set_mixer_ctl(mixer, AUX_PCM_SAMPLERATE, 0);
  }
  set_mixer_ctl(mixer, HIFI_RX_MULTIMEDIA_MIXER, 1);
  set_mixer_ctl(mixer, HIFI_TX_MULTIMEDIA_MIXER, 1);
}

void stop_multimedia_mixer() { 
  set_mixer_ctl(mixer, HIFI_RX_MULTIMEDIA_MIXER, 0); 
  set_mixer_ctl(mixer, HIFI_TX_MULTIMEDIA_MIXER, 0); 
  }

void *play_alerting_tone() {
  char *buffer;
  int size;
  int num_read;
  FILE *file;
  struct pcm *pcm0;
  struct mixer *mymixer;
  /*
   * Ensure we loop the file while alerting
   */
  while (audio_runtime_state.is_alerting) {

    logger(MSG_INFO, "%s: Playing custom alert tone\n", __func__);
    set_multimedia_mixer();

    pcm0 = pcm_open((PCM_OUT | PCM_MONO), PCM_DEV_HIFI);
    if (pcm0 == NULL) {
      logger(MSG_INFO, "%s: Error opening %s, custom alert tone won't play\n",
             __func__, PCM_DEV_HIFI);
      return NULL;
    }

    pcm0->channels = 1;
    pcm0->flags = PCM_OUT | PCM_MONO;
    pcm0->format = PCM_FORMAT_S16_LE;
    pcm0->rate = 8000;
    pcm0->period_size = 1024;
    pcm0->period_cnt = 1;
    pcm0->buffer_size = 32768;

    file = fopen("/tmp/ring8k.wav", "r");
    if (file == NULL) {
      logger(MSG_INFO, "%s: Trying to fallback to persistent storage\n",
             __func__);
      file = fopen("/persist/ring8k.wav", "r");
    }

    if (file == NULL) {
      logger(MSG_INFO, "%s: Falling back to default tone\n", __func__);
      file = fopen("/usr/share/tones/ring8k.wav", "r");
    }

    if (file == NULL) {
      logger(MSG_ERROR, "%s: Unable to open file\n", __func__);
      pcm_close(pcm0);
      return NULL;
    }

    fseek(file, 44, SEEK_SET);

    if (set_params(pcm0, PCM_OUT)) {
      logger(MSG_ERROR, "Error setting TX Params\n");
      pcm_close(pcm0);
      return NULL;
    }

    if (!pcm0) {
      logger(MSG_ERROR, "%s: Unable to open PCM device\n", __func__);
      return NULL;
    }

    size = pcm_frames_to_bytes(pcm0, pcm_get_buffer_size(pcm0));
    buffer = malloc(size * 1024);

    if (!buffer) {
      logger(MSG_ERROR, "Unable to allocate %d bytes\n", size);
      free(buffer);
      return NULL;
    }

    do {
      num_read = fread(buffer, 1, size, file);
      if (num_read > 0) {
        if (pcm_write(pcm0, buffer, num_read)) {
          logger(MSG_ERROR, "Error playing sample\n");
          break;
        }
      }
    } while (num_read > 0 && audio_runtime_state.is_alerting);
    fclose(file);
    free(buffer);
    pcm_close(pcm0);

    mymixer = mixer_open(SND_CTL);
    if (!mymixer) {
      logger(MSG_ERROR, "error opening mixer! %s:\n", strerror(errno));
      return NULL;
    }
    set_mixer_ctl(mymixer, HIFI_RX_MULTIMEDIA_MIXER, 0);
  }

  return NULL;
}
int populate_filename(bool mode, char *filename, int sz) {
  int fnsize;
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  if (!use_persistent_logging()) {
    fnsize = snprintf(filename, sz, "/tmp/");
  } else {
    fnsize = snprintf(filename, sz, "/persist/");
  }
  if (mode) {
    fnsize += snprintf(filename + fnsize, sz - fnsize, "MANUAL-");
  } else {
    fnsize += snprintf(filename + fnsize, sz - fnsize, "AUTO-");
  }

  fnsize += snprintf(
      filename + fnsize, sz - fnsize, "%d-%02d-%02d_%02d-%02d-%02d_%s.wav",
      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min,
      tm.tm_sec,
      audio_runtime_state.calls[audio_runtime_state.current_active_call_id]
          .phone_number);
  return 0;
}
uint8_t watch_storage(char *filename) {
  uint8_t kill_recording = 0;
  uint32_t avail_space_persist = get_available_space_persist_mb();
  uint32_t avail_space_tmpfs = get_available_space_tmpfs_mb();
  logger(MSG_ERROR, "%s: Available space: Persist %i Mb, Volatile: %i Mb\n",
         __func__, avail_space_persist, avail_space_tmpfs);
  /* AVAILABLE SPACE MONITORING! */
  if (use_persistent_logging()) {
    if (avail_space_persist != -EINVAL && avail_space_persist < 2) {
      kill_recording = 1;
    } else if (avail_space_persist != -EINVAL && avail_space_persist < 3) {
      // AGGRESSIVE
      cleanup_storage(1, true, filename);
    } else if (avail_space_persist != -EINVAL && avail_space_persist < 5) {
      // NORMAL
      cleanup_storage(1, false, filename);
    } else if (avail_space_persist != -EINVAL && avail_space_persist < 10) {
      // Only log errors from now on to avoid wasting more space
      set_log_level(MSG_ERROR);
    }
  } else {
    if (avail_space_tmpfs != -EINVAL && avail_space_tmpfs < 2) {
      kill_recording = 1;
    } else if (avail_space_tmpfs != -EINVAL && avail_space_tmpfs < 3) {
      // AGGRESSIVE
      cleanup_storage(0, true, filename);
    } else if (avail_space_tmpfs != -EINVAL && avail_space_tmpfs < 5) {
      // NORMAL
      cleanup_storage(0, false, filename);
    } else if (avail_space_tmpfs != -EINVAL && avail_space_tmpfs < 10) {
      // Only log errors from now on to avoid wasting more space
      set_log_level(MSG_ERROR);
    }
  }
  return kill_recording;
}
void *incall_recording_tread() {
  char *buffer;
  size_t bufsize;
  FILE *file_rx;
  struct pcm *incall_pcm_rx;
  char filename[256];
  char filename_alt[256];
  uint8_t kill_recording = 0;
  uint8_t *sms_message = calloc(MAX_MESSAGE_SIZE, sizeof(unsigned char));
  int msgsz = 0;
  /*
   * We need to make sure persistent storage is enabled
   * Otherwise we'll fail to create the file
   */

  if (audio_runtime_state.is_recording) {
    logger(MSG_ERROR, "%s: Can't start recording thread, already recording!\n",
           __func__);
    free(sms_message);
    return NULL;
  }

  /* Sanity check */
  if (audio_runtime_state.current_call_state == CALL_STATUS_IDLE) {
    logger(MSG_ERROR, "%s: Can't record a call that doesn't exist!\n",
           __func__);
  }
  // Set the flag so we don't have multiple threads running
  audio_runtime_state.is_recording = 1;
  /* Prepare Multimedia1  */
  set_mixer_ctl(mixer, MULTIMEDIA_MIXER_TO_AUX_PCM, 1);

  /* We enable the mixers first */
  set_mixer_ctl(mixer, REC_DL, 1);
  set_mixer_ctl(mixer, REC_UL, 1);

  incall_pcm_rx = pcm_open((PCM_IN | PCM_MONO | PCM_MMAP), PCM_DEV_HIFI);
  if (incall_pcm_rx == NULL) {
    logger(MSG_INFO, "%s: Error opening %s (rx), bailing out\n", __func__,
           PCM_DEV_HIFI);
    return NULL;
  }
  incall_pcm_rx->channels = 1;
  incall_pcm_rx->flags = PCM_IN | PCM_MONO;
  incall_pcm_rx->format = PCM_FORMAT_S16_LE;
  incall_pcm_rx->rate = 8000;

  if (set_params(incall_pcm_rx, PCM_IN)) {
    logger(MSG_ERROR, "Error setting RX Params\n");
    pcm_close(incall_pcm_rx);
    return NULL;
  }
  populate_filename(1, filename_alt, 256);
  if (audio_runtime_state.record_next_call) {
    populate_filename(1, filename, 256);
  } else {
    populate_filename(0, filename, 256);
  }
  
  file_rx = fopen(filename, "w");
  struct wav_header *file_header;
  file_header = malloc(sizeof(struct wav_header));

  // Main header
  memcpy(file_header->riff_header, "RIFF", 4);
  file_header->wav_size = 0; // To be filled later
  memcpy(file_header->wave_header, "WAVE", 4);
  // Format header
  memcpy(file_header->fmt_header, "fmt ", 4);
  file_header->fmt_chunk_size = 16;               // 16bit signed
  file_header->audio_format = 1;                  // PCM
  file_header->num_channels = 1;                  // Number of channels
  file_header->sample_rate = incall_pcm_rx->rate; // 8000 Hz
  file_header->bits_per_sample = 16;
  file_header->byte_rate = (incall_pcm_rx->rate * file_header->num_channels *
                            file_header->bits_per_sample) /
                           8;
  file_header->block_align =
      (file_header->num_channels * file_header->bits_per_sample) / 8;

  memcpy(file_header->data_header, "data", 4);
  file_header->data_bytes = 0;

  /*
   * Ensure we loop the file while alerting
   */
  while (audio_runtime_state.current_call_state != CALL_STATUS_IDLE) {
    logger(MSG_INFO, "%s: Call recording started: %s\n", __func__, filename);
    if (file_rx == NULL) {
      pcm_close(incall_pcm_rx);
      logger(MSG_ERROR, "%s: Error opening files for writing\n", __func__);
      audio_runtime_state.is_recording = 0; // Clear recording flag
      audio_runtime_state.record_next_call = 0;
      return NULL;
    }

    fseek(file_rx, 44, SEEK_SET); // Skip header block so everything is aligned
    /* Frame size */
    bufsize = pcm_get_buffer_size(incall_pcm_rx);
    buffer = malloc(bufsize);

    if (!buffer) {
      logger(MSG_ERROR, "Unable to allocate %d bytes\n", bufsize);
      audio_runtime_state.is_recording = 0; // Clear recording flag
      audio_runtime_state.record_next_call = 0;
      fclose(file_rx);
      pcm_close(incall_pcm_rx);
      return NULL;
    }

    do {
      if (pcm_read(incall_pcm_rx, buffer,
                   pcm_bytes_to_frames(incall_pcm_rx,
                                       pcm_get_buffer_size(incall_pcm_rx))) ==
          0) {
        size_t fret = fwrite(buffer, bufsize, 1, file_rx);
        if (fret != 1) {
          logger(MSG_WARN, "%s: Error writing to file, fwrite returned %u\n", __func__, fret);
        }
        file_header->data_bytes += bufsize;
      } else {
        logger(MSG_ERROR, "%s: Error reading RX\n", __func__);
      }

      kill_recording = watch_storage(filename);

      if (kill_recording) {
        logger(MSG_ERROR, "%s: Killing the call due to lack of space\n",
               __func__);
        add_message_to_queue(
            (uint8_t *)"I can't keep recording the call, there's not enough "
                       "space available, sorry",
            strlen("I can't keep recording the call, there's not enough space "
                   "available, sorry"));
      }
    } while (audio_runtime_state.current_call_state != CALL_STATUS_IDLE &&
             kill_recording == 0);
    if (buffer != NULL) {
      free(buffer);
    }
    // Finish the file
    file_header->wav_size = file_header->data_bytes - 8;
    fseek(file_rx, 0, SEEK_SET); // Save the header
    fwrite(file_header, sizeof(struct wav_header), 1, file_rx);
    free(file_header);
    fclose(file_rx);
    // Close MultiMedia 1
    pcm_close(incall_pcm_rx);
    /* We disable the mixers last */
    set_mixer_ctl(mixer, REC_DL, 0);
    set_mixer_ctl(mixer, REC_UL, 0);
    set_mixer_ctl(mixer, MULTIMEDIA_MIXER_TO_AUX_PCM, 0);

    /* Check if we need to keep the file, remove it or rename it 
     * Notify the user of what we're doing
     */
    if (is_automatic_call_recording_enabled() > 0 &&
        audio_runtime_state.record_next_call) {
      if (rename(filename, filename_alt) == 0) {
        msgsz = snprintf((char *)sms_message, MAX_MESSAGE_SIZE,
                         "Recording has been saved as %s\n", filename_alt);
      } else {
        msgsz = snprintf((char *)sms_message, MAX_MESSAGE_SIZE,
                         "Error renaming the recording! (was %s)\n", filename);
      }
      add_message_to_queue(sms_message, msgsz);
    } else if (is_automatic_call_recording_enabled() == 2) {
      // Watch for errors, reporting
      if (remove(filename) == 0) {
        msgsz = snprintf((char *)sms_message, MAX_MESSAGE_SIZE,
                         "Recording has been discarded\n");
      } else {
        msgsz = snprintf(
            (char *)sms_message, MAX_MESSAGE_SIZE,
            "Error wiping the recording! Please delete it manually (%s)\n",
            filename);
      }

      add_message_to_queue(sms_message, msgsz);
    } else if (is_automatic_call_recording_enabled() == 1) {
      msgsz = snprintf((char *)sms_message, MAX_MESSAGE_SIZE,
                       "Recording has been saved as %s\n", filename);
      add_message_to_queue(sms_message, msgsz);
    }
  }

  free(sms_message);
  audio_runtime_state.is_recording = 0; // Clear recording flag
  audio_runtime_state.record_next_call = 0;
  return NULL;
}

void record_next_call(bool en) {
  if (en)
    audio_runtime_state.record_next_call = 1;
  else
    audio_runtime_state.record_next_call = 0;
}

int record_current_call() {
  int ret;
  pthread_t call_thread;
  logger(MSG_INFO, "%s: \n", __func__);
  if (audio_runtime_state.current_call_state == CALL_STATUS_IDLE) {
    return -EINVAL;
  }
  audio_runtime_state.record_next_call = 1;
  if ((ret =
           pthread_create(&call_thread, NULL, &incall_recording_tread, NULL))) {
    logger(MSG_ERROR, "%s: Error creating call recording thread\n", __func__);
  }

  return 0;
}
void set_output_device(int device) {
  logger(MSG_DEBUG, "%s: Setting audio output to %i \n", __func__, device);
  audio_runtime_state.output_device = device;
}

uint8_t get_output_device() { return audio_runtime_state.output_device; }

void set_auxpcm_sampling_rate(uint8_t mode) {
  int previous_call_state = audio_runtime_state.current_call_state;
  audio_runtime_state.sampling_rate = mode;
  // If in call, restart audio
  if (audio_runtime_state.current_call_state != CALL_STATUS_IDLE) {
    stop_audio();
    start_audio(previous_call_state);
  }
}

void handle_call_pkt(uint8_t *pkt, int sz,
                     uint8_t phone_number[MAX_PHONE_NUMBER_LENGTH],
                     size_t phone_num_len) {
  uint8_t mode = CALL_STATUS_CS;
  uint8_t ret;
  uint8_t calls_active = 0;
  pthread_t tone_thread;
  /* REDO */

  int offset = get_tlv_offset_by_id((uint8_t *)pkt, sz, TLV_CALL_INFO);
  if (offset <= 0) {
    logger(MSG_ERROR, "%s:Couldn't retrieve call metadata \n", __func__);
  } else if (offset > 0) {
    struct call_status_meta *meta;
    meta = (struct call_status_meta *)(pkt + offset);
    calls_active = meta->num_instances;
    audio_runtime_state.calls[meta->call_id].direction = meta->call_direction;
    audio_runtime_state.calls[meta->call_id].state = meta->call_state;
    audio_runtime_state.calls[meta->call_id].call_type = meta->call_mode;
    memcpy(audio_runtime_state.calls[meta->call_id].phone_number, phone_number,
           phone_num_len);
    logger(MSG_INFO, "Call ID set: ID %i\n", meta->call_id);
    switch (meta->call_direction) {
    case CALL_DIRECTION_OUTGOING:
      logger(MSG_WARN, "%s: Call %i of %i: Outgoing \n", __func__,
             meta->call_id, calls_active);
      break;
    case CALL_DIRECTION_INCOMING:
      logger(MSG_WARN, "%s: Call %i of %i: Incoming \n", __func__,
             meta->call_id, calls_active);
      break;
    default:
      logger(MSG_WARN, "%s: Call %i of %i: Unknown direction \n", __func__,
             meta->call_id, calls_active);
      break;
    }

    switch (meta->call_mode) {
    case CALL_MODE_NO_NETWORK:
    case CALL_MODE_UNKNOWN:
    case CALL_MODE_GSM:
    case CALL_MODE_UMTS:
    case CALL_MODE_UNKNOWN_ALT:
      mode = CALL_STATUS_CS;
      logger(MSG_INFO, "%s: --> Circuit Switch \n", __func__);
      break;
    case CALL_MODE_VOLTE:
      mode = CALL_STATUS_VOLTE;
      logger(MSG_INFO, "%s: --> VoLTE \n", __func__);
      break;
    default:
      logger(MSG_ERROR, "%s: --> Unknown call type \n", __func__);
      break;
    }

    switch (meta->call_state) { // Call status
    case CALL_STATE_PREPARING:
      audio_runtime_state.is_alerting = 0;
      logger(MSG_INFO, "%s: -->  Preparing call... \n", __func__);
      start_audio(mode);
      break;
    case CALL_STATE_ATTEMPT:
      audio_runtime_state.is_alerting = 0;
      logger(MSG_INFO, "%s: --> Attempt... \n", __func__);
      start_audio(mode);
      break;
    case CALL_STATE_ORIGINATING:
      audio_runtime_state.is_alerting = 0;
      logger(MSG_INFO, "%s: --> Originating... \n", __func__);
      start_audio(mode);
      break;
    case CALL_STATE_RINGING:
      audio_runtime_state.is_alerting = 0;
      logger(MSG_INFO, "%s: --> Ringing \n", __func__);
      start_audio(mode);
      break;
    case CALL_STATE_ESTABLISHED:
      audio_runtime_state.is_alerting = 0;
      logger(MSG_INFO, "%s: --> Call established, mode %i\n", __func__, mode);
      /* Workaround for the Pinephone Pro:
        Analog codec seems to get shutdown on suspend, and the modem tries to
        turn it on before power has been restored, ending up in a call with
        no audio. This force-resets audio setup when the call is established,
        so we ensure everything is set-up correctly... but we waste 200ms in
        the process */
      if (use_external_codec()) {
        stop_audio();
      }
      audio_runtime_state.current_active_call_id = meta->call_id;
      start_audio(mode);
      /* REC PATCH IN*/
      if (audio_runtime_state.record_next_call ||
          is_automatic_call_recording_enabled() > 0) {
        if ((ret = pthread_create(&tone_thread, NULL, &incall_recording_tread,
                                  NULL))) {
          logger(MSG_ERROR, "%s: Error creating call recording thread\n",
                 __func__);
        }
        audio_runtime_state.record_next_call = 0; // Clear the flag
      }

      break;
    case CALL_STATE_ON_HOLD:
      audio_runtime_state.is_alerting = 0;
      logger(MSG_INFO, "%s: --> Call is on hold \n", __func__);
      break;
    case CALL_STATE_WAITING:
      audio_runtime_state.is_alerting = 0;
      logger(MSG_INFO, "%s: --> Call waiting \n", __func__);
      break;
    case CALL_STATE_ALERTING:
      logger(MSG_INFO, "%s: --> Alerting state \n", __func__);
      if (!use_custom_alert_tone()) {
        start_audio(mode);
      } else if (use_custom_alert_tone() && !audio_runtime_state.is_alerting) {
        audio_runtime_state.is_alerting = 1;
        stop_audio();
        if ((ret = pthread_create(&tone_thread, NULL, &play_alerting_tone,
                                  NULL))) {
          logger(MSG_ERROR, "%s: Error creating dialing tone thread\n",
                 __func__);
        }
      } else {
        logger(MSG_INFO,
               "%s: Call is in alerting state but dont know what to do\n",
               __func__);
      }
      break;
    case CALL_STATE_DISCONNECTING:
    case CALL_STATE_HANGUP:
      /* Depending on the call direction/type we might
         end up here with 0 or 1 calls active...) */
      if (calls_active < 2) {
        stop_audio();
      }
      audio_runtime_state.is_alerting = 0;
      logger(MSG_INFO, "%s: -->  Disconnecting call... \n", __func__);
      break;

    default:
      audio_runtime_state.is_alerting = 0;
      logger(MSG_ERROR, "%s: Unknown call status %i \n", __func__,
             meta->call_state);
      break;
    }
    meta = NULL;
  }
}

/* Looks for the alsa control and sets its value */
int set_mixer_ctl(struct mixer *mixer, char *name, int value) {
  struct mixer_ctl *ctl;
  ctl = get_ctl(mixer, name);
  logger(MSG_DEBUG, "%s: Set %s to %i\n", __func__, name, value);
  int r;
  if (!ctl) {
    logger(MSG_ERROR, "%s: Setting %s to value %i failed, cant find control \n",
           __func__, name, value);
    return 0;
  }

  r = mixer_ctl_set_value(ctl, 1, value);
  if (r < 0) {
    logger(MSG_ERROR, "%s: Setting %s to value %i failed \n", __func__, name,
           value);
  }
  return 0;
}

/* Same as before, but just for the RX gain control */
int set_gain_ctl(struct mixer *mixer, char *name, int type, int value) {
  struct mixer_ctl *ctl;
  ctl = get_ctl(mixer, name);
  int r;
  if (!ctl) {
    logger(MSG_ERROR, "%s: Setting %s to value %i failed, cant find control \n",
           __func__, name, value);
    return 0;
  }

  r = mixer_ctl_set_gain(ctl, type, value);
  if (r < 0) {
    logger(MSG_ERROR, "%s: Setting %s to value %i failed \n", __func__, name,
           value);
  }
  return 0;
}

/* Stop mixers and pcm for previously active audio */
int stop_audio() {

  audio_runtime_state.current_active_call_id = 0;
  if (audio_runtime_state.current_call_state == CALL_STATUS_IDLE) {
    logger(MSG_ERROR, "%s: No call in progress \n", __func__);
    return 1;
  }
  if (pcm_tx == NULL || pcm_rx == NULL) {
    logger(MSG_ERROR, "%s: Invalid PCM, did it fail to open?\n", __func__);
    return 1;
  }
  if (pcm_tx->fd >= 0)
    pcm_close(pcm_tx);
  if (pcm_rx->fd >= 0)
    pcm_close(pcm_rx);

  if (!mixer) {
    logger(MSG_ERROR, "error opening mixer! %s:\n", strerror(errno), __LINE__);
    mixer = mixer_open(SND_CTL);
    if (!mixer) {
      logger(MSG_ERROR, "error opening mixer! %s:\n", strerror(errno));
      return 0;
    }
  }

  switch (audio_runtime_state.output_device) {
  case AUDIO_MODE_I2S: // I2S Audio
    // We close all the mixers
    if (audio_runtime_state.current_call_state == 1) {
      set_mixer_ctl(mixer, TXCTL_VOICE, 0); // Playback
      set_mixer_ctl(mixer, RXCTL_VOICE, 0); // Capture
    } else if (audio_runtime_state.current_call_state == 2) {
      set_mixer_ctl(mixer, TXCTL_VOLTE, 0); // Playback
      set_mixer_ctl(mixer, RXCTL_VOLTE, 0); // Capture
    }
    break;
  case AUDIO_MODE_USB: // USB Audio
    // We close all the mixers
    if (audio_runtime_state.current_call_state == 1) {
      set_mixer_ctl(mixer, AFETX_VOICE, 0); // Playback
      set_mixer_ctl(mixer, AFERX_VOICE, 0); // Capture
    } else if (audio_runtime_state.current_call_state == 2) {
      set_mixer_ctl(mixer, AFETX_VOLTE, 0); // Playback
      set_mixer_ctl(mixer, AFERX_VOLTE, 0); // Capture
    }
    break;
  }

  audio_runtime_state.current_call_state = CALL_STATUS_IDLE;
  audio_runtime_state.is_muted = 0;
  return 1;
}

/*	Setup mixers and open PCM devs
 *		type: 0: CS Voice Call
 *		      1: VoLTE Call
 * If a call wasn't actually in progress the kernel
 * will complain with ADSP_FAILED / EADSP_BUSY
 */
int start_audio(int type) {
  char pcm_device[18];

  if (audio_runtime_state.current_call_state != CALL_STATUS_IDLE &&
      type != audio_runtime_state.current_call_state) {
    logger(MSG_WARN, "%s: Switching audio profiles: 0x%.2x --> 0x%.2x\n",
           __func__, audio_runtime_state.current_call_state, type);
    stop_audio();
  } else if (audio_runtime_state.current_call_state != CALL_STATUS_IDLE &&
             type == audio_runtime_state.current_call_state) {
    logger(MSG_INFO, "%s: Not doing anything, already set.\n", __func__);
    return 0;
  }

  if (!mixer) {
    logger(MSG_ERROR, "%s: Error opening mixer!\n", __func__);
    mixer = mixer_open(SND_CTL);
    if (!mixer) {
      logger(MSG_ERROR, "error opening mixer! %s:\n", strerror(errno));
      return 0;
    }
  }

  if (use_external_codec()) {
    set_mixer_ctl(mixer, AUX_PCM_MODE, 0);
    set_mixer_ctl(mixer, SEC_AUXPCM_MODE, 0);
    set_mixer_ctl(mixer, AUX_PCM_SAMPLERATE, 1);
  } else {
    set_mixer_ctl(mixer, AUX_PCM_MODE, 1);
    set_mixer_ctl(mixer, SEC_AUXPCM_MODE, 1);
    set_mixer_ctl(mixer, AUX_PCM_SAMPLERATE, 0);
  }

  switch (audio_runtime_state.output_device) {
  case AUDIO_MODE_I2S:
    switch (type) {
    case 1:
      logger(MSG_DEBUG, "Call in progress: Circuit Switch\n");
      set_mixer_ctl(mixer, TXCTL_VOICE, 1); // Playback
      set_mixer_ctl(mixer, RXCTL_VOICE, 1); // Capture
      /* Testing:
       * Q6Voice has a control for the RX Gain of each voice type session.
       * I added this so we can know if there's any difference
       * Also check mixers.c
       */
      set_gain_ctl(mixer, RX_GAIN_LEV, type,
                   100); // Q6Voice session (Vol, session, ramp)
      strncpy(pcm_device, PCM_DEV_VOCS, PCM_DEV_SIZE);
      break;
    case 2:
      logger(MSG_DEBUG, "Call in progress: VoLTE\n");
      set_mixer_ctl(mixer, TXCTL_VOLTE, 1); // Playback
      set_mixer_ctl(mixer, RXCTL_VOLTE, 1); // Capture
      set_gain_ctl(mixer, RX_GAIN_LEV, type,
                   100); // Q6Voice session (Vol, session, ramp)
      strncpy(pcm_device, PCM_DEV_VOLTE, PCM_DEV_SIZE);
      break;
    default:
      logger(MSG_ERROR, "%s: Can't set mixers, unknown call type %i\n",
             __func__, type);
      return -EINVAL;
    }
    break;

  case AUDIO_MODE_USB: // MODE usb
    switch (type) {
    case 1:
      logger(MSG_DEBUG, "Call in progress: Circuit Switch\n");
      set_mixer_ctl(mixer, AFETX_VOICE, 1); // Playback
      set_mixer_ctl(mixer, AFERX_VOICE, 1); // Capture
      strncpy(pcm_device, PCM_DEV_VOCS, PCM_DEV_SIZE);
      break;
    case 2:
      logger(MSG_DEBUG, "Call in progress: VoLTE\n");
      set_mixer_ctl(mixer, AFETX_VOLTE, 1); // Playback
      set_mixer_ctl(mixer, AFERX_VOLTE, 1); // Capture
      strncpy(pcm_device, PCM_DEV_VOLTE, PCM_DEV_SIZE);
      break;
    default:
      logger(MSG_ERROR, "%s: Can't set mixers, unknown call type %i\n",
             __func__, type);
      return -EINVAL;
    }
    break;
  }

  pcm_rx = pcm_open((PCM_IN | PCM_MONO | PCM_MMAP), pcm_device);
  pcm_rx->channels = 1;
  pcm_rx->flags = PCM_IN | PCM_MONO;
  pcm_rx->format = PCM_FORMAT_S16_LE;

  pcm_tx = pcm_open((PCM_OUT | PCM_MONO | PCM_MMAP), pcm_device);
  pcm_tx->channels = 1;
  pcm_tx->flags = PCM_OUT | PCM_MONO;
  pcm_tx->format = PCM_FORMAT_S16_LE;

  if (audio_runtime_state.sampling_rate == 1) {
    pcm_rx->rate = 16000;
    pcm_tx->rate = 16000;
  } else if (audio_runtime_state.sampling_rate == 2) {
    pcm_rx->rate = 48000;
    pcm_tx->rate = 48000;
  } else {
    pcm_rx->rate = 8000;
    pcm_tx->rate = 8000;
  }

  logger(MSG_INFO, "Selected sampling rate: RX: %i, TX: %i\n", pcm_rx->rate,
         pcm_tx->rate);

  if (set_params(pcm_rx, PCM_IN)) {
    logger(MSG_ERROR, "Error setting RX Params\n");
    pcm_close(pcm_rx);
    return -EINVAL;
  }

  if (set_params(pcm_tx, PCM_OUT)) {
    logger(MSG_ERROR, "Error setting TX Params\n");
    pcm_close(pcm_tx);
    return -EINVAL;
  }

  if (ioctl(pcm_rx->fd, SNDRV_PCM_IOCTL_PREPARE)) {
    logger(MSG_ERROR, "Error getting RX PCM ready\n");
    pcm_close(pcm_rx);
    return -EINVAL;
  }

  if (ioctl(pcm_tx->fd, SNDRV_PCM_IOCTL_PREPARE)) {
    logger(MSG_ERROR, "Error getting TX PCM ready\n");
    pcm_close(pcm_tx);
    return -EINVAL;
  }

  if (ioctl(pcm_tx->fd, SNDRV_PCM_IOCTL_START) < 0) {
    logger(MSG_ERROR, "PCM ioctl start failed for TX\n");
    pcm_close(pcm_tx);
    return -EINVAL;
  }

  if (ioctl(pcm_rx->fd, SNDRV_PCM_IOCTL_START) < 0) {
    logger(MSG_ERROR, "PCM ioctl start failed for RX\n");
    pcm_close(pcm_rx);
  }

  if (type == CALL_STATUS_CS || type == CALL_STATUS_VOLTE) {
    audio_runtime_state.current_call_state = type;
  }

  return 0;
}

int set_audio_defaults() {
  set_auxpcm_sampling_rate(0); // Set audio mode to 8KPCM
  set_mixer_ctl(mixer, AUX_PCM_MODE, 1);
  set_mixer_ctl(mixer, SEC_AUXPCM_MODE, 1);
  set_mixer_ctl(mixer, AUX_PCM_SAMPLERATE, 0);
  return 0;
}

int set_external_codec_defaults() {
  set_auxpcm_sampling_rate(1); // Set audio mode to 16KPCM
  set_mixer_ctl(mixer, AUX_PCM_MODE, 0);
  set_mixer_ctl(mixer, SEC_AUXPCM_MODE, 0);
  set_mixer_ctl(mixer, AUX_PCM_SAMPLERATE, 1);
  return 0;
}

void setup_codec() {
  mixer = mixer_open(SND_CTL);
  if (use_external_codec()) {
    set_auxpcm_sampling_rate(1); // Set audio mode to 16KPCM
    set_mixer_ctl(mixer, AUX_PCM_MODE, 0);
    set_mixer_ctl(mixer, SEC_AUXPCM_MODE, 0);
    set_mixer_ctl(mixer, AUX_PCM_SAMPLERATE, 1);
  } else {
    set_auxpcm_sampling_rate(0); // Set audio mode to 8KPCM
    set_mixer_ctl(mixer, AUX_PCM_MODE, 1);
    set_mixer_ctl(mixer, SEC_AUXPCM_MODE, 1);
    set_mixer_ctl(mixer, AUX_PCM_SAMPLERATE, 0);
  }
}
