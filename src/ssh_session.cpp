#include "ssh_session.h"

// LibSSH-ESP32 shim — must be included before <libssh/libssh.h>.
#include "libssh_esp32.h"
#include <libssh/libssh.h>
#include "keys.h"
#include "fsx.h"

SshSession gSsh;

void SshSession::begin() {
  inMx_  = xSemaphoreCreateMutex();
  outMx_ = xSemaphoreCreateMutex();
  libssh_begin();   // initialise libssh once per boot
}

void SshSession::start(const SshEnv& env) {
  if (task_) stop();
  env_       = env;
  wantStop_  = false;
  state_     = State::CONNECTING;
  statusMsg_ = "connecting…";
  {
    // clear buffers
    xSemaphoreTake(inMx_, portMAX_DELAY);  inBuf_  = "";  xSemaphoreGive(inMx_);
    xSemaphoreTake(outMx_, portMAX_DELAY); outBuf_ = "";  xSemaphoreGive(outMx_);
  }
  // libssh + mbedTLS need a large stack; pin to core 1 (core 0 runs WiFi).
  xTaskCreatePinnedToCore(taskTrampoline, "ssh", 51200, this, 3, &task_, 1);
}

void SshSession::stop() {
  wantStop_ = true;
  // The task clears task_ on exit; give it a moment.
  for (int i = 0; i < 50 && task_; i++) delay(20);
  task_  = nullptr;
  state_ = State::IDLE;
}

void SshSession::send(const char* data, size_t len) {
  if (!outMx_) return;
  xSemaphoreTake(outMx_, portMAX_DELAY);
  outBuf_ += String();        // ensure capacity path
  for (size_t i = 0; i < len; i++) outBuf_ += data[i];
  xSemaphoreGive(outMx_);
}

String SshSession::drain() {
  String out;
  if (!inMx_) return out;
  xSemaphoreTake(inMx_, portMAX_DELAY);
  if (inBuf_.length()) { out = inBuf_; inBuf_ = ""; }
  xSemaphoreGive(inMx_);
  return out;
}

void SshSession::resize(int cols, int rows) {
  cols_ = cols; rows_ = rows; resizePending_ = true;
}

String SshSession::status() const { return statusMsg_; }

void SshSession::taskTrampoline(void* arg) {
  static_cast<SshSession*>(arg)->run();
}

