#include "oscilloview.h"
#include <mmsystem.h>
#include <shellapi.h>
#include <windowsx.h>
#include <commdlg.h>
#include <stdio.h>

enum {
  TIMER_UPDATE = 1
};

enum {
  IDM_BG_COLOR = 0x100,
  IDM_LINE_COLOR,
  IDM_NAME_COLOR,
  IDM_NAME,
  IDM_FONT,
};

struct oscilloview oscilloview_g = {
  .flag = ATOMIC_FLAG_INIT
};

enum {
  VIEW_SAMPLES = 1024,
  VIEW_SKIP = 2,
};

struct channel_config {
  COLORREF bg_color;
  COLORREF line_color;
  COLORREF name_color;
  wchar_t name[64];
  LOGFONT font;
  HFONT hfont;
  HPEN pen;
  HBRUSH brush;
};

static struct {
  HINSTANCE hinst;
  HWND parent;
  HWND oscilloview;
  ATOM oscilloview_class;
  struct oscillodata oscillodata[LIBOPNA_OSCILLO_TRACK_COUNT];
  UINT mmtimer;
  void (*closecb)(void *ptr);
  void *cbptr;
  struct channel_config channels[9];
  int clicked_channel;
} g;

/* ------------------------------------------------------------ */

static void on_destroy(HWND hwnd) {
  (void)hwnd;
  g.oscilloview = 0;
  timeKillEvent(g.mmtimer);
  for (int i = 0; i < 9; i++) {
    if (g.channels[i].pen) DeleteObject(g.channels[i].pen);
    if (g.channels[i].brush) DeleteObject(g.channels[i].brush);
    if (g.channels[i].hfont) DeleteObject(g.channels[i].hfont);
  }
  if (g.closecb) g.closecb(g.cbptr);
}

static void CALLBACK mmtimer_cb(UINT timerid, UINT msg,
                                DWORD_PTR userptr,
                                DWORD_PTR dw1, DWORD_PTR dw2) {
  (void)timerid; (void)msg; (void)userptr; (void)dw1; (void)dw2;
  PostMessage(g.oscilloview, WM_USER, 0, 0);
}

static bool on_create(HWND hwnd, const CREATESTRUCT *cs) {
  (void)cs;
  NONCLIENTMETRICS ncm;
  ncm.cbSize = sizeof(ncm);
  SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

  for (int i = 0; i < 9; i++) {
    g.channels[i].bg_color = RGB(0,0,0);
    g.channels[i].line_color = RGB(255,255,255);
    g.channels[i].name_color = RGB(255,255,255);
    swprintf(g.channels[i].name, 64,
      i < 6 ? L"FM %d" : L"SSG %d", i < 6 ? i + 1 : i - 5);
    g.channels[i].font = ncm.lfMessageFont;
    g.channels[i].hfont = CreateFontIndirect(&g.channels[i].font);
    g.channels[i].pen = CreatePen(PS_SOLID, 1, g.channels[i].line_color);
    g.channels[i].brush = CreateSolidBrush(g.channels[i].bg_color);
  }

  g.mmtimer = timeSetEvent(16, 16, mmtimer_cb, 0, TIME_PERIODIC);
  DragAcceptFiles(hwnd, TRUE);
  return true;
}

/* ------------------------------------------------------------ */

static void draw_track(HDC dc, int x, int y, int w, int h,
                       const struct oscillodata *data,
                       const struct channel_config *config) {
  RECT r = {x,y,x+w,y+h};
  FillRect(dc, &r, config->brush);

  int start = OSCILLO_SAMPLE_COUNT - VIEW_SAMPLES;
  start -= (data->offset >> OSCILLO_OFFSET_SHIFT);
  if (start < 0) start = 0;

  SelectObject(dc, config->pen);
  MoveToEx(dc, x, y + h/2, 0);
  for (int i = 0; i < VIEW_SAMPLES/VIEW_SKIP; i++) {
    LineTo(dc,
      x + (i * w) / (VIEW_SAMPLES / VIEW_SKIP),
      y + h/2 - (data->buf[start + i*VIEW_SKIP] * h) / 32768);
  }

  SelectObject(dc, config->hfont);
  SetTextColor(dc, config->name_color);
  SetBkMode(dc, TRANSPARENT);
  TextOut(dc, x+2, y+2, config->name, wcslen(config->name));
}

