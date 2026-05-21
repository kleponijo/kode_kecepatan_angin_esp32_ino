// ═══════════════════════════════════════════════════════════════
//  kecepatan_angin.ino  —  Sistem Anemometer + WiFi Manager
// ═══════════════════════════════════════════════════════════════

#include "cfg_config.h"
#include "wifi_manager_updated.h"
#include "fb_firebase_helper.h"
#include "ota_github.h"

FirebaseData   fbdo;
FirebaseAuth   fbAuth;
FirebaseConfig fbConfig;

// ── Settings dari Firebase (k_faktor, interval) ───────────────
SensorSettings gSettings;
unsigned long  lastSettingsSync   = 0;
const unsigned long SETTINGS_SYNC = 300000UL; // sync ulang tiap 5 menit

volatile int  pulseCount  = 0;

void IRAM_ATTR hitungPulsa() {
  pulseCount++;
}

unsigned long lastRealtime = 0;
unsigned long lastHistory = 0;

float totalSpeed   = 0;
float maxSpeed     = 0.0f;
int   jumlahSample = 0;


void setup() {
  Serial.begin(115200);
  delay(500);
  randomSeed(analogRead(0));

  pinMode(PIN_HALL,   INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  Serial.println("\n╔══════════════════════════════════════════╗");
  Serial.println("║    Sistem Anemometer Klimatologi IoT     ║");
  Serial.println("╚══════════════════════════════════════════╝");

  wifiManagerBegin();

  if (wifiIsConnected()) {
    setupFirebase(fbdo, fbAuth, fbConfig);

    // Baca settings awal dari Firebase
    gSettings = fetchSettings(fbdo);

    // Log boot ke Firebase
    String bootMsg = "Boot OK | FW=" + String(FIRMWARE_VERSION)
                   + " | k=" + String(gSettings.kFaktor, 2)
                   + " | RT=" + String(gSettings.intervalRealtime / 1000) + "s"
                   + " | H=" + String(gSettings.intervalHistory / 60000) + "m";
    sendLog(fbdo, bootMsg);

    checkAndUpdateOTA(); 
  } else {
    Serial.println("[Main] Mode AP aktif — Firebase dilewati.");
    Serial.printf( "[Main] Sambungkan HP ke hotspot \"%s\"\n", AP_SSID);
  }

  attachInterrupt(digitalPinToInterrupt(PIN_HALL), hitungPulsa, FALLING);
  Serial.println("[Main] Setup selesai. Sistem aktif.\n");
}

void loop() {
  wifiManagerLoop();

   static unsigned long lastOtaCheck = 0;
  if (wifiIsConnected() && millis() - lastOtaCheck >= OTA_CHECK_INTERVAL) {
    lastOtaCheck = millis();
    checkAndUpdateOTA();
  }

  // ── Sync settings dari Firebase tiap 5 menit ───────────────
  // (k_faktor, interval realtime, interval history)
  if (wifiIsConnected() && millis() - lastSettingsSync >= SETTINGS_SYNC) {
    lastSettingsSync = millis();
    gSettings = fetchSettings(fbdo);
  }

  if (!wifiIsConnected()) return;

  // ── REALTIME ─────────────────────────────────────────────────
  // ── REALTIME — kirim speed rata-rata per interval ───────────
  if (millis() - lastRealtime >= gSettings.intervalRealtime) {
    // Ambil pulsa secara atomic
    noInterrupts();
    int pulsa  = pulseCount;
    pulseCount = 0;
    interrupts();

    lastRealtime = millis();

    float intervalDetik = gSettings.intervalRealtime / 1000.0f;
    float rps     = pulsa / intervalDetik;                    // rotasi per detik
    float speedMS;

#if ENABLE_DUMMY_DATA

  // Dummy speed 0.5 - 12 m/s
  speedMS = random(5, 120) / 10.0;

  // Simulasi pulsa berdasarkan speed dummy
  pulsa = random(1, 20);

#else

  speedMS = 2.0f * PI * RADIUS_M * rps * gSettings.kFaktor;

#endif

    Serial.printf("[Main] Pulsa: %d | RPS: %.3f | Speed: %.4f m/s (k=%.1f)\n",
                  pulsa, rps, speedMS, gSettings.kFaktor);

    // Kirim ke Firebase
    sendRealtime(fbdo, speedMS, pulsa, gSettings, fbConfig);

    // Akumulator untuk history
    totalSpeed += speedMS;
    if (speedMS > maxSpeed) maxSpeed = speedMS;
    jumlahSample++;
  }

  // ── HISTORY — push rata-rata per interval ───────────────────
  if (millis() - lastHistory >= gSettings.intervalHistory) {
    lastHistory = millis();

    if (jumlahSample > 0) {
      float avgSpeed = totalSpeed / jumlahSample;

      sendHistory(fbdo, avgSpeed, maxSpeed, jumlahSample, gSettings, fbConfig);

      // Reset akumulator
      totalSpeed   = 0.0f;
      maxSpeed     = 0.0f;
      jumlahSample = 0;
    } else {
      Serial.println("[Main] History skip — belum ada sample.");
    }
  }
}
