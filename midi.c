#define _DEFAULT_SOURCE  
#define _POSIX_C_SOURCE 200809L

#include "midi.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t be_u32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint16_t be_u16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t read_vlq(const uint8_t *data, size_t len, size_t *pos) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) {
    if (*pos >= len)
      return v;
    uint8_t b = data[(*pos)++];
    v = (v << 7) | (b & 0x7F);
    if (!(b & 0x80))
      break;
  }
  return v;
}

// GH/Clone Hero pitch mapping (common convention)
static int gh_map_pitch(int pitch, int *out_diff, int *out_lane) {
  if (pitch >= 60 && pitch <= 64) {
    *out_diff = 0;
    *out_lane = pitch - 60;
    return 1;
  } // Easy
  if (pitch >= 72 && pitch <= 76) {
    *out_diff = 1;
    *out_lane = pitch - 72;
    return 1;
  } // Medium
  if (pitch >= 84 && pitch <= 88) {
    *out_diff = 2;
    *out_lane = pitch - 84;
    return 1;
  } // Hard
  if (pitch >= 96 && pitch <= 100) {
    *out_diff = 3;
    *out_lane = pitch - 96;
    return 1;
  } // Expert
  return 0;
}





void nv_push(NoteVec* a, NoteOn e) {
  if (a->n == a->cap) {
    size_t nc = a->cap ? a->cap * 2 : 2048;
    NoteOn* nv = (NoteOn*)realloc(a->v, nc * sizeof(NoteOn));
    if (!nv) { perror("realloc"); exit(1); }
    a->v = nv; a->cap = nc;
  }
  a->v[a->n++] = e;
}





void tnv_push(TrackNameVec* a, TrackName e) {
  if (a->n == a->cap) {
    size_t nc = a->cap ? a->cap * 2 : 16;
    TrackName* nv = (TrackName*)realloc(a->v, nc * sizeof(TrackName));
    if (!nv) { perror("realloc"); exit(1); }
    a->v = nv; a->cap = nc;
  }
  a->v[a->n++] = e;
}

typedef struct {
  uint64_t tick;
  uint32_t us_per_qn;
} Tempo;

typedef struct {
  Tempo* v;
  size_t n, cap;
} TempoVec;

static void tv_push(TempoVec* a, Tempo e) {
  if (a->n == a->cap) {
    size_t nc = a->cap ? a->cap * 2 : 64;
    Tempo* nv = (Tempo*)realloc(a->v, nc * sizeof(Tempo));
    if (!nv) { perror("realloc"); exit(1); }
    a->v = nv; a->cap = nc;
  }
  a->v[a->n++] = e;
}

static int cmp_tempo(const void* A, const void* B) {
  const Tempo* a = (const Tempo*)A;
  const Tempo* b = (const Tempo*)B;
  if (a->tick < b->tick) return -1;
  if (a->tick > b->tick) return 1;
  // if same tick, keep the last one (stable sort not guaranteed; handle later)
  return 0;
}

static int cmp_note_tick(const void* A, const void* B) {
  const NoteOn* a = (const NoteOn*)A;
  const NoteOn* b = (const NoteOn*)B;
  if (a->tick < b->tick) return -1;
  if (a->tick > b->tick) return 1;
  return a->lane - b->lane;
}

static double tick_to_sec(uint64_t tick, const TempoVec* tv, int tpqn) {
  // tv must be sorted ascending and must have tv->v[0].tick == 0
  double sec = 0.0;
  uint64_t prev_tick = 0;
  uint32_t cur_us = tv->v[0].us_per_qn;

  size_t i = 1;
  while (i < tv->n && tv->v[i].tick <= tick) {
    uint64_t seg_end = tv->v[i].tick;
    uint64_t dt = seg_end - prev_tick;
    sec += (double)dt * ((double)cur_us / 1e6) / (double)tpqn;

    prev_tick = seg_end;
    cur_us = tv->v[i].us_per_qn;
    i++;
  }

  uint64_t dt = tick - prev_tick;
  sec += (double)dt * ((double)cur_us / 1e6) / (double)tpqn;
  return sec;
}

