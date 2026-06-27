#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

// Interactive SSH shell. libssh is blocking and stack-hungry, so the whole
// session runs in a dedicated FreeRTOS task. The UI thread talks to it only
// through two mutex-guarded buffers: inBuf_ (server -> screen) and
// outBuf_ (keyboard -> server).

class SshSession {
 public:
  enum class State { IDLE, CONNECTING, HOSTKEY, AUTH, CONNECTED, CLOSED, ERROR };

  void begin();                       // one-time libssh init
  void start(const SshEnv& env);      // spawn the session task
  void stop();                        // tear down

  // UI thread API
  void   send(const char* data, size_t len);  // queue bytes to the server
  void   send(const String& s) { send(s.c_str(), s.length()); }
  String drain();                              // pull pending server output
  void   resize(int cols, int rows);

  State  state() const { return state_; }
  String status() const;

  // Host-key verification (trust-on-first-use). When state() == HOSTKEY the task
  // is blocked waiting for the UI to call resolveHostKey().
  String fingerprint()    const { return fingerprint_; }
  bool   hostKeyChanged() const { return hostKeyChanged_; }
  void   resolveHostKey(bool accept) { hostKeyDecision_ = accept ? 1 : 2; }

 private:
  static void taskTrampoline(void* arg);
  void run();

  SshEnv             env_;
  volatile State     state_ = State::IDLE;
  String             statusMsg_ = "idle";
  TaskHandle_t       task_  = nullptr;
  SemaphoreHandle_t  inMx_  = nullptr;
  SemaphoreHandle_t  outMx_ = nullptr;
  String             inBuf_;
  String             outBuf_;
  volatile bool      wantStop_ = false;
  volatile int       cols_ = TERM_COLS, rows_ = TERM_ROWS;
  volatile bool      resizePending_ = false;

  // Host-key prompt state
  String             fingerprint_;
  volatile bool      hostKeyChanged_ = false;
  volatile int       hostKeyDecision_ = 0;   // 0 pending, 1 accept, 2 reject
};

extern SshSession gSsh;
