#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "audio.h"
#include "config.h"
#include <opus/opusfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Debug: Track callback timing
static FILE *debug_log = NULL;
static struct timespec debug_start_time = {0};
static struct timespec debug_last_log_time = {0};
static uint64_t debug_callback_count = 0;
static uint64_t debug_last_callback_count = 0;

static inline float clamp1(float x) {
  if (x < -1.0f) return -1.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

void audio_cb(void *userdata, Uint8 *stream, int len) {
  AudioEngine *e = (AudioEngine *)userdata;
  float *out = (float *)stream;
  int frames = len / (int)(sizeof(float) * e->channels);

  // Debug: Log callback timing periodically
  if (debug_log && e->started && debug_callback_count % 20 == 0) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = (now.tv_sec - debug_start_time.tv_sec) + 
                     (now.tv_nsec - debug_start_time.tv_nsec) / 1e9;
    double since_last = (now.tv_sec - debug_last_log_time.tv_sec) + 
                        (now.tv_nsec - debug_last_log_time.tv_nsec) / 1e9;
    uint64_t cb_delta = debug_callback_count - debug_last_callback_count;
    double cb_rate = (since_last > 0) ? (cb_delta / since_last) : 0.0;
    
    fprintf(debug_log, "CB#%lu: %.3fs real, %.3fs interval, rate=%.1f CB/s, frames_played=%lu (%.3fs audio)\n",
            debug_callback_count, elapsed, since_last, cb_rate, e->frames_played,
            (double)e->frames_played / (double)e->sample_rate);
    fflush(debug_log);
    
    debug_last_log_time = now;
    debug_last_callback_count = debug_callback_count;
  }
  debug_callback_count++;

  if (!e->started) {
    memset(stream, 0, (size_t)len);
    return;
  }

  for (int f = 0; f < frames; f++) {
    float L = 0.0f, R = 0.0f;
    for (int i = 0; i < e->stem_count; i++) {
      Stem *s = &e->stems[i];
      if (!s->enabled || !s->pcm)
        continue;
      
      // Very fast gain transition for immediate player feedback
      if (s->gain < s->target_gain) {
        s->gain += 0.1f;  // Very fast: ~9 frames = 0.19ms at 48kHz
        if (s->gain > s->target_gain) s->gain = s->target_gain;
      } else if (s->gain > s->target_gain) {
        s->gain -= 0.1f;
        if (s->gain < s->target_gain) s->gain = s->target_gain;
      }
      
      // Only read audio if position is valid, but ALWAYS advance position
      if (s->pos < s->frames) {
        uint64_t idx = s->pos * 2;
        L += s->pcm[idx + 0] * s->gain;
        R += s->pcm[idx + 1] * s->gain;
      }
      // Always increment position to keep all stems synchronized
      s->pos++;
    }
    out[f * 2 + 0] = clamp1(L);
    out[f * 2 + 1] = clamp1(R);
    e->frames_played++;
  }
}

double audio_time_sec(const AudioEngine *e) {
  int64_t compensated_frames = (int64_t)e->frames_played - (int64_t)(e->buffer_size * LATENCY_BUFFER_MULT);
  if (compensated_frames < 0)
    compensated_frames = 0;
  return (double)compensated_frames / (double)e->sample_rate;
}

static void stem_name_from_path(const char *path, char out[32]) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  snprintf(out, 32, "%s", base);
  char *dot = strrchr(out, '.');
  if (dot)
    *dot = '\0';
  if (strlen(out) > 20)
    out[20] = '\0';
}

