#pragma once

// ═══════════════════════════════════════════════════════════════
//  fb_firebase_helper.h  —  Firebase RTDB: Setup, Realtime, History
// ═══════════════════════════════════════════════════════════════

#include <Firebase_ESP_Client.h>
#include <time.h>
#include "cfg_config.h"

// ── Alarm error ───────────────────────────────────────────────────
void bunyiAlarmError() {
  for (int i = 0; i < 10; i++) {
    tone(PIN_BUZZER, 2000); delay(100);
    noTone(PIN_BUZZER);     delay(100);
  }
}

// ── Tunggu NTP sinkron ────────────────────────────────────────────
void waitNTP() {
  Serial.print("[Firebase] Sinkronisasi NTP");
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");  // WIB UTC+7

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

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.print("[Firebase] Menunggu ready");
  int retry = 0;
  while (!Firebase.ready() && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (Firebase.ready()) Serial.println("\n[Firebase] READY!");
  else {
    Serial.println("\n[Firebase] GAGAL READY");
    bunyiAlarmError();
  }
}

// ── Kirim Realtime ────────────────────────────────────────────────
void sendRealtime(FirebaseData &fbdo, float speedMS) {
  static int ok = 0, fail = 0;

  FirebaseJson json;
  json.set("speed",     speedMS);
  json.set("timestamp", (int)time(NULL));

  if (Firebase.RTDB.updateNode(&fbdo, "/anemometer/realtime", &json)) {
    ok++;
    Serial.printf("[Firebase] Realtime OK (%d ok / %d fail)\n", ok, fail);
  } else {
    fail++;
    Serial.printf("[Firebase] Realtime GAGAL (%d ok / %d fail) — %s\n",
                  ok, fail, fbdo.errorReason().c_str());
    bunyiAlarmError();
  }
}

// ── Kirim History ─────────────────────────────────────────────────
void sendHistory(FirebaseData &fbdo, float avgSpeedMS) {
  FirebaseJson json;
  json.set("speed",     avgSpeedMS);
  json.set("timestamp", (int)time(NULL));

  if (Firebase.RTDB.pushJSON(&fbdo, "/anemometer/history", &json))
    Serial.println("[Firebase] History OK");
  else {
    Serial.printf("[Firebase] History GAGAL — %s\n", fbdo.errorReason().c_str());
    bunyiAlarmError();
  }
}
