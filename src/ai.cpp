#include "ai.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "tls.h"

namespace AI {

static const char* kEndpoint = "https://api.anthropic.com/v1/messages";

// System prompt tuned for a tiny terminal: terse, command-first answers.
static const char* kSystem =
    "You are an SSH co-pilot embedded in a handheld terminal with a tiny "
    "320x240 screen. Be extremely concise. When asked for a command, reply "
    "with the command on its own first line, then at most two short lines of "
    "explanation. No markdown fences, no preamble.";

bool configured() { return !gConfig.anthropicKey.isEmpty(); }

String ask(const String& question, const String& context) {
  if (!configured()) return "ERR: no API key set (Settings > API key)";

  WiFiClientSecure client;
  // Validates against /ca/anthropic.pem if present; see Tls::configure / README.
  Tls::configure(client);

  HTTPClient https;
  https.setTimeout(20000);
  if (!https.begin(client, kEndpoint)) return "ERR: TLS begin failed";

  https.addHeader("content-type", "application/json");
  https.addHeader("x-api-key", gConfig.anthropicKey);
  https.addHeader("anthropic-version", "2023-06-01");

  // Build the request body. We embed any terminal context in the user turn.
  JsonDocument req;
  req["model"]      = gConfig.aiModel;
  req["max_tokens"] = 800;
  req["system"]     = kSystem;

  String userMsg = question;
  if (!context.isEmpty()) {
    userMsg = "Recent terminal output:\n```\n" + context + "\n```\n\n" + question;
  }
  JsonArray msgs = req["messages"].to<JsonArray>();
  JsonObject m = msgs.add<JsonObject>();
  m["role"]    = "user";
  m["content"] = userMsg;

  String body;
  serializeJson(req, body);

  int code = https.POST(body);
  if (code != 200) {
    String err = https.getString();
    https.end();
    return "ERR: HTTP " + String(code) + " " + err.substring(0, 160);
  }

  // Parse: { content: [ { type:"text", text:"..." }, ... ] }
  JsonDocument resp;
  // Filter keeps PSRAM use down — we only need content[].text.
  JsonDocument filter;
  filter["content"][0]["text"] = true;
  DeserializationError e =
      deserializeJson(resp, https.getStream(), DeserializationOption::Filter(filter));
  https.end();
  if (e) return String("ERR: parse ") + e.c_str();

  String out;
  for (JsonObject blk : resp["content"].as<JsonArray>()) {
    const char* t = blk["text"];
    if (t) out += t;
  }
  out.trim();
  return out.isEmpty() ? "ERR: empty response" : out;
}

}  // namespace AI
