#pragma once

// ═══════════════════════════════════════════════════════════════
//  wifi_manager.h  —  WiFi Manager + Captive Portal + Serial CMD
//
//  Fitur:
//  1. Simpan SSID & Password ke NVS (Preferences) — tidak hilang
//     saat ESP restart atau upload firmware baru.
//  2. Saat boot: coba konek WiFi tersimpan, retry N kali.
//  3. Jika gagal → masuk AP mode + Captive Portal (web form).
//  4. Ubah WiFi via Serial Monitor saat terhubung USB.
//  5. Buzzer feedback: connecting / sukses / gagal.
//
//  Serial Commands (baud 115200):
//  ┌─────────────────────────────────────────────────────────┐
//  │ SETWIFI:<SSID>,<PASSWORD>  → ganti WiFi, restart auto   │
//  │ GETWIFI                    → lihat SSID tersimpan        │
//  │ CLEARWIFI                  → hapus kredensial            │
//  │ RESTART                    → restart ESP                 │
//  └─────────────────────────────────────────────────────────┘
// ═══════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "cfg_config.h"

// ── Objek internal ────────────────────────────────────────────────
static Preferences  _prefs;
static WebServer    _server(80);
static DNSServer    _dns;
static bool         _apModeActive = false;
static String       _serialBuffer = "";

// ── Buzzer helpers ────────────────────────────────────────────────
static void _beepConnecting() {
  tone(PIN_BUZZER, 1000); delay(300);
  noTone(PIN_BUZZER);     delay(700);
}
static void _beepSuccess() {
  tone(PIN_BUZZER, 1500); delay(500);
  noTone(PIN_BUZZER);
}
static void _beepError() {
  for (int i = 0; i < 6; i++) {
    tone(PIN_BUZZER, 2000); delay(100);
    noTone(PIN_BUZZER);     delay(100);
  }
}

// ════════════════════════════════════════════════════════════════
//  NVS  —  Simpan & Baca SSID/Password ke flash ESP32
// ════════════════════════════════════════════════════════════════
static void _saveCredentials(const String& ssid, const String& pass) {
  _prefs.begin("wifi", false);
  _prefs.putString("ssid", ssid);
  _prefs.putString("pass", pass);
  _prefs.end();
  Serial.println("[WiFiMgr] Kredensial disimpan ke NVS.");
}

static bool _loadCredentials(String& ssid, String& pass) {
  _prefs.begin("wifi", true);
  ssid = _prefs.getString("ssid", "");
  pass = _prefs.getString("pass", "");
  _prefs.end();
  return ssid.length() > 0;
}

