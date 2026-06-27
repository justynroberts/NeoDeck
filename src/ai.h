#pragma once
#include <Arduino.h>

// On-device AI assistant backed by the Claude Messages API.
// The model id comes from gConfig.aiModel (default "claude-opus-4-8"; switch to
// "claude-haiku-4-5" for snappier replies on the small screen).
namespace AI {

  // Ask a question. `context` is optional recent terminal output that gets
  // folded into the prompt so the model can reason about what's on screen.
  // Returns the assistant's text, or an error string prefixed with "ERR:".
  String ask(const String& question, const String& context = "");

  bool configured();   // true if an API key is set
}
