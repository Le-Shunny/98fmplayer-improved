#include "oscilloview.h"
#include <mmsystem.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commdlg.h>
#include <stdio.h>
#include <windowsx.h>

enum {
  TIMER_UPDATE = 1
};

enum {
  IDM_VISIBLE = 0x1000,
  IDM_LINE_COLOR,
  IDM_BG_COLOR,
  IDM_NAME,
  IDM_NAME_COLOR,
  IDM_NAME_FONT,
};

struct oscilloview oscilloview_g = {
  .flag = ATOMIC_FLAG_INIT
};

enum {
  VIEW_SAMPLES = 1024,
  VIEW_SKIP = 2,
};

struct track_config {
  bool visible;
  COLORREF line_color;
  COLORREF bg_color;
  wchar_t name[32];
  COLORREF name_color;
  LOGFONTW name_font;
  HFONT hfont;
  HPEN hpen;
  HBRUSH hbrush;
};

static struct {
  HINSTANCE hinst;
  HWND parent;
  HWND oscilloview;
  ATOM oscilloview_class;
  struct oscillodata oscillodata[LIBOPNA_OSCILLO_TRACK_COUNT];
  struct track_config track_config[LIBOPNA_OSCILLO_TRACK_COUNT];
  UINT mmtimer;
  void (*closecb)(void *ptr);
  void *cbptr;
  int clicked_track;
  COLORREF custom_colors[16];
  CHOOSECOLORW cc;
  CHOOSEFONTW cf;
} g;

/* --------------------------------------------------------- */

static void delete_track_resources(int i) {
  if (g.track_config[i].hfont) DeleteObject(g.track_config[i].hfont);
  if (g.track_config[i].hpen) DeleteObject(g.track_config[i].hpen);
  if (g.track_config[i].hbrush) DeleteObject(g.track_config[i].hbrush);
  g.track_config[i].hfont = 0;
  g.track_config[i].hpen = 0;
  g.track_config[i].hbrush = 0;
}

static void create_track_resources(int i) {
  delete_track_resources(i);
  g.track_config[i].hfont = CreateFontIndirectW(&g.track_config[i].name_font);
  g.track_config[i].hpen = CreatePen(PS_SOLID, 1, g.track_config[i].line_color);
  g.track_config[i].hbrush = CreateSolidBrush(g.track_config[i].bg_color);
}

/* --------------------------------------------------------- */

static void reset_to_defaults(void) {
  for (int i = 0; i < LIBOPNA_OSCILLO_TRACK_COUNT; i++) {
    g.track_config[i].visible = (i < 9);
    g.track_config[i].line_color = RGB(255,255,255);
    g.track_config[i].bg_color = RGB(0,0,0);
    g.track_config[i].name_color = RGB(255,255,255);

    if (i < 6) {
      swprintf(g.track_config[i].name, 32, L"FM %d", i);
    } else if (i < 9) {
      swprintf(g.track_config[i].name, 32, L"SSG %d", i - 6);
    } else {
      swprintf(g.track_config[i].name, 32, L"FM %d", i - 3);
    }

    ZeroMemory(&g.track_config[i].name_font, sizeof(LOGFONTW));
    g.track_config[i].name_font.lfHeight = -13;
    g.track_config[i].name_font.lfWeight = FW_NORMAL;
    lstrcpyW(g.track_config[i].name_font.lfFaceName, L"Segoe UI");

    create_track_resources(i);
  }

  // Reset custom colors to default
  for (int i = 0; i < 16; i++) {
    g.custom_colors[i] = RGB(255,255,255);
  }
}

/* --------------------------------------------------------- */

static void on_destroy(HWND hwnd) {
  (void)hwnd;
  g.oscilloview = NULL;
  KillTimer(hwnd, TIMER_UPDATE);

  for (int i = 0; i < LIBOPNA_OSCILLO_TRACK_COUNT; i++)
    delete_track_resources(i);

  reset_to_defaults();

  if (g.closecb) g.closecb(g.cbptr);
}

/* --------------------------------------------------------- */

static void on_timer(HWND hwnd, UINT id) {
  if (id == TIMER_UPDATE) {
    if (!atomic_flag_test_and_set_explicit(
      &oscilloview_g.flag, memory_order_acquire)) {
      memcpy(g.oscillodata,
             oscilloview_g.oscillodata,
             sizeof(oscilloview_g.oscillodata));
      atomic_flag_clear_explicit(&oscilloview_g.flag, memory_order_release);
    }
    InvalidateRect(hwnd, NULL, FALSE);
  }
}

/* --------------------------------------------------------- */

static bool on_create(HWND hwnd, const CREATESTRUCT *cs) {
  (void)cs;

  reset_to_defaults();

  g.cc.lStructSize = sizeof(g.cc);
  g.cc.lpCustColors = g.custom_colors;
  g.cc.Flags = CC_FULLOPEN | CC_RGBINIT;

  g.cf.lStructSize = sizeof(g.cf);
  g.cf.Flags = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_NOSCRIPTSEL;

  ShowWindow(hwnd, SW_SHOW);
  SetTimer(hwnd, TIMER_UPDATE, 16, NULL);
  DragAcceptFiles(hwnd, TRUE);
  return true;
}

