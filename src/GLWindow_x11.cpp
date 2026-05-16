// GLWindow implementation for Linux using raw X11+GLX (no SDL2 dependency)
#ifndef _WIN32

#include "basetypes.h"
#include <mutex>
#include <GL/glew.h>
#include <GL/gl.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/glx.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "GLWindow.h"
#include "NuanceMain.h"
#include "NuanceUI.h"
#include "video.h"
#include "joystick.h"

extern bool Load(const char* file);
extern vidTexInfo videoTexInfo;
extern std::mutex gfx_lock;

static Display* xDisplay = nullptr;
static Window xWindow = 0;
static GLXContext glxContext = nullptr;
static Atom wmDeleteMessage;

// XDND (drag-and-drop) atoms and per-drag state. Filled in CreateWindowGL.
static Atom xdndAware, xdndEnter, xdndPosition, xdndStatus, xdndDrop, xdndLeave;
static Atom xdndFinished, xdndSelection, xdndActionCopy, xdndUriList, xdndTargetProp;
static Window  xdndSourceWindow = 0;
static long    xdndSourceVersion = 0;
static bool    xdndWillAccept   = false;

GLWindow::GLWindow()
{
  bFullScreen = false;
  bVisible = false;
  keyDownHandler = nullptr;
  keyUpHandler = nullptr;
  paintHandler = nullptr;
  resizeHandler = nullptr;
  applyControllerState = nullptr;
  inputManager = nullptr;
  clientWidth = VIDEO_WIDTH;
  clientHeight = VIDEO_HEIGHT;
  fullScreenWidth = 1920;
  fullScreenHeight = 1080;
  x = 100; y = 100;
  width = clientWidth; height = clientHeight;
  windowStyle = 0; windowExtendedStyle = 0;
  fullScreenWindowStyle = 0; fullScreenWindowExtendedStyle = 0;
  threadHandle = 0; threadID = 0;
  restoreWidth = clientWidth; restoreHeight = clientHeight;
  restoreX = x; restoreY = y;
  hInstance = nullptr; hWnd = nullptr; hDC = nullptr; hRC = nullptr;
}

GLWindow::~GLWindow() {}

void GLWindow::UpdateRestoreValues()
{
  restoreX = x; restoreY = y;
  restoreWidth = clientWidth; restoreHeight = clientHeight;
}

void GLWindow::OnResize(int _width, int _height)
{
  clientWidth = _width;
  clientHeight = _height;
  if (!bFullScreen) UpdateRestoreValues();
}

