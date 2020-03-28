#ifndef MOUSE_EVENTS_H
#define MOUSE_EVENTS_H

#include <bitset>
#include <cstdint>

extern "C" {
#include "X11/Xlib.h"
#include "lua.h"
}

enum mouse_event_type {
  MOUSE_DOWN = 0,
  MOUSE_UP,
  MOUSE_SCROLL,
  MOUSE_MOVE,
  AREA_ENTER,
  AREA_LEAVE,
  MOUSE_EVENT_COUNT,
};

struct mouse_event {
  mouse_event_type type;
  uint64_t time = 0L;  // event time

  void push_lua_table(lua_State *L) const;

  virtual void push_lua_data(lua_State *L) const = 0;
};

struct mouse_positioned_event : public mouse_event {
  int x = 0, y = 0;          // positions relative to window
  int x_abs = 0, y_abs = 0;  // positions relative to root

  void push_lua_data(lua_State *L) const;
};

struct mouse_move_event : public mouse_positioned_event {
  std::bitset<13> mods;  // held buttons and modifiers (ctrl, shift, ...)

  explicit mouse_move_event(XMotionEvent *ev);

  void push_lua_data(lua_State *L) const;
};

struct mouse_button_event : public mouse_positioned_event {
  std::bitset<13> mods;  // held buttons and modifiers (ctrl, shift, ...)
  uint button = 0;

  explicit mouse_button_event(XButtonEvent *ev);

  void push_lua_data(lua_State *L) const;
};

typedef struct mouse_button_event mouse_press_event;
typedef struct mouse_button_event mouse_release_event;

struct mouse_crossing_event : public mouse_positioned_event {
  explicit mouse_crossing_event(XCrossingEvent *ev);
};

typedef struct mouse_crossing_event mouse_enter_event;
typedef struct mouse_crossing_event mouse_leave_event;

#endif /* MOUSE_EVENTS_H */