/* --------------------------------------------------------- */

static void on_rbuttonup(HWND hwnd, int x, int y, UINT flags) {
  (void)flags;

  RECT cr;
  GetClientRect(hwnd, &cr);
  int width = cr.right / 3;
  int height = cr.bottom / 3;
  if (!width || !height) return;

  int tx = x / width;
  int ty = y / height;
  if (tx > 2) tx = 2;
  if (ty > 2) ty = 2;
  g.clicked_track = tx * 3 + ty;

  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, IDM_VISIBLE, L"Visible");
  AppendMenuW(menu, MF_SEPARATOR, 0, 0);
  AppendMenuW(menu, MF_STRING, IDM_LINE_COLOR, L"Line Color...");
  AppendMenuW(menu, MF_STRING, IDM_BG_COLOR, L"Background Color...");
  AppendMenuW(menu, MF_STRING, IDM_NAME, L"Rename...");
  AppendMenuW(menu, MF_STRING, IDM_NAME_COLOR, L"Name Color...");
  AppendMenuW(menu, MF_STRING, IDM_NAME_FONT, L"Name Font...");

  POINT pt = { x, y };
  ClientToScreen(hwnd, &pt);

  /* ✅ CRITICAL FOREGROUND FIX */
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
  PostMessage(hwnd, WM_NULL, 0, 0);

  DestroyMenu(menu);
}

static bool select_color(HWND hwnd, COLORREF *color) {
  g.cc.hwndOwner = hwnd;
  g.cc.rgbResult = *color;
  SetForegroundWindow(hwnd);
  if (ChooseColorW(&g.cc)) {
    *color = g.cc.rgbResult;
    return true;
  }
  return false;
}

static bool select_font(HWND hwnd, LOGFONTW *lf) {
  g.cf.hwndOwner = hwnd;
  g.cf.lpLogFont = lf;
  SetForegroundWindow(hwnd);
  if (ChooseFontW(&g.cf)) {
    return true;
  }
  return false;
}

static INT_PTR CALLBACK rename_dlgproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_INITDIALOG:
    SetWindowTextW(hwnd, L"Rename Track");
    SetDlgItemTextW(hwnd, 100, g.track_config[g.clicked_track].name);
    return TRUE;
  case WM_COMMAND:
    if (LOWORD(wp) == IDOK) {
      GetDlgItemTextW(hwnd, 100, g.track_config[g.clicked_track].name, 32);
      EndDialog(hwnd, IDOK);
      return TRUE;
    } else if (LOWORD(wp) == IDCANCEL) {
      EndDialog(hwnd, IDCANCEL);
      return TRUE;
    }
    break;
  }
  return FALSE;
}

extern HWND g_currentdlg;

static void on_activate(HWND hwnd, bool activate, HWND targetwnd, bool state) {
  (void)targetwnd;
  (void)state;
  if (activate) g_currentdlg = hwnd;
  else g_currentdlg = 0;
}

static void on_command(HWND hwnd, int id, HWND hwnd_ctl, UINT code) {
  (void)hwnd_ctl;
  (void)code;
  switch (id) {
  case IDM_VISIBLE:
    g.track_config[g.clicked_track].visible = !g.track_config[g.clicked_track].visible;
    InvalidateRect(hwnd, 0, FALSE);
    break;
  case IDM_LINE_COLOR:
    if (select_color(hwnd, &g.track_config[g.clicked_track].line_color)) {
      create_track_resources(g.clicked_track);
      InvalidateRect(hwnd, 0, FALSE);
    }
    break;
  case IDM_BG_COLOR:
    if (select_color(hwnd, &g.track_config[g.clicked_track].bg_color)) {
      create_track_resources(g.clicked_track);
      InvalidateRect(hwnd, 0, FALSE);
    }
    break;
  case IDM_NAME_COLOR:
    if (select_color(hwnd, &g.track_config[g.clicked_track].name_color)) {
      InvalidateRect(hwnd, 0, FALSE);
    }
    break;
  case IDM_NAME:
    {
#pragma pack(push, 2)
      struct {
        DLGTEMPLATE t;
        WORD menu, class, title;
        WORD point;
        wchar_t font[9];
        DLGITEMTEMPLATE i1;
        WORD i1class[2], i1title[1], i1extra;
        WORD pad1;
        DLGITEMTEMPLATE i2;
        WORD i2class[2], i2title[3], i2extra;
        WORD pad2;
        DLGITEMTEMPLATE i3;
        WORD i3class[2], i3title[7], i3extra;
      } d = {
        {WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER, 0, 3, 0, 0, 160, 60}, 0, 0, 0, 9, L"Segoe UI",
        {WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 10, 10, 140, 14, 100}, {0xFFFF, 0x0081}, {0}, 0, 0,
        {WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 0, 10, 35, 60, 14, IDOK}, {0xFFFF, 0x0080}, {L'O', L'K', 0}, 0, 0,
        {WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 0, 90, 35, 60, 14, IDCANCEL}, {0xFFFF, 0x0080}, {L'C', L'a', L'n', L'c', L'e', L'l', 0}, 0,
      };
#pragma pack(pop)
      SetForegroundWindow(hwnd);
      if (DialogBoxIndirectW(g.hinst, &d.t, hwnd, rename_dlgproc) == IDOK) {
        InvalidateRect(hwnd, 0, FALSE);
      }
    }
    break;
  case IDM_NAME_FONT:
    if (select_font(hwnd, &g.track_config[g.clicked_track].name_font)) {
      create_track_resources(g.clicked_track);
      InvalidateRect(hwnd, 0, FALSE);
    }
    break;
  }
}

