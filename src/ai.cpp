#include "ai.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cstring>
#include <vector>
#include "config.h"
#include "tls.h"

namespace AI {

static const char* kEndpoint = "https://api.anthropic.com/v1/messages";

// System prompt tuned for a tiny terminal: terse, command-first answers.
static const char* kSystem =
  "You are a helpful AI assistant running on a small handheld computer. "
  "Give clear, concise, accurate answers. "
  "Use plain text only. Never use Markdown, HTML, or formatting syntax. "
  "Do not use asterisks for bold, backticks for code, or hash symbols for headings. "
  "Avoid emojis and decorative Unicode characters. "
  "Keep paragraphs short and lines reasonably brief. "
  "For technical questions, provide useful commands and explanations. "
  "When asked for a command, show the command first. "
  "If a web-search tool is provided, use it when current information is needed. "
  "Otherwise answer from existing knowledge and say when information may be outdated. "
  "Give the answer itself, not a list of links. "
  "Avoid URLs unless the user explicitly asks for sources or links. "
  "Do not invent current facts if web search fails.";

struct Turn { String question; String answer; };
static std::vector<Turn> history;
static const size_t kMaxTurns = 4;
static const size_t kMaxHistoryChars = 3600;

void resetConversation() { history.clear(); }

bool configured() { return !gConfig.anthropicKey.isEmpty(); }

String ask(const String& question, const String& context, bool allowWebSearch) {
  if (!configured()) return "ERR: no API key set (Settings > API key)";

  WiFiClientSecure client;
  // Validates against /ca/anthropic.pem if present; see Tls::configure / README.
  Tls::configure(client);

  HTTPClient https;
  // Web searches can take much longer than ordinary messages.
  https.setTimeout(90000);
  if (!https.begin(client, kEndpoint)) return "ERR: TLS begin failed";

  https.addHeader("content-type", "application/json");
  https.addHeader("x-api-key", gConfig.anthropicKey);
  https.addHeader("anthropic-version", "2023-06-01");

  // Build the request body. We embed any terminal context in the user turn.
  JsonDocument req;
  req["model"]      = gConfig.aiModel;
  // Allow enough output budget for Anthropic to execute a server-side search
  // and still return a useful answer. The prompt keeps actual answers short.
  req["max_tokens"] = 1024;
  req["system"]     = kSystem;

  // Explicit per-question permission. No search tool is sent when disabled.
  if (allowWebSearch) {
    JsonArray tools = req["tools"].to<JsonArray>();
    JsonObject webSearch = tools.add<JsonObject>();
    webSearch["type"] = "web_search_20250305";
    webSearch["name"] = "web_search";
    webSearch["max_uses"] = 1;
  }

  String userMsg = question;
  if (!context.isEmpty()) {
    userMsg = "Recent terminal output:\n```\n" + context + "\n```\n\n" + question;
  }
  JsonArray msgs = req["messages"].to<JsonArray>();
  // Re-send only a few recent turns to control Sonnet token costs.
  size_t retained = 0;
  size_t start = history.size();
  while (start > 0 && retained + history[start - 1].question.length() +
                         history[start - 1].answer.length() <= kMaxHistoryChars) {
    --start;
    retained += history[start].question.length() + history[start].answer.length();
  }
  for (size_t i = start; i < history.size(); ++i) {
    JsonObject prevUser = msgs.add<JsonObject>();
    prevUser["role"] = "user";
    prevUser["content"] = history[i].question;
    JsonObject prevAnswer = msgs.add<JsonObject>();
    prevAnswer["role"] = "assistant";
    prevAnswer["content"] = history[i].answer;
  }
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

  // HTTPClient::getString() handles HTTP transfer framing (including
  // chunked responses) before ArduinoJson sees the response body. Parsing
  // getStream() directly can leave the JSON decoder with no usable content.
  String responseBody = https.getString();
  https.end();

  if (responseBody.isEmpty()) return "ERR: HTTP 200 but empty body";

  JsonDocument resp;
  // Anthropic may return large search-result/citation blocks. Keep only
  // answer text, stop reason and lightweight diagnostic data in the JSON tree.
  JsonDocument filter;
  filter["content"][0]["type"] = true;
  filter["content"][0]["text"] = true;
  filter["stop_reason"] = true;
  filter["error"]["message"] = true;
  filter["type"] = true;
  DeserializationError e = deserializeJson(
      resp, responseBody, DeserializationOption::Filter(filter));
  if (e) {
    return String("ERR: JSON parse: ") + e.c_str() +
           " (bytes=" + String(responseBody.length()) + ")";
  }

  // Do not assume HTTP 200 means a successful Messages API payload.
  if (!resp["content"].is<JsonArray>()) {
    const char* type = resp["type"] | "unknown";
    const char* message = resp["error"]["message"] | "no content array";
    return String("ERR: API response ") + type + ": " +
           String(message).substring(0, 120);
  }

  String out;
  for (JsonObject blk : resp["content"].as<JsonArray>()) {
    if (blk["type"] == "text") {
      const char* t = blk["text"].as<const char*>();
      if (t) out += t;
    }
  }
  out.trim();
  const char* stop = resp["stop_reason"] | "unknown";
  if (strcmp(stop, "pause_turn") == 0) {
    // A paused server-tool run requires API conversation continuation, not
    // implemented here. Never present a partial search as a final answer.
    return "ERR: web search paused; please ask a shorter question";
  }
  if (out.isEmpty()) {
    return String("ERR: no text blocks (stop_reason=") + stop + ")";
  }
  // Failed requests are not included in conversation history.
  history.push_back({question, out});
  if (history.size() > kMaxTurns) history.erase(history.begin());
  return out;
}

}  // namespace AI
