#ifndef AUDIO_H
#define AUDIO_H

#include <SDL2/SDL.h>
#include <stdint.h>

typedef struct {
  char name[32];
  float *pcm;
  uint64_t frames;
  uint64_t pos;
  float gain;
  float target_gain;  // Target volume for smooth transitions
  int enabled;
  int is_player_track;  // Flag for guitar/player track
} Stem;

typedef struct {
  Stem *stems;
  int stem_count;
  int sample_rate;
  int channels;
  SDL_AudioDeviceID dev;
  uint64_t frames_played;
  int buffer_size;
  int started;
} AudioEngine;

double audio_time_sec(const AudioEngine *e);
void audio_cb(void *userdata, Uint8 *stream, int len);
void load_opus_file(const char *path, Stem *stem);
void audio_init(AudioEngine *e, int sample_rate);

#endif
