#pragma once

/*
  Header shim for environments where libopus headers live in /usr/include/opus.
  libopusfile includes <opus_multistream.h> (no "opus/" prefix).
*/

#include <opus/opus_multistream.h>
