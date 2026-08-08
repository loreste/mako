/*
 * mako_wsi.h — Window System Integration for Mako
 *
 * Real OS windows with software-rendered pixel buffer presentation.
 * Platforms: macOS (Cocoa/CoreGraphics), Linux (X11/Xlib), Windows (Win32/GDI).
 *
 * All functions are inline in this header — no separate .c/.m file needed.
 * The compiler links the platform frameworks automatically:
 *   macOS:   -framework Cocoa -framework CoreGraphics
 *   Linux:   -lX11
 *   Windows: -lgdi32 -luser32
 */

#ifndef MAKO_WSI_H
#define MAKO_WSI_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ── Event types ────────────────────────────────────────────────────── */

#define MAKO_WSI_EVENT_NONE     0
#define MAKO_WSI_EVENT_CLOSE    1
#define MAKO_WSI_EVENT_KEY_DOWN 2
#define MAKO_WSI_EVENT_KEY_UP   3
#define MAKO_WSI_EVENT_MOUSE_MOVE  4
#define MAKO_WSI_EVENT_MOUSE_DOWN  5
#define MAKO_WSI_EVENT_MOUSE_UP    6
#define MAKO_WSI_EVENT_RESIZE      7

typedef struct {
    int type;
    int keycode;       /* platform keycode for key events */
    int mouse_button;  /* 0=left, 1=right, 2=middle */
    int mouse_x;
    int mouse_y;
    int width;         /* new size for resize events */
    int height;
} MakoWsiEvent;

/* ── Platform handle (opaque per platform) ──────────────────────────── */

typedef struct MakoWsiWindow MakoWsiWindow;

/* ── API ────────────────────────────────────────────────────────────── */

static MakoWsiWindow *mako_wsi_open(int w, int h, const char *title);
static void           mako_wsi_close(MakoWsiWindow *win);
static int            mako_wsi_is_open(MakoWsiWindow *win);
static int            mako_wsi_poll(MakoWsiWindow *win, MakoWsiEvent *ev);
static void           mako_wsi_present(MakoWsiWindow *win, const uint32_t *pixels, int w, int h);
static void           mako_wsi_set_title(MakoWsiWindow *win, const char *title);

/* ═══════════════════════════════════════════════════════════════════════
 * macOS — Cocoa via objc_msgSend (plain C, no .m file needed)
 * ═══════════════════════════════════════════════════════════════════════ */
#if defined(__APPLE__) && defined(__MACH__)

#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <objc/NSObjCRuntime.h>

/* Avoid including CoreGraphics — it defines struct Point which collides
 * with user code. Declare only what we need. */
typedef double CGFloat;
typedef struct { CGFloat x, y; } MakoWSI_CGPoint;
typedef struct { CGFloat width, height; } MakoWSI_CGSize;
typedef struct { MakoWSI_CGPoint origin; MakoWSI_CGSize size; } MakoWSI_CGRect;

/* CGBitmapContext/CGImage functions — linked via CoreGraphics framework. */
typedef const void *CGColorSpaceRef;
typedef const void *CGContextRef;
typedef const void *CGImageRef;
extern CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void);
extern void CGColorSpaceRelease(CGColorSpaceRef);
extern CGContextRef CGBitmapContextCreate(void *, size_t, size_t, size_t, size_t, CGColorSpaceRef, uint32_t);
extern CGImageRef CGBitmapContextCreateImage(CGContextRef);
extern void CGContextRelease(CGContextRef);
extern void CGImageRelease(CGImageRef);
/* kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little */
#define MAKO_WSI_CG_BITMAP_FLAGS (5 | (2 << 12))

/* Convenience: all Cocoa calls go through objc_msgSend cast to the right sig. */
#define MSG ((id(*)(id, SEL, ...))objc_msgSend)
#define CLS(name) ((id)objc_getClass(name))
#define SEL(name) sel_registerName(name)

struct MakoWsiWindow {
    id ns_window;
    id ns_view;
    id ns_app;       /* shared application */
    int open;
    int width, height;
    /* Event queue (small ring) */
    MakoWsiEvent events[64];
    int ev_head, ev_tail;
};

