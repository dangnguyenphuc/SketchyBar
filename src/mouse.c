#include <Carbon/Carbon.h>
#include "mouse.h"

static const EventTypeSpec mouse_events [] = {
    { kEventClassMouse, kEventMouseUp },
    { kEventClassMouse, kEventMouseDragged },
    { kEventClassMouse, kEventMouseEntered },
    { kEventClassMouse, kEventMouseExited },
    { kEventClassMouse, kEventMouseWheelMoved },
    { kEventClassMouse, kEventMouseScroll }
};

static int carbon_event_translation[] = {
  [kEventMouseUp] = MOUSE_UP,
  [kEventMouseDragged] = MOUSE_DRAGGED,
  [kEventMouseEntered] = MOUSE_ENTERED,
  [kEventMouseExited]  = MOUSE_EXITED,
  [kEventMouseWheelMoved] = MOUSE_SCROLLED,
  [kEventMouseScroll] = MOUSE_SCROLLED
};

static pascal OSStatus mouse_handler(EventHandlerCallRef next, EventRef e, void *data) {
  enum event_type event_type = carbon_event_translation[GetEventKind(e)];

  CGEventRef cg_event = CopyEventCGEvent(e);
  struct event event = { (void *) cg_event, event_type };
  event_post(&event);
  CFRelease(cg_event);

  return CallNextEventHandler(next, e);
}

static CFMachPortRef g_outside_click_tap = NULL;

static CGEventRef mouse_outside_click_callback(CGEventTapProxy proxy,
                                               CGEventType type,
                                               CGEventRef event,
                                               void* context        ) {
  if (type == kCGEventTapDisabledByTimeout
      || type == kCGEventTapDisabledByUserInput) {
    if (g_outside_click_tap) CGEventTapEnable(g_outside_click_tap, true);
    return event;
  }

  struct event ev = { (void*)event, MOUSE_DOWN_GLOBAL };
  event_post(&ev);
  return event;
}

void mouse_watch_outside_clicks(bool enabled) {
  if (enabled && !g_outside_click_tap) {
    CGEventMask mask = CGEventMaskBit(kCGEventLeftMouseDown)
                       | CGEventMaskBit(kCGEventRightMouseDown)
                       | CGEventMaskBit(kCGEventOtherMouseDown);

    g_outside_click_tap = CGEventTapCreate(kCGSessionEventTap,
                                           kCGHeadInsertEventTap,
                                           kCGEventTapOptionListenOnly,
                                           mask,
                                           mouse_outside_click_callback,
                                           NULL                         );
    if (!g_outside_click_tap) {
      printf("[!] Mouse: could not create the outside click event tap\n");
      return;
    }

    CFRunLoopSourceRef source
      = CFMachPortCreateRunLoopSource(NULL, g_outside_click_tap, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
    CFRelease(source);
  }

  if (g_outside_click_tap) CGEventTapEnable(g_outside_click_tap, enabled);
}

void mouse_begin(void) {
  InstallEventHandler(GetEventDispatcherTarget(),
                      NewEventHandlerUPP(mouse_handler),
                      GetEventTypeCount(mouse_events),
                      mouse_events, 0, 0);
}
