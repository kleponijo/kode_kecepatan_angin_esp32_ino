// ═══════════════════════════════════════════════════════════════
//  kecepatan_angin.ino  —  Sistem Anemometer + WiFi Manager
//
//  Struktur file (satu folder, Arduino IDE compatible):
//  kecepatan_angin/
//  ├── kecepatan_angin.ino       ← file ini (entry point)
//  ├── cfg_config.h              ← [CFG] semua konstanta & pin
//  ├── wifi_manager.h            ← [WIFI] WiFi Manager + Captive Portal + Serial CMD
//  └── fb_firebase_helper.h      ← [FB] Firebase RTDB helper
//
//  ── Serial Commands (baud 115200) ────────────────────────────
//  SETWIFI:<SSID>,<PASSWORD>   → ganti WiFi, restart otomatis
//  GETWIFI                     → lihat SSID tersimpan
//  CLEARWIFI                   → hapus kredensial
//  RESTART                     → restart ESP
// ═══════════════════════════════════════════════════════════════

#include "cfg_config.h"
#include "wifi_manager.h"
#include "fb_firebase_helper.h"

// ── Firebase objects ──────────────────────────────────────────────
FirebaseData   fbdo;
FirebaseAuth   fbAuth;
FirebaseConfig fbConfig;

// ── Anemometer state ──────────────────────────────────────────────
volatile int  pulseCount  = 0;
unsigned long lastSecond  = 0;
unsigned long lastHistory = 0;

float totalSpeed   = 0;
int   jumlahSample = 0;

// ── ISR: hitung pulsa Hall sensor ─────────────────────────────────
void IRAM_ATTR hitungPulsa() {
  pulseCount++;
}

// ═════════════════════════════════════════════════════════════════
//  SETUP
// ═════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_HALL,   INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println("║    Sistem Anemometer Klimatologi IoT     ║");
  Serial.println("╚══════════════════════════════════════════╝");

  // 1. WiFi Manager: load NVS → coba konek → atau buka AP captive portal
  wifiManagerBegin();

  // 2. Firebase: hanya jika WiFi tersambung
  if (wifiIsConnected()) {
    setupFirebase(fbdo, fbAuth, fbConfig);
  } else {
    Serial.println("[Main] Mode AP aktif — Firebase dilewati.");
    Serial.printf( "[Main] Sambungkan HP ke hotspot \"%s\"\n", AP_SSID);
    Serial.println("[Main] lalu buka browser untuk isi WiFi baru.");
  }

  // 3. Attach interrupt Hall sensor
  attachInterrupt(digitalPinToInterrupt(PIN_HALL), hitungPulsa, FALLING);

  Serial.println("[Main] Setup selesai. Sistem aktif.\n");
}

// ═════════════════════════════════════════════════════════════════
//  LOOP
// ═════════════════════════════════════════════════════════════════
void loop() {
  // Selalu jalankan WiFi Manager:
  // menangani captive portal, serial commands, auto-reconnect
  wifiManagerLoop();

  // Hanya kirim data jika WiFi tersambung
  if (!wifiIsConnected()) return;

  // ── REALTIME (tiap INTERVAL_REALTIME ms) ─────────────────────
  if (millis() - lastSecond >= INTERVAL_REALTIME) {
    int pulsa  = pulseCount;
    pulseCount = 0;
    lastSecond = millis();

    float intervalDetik = INTERVAL_REALTIME / 1000.0;
    float speedMS = 2.0 * PI * RADIUS_M * (pulsa / intervalDetik) * K_FAKTOR;

    Serial.printf("[Main] Pulsa: %d | Speed: %.4f m/s\n", pulsa, speedMS);
    sendRealtime(fbdo, speedMS);

    totalSpeed += speedMS;
    jumlahSample++;
  }

  // ── HISTORY (tiap INTERVAL_HISTORY ms) ───────────────────────
  if (millis() - lastHistory >= INTERVAL_HISTORY) {
    lastHistory = millis();

    if (jumlahSample > 0) {
      float avgSpeed = totalSpeed / jumlahSample;
      Serial.printf("[Main] History — Avg: %.4f m/s dari %d sample\n",
                    avgSpeed, jumlahSample);
      sendHistory(fbdo, avgSpeed);
      totalSpeed   = 0;
      jumlahSample = 0;
    }
  }
}