static void draw_track(HDC dc,
                       int x, int y, int w, int h,
                       int index,
                       const struct oscillodata *data) {
  const struct track_config *conf = &g.track_config[index];
  if (!conf->visible) return;

  RECT r = {x, y, x + w, y + h};
  FillRect(dc, &r, conf->hbrush);

  SelectObject(dc, conf->hpen);
  int start = OSCILLO_SAMPLE_COUNT - VIEW_SAMPLES;
  start -= (data->offset >> OSCILLO_OFFSET_SHIFT);
  if (start < 0) start = 0;
  MoveToEx(dc, x, y + h/2.0 - (data->buf[start] / 16384.0) * h/2, 0);
  for (int i = 0; i < (VIEW_SAMPLES / VIEW_SKIP); i++) {
    LineTo(dc, (double)x + ((i)*w)/(VIEW_SAMPLES / VIEW_SKIP), y + h/2.0 - (data->buf[start + i*VIEW_SKIP] / 16384.0) * h/2);
  }

  SelectObject(dc, conf->hfont);
  SetTextColor(dc, conf->name_color);
  SetBkMode(dc, TRANSPARENT);
  TextOutW(dc, x + 2, y + 2, conf->name, lstrlenW(conf->name));
}

static void on_paint(HWND hwnd) {
  RECT cr;
  GetClientRect(hwnd, &cr);
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  HDC mdc = CreateCompatibleDC(dc);
  HBITMAP bitmap = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
  SelectObject(mdc, bitmap);

  FillRect(mdc, &cr, GetStockObject(BLACK_BRUSH));
  int width = cr.right / 3;
  int height = cr.bottom / 3;
  if (width && height) {
    for (int x = 0; x < 3; x++) {
      for (int y = 0; y < 3; y++) {
        draw_track(mdc, x*width, y*height, width, height, x*3+y, &g.oscillodata[x*3+y]);
      }
    }
  }

  BitBlt(dc, 0, 0, cr.right, cr.bottom, mdc, 0, 0, SRCCOPY);
  SelectObject(mdc, 0);
  DeleteObject(bitmap);
  DeleteDC(mdc);
  EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK wndproc(
  HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam
) {
  switch (msg) {
  HANDLE_MSG(hwnd, WM_DESTROY, on_destroy);
  HANDLE_MSG(hwnd, WM_CREATE, on_create);
  HANDLE_MSG(hwnd, WM_TIMER, on_timer);
  HANDLE_MSG(hwnd, WM_PAINT, on_paint);
  HANDLE_MSG(hwnd, WM_RBUTTONUP, on_rbuttonup);
  HANDLE_MSG(hwnd, WM_COMMAND, on_command);
  HANDLE_MSG(hwnd, WM_ACTIVATE, on_activate);
  case WM_ERASEBKGND:
    return 1;
  case WM_DROPFILES:
    return SendMessage(g.parent, msg, wParam, lParam);
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

void oscilloview_open(HINSTANCE hinst, HWND parent, void (*closecb)(void *ptr), void *cbptr) {
  g.closecb = closecb;
  g.cbptr = cbptr;
  g.hinst = hinst;
  g.parent = parent;
  if (!g.oscilloview) {
    if (!g.oscilloview_class) {
      WNDCLASS wc = {0};
      wc.style = 0;
      wc.lpfnWndProc = wndproc;
      wc.hInstance = g.hinst;
      wc.hIcon = LoadIcon(g.hinst, MAKEINTRESOURCE(1));
      wc.hCursor = LoadCursor(NULL, IDC_ARROW);
      wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
      wc.lpszClassName = L"myon_fmplayer_ym2608_oscilloviewer";
      g.oscilloview_class = RegisterClass(&wc);
    }
    if (!g.oscilloview_class) {
      MessageBox(parent, L"Cannot register oscilloviewer class", L"Error", MB_ICONSTOP);
      return;
    }
    g.oscilloview = CreateWindowEx(0,
                                     MAKEINTATOM(g.oscilloview_class),
                                     L"FMPlayer Oscilloview",
                                     WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN | WS_SIZEBOX | WS_MAXIMIZEBOX,
                                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                     NULL, 0, g.hinst, 0);
  } else {
    SetForegroundWindow(g.oscilloview);
  }
}

void oscilloview_close(void) {
  if (g.oscilloview) {
    g.closecb = 0;
    DestroyWindow(g.oscilloview);
  }
}