void GLWindow::ToggleFullscreen()
{
  // Simple fullscreen toggle via _NET_WM_STATE
  if (!xDisplay || !xWindow) return;
  bFullScreen = !bFullScreen;

  XEvent ev = {};
  ev.type = ClientMessage;
  ev.xclient.window = xWindow;
  ev.xclient.message_type = XInternAtom(xDisplay, "_NET_WM_STATE", False);
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = bFullScreen ? 1 : 0; // _NET_WM_STATE_ADD / REMOVE
  ev.xclient.data.l[1] = XInternAtom(xDisplay, "_NET_WM_STATE_FULLSCREEN", False);
  ev.xclient.data.l[2] = 0;
  XSendEvent(xDisplay, DefaultRootWindow(xDisplay), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
  XFlush(xDisplay);

  // Re-grab keyboard focus after fullscreen toggle
  if (bFullScreen) {
    XGrabKeyboard(xDisplay, xWindow, True, GrabModeAsync, GrabModeAsync, CurrentTime);
  } else {
    XUngrabKeyboard(xDisplay, CurrentTime);
  }
  XSetInputFocus(xDisplay, xWindow, RevertToParent, CurrentTime);
  XFlush(xDisplay);
}

bool GLWindow::ChangeScreenResolution(int, int) { return true; }

bool GLWindow::CreateWindowGL()
{
  xDisplay = XOpenDisplay(nullptr);
  if (!xDisplay) {
    fprintf(stderr, "Cannot open X display\n");
    return false;
  }

  int screen = DefaultScreen(xDisplay);

  int glxAttribs[] = {
    GLX_RGBA,
    GLX_DOUBLEBUFFER,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    None
  };

  XVisualInfo* vi = glXChooseVisual(xDisplay, screen, glxAttribs);
  if (!vi) {
    fprintf(stderr, "glXChooseVisual failed\n");
    return false;
  }

  Colormap cmap = XCreateColormap(xDisplay, RootWindow(xDisplay, vi->screen), vi->visual, AllocNone);

  XSetWindowAttributes swa = {};
  swa.colormap = cmap;
  swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | StructureNotifyMask | FocusChangeMask
    | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;

  xWindow = XCreateWindow(xDisplay, RootWindow(xDisplay, vi->screen),
    100, 100, clientWidth, clientHeight, 0,
    vi->depth, InputOutput, vi->visual,
    CWColormap | CWEventMask, &swa);

  XStoreName(xDisplay, xWindow, "Nuance (F1 to toggle fullscreen)");

  wmDeleteMessage = XInternAtom(xDisplay, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(xDisplay, xWindow, &wmDeleteMessage, 1);

  // XDND drag-and-drop: cache atoms and advertise version 5 to drop sources.
  xdndAware       = XInternAtom(xDisplay, "XdndAware",       False);
  xdndEnter       = XInternAtom(xDisplay, "XdndEnter",       False);
  xdndPosition    = XInternAtom(xDisplay, "XdndPosition",    False);
  xdndStatus      = XInternAtom(xDisplay, "XdndStatus",      False);
  xdndDrop        = XInternAtom(xDisplay, "XdndDrop",        False);
  xdndLeave       = XInternAtom(xDisplay, "XdndLeave",       False);
  xdndFinished    = XInternAtom(xDisplay, "XdndFinished",    False);
  xdndSelection   = XInternAtom(xDisplay, "XdndSelection",   False);
  xdndActionCopy  = XInternAtom(xDisplay, "XdndActionCopy",  False);
  xdndUriList     = XInternAtom(xDisplay, "text/uri-list",   False);
  xdndTargetProp  = XInternAtom(xDisplay, "NUANCE_XDND",     False);
  const long xdndVersion = 5;
  XChangeProperty(xDisplay, xWindow, xdndAware, XA_ATOM, 32,
                  PropModeReplace, (unsigned char*)&xdndVersion, 1);

  XMapWindow(xDisplay, xWindow);

  glxContext = glXCreateContext(xDisplay, vi, nullptr, GL_TRUE);
  XFree(vi);

  if (!glxContext) {
    fprintf(stderr, "glXCreateContext failed\n");
    return false;
  }

  glXMakeCurrent(xDisplay, xWindow, glxContext);

  fprintf(stderr, "GL Vendor: %s\nGL Renderer: %s\nGL Version: %s\n",
    glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));

  // Disable vsync so glXSwapBuffers doesn't block the main loop waiting
  // for a hardware vblank. With vsync on, intel HD blocks each frame
  // at 60Hz which throttles the NUON emulator's main loop to <2 Hz
  // effective video-tick rate (since the loop is structured around
  // SwapBuffers, not around its own timer). The emulator's own
  // soft-timer at timer_rate[2] handles pacing. Software rendering
  // (llvmpipe) doesn't vsync, which is why LIBGL_ALWAYS_SOFTWARE=1
  // "fixes" the apparent IS3 boot regression.
  typedef int (*PFN_glXSwapIntervalEXT)(Display*, GLXDrawable, int);
  typedef int (*PFN_glXSwapIntervalMESA)(unsigned int);
  typedef int (*PFN_glXSwapIntervalSGI)(int);
  PFN_glXSwapIntervalEXT pglXSwapIntervalEXT =
      (PFN_glXSwapIntervalEXT)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalEXT");
  PFN_glXSwapIntervalMESA pglXSwapIntervalMESA =
      (PFN_glXSwapIntervalMESA)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalMESA");
  PFN_glXSwapIntervalSGI pglXSwapIntervalSGI =
      (PFN_glXSwapIntervalSGI)glXGetProcAddressARB((const GLubyte*)"glXSwapIntervalSGI");
  if (pglXSwapIntervalEXT) {
    pglXSwapIntervalEXT(xDisplay, xWindow, 0);
    fprintf(stderr, "GL: vsync disabled via glXSwapIntervalEXT\n");
  } else if (pglXSwapIntervalMESA) {
    pglXSwapIntervalMESA(0);
    fprintf(stderr, "GL: vsync disabled via glXSwapIntervalMESA\n");
  } else if (pglXSwapIntervalSGI) {
    pglXSwapIntervalSGI(0);
    fprintf(stderr, "GL: vsync disabled via glXSwapIntervalSGI\n");
  } else {
    fprintf(stderr, "GL: warning, no glXSwapInterval available — vsync may throttle main loop\n");
  }

  glewExperimental = GL_TRUE;
  GLenum err = glewInit();
  if (err != GLEW_OK) {
    fprintf(stderr, "glewInit failed (code %d): %s\n", (int)err, glewGetErrorString(err));
    fprintf(stderr, "Continuing without GLEW...\n");
  } else {
    fprintf(stderr, "GLEW initialized OK\n");
  }
  fflush(stderr);

  NuanceUI_Init(xDisplay, xWindow);

  inputManager = InputManager::Create();
  if (!inputManager->Init(nullptr)) {
    delete inputManager;
    inputManager = nullptr;
  }

  bVisible = true;
  return true;
}

void GLWindow::CleanUp()
{
  NuanceUI_Shutdown();
  delete inputManager;
  inputManager = nullptr;

  if (glxContext) {
    glXMakeCurrent(xDisplay, None, nullptr);
    glXDestroyContext(xDisplay, glxContext);
    glxContext = nullptr;
  }
  if (xWindow) { XDestroyWindow(xDisplay, xWindow); xWindow = 0; }
  if (xDisplay) { XCloseDisplay(xDisplay); xDisplay = nullptr; }
}

bool GLWindow::RegisterWindowClass() { return true; }

bool GLWindow::Create()
{
  GLWindowMain(this);
  return true;
}

static int XKeyToVKey(KeySym key)
{
  if (key >= XK_a && key <= XK_z) return 'A' + (key - XK_a);
  if (key >= XK_A && key <= XK_Z) return 'A' + (key - XK_A);
  if (key >= XK_0 && key <= XK_9) return '0' + (key - XK_0);
  switch (key) {
    case XK_Up: return VK_UP;
    case XK_Down: return VK_DOWN;
    case XK_Left: return VK_LEFT;
    case XK_Right: return VK_RIGHT;
    case XK_Return: return VK_RETURN;
    case XK_space: return VK_SPACE;
    case XK_Escape: return VK_ESCAPE;
    case XK_F1: return VK_F1;
    case XK_F12: return VK_F12;
    default: return key & 0xFF;
  }
}

// URL-decode in place (handles %XX escapes). Used for parsing text/uri-list payloads.
static void UrlDecodeInPlace(std::string& s)
{
  size_t w = 0;
  for (size_t r = 0; r < s.size(); ) {
    if (s[r] == '%' && r + 2 < s.size()) {
      auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
      };
      const int hi = hex(s[r+1]), lo = hex(s[r+2]);
      if (hi >= 0 && lo >= 0) { s[w++] = (char)((hi << 4) | lo); r += 3; continue; }
    }
    s[w++] = s[r++];
  }
  s.resize(w);
}

