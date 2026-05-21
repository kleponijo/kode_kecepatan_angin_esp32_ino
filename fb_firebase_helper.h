#pragma once

// ═══════════════════════════════════════════════════════════════
//  fb_firebase_helper.h  —  Firebase RTDB: Setup, Realtime, History
//
//  Perbaikan:
//  - Auto token refresh saat token expired/revoked
//  - Auto-reboot ESP jika gagal terus > MAX_FAIL_BEFORE_REBOOT
// ═══════════════════════════════════════════════════════════════

#include <Firebase_ESP_Client.h>
#include <time.h>
#include "cfg_config.h"

// ── Konstanta error handling ──────────────────────────────────────
// 600 gagal × 1 detik = 10 menit terus gagal → auto reboot
const int MAX_FAIL_BEFORE_REBOOT = 600;

// ── State internal ────────────────────────────────────────────────
static int           _consecutiveFail    = 0;
static unsigned long _lastReinitAttempt  = 0;
const  unsigned long REINIT_COOLDOWN     = 30000UL; // coba reinit tiap 30 detik

// ── Struct settings (nilai dari Firebase atau default) ────────
struct SensorSettings {
  float         kFaktor          = DEFAULT_K_FAKTOR;
  float         radiusM          = RADIUS_M; // Baca dari FireBase, fallback ke cfg_config.h
  unsigned long intervalRealtime = DEFAULT_INTERVAL_REALTIME;
  unsigned long intervalHistory  = DEFAULT_INTERVAL_HISTORY;
};

// ── Device path prefix ────────────────────────────────────────
// Semua path di bawah /anemometer/{DEVICE_ID}/
static String _basePath() {
  return String("/anemometer/") + DEVICE_ID;
}

// ── Alarm error (tetap seperti semula) ───────────────────────────
void bunyiAlarmError() {
  for (int i = 0; i < 10; i++) {
    tone(PIN_BUZZER, 2000); delay(100);
    noTone(PIN_BUZZER);     delay(100);
  }
}

// ── Tunggu NTP sinkron ────────────────────────────────────────────
void waitNTP() {
  Serial.print("[Firebase] Sinkronisasi NTP");
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = 0;
  int retry  = 0;
  while (now < 100000 && retry < 40) {
    delay(500);
    time(&now);
    Serial.print(".");
    retry++;
  }

  if (now < 100000) Serial.println("\n[Firebase] NTP GAGAL!");
  else Serial.printf("\n[Firebase] NTP OK! Unix: %lu\n", (unsigned long)now);
}

