#include  <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

extern "C" {
#include "user_interface.h"
#include "wpa2_enterprise.h"
#include "c_types.h"
}

// SSID to connect to
char ssid[] = "iotroam";
//char username[] = "";
//char identity[] = "";
char password[] = "********";

const int DOORPIN = 16;
const String token = "********";

int prev_door;

// Retry a failed update this often instead of silently dropping it
const unsigned long RETRY_INTERVAL_MS = 10000;
unsigned long last_attempt = 0;

// Reboot if WiFi stays down this long, or if the heap is too fragmented for a TLS handshake
const unsigned long WIFI_DOWN_RESTART_MS = 5 * 60 * 1000;
const uint32_t MIN_FREE_BLOCK = 20000;
unsigned long wifi_down_since = 0;


uint8_t target_esp_mac[6] = {0x24, 0x0a, 0xc5, 0x9a, 0x58, 0x28};
void setup() {
  Serial.println("of brat");
  WiFi.mode(WIFI_STA);
  Serial.begin(9600);
  delay(1000);
  Serial.setDebugOutput(true);
  Serial.printf("SDK version: %s\n", system_get_sdk_version());
  Serial.printf("Free Heap: %4d\n",ESP.getFreeHeap());
  
  // Setting ESP into STATION mode only (no AP mode or dual mode)
  wifi_set_opmode(STATION_MODE);

  struct station_config wifi_config;

  memset(&wifi_config, 0, sizeof(wifi_config));
  strcpy((char*)wifi_config.ssid, ssid);
  strcpy((char*)wifi_config.password, password);

  wifi_station_set_config(&wifi_config);
  wifi_set_macaddr(STATION_IF,target_esp_mac);
  

  wifi_station_set_wpa2_enterprise_auth(1);

  // Clean up to be sure no old data is still inside
  wifi_station_clear_cert_key();
  wifi_station_clear_enterprise_ca_cert();
  wifi_station_clear_enterprise_identity();
  wifi_station_clear_enterprise_username();
  wifi_station_clear_enterprise_password();
  wifi_station_clear_enterprise_new_password();
  
  //wifi_station_set_enterprise_identity((uint8*)identity, strlen(identity));
  //wifi_station_set_enterprise_username((uint8*)username, strlen(username));
  wifi_station_set_enterprise_password((uint8*)password, strlen((char*)password));

  
  wifi_station_connect();
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  pinMode(DOORPIN, INPUT_PULLDOWN_16);
  // Force a status push on boot so the website is correct after a restart
  prev_door = -1;
}

bool sendStatus(int door) {
  // Force HTTPS
  String url = "https://cosmostue.nl/door-status?access_token=" + token + "&status=" + String(door);

  Serial.println("Attempting Request");
  Serial.printf("Free Heap: %d, Max Block: %d\n", ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());

  // Local client so the TLS buffers are always freed when this function returns
  WiFiClientSecure client;
  HTTPClient httpClient;
  client.setInsecure();

  // Don't keep the TLS connection open between requests (HTTPClient reuses by default)
  httpClient.setReuse(false);
  httpClient.setTimeout(10000);

  // Initialize HTTP Client
  if (!httpClient.begin(client, url)) {
    Serial.println("HTTP begin failed");
    return false;
  }

  // Add a standard Browser User-Agent (Servers often block "Arduino" or "ESP")
  httpClient.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");

  // Send request
  int httpCode = httpClient.GET();

  // Received response
  Serial.printf("HTTP Response Code: %d\n", httpCode);

  // end
  httpClient.end();
  client.stop();
  return httpCode >= 200 && httpCode < 300;
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (wifi_down_since == 0) {
      wifi_down_since = now;
      Serial.println("WiFi lost");
    } else if (now - wifi_down_since > WIFI_DOWN_RESTART_MS) {
      Serial.println("WiFi down too long, restarting");
      ESP.restart();
    }
    delay(1000);
    return;
  }
  wifi_down_since = 0;

  if (ESP.getMaxFreeBlockSize() < MIN_FREE_BLOCK) {
    Serial.println("Heap fragmented, restarting");
    ESP.restart();
  }

  int door = !digitalRead(DOORPIN);

  // Only mark the status as sent once the server accepted it; retry otherwise
  if (door != prev_door && (last_attempt == 0 || now - last_attempt > RETRY_INTERVAL_MS)) {
    last_attempt = now;
    if (sendStatus(door)) {
      prev_door = door;
      last_attempt = 0;
    }
  }

  delay(1000);
}