static void midi_collect_tempos(const uint8_t* data, size_t len, TempoVec* tempos, int* out_tpqn) {
  if (len < 14 || memcmp(data, "MThd", 4) != 0) {
    fprintf(stderr, "Not a valid MIDI (missing MThd)\n");
    exit(1);
  }
  uint32_t hdr_len = be_u32(data + 4);
  if (hdr_len < 6 || 8 + hdr_len > len) {
    fprintf(stderr, "Invalid MThd length\n");
    exit(1);
  }

  uint16_t ntrks = be_u16(data + 10);
  uint16_t div   = be_u16(data + 12);
  if (div & 0x8000) {
    fprintf(stderr, "SMPTE time division not supported.\n");
    exit(1);
  }
  int tpqn = (int)div;
  if (tpqn <= 0) { fprintf(stderr, "Invalid TPQN\n"); exit(1); }
  *out_tpqn = tpqn;

  size_t pos = 8 + hdr_len;

  for (uint16_t trk = 0; trk < ntrks; trk++) {
    if (pos + 8 > len || memcmp(data + pos, "MTrk", 4) != 0) {
      fprintf(stderr, "Missing MTrk at track %u\n", trk);
      exit(1);
    }
    uint32_t trk_len = be_u32(data + pos + 4);
    pos += 8;
    if (pos + trk_len > len) { fprintf(stderr, "Track bounds\n"); exit(1); }

    const uint8_t* tdat = data + pos;
    size_t tpos = 0;
    uint8_t running_status = 0;
    uint64_t abs_ticks = 0;

    while (tpos < trk_len) {
      uint32_t dt = read_vlq(tdat, trk_len, &tpos);
      abs_ticks += dt;
      if (tpos >= trk_len) break;

      uint8_t b = tdat[tpos++];

      if (b == 0xFF) {
        if (tpos >= trk_len) break;
        uint8_t meta_type = tdat[tpos++];
        uint32_t mlen = read_vlq(tdat, trk_len, &tpos);
        if (tpos + mlen > trk_len) break;

        if (meta_type == 0x51 && mlen == 3) {
          uint32_t us = ((uint32_t)tdat[tpos] << 16) | ((uint32_t)tdat[tpos+1] << 8) | (uint32_t)tdat[tpos+2];
          tv_push(tempos, (Tempo){ .tick = abs_ticks, .us_per_qn = us });
        }
        tpos += mlen;
        continue;
      }

      if (b == 0xF0 || b == 0xF7) {
        uint32_t slen = read_vlq(tdat, trk_len, &tpos);
        if (tpos + slen > trk_len) break;
        tpos += slen;
        running_status = 0;
        continue;
      }

      // Skip MIDI events (need to advance correctly for running status)
      uint8_t status;
      uint8_t d1;

      if (b & 0x80) {
        status = b;
        running_status = status;
        if (tpos >= trk_len) break;
        d1 = tdat[tpos++];
      } else {
        if (!running_status) continue;
        status = running_status;
        d1 = b;
      }

      uint8_t type = status & 0xF0;
      if (type == 0xC0 || type == 0xD0) {
        (void)d1; // one data byte only
      } else {
        if (tpos >= trk_len) break;
        (void)tdat[tpos++]; // d2
      }
    }

    pos += trk_len;
  }

  // Ensure tempo at tick 0
  qsort(tempos->v, tempos->n, sizeof(Tempo), cmp_tempo);
  if (tempos->n == 0 || tempos->v[0].tick != 0) {
    tv_push(tempos, (Tempo){ .tick = 0, .us_per_qn = 500000 }); // 120 BPM default
    qsort(tempos->v, tempos->n, sizeof(Tempo), cmp_tempo);
  }

  // If multiple tempos at same tick, keep the last one.
  // Compact in-place:
  size_t w = 0;
  for (size_t i = 0; i < tempos->n; ) {
    size_t j = i;
    uint64_t tick = tempos->v[i].tick;
    uint32_t last = tempos->v[i].us_per_qn;
    while (j < tempos->n && tempos->v[j].tick == tick) {
      last = tempos->v[j].us_per_qn;
      j++;
    }
    tempos->v[w++] = (Tempo){ .tick = tick, .us_per_qn = last };
    i = j;
  }
  tempos->n = w;
}