// ---------------------------------------------------------------------------
//  The actual session — runs entirely inside the FreeRTOS task.
// ---------------------------------------------------------------------------
void SshSession::run() {
  ssh_session session = ssh_new();
  ssh_channel channel = nullptr;

  auto fail = [&](const char* why) {
    statusMsg_ = String("error: ") + why;
    state_ = State::ERROR;
  };

  if (!session) { fail("ssh_new"); goto cleanup; }

  {
    unsigned int port = env_.port;
    long timeout = 12;  // seconds
    ssh_options_set(session, SSH_OPTIONS_HOST, env_.host.c_str());
    ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(session, SSH_OPTIONS_USER, env_.user.c_str());
    ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);
    // known_hosts on the active filesystem's VFS mount (libssh uses stdio).
    String kh = fsx::knownHostsVfs();
    ssh_options_set(session, SSH_OPTIONS_KNOWNHOSTS, kh.c_str());
  }

  if (ssh_connect(session) != SSH_OK) { fail(ssh_get_error(session)); goto cleanup; }

  // --- Host-key verification (trust-on-first-use) ---
  {
    ssh_key srv = nullptr;
    if (ssh_get_server_publickey(session, &srv) == SSH_OK) {
      unsigned char* hash = nullptr; size_t hlen = 0;
      if (ssh_get_publickey_hash(srv, SSH_PUBLICKEY_HASH_SHA256, &hash, &hlen) == 0) {
        char* hexa = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, hlen);
        if (hexa) { fingerprint_ = hexa; ssh_string_free_char(hexa); }
        ssh_clean_pubkey_hash(&hash);
      }
      ssh_key_free(srv);
    }

    enum ssh_known_hosts_e known = ssh_session_is_known_server(session);
    if (known != SSH_KNOWN_HOSTS_OK) {
      // Unknown or changed: block until the UI accepts or rejects.
      hostKeyChanged_  = (known == SSH_KNOWN_HOSTS_CHANGED || known == SSH_KNOWN_HOSTS_OTHER);
      hostKeyDecision_ = 0;
      statusMsg_ = hostKeyChanged_ ? "host key CHANGED" : "verify host key";
      state_ = State::HOSTKEY;
      while (hostKeyDecision_ == 0 && !wantStop_) vTaskDelay(pdMS_TO_TICKS(50));
      if (wantStop_ || hostKeyDecision_ == 2) { fail("host key rejected"); goto cleanup; }
      ssh_session_update_known_hosts(session);   // remember it for next time
    }
  }

  state_ = State::AUTH;
  statusMsg_ = "authenticating…";
  {
    int authrc = SSH_AUTH_ERROR;
    if (env_.auth == 1 && env_.keyName.length()) {
      // Public-key auth: load the PEM from /keys and import it in memory.
      statusMsg_ = "auth (key)…";
      String pem = Keys::read(env_.keyName);
      if (pem.isEmpty()) { fail("key file missing"); goto cleanup; }
      ssh_key pkey = nullptr;
      const char* pass = env_.keyPass.length() ? env_.keyPass.c_str() : nullptr;
      if (ssh_pki_import_privkey_base64(pem.c_str(), pass, nullptr, nullptr, &pkey) != SSH_OK) {
        fail("bad private key");
        goto cleanup;
      }
      authrc = ssh_userauth_publickey(session, nullptr, pkey);
      ssh_key_free(pkey);
    } else {
      authrc = ssh_userauth_password(session, nullptr, env_.pass.c_str());
    }
    if (authrc != SSH_AUTH_SUCCESS) { fail("auth failed"); goto cleanup; }
  }

  channel = ssh_channel_new(session);
  if (!channel || ssh_channel_open_session(channel) != SSH_OK) {
    fail("channel open"); goto cleanup;
  }
  ssh_channel_request_pty_size(channel, "xterm-256color", cols_, rows_);
  if (ssh_channel_request_shell(channel) != SSH_OK) { fail("shell"); goto cleanup; }

  state_ = State::CONNECTED;
  statusMsg_ = "connected";

  // Main I/O loop -----------------------------------------------------------
  {
    char buf[1024];
    while (!wantStop_ && ssh_channel_is_open(channel) && !ssh_channel_is_eof(channel)) {
      // Push any queued keystrokes to the server.
      if (outMx_) {
        xSemaphoreTake(outMx_, portMAX_DELAY);
        if (outBuf_.length()) {
          ssh_channel_write(channel, outBuf_.c_str(), outBuf_.length());
          outBuf_ = "";
        }
        xSemaphoreGive(outMx_);
      }

      if (resizePending_) {
        ssh_channel_change_pty_size(channel, cols_, rows_);
        resizePending_ = false;
      }

      // Pull server output (non-blocking).
      int n = ssh_channel_read_nonblocking(channel, buf, sizeof(buf), 0);
      if (n > 0) {
        xSemaphoreTake(inMx_, portMAX_DELAY);
        // Cap the buffer so a noisy server can't exhaust PSRAM before the UI drains.
        if (inBuf_.length() < 16384) inBuf_.concat(buf, n);
        xSemaphoreGive(inMx_);
      } else if (n == SSH_ERROR) {
        break;
      } else {
        vTaskDelay(pdMS_TO_TICKS(10));  // yield when idle
      }
    }
  }

  statusMsg_ = "session closed";
  state_ = State::CLOSED;

cleanup:
  if (channel) {
    if (ssh_channel_is_open(channel)) ssh_channel_close(channel);
    ssh_channel_free(channel);
  }
  if (session) {
    ssh_disconnect(session);
    ssh_free(session);
  }
  task_ = nullptr;
  vTaskDelete(nullptr);
}