// ════════════════════════════════════════════════════════════════
//  HTML Captive Portal
// ════════════════════════════════════════════════════════════════
static const char PORTAL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Anemometer — WiFi Setup</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Segoe UI', sans-serif;
      background: #0f172a;
      color: #e2e8f0;
      display: flex;
      align-items: center;
      justify-content: center;
      min-height: 100vh;
      padding: 16px;
    }
    .card {
      background: #1e293b;
      border: 1px solid #334155;
      border-radius: 16px;
      padding: 32px 28px;
      width: 100%;
      max-width: 400px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.5);
    }
    .icon { font-size: 40px; text-align: center; margin-bottom: 8px; }
    h1 {
      text-align: center;
      font-size: 20px;
      font-weight: 700;
      color: #38bdf8;
      margin-bottom: 4px;
    }
    p.sub {
      text-align: center;
      font-size: 13px;
      color: #94a3b8;
      margin-bottom: 24px;
    }
    label {
      display: block;
      font-size: 12px;
      font-weight: 600;
      color: #94a3b8;
      text-transform: uppercase;
      letter-spacing: .05em;
      margin-bottom: 6px;
    }
    input[type=text], input[type=password] {
      width: 100%;
      padding: 12px 14px;
      background: #0f172a;
      border: 1px solid #334155;
      border-radius: 8px;
      color: #e2e8f0;
      font-size: 15px;
      margin-bottom: 16px;
      outline: none;
      transition: border-color .2s;
    }
    input:focus { border-color: #38bdf8; }
    .row { display: flex; align-items: center; gap: 8px; margin-bottom: 24px; }
    .row input[type=checkbox] { width: 16px; height: 16px; accent-color: #38bdf8; }
    .row label { margin: 0; font-size: 13px; text-transform: none; letter-spacing: 0; }
    button {
      width: 100%;
      padding: 13px;
      background: #38bdf8;
      color: #0f172a;
      font-size: 15px;
      font-weight: 700;
      border: none;
      border-radius: 8px;
      cursor: pointer;
      transition: background .2s;
    }
    button:hover { background: #7dd3fc; }
    .msg {
      margin-top: 16px;
      padding: 10px 14px;
      border-radius: 8px;
      font-size: 13px;
      text-align: center;
    }
    .msg.ok  { background: #064e3b; color: #6ee7b7; }
    .msg.err { background: #450a0a; color: #fca5a5; }
  </style>
</head>
<body>
<div class="card">
  <div class="icon">📡</div>
  <h1>Anemometer Setup</h1>
  <p class="sub">Masukkan kredensial WiFi untuk perangkat</p>

  <form id="wifiForm">
    <label for="ssid">Nama WiFi (SSID)</label>
    <input type="text" id="ssid" name="ssid" placeholder="Nama jaringan WiFi" required>

    <label for="pass">Password</label>
    <input type="password" id="pass" name="pass" placeholder="Password WiFi">

    <div class="row">
      <input type="checkbox" id="showPass">
      <label for="showPass">Tampilkan password</label>
    </div>

    <button type="submit" id="btn">Simpan &amp; Sambungkan</button>
  </form>
  <div id="msg"></div>
</div>
<script>
  document.getElementById('showPass').addEventListener('change', function() {
    document.getElementById('pass').type = this.checked ? 'text' : 'password';
  });
  document.getElementById('wifiForm').addEventListener('submit', async function(e) {
    e.preventDefault();
    const btn = document.getElementById('btn');
    const msg = document.getElementById('msg');
    btn.disabled = true;
    btn.textContent = 'Menyimpan...';
    msg.className = 'msg'; msg.textContent = '';
    const body = new URLSearchParams({
      ssid: document.getElementById('ssid').value,
      pass: document.getElementById('pass').value
    });
    try {
      const r = await fetch('/save', { method: 'POST', body });
      const t = await r.text();
      if (r.ok) {
        msg.className = 'msg ok';
        msg.textContent = t;
        btn.textContent = 'Tersimpan! ESP akan restart...';
      } else { throw new Error(t); }
    } catch(err) {
      msg.className = 'msg err';
      msg.textContent = 'Gagal: ' + err.message;
      btn.disabled = false;
      btn.textContent = 'Simpan & Sambungkan';
    }
  });
</script>
</body>
</html>
)rawhtml";

// ════════════════════════════════════════════════════════════════
//  AP Mode + Captive Portal
// ════════════════════════════════════════════════════════════════
static void _startCaptivePortal() {
  _apModeActive = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, (strlen(AP_PASSWORD) > 0) ? AP_PASSWORD : nullptr);

  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("[WiFiMgr] AP aktif. SSID: %s | IP: %s\n",
                AP_SSID, apIP.toString().c_str());
  Serial.println("[WiFiMgr] Buka browser → http://" + apIP.toString());

  _dns.start(53, "*", apIP);

  _server.on("/", HTTP_GET, []() {
    _server.send(200, "text/html", PORTAL_HTML);
  });
  _server.on("/generate_204",   HTTP_GET, []() { _server.sendHeader("Location", "/"); _server.send(302); });
  _server.on("/hotspot-detect", HTTP_GET, []() { _server.sendHeader("Location", "/"); _server.send(302); });
  _server.on("/fwlink",         HTTP_GET, []() { _server.sendHeader("Location", "/"); _server.send(302); });
  _server.onNotFound(           []()      { _server.sendHeader("Location", "/"); _server.send(302); });

  _server.on("/save", HTTP_POST, []() {
    if (!_server.hasArg("ssid") || _server.arg("ssid").isEmpty()) {
      _server.send(400, "text/plain", "SSID tidak boleh kosong!");
      return;
    }
    String newSSID = _server.arg("ssid");
    String newPass = _server.hasArg("pass") ? _server.arg("pass") : "";
    _saveCredentials(newSSID, newPass);
    _server.send(200, "text/plain",
      "WiFi berhasil disimpan! ESP akan restart dan mencoba konek ke \"" + newSSID + "\"");
    Serial.printf("[WiFiMgr] Kredensial baru dari portal: SSID=%s\n", newSSID.c_str());
    delay(1500);
    ESP.restart();
  });

  _server.begin();
  Serial.println("[WiFiMgr] Captive portal aktif.");
}

// ════════════════════════════════════════════════════════════════
//  Coba konek WiFi — retry N kali, lalu AP mode
// ════════════════════════════════════════════════════════════════
static bool _tryConnect(const String& ssid, const String& pass) {
  Serial.printf("[WiFiMgr] Konek ke \"%s\"...\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  for (int i = 0; i < WIFI_MAX_RETRY; i++) {
    if (WiFi.status() == WL_CONNECTED) return true;
    _beepConnecting();
    Serial.printf("[WiFiMgr] Retry %d/%d\n", i + 1, WIFI_MAX_RETRY);
    delay(WIFI_RETRY_DELAY);
  }
  return false;
}

// ════════════════════════════════════════════════════════════════
//  Serial Monitor Commands
// ════════════════════════════════════════════════════════════════
static void _printSerialHelp() {
  Serial.println("╔══════════════════════════════════════════════╗");
  Serial.println("║       Serial WiFi Manager — Commands         ║");
  Serial.println("╠══════════════════════════════════════════════╣");
  Serial.println("║ SETWIFI:<SSID>,<PASSWORD>  → Set WiFi baru   ║");
  Serial.println("║ GETWIFI                    → Lihat SSID       ║");
  Serial.println("║ CLEARWIFI                  → Hapus tersimpan  ║");
  Serial.println("║ RESTART                    → Restart ESP      ║");
  Serial.println("╚══════════════════════════════════════════════╝");
}

static void _handleSerialCommand(const String& cmd) {
  String trimmed = cmd;
  trimmed.trim();

  // GETWIFI
  String upper = trimmed;
  upper.toUpperCase();

  if (upper == "GETWIFI") {
    String ssid, pass;
    if (_loadCredentials(ssid, pass))
      Serial.printf("[WiFiMgr] SSID tersimpan: \"%s\" | Password: ***\n", ssid.c_str());
    else
      Serial.println("[WiFiMgr] Belum ada kredensial tersimpan.");
    return;
  }

  // CLEARWIFI
  if (upper == "CLEARWIFI") {
    _prefs.begin("wifi", false);
    _prefs.clear();
    _prefs.end();
    Serial.println("[WiFiMgr] Kredensial dihapus. Restart untuk masuk AP mode.");
    return;
  }

  // RESTART
  if (upper == "RESTART") {
    Serial.println("[WiFiMgr] Restarting...");
    delay(500);
    ESP.restart();
    return;
  }

  // SETWIFI:<SSID>,<PASSWORD>
  if (upper.startsWith("SETWIFI:")) {
    String payload  = trimmed.substring(8);  // jaga huruf asli SSID & pass
    int    commaIdx = payload.indexOf(',');
    String newSSID  = (commaIdx == -1) ? payload : payload.substring(0, commaIdx);
    String newPass  = (commaIdx == -1) ? ""       : payload.substring(commaIdx + 1);
    newSSID.trim();

    if (newSSID.isEmpty()) {
      Serial.println("[WiFiMgr] ERROR: SSID tidak boleh kosong!");
      Serial.println("[WiFiMgr] Format: SETWIFI:<SSID>,<PASSWORD>");
      return;
    }

    Serial.printf("[WiFiMgr] SSID baru    : \"%s\"\n", newSSID.c_str());
    Serial.printf("[WiFiMgr] Password baru: \"%s\"\n", newPass.c_str());
    _saveCredentials(newSSID, newPass);
    Serial.println("[WiFiMgr] Tersimpan! ESP akan restart...");
    delay(1000);
    ESP.restart();
    return;
  }

  // Tidak dikenal
  if (trimmed.length() > 0) {
    Serial.printf("[WiFiMgr] Perintah tidak dikenal: \"%s\"\n", trimmed.c_str());
    _printSerialHelp();
  }
}

// ════════════════════════════════════════════════════════════════
//  PUBLIC API
// ════════════════════════════════════════════════════════════════

/** Panggil di setup() setelah Serial.begin() */
void wifiManagerBegin() {
  Serial.println("\n[WiFiMgr] ══ WiFi Manager Start ══");
  _printSerialHelp();

  String ssid, pass;
  bool hasCredentials = _loadCredentials(ssid, pass);

  if (!hasCredentials) {
    Serial.println("[WiFiMgr] Tidak ada kredensial → masuk AP mode");
    _beepError();
    _startCaptivePortal();
    return;
  }

  bool connected = _tryConnect(ssid, pass);

  if (connected) {
    _beepSuccess();
    Serial.println("[WiFiMgr] WiFi Connected!");
    Serial.println("[WiFiMgr] IP : " + WiFi.localIP().toString());
    Serial.printf( "[WiFiMgr] SSID: %s | RSSI: %d dBm\n", WiFi.SSID().c_str(), WiFi.RSSI());
  } else {
    Serial.println("[WiFiMgr] Gagal konek → masuk AP / Captive Portal");
    _beepError();
    _startCaptivePortal();
  }
}

/** Panggil di loop() setiap iterasi */
void wifiManagerLoop() {
  // Captive portal handler
  if (_apModeActive) {
    _dns.processNextRequest();
    _server.handleClient();
  }

  // Serial command handler
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (_serialBuffer.length() > 0) {
        _handleSerialCommand(_serialBuffer);
        _serialBuffer = "";
      }
    } else {
      _serialBuffer += c;
    }
  }

  // Auto-reconnect (hanya STA mode)
  if (!_apModeActive && WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFiMgr] WiFi putus! Mencoba reconnect...");
    String ssid, pass;
    if (_loadCredentials(ssid, pass)) {
      bool ok = _tryConnect(ssid, pass);
      if (ok) {
        _beepSuccess();
        Serial.println("[WiFiMgr] Reconnect berhasil! IP: " + WiFi.localIP().toString());
      } else {
        Serial.println("[WiFiMgr] Reconnect gagal.");
        _beepError();
      }
    }
  }
}

/** Helper cek status koneksi dari .ino utama */
bool wifiIsConnected() {
  return !_apModeActive && (WiFi.status() == WL_CONNECTED);
}
