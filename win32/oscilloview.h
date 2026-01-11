#ifndef MYON_FMPLAYER_WIN32_OSCILLOVIEW_H_INCLUDED
#define MYON_FMPLAYER_WIN32_OSCILLOVIEW_H_INCLUDED

#include "libopna/opna.h"
#include "oscillo/oscillo.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#if defined(_MSC_VER) && !defined(__cplusplus)
#include "stdatomic.h"
#else
#include <stdatomic.h>
#endif

extern struct oscilloview {
  atomic_flag flag;
  struct oscillodata oscillodata[LIBOPNA_OSCILLO_TRACK_COUNT];
} oscilloview_g;

void oscilloview_open(HINSTANCE hinst, HWND parent, void (*closecb)(void *ptr), void *cbptr);
void oscilloview_close(void);

#endif // MYON_FMPLAYER_WIN32_OSCILLOVIEW_H_INCLUDED