static void mako_wsi_push_event(MakoWsiWindow *w, MakoWsiEvent e) {
    int next = (w->ev_tail + 1) % 64;
    if (next == w->ev_head) return; /* full — drop oldest */
    w->events[w->ev_tail] = e;
    w->ev_tail = next;
}

/* ponytail: single-window global avoids objc associated-object API issues. */
static MakoWsiWindow *mako_wsi_g_win = NULL;

/* Delegate: windowShouldClose → push close event, return NO (we handle it). */
static BOOL mako_wsi_should_close(id self, SEL _cmd, id sender) {
    (void)self; (void)_cmd; (void)sender;
    if (mako_wsi_g_win) {
        MakoWsiEvent ev = {0};
        ev.type = MAKO_WSI_EVENT_CLOSE;
        mako_wsi_push_event(mako_wsi_g_win, ev);
        mako_wsi_g_win->open = 0;
    }
    return NO;
}

static MakoWsiWindow *mako_wsi_open(int w, int h, const char *title) {
    MakoWsiWindow *win = (MakoWsiWindow *)calloc(1, sizeof(MakoWsiWindow));
    if (!win) return NULL;
    win->width = w > 0 ? w : 640;
    win->height = h > 0 ? h : 480;
    win->open = 1;

    /* Start the shared NSApplication if not already. */
    id app = MSG(CLS("NSApplication"), SEL("sharedApplication"));
    MSG(app, SEL("setActivationPolicy:"), (long)0); /* NSApplicationActivationPolicyRegular */
    win->ns_app = app;

    /* Create NSWindow with a content rect. */
    MakoWSI_CGRect frame = {{100, 100}, {(CGFloat)win->width, (CGFloat)win->height}};
    unsigned long style = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3); /* titled|closable|miniaturizable|resizable */
    id ns_win = MSG(MSG(CLS("NSWindow"), SEL("alloc")),
                    SEL("initWithContentRect:styleMask:backing:defer:"),
                    frame, style, (unsigned long)2 /* NSBackingStoreBuffered */, NO);
    win->ns_window = ns_win;

    /* Set title. */
    id ns_title = MSG(CLS("NSString"), SEL("stringWithUTF8String:"), title ? title : "Mako");
    MSG(ns_win, SEL("setTitle:"), ns_title);

    /* Create a delegate class for windowShouldClose. */
    Class del_class = objc_allocateClassPair((Class)objc_getClass("NSObject"), "MakoWsiDelegate", 0);
    if (del_class) {
        class_addMethod(del_class, SEL("windowShouldClose:"),
                        (IMP)mako_wsi_should_close, "B@:@");
        objc_registerClassPair(del_class);
    } else {
        del_class = (Class)objc_getClass("MakoWsiDelegate");
    }
    id delegate = MSG((id)del_class, SEL("new"));
    mako_wsi_g_win = win;
    MSG(ns_win, SEL("setDelegate:"), delegate);

    /* Get the content view for drawing. */
    win->ns_view = MSG(ns_win, SEL("contentView"));

    /* Show. */
    MSG(ns_win, SEL("makeKeyAndOrderFront:"), (id)nil);
    MSG(app, SEL("activateIgnoringOtherApps:"), YES);

    return win;
}

static void mako_wsi_close(MakoWsiWindow *win) {
    if (!win) return;
    if (win->ns_window) {
        MSG(win->ns_window, SEL("close"));
    }
    win->open = 0;
    free(win);
}

static int mako_wsi_is_open(MakoWsiWindow *win) {
    return win ? win->open : 0;
}

