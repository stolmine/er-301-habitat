#ifndef WARPS_DRIVERS_DEBUG_PIN_H_
#define WARPS_DRIVERS_DEBUG_PIN_H_

// Stub for ER-301 — no GPIO debug pins
struct DebugPin {
  static void Init() { }
  static void High() { }
  static void Low() { }
};

#define TIC
#define TOC

#endif