static void midi_collect_notes_ticks(const uint8_t* data, size_t len, NoteVec* notes, int tpqn) {
  (void)tpqn; // tpqn not needed here, we keep ticks only

  uint32_t hdr_len = be_u32(data + 4);
  uint16_t ntrks = be_u16(data + 10);

  size_t pos = 8 + hdr_len;

  for (uint16_t trk = 0; trk < ntrks; trk++) {
    if (pos + 8 > len || memcmp(data + pos, "MTrk", 4) != 0) {
      fprintf(stderr, "Missing MTrk at track %u\n", trk);
      exit(1);
    }
    uint32_t trk_len = be_u32(data + pos + 4);
    pos += 8;
    if (pos + trk_len > len) { fprintf(stderr, "Track bounds\n"); exit(1); }

    const uint8_t* tdat = data + pos;
    size_t tpos = 0;
    uint8_t running_status = 0;
    uint64_t abs_ticks = 0;

    while (tpos < trk_len) {
      uint32_t dt = read_vlq(tdat, trk_len, &tpos);
      abs_ticks += dt;
      if (tpos >= trk_len) break;

      uint8_t b = tdat[tpos++];

      if (b == 0xFF) {
        if (tpos >= trk_len) break;
        (void)tdat[tpos++]; // meta type
        uint32_t mlen = read_vlq(tdat, trk_len, &tpos);
        if (tpos + mlen > trk_len) break;
        tpos += mlen;
        continue;
      }

      if (b == 0xF0 || b == 0xF7) {
        uint32_t slen = read_vlq(tdat, trk_len, &tpos);
        if (tpos + slen > trk_len) break;
        tpos += slen;
        running_status = 0;
        continue;
      }

      uint8_t status;
      uint8_t d1, d2;

      if (b & 0x80) {
        status = b;
        running_status = status;
        if (tpos >= trk_len) break;
        d1 = tdat[tpos++];
      } else {
        if (!running_status) continue;
        status = running_status;
        d1 = b;
      }

      uint8_t type = status & 0xF0;
      if (type == 0xC0 || type == 0xD0) {
        continue; // 1 data byte only
      }
      if (tpos >= trk_len) break;
      d2 = tdat[tpos++];

      if (type == 0x90) {
        int pitch = (int)d1;
        int vel   = (int)d2;
        if (vel > 0) {
          int diff = -1, lane = -1;
          if (gh_map_pitch(pitch, &diff, &lane)) {
            NoteOn ev = {
              .tick = abs_ticks,
              .t_sec = 0.0, // set later
              .pitch = pitch,
              .lane = lane,
              .diff = diff,
              .vel = vel,
              .track = trk  // Store MIDI track number
            };
            nv_push(notes, ev);
          }
        }
      }
    }

    pos += trk_len;
  }

  qsort(notes->v, notes->n, sizeof(NoteOn), cmp_note_tick);
}

static void midi_extract_track_names(const uint8_t* data, size_t len, TrackNameVec* track_names) {
  if (len < 14 || memcmp(data, "MThd", 4) != 0) {
    return;
  }
  uint32_t hdr_len = be_u32(data + 4);
  uint16_t ntrks = be_u16(data + 10);

  size_t pos = 8 + hdr_len;

  for (uint16_t trk = 0; trk < ntrks; trk++) {
    if (pos + 8 > len || memcmp(data + pos, "MTrk", 4) != 0) {
      break;
    }
    uint32_t trk_len = be_u32(data + pos + 4);
    pos += 8;
    if (pos + trk_len > len) break;

    const uint8_t* tdat = data + pos;
    size_t tpos = 0;

    // Look for track name meta event (0xFF 0x03) at the beginning of the track
    while (tpos < trk_len && tpos < 512) { // Only check first 512 bytes
      uint32_t dt = read_vlq(tdat, trk_len, &tpos);
      (void)dt;
      if (tpos >= trk_len) break;

      uint8_t b = tdat[tpos++];

      if (b == 0xFF) {
        if (tpos >= trk_len) break;
        uint8_t meta_type = tdat[tpos++];
        uint32_t mlen = read_vlq(tdat, trk_len, &tpos);
        if (tpos + mlen > trk_len) break;

        // Meta event 0x03 is "Sequence/Track Name"
        if (meta_type == 0x03 && mlen > 0 && mlen < 64) {
          TrackName tn;
          tn.track_num = (int)trk;
          size_t copy_len = mlen < 63 ? mlen : 63;
          memcpy(tn.name, tdat + tpos, copy_len);
          tn.name[copy_len] = '\0';
          tnv_push(track_names, tn);
          break; // Found track name, move to next track
        }
        tpos += mlen;
        continue;
      }

      // Skip other events quickly
      break;
    }

    pos += trk_len;
  }
}

static uint8_t *slurp(const char *path, size_t *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror("fopen");
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) {
    fclose(f);
    return NULL;
  }
  uint8_t *buf = (uint8_t *)malloc((size_t)sz);
  if (!buf) {
    perror("malloc");
    fclose(f);
    return NULL;
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    perror("fread");
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *out_len = (size_t)sz;
  return buf;
}
static void parse_midi_notes_with_global_tempo(const uint8_t* data, size_t len, NoteVec* out_notes, int* out_tpqn) {
  TempoVec tempos = {0};
  int tpqn = 0;

  midi_collect_tempos(data, len, &tempos, &tpqn);
  midi_collect_notes_ticks(data, len, out_notes, tpqn);

  // Convert ticks -> seconds using global tempo map
  for (size_t i=0; i<out_notes->n; i++) {
    out_notes->v[i].t_sec = tick_to_sec(out_notes->v[i].tick, &tempos, tpqn);
  }

  free(tempos.v);
  *out_tpqn = tpqn;
}
static int cmp_note_time(const void *A, const void *B) {
  const NoteOn *a = (const NoteOn *)A;
  const NoteOn *b = (const NoteOn *)B;
  if (a->t_sec < b->t_sec)
    return -1;
  if (a->t_sec > b->t_sec)
    return 1;
  return a->lane - b->lane;
}