static int mako_wsi_poll(MakoWsiWindow *win, MakoWsiEvent *ev) {
    if (!win) return 0;
    /* Pump the Cocoa event loop (non-blocking). */
    id pool = MSG(MSG(CLS("NSAutoreleasePool"), SEL("alloc")), SEL("init"));
    id ns_ev;
    while ((ns_ev = MSG(win->ns_app, SEL("nextEventMatchingMask:untilDate:inMode:dequeue:"),
                        (unsigned long)~0UL, (id)nil,
                        MSG(CLS("NSString"), SEL("stringWithUTF8String:"), "kCFRunLoopDefaultMode"),
                        YES))) {
        long type = (long)MSG(ns_ev, SEL("type"));
        MakoWsiEvent we = {0};
        switch (type) {
            case 10: /* NSEventTypeKeyDown */
                we.type = MAKO_WSI_EVENT_KEY_DOWN;
                we.keycode = (int)(long)MSG(ns_ev, SEL("keyCode"));
                mako_wsi_push_event(win, we);
                break;
            case 11: /* NSEventTypeKeyUp */
                we.type = MAKO_WSI_EVENT_KEY_UP;
                we.keycode = (int)(long)MSG(ns_ev, SEL("keyCode"));
                mako_wsi_push_event(win, we);
                break;
            case 1: /* NSEventTypeLeftMouseDown */
            case 3: /* NSEventTypeRightMouseDown */
                we.type = MAKO_WSI_EVENT_MOUSE_DOWN;
                we.mouse_button = (type == 1) ? 0 : 1;
                mako_wsi_push_event(win, we);
                break;
            case 2: /* NSEventTypeLeftMouseUp */
            case 4: /* NSEventTypeRightMouseUp */
                we.type = MAKO_WSI_EVENT_MOUSE_UP;
                we.mouse_button = (type == 2) ? 0 : 1;
                mako_wsi_push_event(win, we);
                break;
            case 5: /* NSEventTypeMouseMoved */
            case 6: /* NSEventTypeLeftMouseDragged */
            case 7: /* NSEventTypeRightMouseDragged */ {
                MakoWSI_CGPoint loc = ((MakoWSI_CGPoint(*)(id, SEL))objc_msgSend)(ns_ev, SEL("locationInWindow"));
                we.type = MAKO_WSI_EVENT_MOUSE_MOVE;
                we.mouse_x = (int)loc.x;
                we.mouse_y = win->height - (int)loc.y; /* flip Y */
                mako_wsi_push_event(win, we);
                break;
            }
            default:
                break;
        }
        MSG(win->ns_app, SEL("sendEvent:"), ns_ev);
    }
    MSG(pool, SEL("drain"));
    /* Pop from our ring. */
    if (win->ev_head == win->ev_tail) {
        if (ev) memset(ev, 0, sizeof(*ev));
        return 0;
    }
    if (ev) *ev = win->events[win->ev_head];
    win->ev_head = (win->ev_head + 1) % 64;
    return 1;
}

static void mako_wsi_present(MakoWsiWindow *win, const uint32_t *pixels, int w, int h) {
    if (!win || !win->ns_view || !pixels) return;
    /* Create a CGImage from the ARGB pixel buffer and draw it into the view's context. */
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        (void *)pixels, (size_t)w, (size_t)h, 8, (size_t)(w * 4),
        cs, MAKO_WSI_CG_BITMAP_FLAGS);
    CGImageRef img = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);

    /* Draw via NSGraphicsContext on the view's bounds. */
    id ns_img = MSG(CLS("NSImage"), SEL("alloc"));
    ns_img = MSG(ns_img, SEL("initWithCGImage:size:"), img, (MakoWSI_CGSize){(CGFloat)w, (CGFloat)h});

    MSG(win->ns_view, SEL("lockFocus"));
    MakoWSI_CGRect dst = {{0, 0}, {(CGFloat)win->width, (CGFloat)win->height}};
    MakoWSI_CGRect src = {{0, 0}, {(CGFloat)w, (CGFloat)h}};
    MSG(ns_img, SEL("drawInRect:fromRect:operation:fraction:"),
        dst, src, (long)1 /* NSCompositingOperationCopy */, (CGFloat)1.0);
    MSG(win->ns_view, SEL("unlockFocus"));
    MSG(win->ns_window, SEL("flushWindow"));

    CGImageRelease(img);
    MSG(ns_img, SEL("release"));
}

