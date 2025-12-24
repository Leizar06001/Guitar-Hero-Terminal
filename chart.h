#ifndef CHART_H
#define CHART_H

#include "midi.h"
#include <stdint.h>

// Parse .chart file and populate note vector
// Returns 0 on success, -1 on error
int chart_parse(const char *path, NoteVec *notes, TrackNameVec *track_names);

#endif