// Send an XdndStatus reply telling the source whether we accept and which action.
static void XdndSendStatus(Window source, bool accept)
{
  XEvent ev = {};
  ev.xclient.type = ClientMessage;
  ev.xclient.display = xDisplay;
  ev.xclient.window = source;
  ev.xclient.message_type = xdndStatus;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = (long)xWindow;
  ev.xclient.data.l[1] = accept ? 1 : 0; // bit 0: will accept; bit 1: want subsequent positions
  ev.xclient.data.l[2] = 0; // x,y of safe rectangle (none = whole window)
  ev.xclient.data.l[3] = 0; // w,h
  ev.xclient.data.l[4] = accept ? (long)xdndActionCopy : 0;
  XSendEvent(xDisplay, source, False, NoEventMask, &ev);
  XFlush(xDisplay);
}

// Send XdndFinished after we're done processing the drop.
static void XdndSendFinished(Window source, bool accepted)
{
  XEvent ev = {};
  ev.xclient.type = ClientMessage;
  ev.xclient.display = xDisplay;
  ev.xclient.window = source;
  ev.xclient.message_type = xdndFinished;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = (long)xWindow;
  ev.xclient.data.l[1] = accepted ? 1 : 0;
  ev.xclient.data.l[2] = accepted ? (long)xdndActionCopy : 0;
  XSendEvent(xDisplay, source, False, NoEventMask, &ev);
  XFlush(xDisplay);
}

