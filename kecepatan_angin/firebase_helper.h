#include <Firebase_ESP_Client.h>
#include <time.h>

void setupFirebase(FirebaseData &fbdo, FirebaseAuth &auth, FirebaseConfig &config) {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Pakai UTC 0 agar fleksibel di aplikasi
  configTime(0, 0, "pool.ntp.org"); 
}

void sendRealtime(FirebaseData &fbdo, float speed) {
  FirebaseJson json;
  json.set("speed", speed);
  json.set("timestamp", (int)time(NULL)); 
  Firebase.RTDB.updateNode(&fbdo, "/anemometer/realtime", &json);
}

void sendHistory(FirebaseData &fbdo, float avgSpeed) {
  FirebaseJson json;
  json.set("speed", avgSpeed);
  json.set("timestamp", (int)time(NULL));
  Firebase.RTDB.pushJSON(&fbdo, "/anemometer/history", &json);
}