void build_chords(const NoteVec *notes, int diff, int track, int hopo_threshold_ticks, ChordVec *out) {
  // Collect selected diff and track notes, sort by time, group notes within epsilon into
  // chords. Use track=-1 to include all tracks.
  NoteOn *tmp = (NoteOn *)malloc(notes->n * sizeof(NoteOn));
  if (!tmp) {
    perror("malloc");
    exit(1);
  }
  size_t m = 0;
  for (size_t i = 0; i < notes->n; i++) {
    if (notes->v[i].diff == diff && (track == -1 || notes->v[i].track == track))
      tmp[m++] = notes->v[i];
  }
  if (m == 0) {
    free(tmp);
    return;
  }
  qsort(tmp, m, sizeof(NoteOn), cmp_note_time);

  const double eps = 0.0015; // 1.5ms grouping
  double cur_t = tmp[0].t_sec;
  uint8_t cur_mask = 0;
  uint64_t cur_tick = tmp[0].tick;
  int cur_min_vel = 127;  // Track minimum velocity in chord
  
  uint64_t prev_tick = 0;  // Previous chord tick for HOPO detection
  uint8_t prev_mask = 0;   // Previous chord mask

  for (size_t i = 0; i < m; i++) {
    if (fabs(tmp[i].t_sec - cur_t) <= eps) {
      cur_mask |= (uint8_t)(1u << tmp[i].lane);
      if (tmp[i].vel < cur_min_vel) {
        cur_min_vel = tmp[i].vel;
      }
    } else {
      // Emit current chord with HOPO detection
      uint8_t is_hopo = 0;
      
      // HOPO rules:
      // 1. First note can't be HOPO
      // 2. Velocity < 100 = HOPO
      // 3. OR time since previous note < hopo_threshold_ticks = HOPO
      // 4. Must be different fret than previous note
      if (out->n > 0) {  // Not first note
        uint64_t tick_delta = cur_tick - prev_tick;
        int different_fret = (cur_mask != prev_mask);
        
        if (different_fret && (cur_min_vel < 100 || tick_delta < (uint64_t)hopo_threshold_ticks)) {
          is_hopo = 1;
        }
      }
      
      cv_push(out, (Chord){.t_sec = cur_t, .mask = cur_mask, .is_hopo = is_hopo});
      
      prev_tick = cur_tick;
      prev_mask = cur_mask;
      cur_t = tmp[i].t_sec;
      cur_tick = tmp[i].tick;
      cur_mask = (uint8_t)(1u << tmp[i].lane);
      cur_min_vel = tmp[i].vel;
    }
  }
  
  // Emit final chord
  uint8_t is_hopo = 0;
  if (out->n > 0) {
    uint64_t tick_delta = cur_tick - prev_tick;
    int different_fret = (cur_mask != prev_mask);
    
    if (different_fret && (cur_min_vel < 100 || tick_delta < (uint64_t)hopo_threshold_ticks)) {
      is_hopo = 1;
    }
  }
  cv_push(out, (Chord){.t_sec = cur_t, .mask = cur_mask, .is_hopo = is_hopo});

  free(tmp);
}

void midi_parse(const char *path, NoteVec *notes, TrackNameVec *track_names) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror("fopen");
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0) {
    fclose(f);
    fprintf(stderr, "Invalid MIDI file\n");
    exit(1);
  }
  uint8_t *buf = (uint8_t *)malloc((size_t)sz);
  if (!buf) {
    perror("malloc");
    fclose(f);
    exit(1);
  }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
    perror("fread");
    free(buf);
    fclose(f);
    exit(1);
  }
  fclose(f);
  
  int tpqn;
  parse_midi_notes_with_global_tempo(buf, (size_t)sz, notes, &tpqn);
  midi_extract_track_names(buf, (size_t)sz, track_names);
  
  free(buf);
}

void cv_push(ChordVec *a, Chord e) {
  if (a->n == a->cap) {
    size_t nc = a->cap ? a->cap * 2 : 2048;
    Chord *nv = (Chord *)realloc(a->v, nc * sizeof(Chord));
    if (!nv) {
      perror("realloc");
      exit(1);
    }
    a->v = nv;
    a->cap = nc;
  }
  a->v[a->n++] = e;
}
