#ifndef MYON_FMPLAYER_WIN32_TONEVIEW_H_INCLUDED
#define MYON_FMPLAYER_WIN32_TONEVIEW_H_INCLUDED

#include "tonedata/tonedata.h"
#if defined(_MSC_VER) && !defined(__cplusplus)
#include "stdatomic.h"
#else
#include <stdatomic.h>
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern struct toneview_g {
  struct fmplayer_tonedata tonedata;
  atomic_flag flag;
} toneview_g;

void toneview_open(HINSTANCE hinst, HWND parent, void (*closecb)(void *ptr), void *cbptr);
void toneview_close(void);

#endif // MYON_FMPLAYER_WIN32_TONEVIEW_H_INCLUDED
