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
int lastSentHour = -1;

float totalSpeed   = 0;
float maxSpeed     = 0.0f;
int   jumlahSample = 0;


void setup() {
  Serial.begin(115200);
  delay(500);

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

    checkAndUpdateOTA(fbdo); 
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
  static unsigned long lastCmdCheck = 0;
  const  unsigned long CMD_CHECK_INTERVAL = 5000UL; // cek tiap 5 detik

  if (wifiIsConnected() && millis() - lastOtaCheck >= OTA_CHECK_INTERVAL) {
    lastOtaCheck = millis();
    checkAndUpdateOTA(fbdo);
  }

  // lalu di dalam loop(), setelah blok OTA check:
  if (wifiIsConnected() && millis() - lastCmdCheck >= CMD_CHECK_INTERVAL) {
  lastCmdCheck = millis();
  checkRemoteCommand(fbdo);
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
    float rps = pulsa / (intervalDetik * (float)gSettings.magnetCount);
    float speedMS = 2.0f * PI * gSettings.radiusM * rps * gSettings.kFaktor;

    Serial.printf("[Main] Pulsa: %d | RPS: %.3f | Speed: %.4f m/s (k=%.1f)\n",
                  pulsa, rps, speedMS, gSettings.kFaktor);

    // Kirim ke Firebase
    sendRealtime(fbdo, speedMS, pulsa, gSettings, fbConfig);

    // Akumulator untuk history
    totalSpeed += speedMS;
    if (speedMS > maxSpeed) maxSpeed = speedMS;
    jumlahSample++;
  }

  // ── HISTORY — push rata-rata tepat saat jam berganti ────────
  // configTime(7*3600) → localtime() sudah WIB, jadi 07:00/08:00/dst
  {
  time_t    nowT = time(NULL);
  struct tm* tNow = localtime(&nowT);

  if (tNow->tm_hour != lastSentHour && lastSentHour != -1) {
    // Jam baru → kirim akumulasi jam sebelumnya
    if (jumlahSample > 0) {
      float avgSpeed = totalSpeed / jumlahSample;
      Serial.printf("[Main] History jam %02d:00 — avg=%.4f max=%.4f dari %d sample\n",
                    lastSentHour, avgSpeed, maxSpeed, jumlahSample);
      sendHistory(fbdo, avgSpeed, maxSpeed, jumlahSample, gSettings, fbConfig);
      totalSpeed   = 0.0f;
      maxSpeed     = 0.0f;
      jumlahSample = 0;
        } else {
      Serial.printf("[Main] History jam %02d:00 skip — tidak ada sample.\n", lastSentHour);
        }
      }
  lastSentHour = tNow->tm_hour; // update selalu
  }
}