// Pull the dropped data out of our XDND target property and route it through Load().
static void XdndHandleSelectionNotify()
{
  Atom actualType = None;
  int actualFormat = 0;
  unsigned long nItems = 0, bytesAfter = 0;
  unsigned char* data = nullptr;

  if (XGetWindowProperty(xDisplay, xWindow, xdndTargetProp, 0, 65536, True,
                         AnyPropertyType, &actualType, &actualFormat,
                         &nItems, &bytesAfter, &data) == Success && data) {
    // text/uri-list: lines separated by CRLF, each "file:///path%20with%20escapes".
    // Take the first non-empty, non-comment line.
    std::string blob((const char*)data, nItems);
    XFree(data);

    size_t lineEnd = blob.find('\n');
    std::string first = (lineEnd == std::string::npos) ? blob : blob.substr(0, lineEnd);
    while (!first.empty() && (first.back() == '\r' || first.back() == '\n'))
      first.pop_back();

    if (!first.empty() && first[0] != '#') {
      const char* kFilePrefix = "file://";
      if (first.compare(0, strlen(kFilePrefix), kFilePrefix) == 0)
        first.erase(0, strlen(kFilePrefix));
      UrlDecodeInPlace(first);
      if (!first.empty()) Load(first.c_str());
    }
  }

  if (xdndSourceWindow) {
    XdndSendFinished(xdndSourceWindow, xdndWillAccept);
    xdndSourceWindow = 0;
    xdndWillAccept = false;
  }
}