static void on_paint(HWND hwnd) {
  RECT cr; GetClientRect(hwnd, &cr);
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  HDC mdc = CreateCompatibleDC(dc);
  HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
  SelectObject(mdc, bmp);

  int w = cr.right / 3;
  int h = cr.bottom / 3;
  for (int x=0;x<3;x++)
    for (int y=0;y<3;y++)
      draw_track(mdc, x*w, y*h, w, h,
        &g.oscillodata[x*3+y],
        &g.channels[x*3+y]);

  BitBlt(dc,0,0,cr.right,cr.bottom,mdc,0,0,SRCCOPY);
  DeleteObject(bmp);
  DeleteDC(mdc);
  EndPaint(hwnd,&ps);
}

/* ------------------------------------------------------------ */

static LRESULT CALLBACK name_edit_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CREATE: {
    CREATESTRUCT *cs = (CREATESTRUCT*)lParam;
    HWND edit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT",
      (LPCWSTR)cs->lpCreateParams,
      WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
      10,10,180,24, hwnd,(HMENU)1,g.hinst,0);
    CreateWindow(L"BUTTON",L"OK",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
      10,45,80,25, hwnd,(HMENU)IDOK,g.hinst,0);
    CreateWindow(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE,
      110,45,80,25, hwnd,(HMENU)IDCANCEL,g.hinst,0);
    SetFocus(edit);
    SendMessage(edit, EM_SETSEL, 0, -1);
    return 0;
  }
  case WM_COMMAND:
    if (LOWORD(wParam)==IDOK) {
      GetWindowText(GetDlgItem(hwnd,1),
        g.channels[g.clicked_channel].name,64);
      DestroyWindow(hwnd);
    } else if (LOWORD(wParam)==IDCANCEL)
      DestroyWindow(hwnd);
    return 0;
  }
  return DefWindowProc(hwnd,msg,wParam,lParam);
}

static void set_channel_name(HWND owner) {
  static ATOM atom = 0;
  if (!atom) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = name_edit_wndproc;
    wc.hInstance = g.hinst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName = L"fmplayer_name_edit";
    atom = RegisterClass(&wc);
  }

  HWND dlg = CreateWindowEx(
    WS_EX_DLGMODALFRAME, MAKEINTATOM(atom), L"Channel Name",
    WS_CAPTION|WS_SYSMENU,
    CW_USEDEFAULT,CW_USEDEFAULT,215,120,
    owner,NULL,g.hinst,
    g.channels[g.clicked_channel].name);

  ShowWindow(dlg, SW_SHOW);
  MSG msg;
  while (IsWindow(dlg) && GetMessage(&msg,0,0,0)) {
    if (!IsDialogMessage(dlg,&msg)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
}

/* ------------------------------------------------------------ */

static void on_rbuttonup(HWND hwnd, int x, int y, UINT flags) {
  (void)flags;
  RECT cr; GetClientRect(hwnd,&cr);
  int w = cr.right/3, h = cr.bottom/3;
  g.clicked_channel = (x/w)*3 + (y/h);

  HMENU menu = CreatePopupMenu();
  AppendMenu(menu,MF_STRING,IDM_BG_COLOR,L"Set Background Color...");
  AppendMenu(menu,MF_STRING,IDM_LINE_COLOR,L"Set Line Color...");
  AppendMenu(menu,MF_STRING,IDM_NAME_COLOR,L"Set Name Color...");
  AppendMenu(menu,MF_SEPARATOR,0,NULL);
  AppendMenu(menu,MF_STRING,IDM_NAME,L"Set Channel Name...");
  AppendMenu(menu,MF_STRING,IDM_FONT,L"Set Font...");

  POINT pt={x,y}; ClientToScreen(hwnd,&pt);
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu,TPM_LEFTALIGN|TPM_RIGHTBUTTON,
                 pt.x,pt.y,0,hwnd,NULL);
  PostMessage(hwnd,WM_NULL,0,0);
  DestroyMenu(menu);
}