static void mako_wsi_set_title(MakoWsiWindow *win, const char *title) {
    if (!win || !win->ns_window) return;
    id ns_title = MSG(CLS("NSString"), SEL("stringWithUTF8String:"), title ? title : "");
    MSG(win->ns_window, SEL("setTitle:"), ns_title);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Linux — X11 / Xlib (only when X11 headers are available)
 * ═══════════════════════════════════════════════════════════════════════ */
#elif defined(__linux__) && __has_include(<X11/Xlib.h>)

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

struct MakoWsiWindow {
    Display *dpy;
    Window xwin;
    GC gc;
    XImage *ximg;
    Atom wm_delete;
    int open;
    int width, height;
    MakoWsiEvent events[64];
    int ev_head, ev_tail;
};

static void mako_wsi_push_event(MakoWsiWindow *w, MakoWsiEvent e) {
    int next = (w->ev_tail + 1) % 64;
    if (next == w->ev_head) return;
    w->events[w->ev_tail] = e;
    w->ev_tail = next;
}

static MakoWsiWindow *mako_wsi_open(int w, int h, const char *title) {
    MakoWsiWindow *win = (MakoWsiWindow *)calloc(1, sizeof(MakoWsiWindow));
    if (!win) return NULL;
    win->width = w > 0 ? w : 640;
    win->height = h > 0 ? h : 480;

    win->dpy = XOpenDisplay(NULL);
    if (!win->dpy) { free(win); return NULL; }

    int screen = DefaultScreen(win->dpy);
    win->xwin = XCreateSimpleWindow(win->dpy, RootWindow(win->dpy, screen),
                                    100, 100, win->width, win->height, 0,
                                    BlackPixel(win->dpy, screen),
                                    BlackPixel(win->dpy, screen));

    XStoreName(win->dpy, win->xwin, title ? title : "Mako");
    XSelectInput(win->dpy, win->xwin,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                 StructureNotifyMask);

    /* Intercept window close. */
    win->wm_delete = XInternAtom(win->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(win->dpy, win->xwin, &win->wm_delete, 1);

    win->gc = XCreateGC(win->dpy, win->xwin, 0, NULL);
    XMapWindow(win->dpy, win->xwin);
    XFlush(win->dpy);
    win->open = 1;
    return win;
}

static void mako_wsi_close(MakoWsiWindow *win) {
    if (!win) return;
    if (win->ximg) { win->ximg->data = NULL; XDestroyImage(win->ximg); }
    if (win->gc) XFreeGC(win->dpy, win->gc);
    if (win->xwin) XDestroyWindow(win->dpy, win->xwin);
    if (win->dpy) XCloseDisplay(win->dpy);
    win->open = 0;
    free(win);
}

static int mako_wsi_is_open(MakoWsiWindow *win) {
    return win ? win->open : 0;
}

static int mako_wsi_poll(MakoWsiWindow *win, MakoWsiEvent *ev) {
    if (!win || !win->dpy) return 0;
    while (XPending(win->dpy)) {
        XEvent xe;
        XNextEvent(win->dpy, &xe);
        MakoWsiEvent we = {0};
        switch (xe.type) {
            case ClientMessage:
                if ((Atom)xe.xclient.data.l[0] == win->wm_delete) {
                    we.type = MAKO_WSI_EVENT_CLOSE;
                    win->open = 0;
                    mako_wsi_push_event(win, we);
                }
                break;
            case KeyPress:
                we.type = MAKO_WSI_EVENT_KEY_DOWN;
                we.keycode = xe.xkey.keycode;
                mako_wsi_push_event(win, we);
                break;
            case KeyRelease:
                we.type = MAKO_WSI_EVENT_KEY_UP;
                we.keycode = xe.xkey.keycode;
                mako_wsi_push_event(win, we);
                break;
            case ButtonPress:
                we.type = MAKO_WSI_EVENT_MOUSE_DOWN;
                we.mouse_button = xe.xbutton.button - 1;
                we.mouse_x = xe.xbutton.x;
                we.mouse_y = xe.xbutton.y;
                mako_wsi_push_event(win, we);
                break;
            case ButtonRelease:
                we.type = MAKO_WSI_EVENT_MOUSE_UP;
                we.mouse_button = xe.xbutton.button - 1;
                we.mouse_x = xe.xbutton.x;
                we.mouse_y = xe.xbutton.y;
                mako_wsi_push_event(win, we);
                break;
            case MotionNotify:
                we.type = MAKO_WSI_EVENT_MOUSE_MOVE;
                we.mouse_x = xe.xmotion.x;
                we.mouse_y = xe.xmotion.y;
                mako_wsi_push_event(win, we);
                break;
            case ConfigureNotify:
                if (xe.xconfigure.width != win->width || xe.xconfigure.height != win->height) {
                    win->width = xe.xconfigure.width;
                    win->height = xe.xconfigure.height;
                    we.type = MAKO_WSI_EVENT_RESIZE;
                    we.width = win->width;
                    we.height = win->height;
                    mako_wsi_push_event(win, we);
                }
                break;
            default: break;
        }
    }
    if (win->ev_head == win->ev_tail) {
        if (ev) memset(ev, 0, sizeof(*ev));
        return 0;
    }
    if (ev) *ev = win->events[win->ev_head];
    win->ev_head = (win->ev_head + 1) % 64;
    return 1;
}

static void mako_wsi_present(MakoWsiWindow *win, const uint32_t *pixels, int w, int h) {
    if (!win || !win->dpy || !pixels) return;
    /* XImage wrapping the ARGB buffer — no copy for matching depth. */
    int screen = DefaultScreen(win->dpy);
    int depth = DefaultDepth(win->dpy, screen);
    Visual *vis = DefaultVisual(win->dpy, screen);
    if (win->ximg) { win->ximg->data = NULL; XDestroyImage(win->ximg); win->ximg = NULL; }
    win->ximg = XCreateImage(win->dpy, vis, depth, ZPixmap, 0,
                             (char *)pixels, w, h, 32, w * 4);
    if (win->ximg) {
        XPutImage(win->dpy, win->xwin, win->gc, win->ximg, 0, 0, 0, 0, w, h);
        XFlush(win->dpy);
    }
}

static void mako_wsi_set_title(MakoWsiWindow *win, const char *title) {
    if (!win || !win->dpy) return;
    XStoreName(win->dpy, win->xwin, title ? title : "");
    XFlush(win->dpy);
}

/* ═══════════════════════════════════════════════════════════════════════
 * Windows — Win32 / GDI
 * ═══════════════════════════════════════════════════════════════════════ */
#elif defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct MakoWsiWindow {
    HWND hwnd;
    HDC hdc;
    int open;
    int width, height;
    MakoWsiEvent events[64];
    int ev_head, ev_tail;
};

static void mako_wsi_push_event(MakoWsiWindow *w, MakoWsiEvent e) {
    int next = (w->ev_tail + 1) % 64;
    if (next == w->ev_head) return;
    w->events[w->ev_tail] = e;
    w->ev_tail = next;
}

/* Store window pointer via GWLP_USERDATA. */
static LRESULT CALLBACK mako_wsi_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MakoWsiWindow *win = (MakoWsiWindow *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    MakoWsiEvent ev = {0};
    switch (msg) {
        case WM_CLOSE:
            if (win) { ev.type = MAKO_WSI_EVENT_CLOSE; mako_wsi_push_event(win, ev); win->open = 0; }
            return 0;
        case WM_KEYDOWN:
            if (win) { ev.type = MAKO_WSI_EVENT_KEY_DOWN; ev.keycode = (int)wp; mako_wsi_push_event(win, ev); }
            return 0;
        case WM_KEYUP:
            if (win) { ev.type = MAKO_WSI_EVENT_KEY_UP; ev.keycode = (int)wp; mako_wsi_push_event(win, ev); }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            if (win) {
                ev.type = MAKO_WSI_EVENT_MOUSE_DOWN;
                ev.mouse_button = (msg == WM_LBUTTONDOWN) ? 0 : (msg == WM_RBUTTONDOWN) ? 1 : 2;
                ev.mouse_x = LOWORD(lp); ev.mouse_y = HIWORD(lp);
                mako_wsi_push_event(win, ev);
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            if (win) {
                ev.type = MAKO_WSI_EVENT_MOUSE_UP;
                ev.mouse_button = (msg == WM_LBUTTONUP) ? 0 : (msg == WM_RBUTTONUP) ? 1 : 2;
                ev.mouse_x = LOWORD(lp); ev.mouse_y = HIWORD(lp);
                mako_wsi_push_event(win, ev);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (win) {
                ev.type = MAKO_WSI_EVENT_MOUSE_MOVE;
                ev.mouse_x = LOWORD(lp); ev.mouse_y = HIWORD(lp);
                mako_wsi_push_event(win, ev);
            }
            return 0;
        case WM_SIZE:
            if (win) {
                win->width = LOWORD(lp); win->height = HIWORD(lp);
                ev.type = MAKO_WSI_EVENT_RESIZE;
                ev.width = win->width; ev.height = win->height;
                mako_wsi_push_event(win, ev);
            }
            return 0;
        default:
            return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static MakoWsiWindow *mako_wsi_open(int w, int h, const char *title) {
    MakoWsiWindow *win = (MakoWsiWindow *)calloc(1, sizeof(MakoWsiWindow));
    if (!win) return NULL;
    win->width = w > 0 ? w : 640;
    win->height = h > 0 ? h : 480;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = mako_wsi_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "MakoWSI";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    RECT r = {0, 0, win->width, win->height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    win->hwnd = CreateWindowA("MakoWSI", title ? title : "Mako",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top,
                              NULL, NULL, wc.hInstance, NULL);
    SetWindowLongPtr(win->hwnd, GWLP_USERDATA, (LONG_PTR)win);
    win->hdc = GetDC(win->hwnd);
    win->open = 1;
    return win;
}

static void mako_wsi_close(MakoWsiWindow *win) {
    if (!win) return;
    if (win->hdc) ReleaseDC(win->hwnd, win->hdc);
    if (win->hwnd) DestroyWindow(win->hwnd);
    win->open = 0;
    free(win);
}

static int mako_wsi_is_open(MakoWsiWindow *win) {
    return win ? win->open : 0;
}

static int mako_wsi_poll(MakoWsiWindow *win, MakoWsiEvent *ev) {
    if (!win) return 0;
    MSG msg;
    while (PeekMessageA(&msg, win->hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    if (win->ev_head == win->ev_tail) {
        if (ev) memset(ev, 0, sizeof(*ev));
        return 0;
    }
    if (ev) *ev = win->events[win->ev_head];
    win->ev_head = (win->ev_head + 1) % 64;
    return 1;
}

static void mako_wsi_present(MakoWsiWindow *win, const uint32_t *pixels, int w, int h) {
    if (!win || !win->hdc || !pixels) return;
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(win->hdc, 0, 0, win->width, win->height,
                  0, 0, w, h, pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

static void mako_wsi_set_title(MakoWsiWindow *win, const char *title) {
    if (!win || !win->hwnd) return;
    SetWindowTextA(win->hwnd, title ? title : "");
}

/* ═══════════════════════════════════════════════════════════════════════
 * Unsupported platform — stub (same as before)
 * ═══════════════════════════════════════════════════════════════════════ */
#else

struct MakoWsiWindow { int open; int width, height; };

static MakoWsiWindow *mako_wsi_open(int w, int h, const char *title) {
    (void)title;
    MakoWsiWindow *win = (MakoWsiWindow *)calloc(1, sizeof(MakoWsiWindow));
    if (win) { win->open = 1; win->width = w; win->height = h; }
    return win;
}
static void mako_wsi_close(MakoWsiWindow *win) { if (win) { win->open = 0; free(win); } }
static int  mako_wsi_is_open(MakoWsiWindow *win) { return win ? win->open : 0; }
static int  mako_wsi_poll(MakoWsiWindow *win, MakoWsiEvent *ev) { (void)win; if(ev) memset(ev,0,sizeof(*ev)); return 0; }
static void mako_wsi_present(MakoWsiWindow *win, const uint32_t *p, int w, int h) { (void)win;(void)p;(void)w;(void)h; }
static void mako_wsi_set_title(MakoWsiWindow *win, const char *t) { (void)win;(void)t; }

#endif /* platform */

#endif /* MAKO_WSI_H */
