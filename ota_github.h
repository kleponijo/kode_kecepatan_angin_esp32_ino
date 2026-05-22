#pragma once

// ═══════════════════════════════════════════════════════════════
//  ota_github.h  —  OTA Update dari GitHub Releases
//
//  Alur:
//  1. ESP cek versi terbaru via GitHub API
//  2. Jika ada versi baru → download .bin dari GitHub Releases
//  3. Flash otomatis → restart dengan firmware baru
// ═══════════════════════════════════════════════════════════════

#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "cfg_config.h"
#include "fb_firebase_helper.h"

// ── Cek & jalankan OTA dari GitHub ───────────────────────────────
void checkAndUpdateOTA(FirebaseData &fbdo) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[OTA] WiFi tidak terhubung, skip.");
    return;
  }

  Serial.println("[OTA] Memeriksa update dari GitHub...");
  Serial.printf( "[OTA] Versi saat ini: %s\n", FIRMWARE_VERSION);

  // ── Bangun URL GitHub API ──────────────────────────────────────
  String apiUrl = "https://api.github.com/repos/";
  apiUrl += GITHUB_USER;
  apiUrl += "/";
  apiUrl += GITHUB_REPO;
  apiUrl += "/releases/latest";

  WiFiClientSecure client;
  client.setInsecure(); // skip verifikasi SSL (cukup untuk ESP32)

  HTTPClient http;
  http.begin(client, apiUrl);
  http.addHeader("User-Agent", "ESP32-Anemometer");
  http.setTimeout(10000);

  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("[OTA] Gagal cek GitHub: HTTP %d\n", httpCode);
     // ── BARIS 40: log gagal cek GitHub ──────────────────────────
    sendLog(fbdo, "OTA: gagal cek GitHub HTTP " + String(httpCode));
    http.end();
    return;
  }

  // ── Parse JSON — filter hanya field yang dibutuhkan ────────────
  // (hemat memori, tidak parse semua response)
  StaticJsonDocument<128> filter;
  filter["tag_name"]                              = true;
  filter["assets"][0]["name"]                     = true;
  filter["assets"][0]["browser_download_url"]     = true;

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, http.getStream(),
                               DeserializationOption::Filter(filter));
  http.end();

  if (err) {
    Serial.printf("[OTA] Gagal parse JSON: %s\n", err.c_str());
    // ── BARIS 57: log gagal parse ────────────────────────────────
    sendLog(fbdo, "OTA: gagal parse JSON — " + String(err.c_str()));
    return;
  }

  String latestTag = doc["tag_name"].as<String>();
  Serial.printf("[OTA] Versi terbaru di GitHub: %s\n", latestTag.c_str());

  // ── Bandingkan versi ───────────────────────────────────────────
  if (latestTag == String(FIRMWARE_VERSION)) {
    Serial.println("[OTA] Firmware sudah terbaru. Tidak ada update.");
     sendLog(fbdo, "OTA: firmware sudah terbaru (" + String(FIRMWARE_VERSION) + ")");
    return;
  }

  Serial.printf("[OTA] Update tersedia! %s → %s\n",
                FIRMWARE_VERSION, latestTag.c_str());

   // ── BARIS 71: log ada update tersedia ────────────────────────
  sendLog(fbdo, "OTA: update tersedia " + String(FIRMWARE_VERSION) + " → " + latestTag);

  // ── Cari file .bin di assets ───────────────────────────────────
  String binUrl = "";
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    String nama = asset["name"].as<String>();
    if (nama.endsWith(".bin")) {
      binUrl = asset["browser_download_url"].as<String>();
      break;
    }
  }

  if (binUrl.isEmpty()) {
    Serial.println("[OTA] File .bin tidak ditemukan di release!");
    Serial.println("[OTA] Pastikan kamu upload file .bin saat buat release.");
    sendLog(fbdo, "OTA: GAGAL — .bin tidak ditemukan di release " + latestTag);
    return;
  }

  // ── BARIS 88: log SEBELUM flash (setelah flash ESP langsung ──
  // ── restart, jadi log sesudah tidak akan sempat terkirim) ────
  sendLog(fbdo, "OTA: mulai flash → " + binUrl);
  Serial.println("[OTA] Mulai download & flash...");
  Serial.println("[OTA] URL: " + binUrl);

  WiFiClientSecure updateClient;
  updateClient.setInsecure();
  updateClient.setTimeout(60000); // 60 detik untuk download

  httpUpdate.rebootOnUpdate(true);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // ikuti semua redirect otomatis

  Serial.println("[OTA] Mulai flash dari: " + binUrl);
  t_httpUpdate_return ret = httpUpdate.update(updateClient, binUrl);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("[OTA] GAGAL: (%d) %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      sendLog(fbdo, "OTA: GAGAL flash — " + httpUpdate.getLastErrorString());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[OTA] Tidak ada update (server bilang sama).");
      sendLog(fbdo, "OTA: server bilang tidak ada update");
      break;
    case HTTP_UPDATE_OK:
     // Tidak akan pernah sampai sini karena rebootOnUpdate(true)
      Serial.println("[OTA] Berhasil! ESP akan restart...");
      break;
  }
}