// ── Setup Firebase ────────────────────────────────────────────────
void setupFirebase(FirebaseData &fbdo, FirebaseAuth &auth, FirebaseConfig &config) {
  waitNTP();

  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email     = FB_EMAIL;
  auth.user.password  = FB_PASSWORD;

  fbdo.setResponseSize(4096);
  config.timeout.serverResponse = 15 * 1000;

  // Monitor status token di Serial
  config.token_status_callback = [](TokenInfo info) {
    if (info.status == token_status_error)
      Serial.printf("[Firebase] Token error: %s\n", info.error.message.c_str());
    else if (info.status == token_status_ready)
      Serial.println("[Firebase] Token ready/refreshed.");
  };

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.print("[Firebase] Menunggu ready");
  int retry = 0;
  while (!Firebase.ready() && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (Firebase.ready()) {
    Serial.printf("\n[Firebase] READY! Device ID: %s\n", DEVICE_ID);
    _consecutiveFail = 0;
  } else {
    Serial.println("\n[Firebase] GAGAL READY");
    bunyiAlarmError();
  }
}

// ── Reinit token (dipanggil saat token expired) ───────────────────
static void _tryReinitFirebase(FirebaseConfig &config) {
  unsigned long now = millis();
  if (now - _lastReinitAttempt < REINIT_COOLDOWN) return;
  _lastReinitAttempt = now;

  Serial.println("[Firebase] Token bermasalah → mencoba refresh...");
  Firebase.refreshToken(&config);

  int retry = 0;
  while (!Firebase.ready() && retry < 10) {
    delay(500);
    retry++;
  }

  if (Firebase.ready()) {
    Serial.println("[Firebase] Token refresh berhasil!");
    _consecutiveFail = 0;
  } else {
    Serial.println("[Firebase] Token refresh gagal, akan coba lagi nanti.");
  }
}

// ══════════════════════════════════════════════════════════════
//  fetchSettings — baca k_faktor & interval dari Firebase
//
//  Path Firebase (set dari Flutter app):
//    /anemometer/settings/k_faktor             (float/double)
//    /anemometer/settings/interval_realtime_ms (int, min 1000)
//    /anemometer/settings/interval_history_ms  (int, min 60000)
//
//  Jika node belum ada atau gagal baca → pakai nilai default
//  dari cfg_config.h (tidak crash).
// ══════════════════════════════════════════════════════════════
SensorSettings fetchSettings(FirebaseData &fbdo) {
  SensorSettings s; // mulai dari default

  // --- k_faktor ---
  if (Firebase.RTDB.getFloat(&fbdo, "/anemometer/settings/k_faktor")) {
    float val = fbdo.floatData();
    if (val > 0.0f) {
      s.kFaktor = val;
      Serial.printf("[Settings] k_faktor = %.4f\n", s.kFaktor);
    }
  } else {
    Serial.printf("[Settings] k_faktor gagal dibaca (%s), pakai default %.1f\n",
                  fbdo.errorReason().c_str(), s.kFaktor);
  }

  // --- radius_m ---
  if (Firebase.RTDB.getFloat(&fbdo, "/anemometer/settings/radius_m")) {
    float val = fbdo.floatData();
    if (val > 0.0f && val < 1.0f) {  // sanity check: 0–1 meter masuk akal
      s.radiusM = val;
      Serial.printf("[Settings] radius_m = %.4f m\n", s.radiusM);
    }
  } else {
    Serial.printf("[Settings] radius_m gagal dibaca, pakai default %.4f m\n",
                  s.radiusM);
  }

  // --- interval_realtime_ms ---
  if (Firebase.RTDB.getInt(&fbdo, "/anemometer/settings/interval_realtime_ms")) {
    long val = fbdo.intData();
    if (val >= 1000) {
      s.intervalRealtime = (unsigned long)val;
      Serial.printf("[Settings] interval_realtime = %lu ms\n", s.intervalRealtime);
    }
  } else {
    Serial.printf("[Settings] interval_realtime gagal dibaca, pakai default %lu ms\n",
                  s.intervalRealtime);
  }

  // --- interval_history_ms ---
  if (Firebase.RTDB.getInt(&fbdo, "/anemometer/settings/interval_history_ms")) {
    long val = fbdo.intData();
    if (val >= 60000) {
      s.intervalHistory = (unsigned long)val;
      Serial.printf("[Settings] interval_history  = %lu ms\n", s.intervalHistory);
    }
  } else {
    Serial.printf("[Settings] interval_history gagal dibaca, pakai default %lu ms\n",
                  s.intervalHistory);
  }

  return s;
}

// ══════════════════════════════════════════════════════════════
//  sendLog — push satu baris log ke Firebase
//
//  Path: /anemometer/{DEVICE_ID}/logs/{pushKey}
//    {msg: "...", timestamp: unix}
//
//  Dipakai untuk boot, OTA result, error penting.
//  Gagal kirim → hanya Serial, tidak trigger alarm/reboot.
// ══════════════════════════════════════════════════════════════
void sendLog(FirebaseData &fbdo, const String &msg) {
  String path = _basePath() + "/logs";

  FirebaseJson json;
  json.set("msg",       msg);
  json.set("timestamp", (int)time(NULL));

  if (Firebase.RTDB.pushJSON(&fbdo, path, &json)) {
    Serial.printf("[Log] OK: %s\n", msg.c_str());
  } else {
    Serial.printf("[Log] GAGAL kirim: %s\n", msg.c_str());
  }
}

// ══════════════════════════════════════════════════════════════
//  sendRealtime — update node realtime per interval
//
//  Path: /anemometer/{DEVICE_ID}/realtime
//    {speed_ms, speed_kmh, sample_count, k_faktor, timestamp}
// ══════════════════════════════════════════════════════════════
void sendRealtime(FirebaseData &fbdo,
                  float speedMS,
                  int   sampleCount,
                  const SensorSettings &settings,
                  FirebaseConfig &config) {

  static int ok = 0, fail = 0;

  String path = _basePath() + "/realtime";

  FirebaseJson json;
  json.set("speed_ms",     speedMS);
  json.set("speed_kmh",    speedMS * 3.6f);
  json.set("sample_count", sampleCount);
  json.set("k_faktor",     settings.kFaktor);
  json.set("timestamp",    (int)time(NULL));

  if (Firebase.RTDB.updateNode(&fbdo, path, &json)) {
    ok++;
    _consecutiveFail = 0;
    Serial.printf("[Firebase] Realtime OK (%d ok / %d fail)\n", ok, fail);

  } else {
    fail++;
    _consecutiveFail++;
    String reason = fbdo.errorReason();
    Serial.printf("[Firebase] Realtime GAGAL (%d ok / %d fail) — %s\n",
                  ok, fail, reason.c_str());
    bunyiAlarmError();

    if (reason.indexOf("token")    >= 0 ||
        reason.indexOf("expired")  >= 0 ||
        reason.indexOf("revoked")  >= 0 ||
        reason.indexOf("not ready") >= 0) {
      _tryReinitFirebase(config);
    }

    if (_consecutiveFail >= MAX_FAIL_BEFORE_REBOOT) {
      Serial.printf("[Firebase] Gagal %d kali berturut-turut → AUTO REBOOT!\n",
                    _consecutiveFail);
      delay(1000);
      ESP.restart();
    }
  }
}

// ══════════════════════════════════════════════════════════════
//  sendHistory — push rata-rata per interval history
//
//  Path: /anemometer/{DEVICE_ID}/history/{pushKey}
//    {avg_ms, avg_kmh, max_ms, sample_count, k_faktor,
//     interval_ms, timestamp}
// ══════════════════════════════════════════════════════════════
void sendHistory(FirebaseData &fbdo,
                 float avgSpeedMS,
                 float maxSpeedMS,
                 int   sampleCount,
                 const SensorSettings &settings,
                 FirebaseConfig &config) {

  String path = _basePath() + "/history";

  FirebaseJson json;
  json.set("avg_ms",       avgSpeedMS);
  json.set("avg_kmh",      avgSpeedMS * 3.6f);
  json.set("max_ms",       maxSpeedMS);
  json.set("sample_count", sampleCount);
  json.set("k_faktor",     settings.kFaktor);
  json.set("interval_ms",  (int)settings.intervalHistory);
  json.set("timestamp",    (int)time(NULL));

  if (Firebase.RTDB.pushJSON(&fbdo, path, &json)) {
    Serial.printf("[Firebase] History OK — avg=%.4f max=%.4f dari %d sample\n",
                  avgSpeedMS, maxSpeedMS, sampleCount);
  } else {
    String reason = fbdo.errorReason();
    Serial.printf("[Firebase] History GAGAL — %s\n", reason.c_str());
    bunyiAlarmError();

    if (reason.indexOf("token")     >= 0 ||
        reason.indexOf("expired")   >= 0 ||
        reason.indexOf("not ready") >= 0) {
      _tryReinitFirebase(config);
    }
  }
}
