#ifndef MIDI_H
#define MIDI_H

#include <stdint.h>
#include <stdlib.h>

typedef struct {
  uint64_t tick;
  double t_sec;
  int pitch;
  int lane;
  int diff;
  int vel;
  int track;
} NoteOn;

typedef struct {
  NoteOn* v;
  size_t n, cap;
} NoteVec;

typedef struct {
  int track_num;
  char name[64];
} TrackName;

typedef struct {
  TrackName* v;
  size_t n, cap;
} TrackNameVec;

typedef struct {
  double t_sec;
  uint8_t mask;
  uint8_t is_hopo;  // 1 if hammer-on/pull-off, 0 if requires strum
} Chord;

typedef struct {
  Chord *v;
  size_t n, cap;
} ChordVec;

void nv_push(NoteVec* a, NoteOn e);
void tnv_push(TrackNameVec* a, TrackName e);
void cv_push(ChordVec *a, Chord e);
void build_chords(const NoteVec *notes, int diff, int track, int hopo_threshold_ticks, ChordVec *out);
void midi_parse(const char *path, NoteVec *notes, TrackNameVec *track_names);

#endif
