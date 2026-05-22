#pragma once

// ═══════════════════════════════════════════════════════════════
//  cfg_config.h  —  Konfigurasi Global Sistem Anemometer
//
//  ⚠️  WiFi SSID & Password TIDAK hardcode di sini.
//      Disimpan di NVS (flash) oleh wifi_manager.h
//      dan bisa diubah via Captive Portal atau Serial Monitor.
// ═══════════════════════════════════════════════════════════════

// --- Firebase ---
const char* DATABASE_URL = "https://klimatologiot-default-rtdb.asia-southeast1.firebasedatabase.app";
const char* API_KEY      = "AIzaSyAZrk_k4DQ_ijCa6gp67oRklFMKD2dLcbQ";

// --- Firebase Auth ---
const char* FB_EMAIL    = "coba@gmail.com";
const char* FB_PASSWORD = "coba123";

// --- Pin ---
#define PIN_HALL   32
#define PIN_BUZZER 18

// --- Parameter Anemometer ---
const float RADIUS_M = 0.08;  // jari-jari lengan anemometer (meter) = 8 cm

// --- Default settings (dipakai jika Firebase belum ada / gagal baca) ---
const float          DEFAULT_K_FAKTOR          = 50.0;
const unsigned long  DEFAULT_INTERVAL_REALTIME = 1000UL;      // 1 detik
const unsigned long  DEFAULT_INTERVAL_HISTORY  = 3600000UL;   // 1 jam

// --- Device Identity ---
// Isi manual untuk tiap ESP agar path Firebase tidak tabrakan.
// "esp_percobaan" untuk ESP indoor/test, "esp_lapangan" untuk ESP outdoor.
const char* DEVICE_ID = "esp_lapangan"; // ← ganti ke "esp_lapangan" untuk ESP outdoor

// --- WiFi Manager Settings ---
const char*          AP_SSID        = "Anemometer-Setup"; // nama hotspot captive portal
const char*          AP_PASSWORD    = "";                  // kosong = open AP
const int            WIFI_MAX_RETRY = 10;                  // retry sebelum masuk AP mode
const unsigned long  WIFI_RETRY_DELAY = 500;               // jeda antar retry (ms)

const char* FIRMWARE_VERSION   = "v1.0.0";         // ← ganti tiap mau update

const char* GITHUB_USER        = "kleponijo";   // ← isi username GitHub kamu
const char* GITHUB_REPO        = "kecepatan_angin";  // ← isi nama repo GitHub kamu
const unsigned long OTA_CHECK_INTERVAL = 3600000UL; // cek tiap 1 jam (ms)