void GLWindow::MessagePump()
{
  if (inputManager) inputManager->UpdateState(applyControllerState, nullptr, nullptr);

  if (!xDisplay) return;

  while (XPending(xDisplay)) {
    XEvent ev;
    XNextEvent(xDisplay, &ev);

    // Let ImGui process events first; if UI captures input, skip game input
    bool uiCaptured = NuanceUI_ProcessEvent(&ev);

    switch (ev.type) {
      case ConfigureNotify:
      {
        int newW = ev.xconfigure.width;
        int newH = ev.xconfigure.height;
        if (newW != clientWidth || newH != clientHeight) {
          videoTexInfo.bUpdateDisplayList = true;
          videoTexInfo.bUpdateTextureStates = true;
          OnResize(newW, newH);
          if (resizeHandler) resizeHandler((uint16)newW, (uint16)newH);
        }
        break;
      }
      case KeyPress:
      {
        if (uiCaptured) break;
        KeySym key = XLookupKeysym(&ev.xkey, 0);
        int vkey = XKeyToVKey(key);
        if (inputManager) inputManager->keyDown(applyControllerState, (int16)vkey);
        if (vkey == VK_F1 || (vkey == VK_ESCAPE && bFullScreen))
          ToggleFullscreen();
        if (vkey == VK_F12) {
          // Tear down libavcodec; MpxDecoderActive_IsAtEnd() then
          // returns true, the VLD-BDU stub flips to sequence_end_code,
          // and fmv.run advances naturally. No MPE halt needed.
          extern void MpxSkipCutscene();
          MpxSkipCutscene();
          fprintf(stderr, "[F12] skip cutscene: torn down MPX decoder\n");
        }
        break;
      }
      case KeyRelease:
      {
        if (uiCaptured) break;
        // Filter out X11 autorepeat
        if (XEventsQueued(xDisplay, QueuedAfterReading)) {
          XEvent next;
          XPeekEvent(xDisplay, &next);
          if (next.type == KeyPress && next.xkey.time == ev.xkey.time && next.xkey.keycode == ev.xkey.keycode) {
            XNextEvent(xDisplay, &next);
            break;
          }
        }
        KeySym key = XLookupKeysym(&ev.xkey, 0);
        int vkey = XKeyToVKey(key);
        if (inputManager) inputManager->keyUp(applyControllerState, (int16)vkey);
        break;
      }
      case ClientMessage:
        if (ev.xclient.message_type == xdndEnter) {
          xdndSourceWindow  = (Window)ev.xclient.data.l[0];
          xdndSourceVersion = (ev.xclient.data.l[1] >> 24) & 0xff;
          // Check if source advertises text/uri-list. If the high bit of data.l[1] is
          // set the type list is in a property; otherwise data.l[2..4] hold up to 3 atoms.
          xdndWillAccept = false;
          if (ev.xclient.data.l[1] & 1) {
            Atom actualType = None; int actualFormat = 0;
            unsigned long nItems = 0, bytesAfter = 0;
            unsigned char* data = nullptr;
            Atom typeListAtom = XInternAtom(xDisplay, "XdndTypeList", False);
            if (XGetWindowProperty(xDisplay, xdndSourceWindow, typeListAtom, 0, 1024, False,
                                   XA_ATOM, &actualType, &actualFormat,
                                   &nItems, &bytesAfter, &data) == Success && data) {
              const Atom* atoms = (const Atom*)data;
              for (unsigned long i = 0; i < nItems; i++)
                if (atoms[i] == xdndUriList) { xdndWillAccept = true; break; }
              XFree(data);
            }
          } else {
            for (int i = 2; i <= 4; i++)
              if ((Atom)ev.xclient.data.l[i] == xdndUriList) { xdndWillAccept = true; break; }
          }
        }
        else if (ev.xclient.message_type == xdndPosition) {
          XdndSendStatus((Window)ev.xclient.data.l[0], xdndWillAccept);
        }
        else if (ev.xclient.message_type == xdndLeave) {
          xdndSourceWindow = 0;
          xdndWillAccept = false;
        }
        else if (ev.xclient.message_type == xdndDrop) {
          if (xdndWillAccept) {
            const Time t = (xdndSourceVersion >= 1) ? (Time)ev.xclient.data.l[2] : CurrentTime;
            XConvertSelection(xDisplay, xdndSelection, xdndUriList, xdndTargetProp, xWindow, t);
            // Reply (XdndFinished) is sent from XdndHandleSelectionNotify after the SelectionNotify arrives.
          } else {
            XdndSendFinished((Window)ev.xclient.data.l[0], false);
            xdndSourceWindow = 0;
          }
        }
        else if ((Atom)ev.xclient.data.l[0] == wmDeleteMessage) {
          bQuit = true;
        }
        break;
      case SelectionNotify:
        if (ev.xselection.property == xdndTargetProp)
          XdndHandleSelectionNotify();
        break;
    }
  }
}

unsigned WINAPI GLWindow::GLWindowMain(void *param)
{
  GLWindow* glWindow = (GLWindow*)param;
  if (glWindow->CreateWindowGL()) {
    glWindow->OnResize(glWindow->clientWidth, glWindow->clientHeight);
  }
  return 1;
}

// X11 swap buffers
void SDL2_SwapWindow()
{
  if (xDisplay && xWindow) glXSwapBuffers(xDisplay, xWindow);
}

#endif // !_WIN32