static void on_command(HWND hwnd, int id, HWND hc, UINT code) {
  (void)hc; (void)code;
  struct channel_config *c = &g.channels[g.clicked_channel];

  CHOOSECOLOR cc={0};
  static COLORREF cust[16];
  cc.lStructSize=sizeof(cc);
  cc.hwndOwner=hwnd;
  cc.lpCustColors=cust;
  cc.Flags=CC_FULLOPEN|CC_RGBINIT;

  switch(id){
  case IDM_BG_COLOR:
    cc.rgbResult=c->bg_color;
    if(ChooseColor(&cc)){
      c->bg_color=cc.rgbResult;
      DeleteObject(c->brush);
      c->brush=CreateSolidBrush(c->bg_color);
      InvalidateRect(hwnd,NULL,FALSE);
    }
    break;
  case IDM_LINE_COLOR:
    cc.rgbResult=c->line_color;
    if(ChooseColor(&cc)){
      c->line_color=cc.rgbResult;
      DeleteObject(c->pen);
      c->pen=CreatePen(PS_SOLID,1,c->line_color);
      InvalidateRect(hwnd,NULL,FALSE);
    }
    break;
  case IDM_NAME_COLOR:
    cc.rgbResult=c->name_color;
    if(ChooseColor(&cc)){
      c->name_color=cc.rgbResult;
      InvalidateRect(hwnd,NULL,FALSE);
    }
    break;
  case IDM_FONT: {
    CHOOSEFONT cf={0};
    cf.lStructSize=sizeof(cf);
    cf.hwndOwner=hwnd;
    cf.lpLogFont=&c->font;
    cf.Flags=CF_SCREENFONTS|CF_INITTOLOGFONTSTRUCT;
    if(ChooseFont(&cf)){
      DeleteObject(c->hfont);
      c->hfont=CreateFontIndirect(&c->font);
      InvalidateRect(hwnd,NULL,FALSE);
    }
  } break;
  case IDM_NAME:
    set_channel_name(hwnd);
    break;
  }
}

/* ------------------------------------------------------------ */

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  HANDLE_MSG(hwnd, WM_DESTROY, on_destroy);
  HANDLE_MSG(hwnd, WM_CREATE, on_create);
  HANDLE_MSG(hwnd, WM_PAINT, on_paint);
  HANDLE_MSG(hwnd, WM_RBUTTONUP, on_rbuttonup);
  HANDLE_MSG(hwnd, WM_COMMAND, on_command);
  case WM_ERASEBKGND: return 1;
  case WM_USER:
    if(!atomic_flag_test_and_set(&oscilloview_g.flag)){
      memcpy(g.oscillodata,oscilloview_g.oscillodata,sizeof(g.oscillodata));
      atomic_flag_clear(&oscilloview_g.flag);
    }
    InvalidateRect(hwnd,NULL,FALSE);
    return 0;
  }
  return DefWindowProc(hwnd,msg,wParam,lParam);
}

/* ------------------------------------------------------------ */

void oscilloview_open(HINSTANCE hi, HWND parent,
                      void (*cb)(void*), void *ptr) {
  g.hinst=hi; g.parent=parent; g.closecb=cb; g.cbptr=ptr;

  if(!g.oscilloview){
    if(!g.oscilloview_class){
      WNDCLASS wc={0};
      wc.lpfnWndProc=wndproc;
      wc.hInstance=hi;
      wc.hCursor=LoadCursor(NULL,IDC_ARROW);
      wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
      wc.lpszClassName=L"myon_fmplayer_ym2608_oscilloviewer";
      g.oscilloview_class=RegisterClass(&wc);
    }
    g.oscilloview=CreateWindow(
      MAKEINTATOM(g.oscilloview_class),
      L"FMPlayer Oscilloview",
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,CW_USEDEFAULT,640,480,
      parent,NULL,hi,NULL);
    ShowWindow(g.oscilloview,SW_SHOW);
  }
  SetForegroundWindow(g.oscilloview);
}

void oscilloview_close(void){
  if(g.oscilloview) DestroyWindow(g.oscilloview);
}