void load_opus_file(const char *path, Stem *stem) {
  int err = 0;
  OggOpusFile *of = op_open_file(path, &err);
  if (!of) {
    fprintf(stderr, "opusfile: cannot open %s (err=%d)\n", path, err);
    exit(1);
  }

  const OpusHead *head = op_head(of, -1);
  if (!head) {
    fprintf(stderr, "opusfile: no head %s\n", path);
    exit(1);
  }

  int in_ch = head->channel_count;
  if (in_ch <= 0 || in_ch > 8) {
    fprintf(stderr, "opusfile: unsupported channels=%d\n", in_ch);
    exit(1);
  }

  ogg_int64_t total = op_pcm_total(of, -1);
  uint64_t total_frames_est = (total > 0) ? (uint64_t)total : 0;

  const int chunk = 120 * 48;
  float *tmp = (float *)malloc((size_t)chunk * (size_t)in_ch * sizeof(float));
  if (!tmp) {
    perror("malloc");
    exit(1);
  }

  uint64_t cap_frames = total_frames_est ? total_frames_est : (uint64_t)48000 * 180;
  float *pcm = (float *)malloc((size_t)cap_frames * 2 * sizeof(float));
  if (!pcm) {
    perror("malloc");
    exit(1);
  }
  uint64_t frames = 0;

  while (1) {
    int link = -1;
    int got = op_read_float(of, tmp, chunk * in_ch, &link);
    if (got == 0)
      break;
    if (got < 0) {
      fprintf(stderr, "opusfile: decode error %d on %s\n", got, path);
      exit(1);
    }

    if (frames + (uint64_t)got > cap_frames) {
      uint64_t nc = cap_frames ? cap_frames * 2 : (uint64_t)48000 * 60;
      while (nc < frames + (uint64_t)got)
        nc *= 2;
      float *np = (float *)realloc(pcm, (size_t)nc * 2 * sizeof(float));
      if (!np) {
        perror("realloc");
        exit(1);
      }
      pcm = np;
      cap_frames = nc;
    }

    for (int i = 0; i < got; i++) {
      float L = 0.0f, R = 0.0f;
      if (in_ch == 1) {
        L = R = tmp[i];
      } else {
        L = tmp[i * in_ch + 0];
        R = tmp[i * in_ch + 1];
      }
      pcm[(frames + (uint64_t)i) * 2 + 0] = L;
      pcm[(frames + (uint64_t)i) * 2 + 1] = R;
    }
    frames += (uint64_t)got;
  }

  op_free(of);
  free(tmp);

  stem_name_from_path(path, stem->name);
  stem->pcm = pcm;
  stem->frames = frames;
  stem->pos = 0;
  stem->gain = 1.0f;
  stem->target_gain = 1.0f;
  stem->enabled = 1;
  stem->is_player_track = 0;
}

void audio_init(AudioEngine *e, int sample_rate) {
  memset(e, 0, sizeof(*e));
  e->sample_rate = sample_rate;
  e->channels = 2;

  SDL_AudioSpec want = {0}, have = {0};
  want.freq = e->sample_rate;
  want.format = AUDIO_F32SYS;
  want.channels = (Uint8)e->channels;
  want.samples = AUDIO_BUFFER_SIZE;
  want.callback = audio_cb;
  want.userdata = e;

  e->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!e->dev) {
    fprintf(stderr, "SDL_OpenAudioDevice: %s\n", SDL_GetError());
    exit(1);
  }
  if (have.format != want.format || have.channels != want.channels) {
    fprintf(stderr, "Audio device mismatch (need float stereo)\n");
    exit(1);
  }
  e->sample_rate = have.freq;
  e->buffer_size = have.samples;
  // Keep audio paused until stems are loaded
  SDL_PauseAudioDevice(e->dev, 1);
}

void audio_start(AudioEngine *e) {
  // Initialize debug logging
  debug_log = fopen("/tmp/midifall_audio_debug.log", "w");
  if (debug_log) {
    clock_gettime(CLOCK_MONOTONIC, &debug_start_time);
    debug_last_log_time = debug_start_time;
    debug_callback_count = 0;
    debug_last_callback_count = 0;
    fprintf(debug_log, "Audio debug started\n");
    fflush(debug_log);
  }
  
  e->started = 1;
  SDL_PauseAudioDevice(e->dev, 0);
}

void audio_reset(AudioEngine *e) {
  // Pause audio callback to safely reset state
  SDL_LockAudioDevice(e->dev);
  
  // Reset all stem positions and frames_played
  for (int i = 0; i < e->stem_count; i++) {
    e->stems[i].pos = 0;
  }
  e->frames_played = 0;
  
  SDL_UnlockAudioDevice(e->dev);
  
  // Log the reset
  if (debug_log) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = (now.tv_sec - debug_start_time.tv_sec) + 
                     (now.tv_nsec - debug_start_time.tv_nsec) / 1e9;
    fprintf(debug_log, "RESET at %.3fs real time\n", elapsed);
    fflush(debug_log);
  }
}
