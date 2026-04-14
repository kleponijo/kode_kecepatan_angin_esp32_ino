#include <WiFi.h>
#include "config.h"
#include "firebase_helper.h"

volatile int pulseCount = 0;

float kecepatan = 0;

unsigned long lastSecond = 0;

void IRAM_ATTR hitungPulsa() {
  pulseCount++;
}

void setup() {
  Serial.begin(115200);
  pinMode(pinHallEffect, INPUT_PULLUP);
  pinMode(pinBuzzer, OUTPUT);

  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");

    // Buzzer bunyi 2 kali
    for(int i = 0; i < 2; i++) {
      tone(pinBuzzer, 1000);
      delay(300); // bunyi 0.3 detik

      noTone(pinBuzzer);
      delay(700); // jeda supaya total 1 detik per beep
    }

    delay(5000);
    
  }

  tone(pinBuzzer, 1000);
  delay(500);
  noTone(pinBuzzer);
  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());

  attachInterrupt(digitalPinToInterrupt(pinHallEffect), hitungPulsa, FALLING);

  Serial.println("Sistem Anemometer Aktif");
}

void loop() {

  if (millis() - lastSecond >= intervalSecond) {

    int Rdetik = pulseCount;
    pulseCount = 0;

    kecepatan = 6.7824 * Rdetik * K;

    Serial.print("Realtime km/h: ");
    Serial.println(kecepatan);

    kirimRealtime(kecepatan, Rdetik);

    lastSecond = millis();
  }
}