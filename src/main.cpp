#include <FS.h>                   
#include <WiFiManager.h>          

#include <LittleFS.h>

#include <ArduinoJson.h>          

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>    

#include <YoutubeApi.h>

#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CLK_PIN   14  
#define DATA_PIN  13  
#define CS_PIN    15  

#define  DEBUG  0

#if  DEBUG
#define PRINT(s, x) { Serial.print(F(s)); Serial.print(x); }
#define PRINTS(x) Serial.print(F(x))
#define PRINTX(x) Serial.println(x, HEX)
#else
#define PRINT(s, x)
#define PRINTS(x)
#define PRINTX(x)
#endif

MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

char ytApiV3Key[40] = "YOUR_YT_API_KEY";                
char channelId[25] = "UCibs_X7BMO3mlhuUCMzlRHA";   

// Poprawka bezpieczeństwa: powiększenie bufora do 100, by pasował do rozmiaru w WiFiManager
char channelName[100] = "Pawulon i Przemas";   

bool shouldSaveConfig = false;

// Globalne instancje dla bezpieczeństwa pamięci RAM
WiFiClientSecure client;
YoutubeApi *api = nullptr;

// Parametry portalu - globalne, aby były dostępne w saveConfigCallback
WiFiManagerParameter* pYtKey = nullptr;
WiFiManagerParameter* pChannelId = nullptr;
WiFiManagerParameter* pChannelName = nullptr;

unsigned long timeBetweenRequests = 60 * 1000;  
unsigned long scrollSpeed = 100;  

// --- Struktura configu przechowywana w RTC memory ---
// RTC przetrwa soft restart, a zapis do RTC NIE dotyka flash (więc nie ma
// kolizji z WiFi/PM - unika wdt reset / pm_send_nullfunc crash).
struct RtcConfig {
  uint32_t magic = 0x59434F4E;   // "YCON"
  char ytApiV3Key[40] = "";
  char channelId[25] = "";
  char channelName[100] = "";
  bool dirty = false;   // czy czekamy na zapis do LittleFS przy starcie
};
RtcConfig rtcCfg;

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
  // Tylko kopiujemy świeże wartości z formularza portalu do globali i RTC.
  // NIE piszemy do LittleFS tutaj - zapis na flash w trakcie HTTP (kontekst
  // portalu) blokuje >8s i powoduje wdt reset.
  if (pYtKey) strlcpy(ytApiV3Key, pYtKey->getValue(), sizeof(ytApiV3Key));
  if (pChannelId) strlcpy(channelId, pChannelId->getValue(), sizeof(channelId));
  if (pChannelName) strlcpy(channelName, pChannelName->getValue(), sizeof(channelName));

  // Zapisz konfigu do RTC - przetrwa restart, nie dotyka flash (brak crashu PM/WDT)
  strlcpy(rtcCfg.ytApiV3Key, ytApiV3Key, sizeof(rtcCfg.ytApiV3Key));
  strlcpy(rtcCfg.channelId, channelId, sizeof(rtcCfg.channelId));
  strlcpy(rtcCfg.channelName, channelName, sizeof(rtcCfg.channelName));
  rtcCfg.dirty = true;
  ESP.rtcUserMemoryWrite(0, (uint32_t*)&rtcCfg, sizeof(rtcCfg));
  Serial.println("Config staged to RTC (flash write deferred to next boot)");
}

// --- Bezpieczny odczyt pojedynczej wartości string z pliku JSON config ---
// NIE używamy ArduinoJson do odczytu, bo jego alokacje na heapie (DynamicJsonDocument
// + deserializeJson) uszkadzają strukturę heapa w tym buildzie (ArduinoJson 7 + core
// 3.1.2), co objawia się Illegal instruction w br_rsa_public_get_default przy
// pierwszym połączeniu TLS (BearSSL) w tym samym przebiegu.
// Format pliku: {"ytApiV3Key":"...","channelId":"...","channelName":"..."}
void extractJsonString(const char* json, const char* key, char* out, size_t outSize) {
  char needle[40];
  snprintf(needle, sizeof(needle), "\"%s\":\"", key);
  const char* p = strstr(json, needle);
  if (p != nullptr) {
    p += strlen(needle);
    size_t i = 0;
    while (*p && *p != '"' && i < outSize - 1) {
      out[i++] = *p++;
    }
    out[i] = '\0';
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  P.begin();
  P.displayClear();
  P.setIntensity(2);

  // --- Odczyt RTC: jeśli mamy oczekujący config z RTC, zapisz go do LittleFS ---
  // TO robimy ZANIM WiFi jest aktywne, więc flash jest wolny (brak crashu PM/WDT).
  memset(&rtcCfg, 0, sizeof(rtcCfg));
  ESP.rtcUserMemoryRead(0, (uint32_t*)&rtcCfg, sizeof(rtcCfg));
  if (rtcCfg.magic == 0x59434F4E && rtcCfg.dirty) {
    Serial.println("RTC config found, persisting to LittleFS (radio idle)...");
    if (LittleFS.begin()) {
      File configFile = LittleFS.open("/config.json", "w");
      if (!configFile) {
        Serial.println("Failed to open config file for writing");
      } else {
        configFile.print("{\"ytApiV3Key\":\"");
        configFile.print(rtcCfg.ytApiV3Key);
        configFile.print("\",\"channelId\":\"");
        configFile.print(rtcCfg.channelId);
        configFile.print("\",\"channelName\":\"");
        configFile.print(rtcCfg.channelName);
        configFile.print("\"}");
        configFile.close();
        Serial.println("RTC config persisted to LittleFS");
      }
      LittleFS.end();
    }
    // Wyczyszcz flagę, żeby nie zapisywać ponownie
    rtcCfg.dirty = false;
    ESP.rtcUserMemoryWrite(0, (uint32_t*)&rtcCfg, sizeof(rtcCfg));
  }

  Serial.println("Mounting FS...");

if (LittleFS.begin()) {
    Serial.println("Mounted file system");
    if (LittleFS.exists("/config.json")) {
      Serial.println("Reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("Opened config file");
        // Mały bufor na stosie - BRAK alokacji na heapie (omija bug ArduinoJson/BearSSL)
        char buf[512];
        size_t rd = configFile.readBytes(buf, sizeof(buf) - 1);
        buf[rd] = '\0';
        configFile.close();

        // Ręczne parsowanie (bez ArduinoJson) - zachowuje wartości domyślne,
        // jeśli klucza nie ma w pliku.
        extractJsonString(buf, "ytApiV3Key", ytApiV3Key, sizeof(ytApiV3Key));
        extractJsonString(buf, "channelId", channelId, sizeof(channelId));
        extractJsonString(buf, "channelName", channelName, sizeof(channelName));
        Serial.println("\nParsed config (manual, no ArduinoJson)");
      }
    }
  } else {
    Serial.println("Failed to mount FS");
  }

  WiFiManagerParameter custom_ytApiV3Key("ytkey", "YouTube API v3 key", ytApiV3Key, 40);
  WiFiManagerParameter custom_channelId("channelid", "YouTube channel id", channelId, 25);
  WiFiManagerParameter custom_channelName("channelname", "YouTube channel name", channelName, 100);
  // Globalne wskaźniki dla saveConfigCallback
  pYtKey = &custom_ytApiV3Key;
  pChannelId = &custom_channelId;
  pChannelName = &custom_channelName;

  WiFiManager wifiManager;
  // wifiManager.resetSettings(); // uncomment to reset saved settings
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.addParameter(&custom_ytApiV3Key);
  wifiManager.addParameter(&custom_channelId);
  wifiManager.addParameter(&custom_channelName);

  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
    delay(5000);
  }

  Serial.println("Connected...yeey :)");
  // Znowu wyłącz power saving - WiFiManager mógł zmienić tryb radia podczas portalu
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  delay(100);

  P.displayText("WiFi OK", PA_CENTER, 100, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  while (!P.displayAnimate())
  {
    delay(1);
  }

  // Jeśli config NIE był zapisany w callbacku (np. autoConnect bez portalu),
  // skopiuj wartości z parametrów (domyślne/obecne) do globali.
  if (!shouldSaveConfig) {
    strlcpy(ytApiV3Key, custom_ytApiV3Key.getValue(), sizeof(ytApiV3Key));
    strlcpy(channelId, custom_channelId.getValue(), sizeof(channelId));
    strlcpy(channelName, custom_channelName.getValue(), sizeof(channelName));
  }
  
  Serial.println("The values in the file are: ");
  Serial.println("\tytApiV3Key : " + String(ytApiV3Key));
  Serial.println("\tchannelId : " + String(channelId));
  Serial.println("\tchannelName : " + String(channelName));

  if (shouldSaveConfig) {
    // Config został już zapisany do RTC w saveConfigCallback (bez dotykania flash).
    // Restartujemy, aby przy starcie (radio nieaktywne, flash wolny) RTC->LittleFS
    // zapis wykonał się bezpiecznie, a następnie TLS ruszył na czystym heapie.
    Serial.println("Config staged to RTC. Restarting to persist to LittleFS safely...");
    delay(500);
    // Zapisz RTC jeszcze raz (gwarancja, że restart zobaczy flagę dirty)
    rtcCfg.dirty = true;
    ESP.rtcUserMemoryWrite(0, (uint32_t*)&rtcCfg, sizeof(rtcCfg));
    ESP.restart();
    delay(5000);
  }

  String localIP = WiFi.localIP().toString();
  Serial.println("Local IP:");
  Serial.println(localIP);
  
  // Zostawiłem zakomentowane tak jak u Ciebie
  // P.displayText(localIP.c_str(), PA_CENTER, 100, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  // while (!P.displayAnimate())
  // {
  //   delay(1);
  // }
  
  // Konfiguracja API dopiero po nawiązaniu pełnego połączenia i zwoleniu pamięci (Optymalizacja)
  client.setInsecure();
  
  if (api != nullptr) {
    delete api;
  }
  api = new YoutubeApi(ytApiV3Key, client);

  // --- Poprawka: zmniejsz rozmiar buforów SSL (mniejsze zapotrzebowanie na heap BearSSL) ---
  // Większe bufory (1024) + BearSSL potrafią przekroczyć dostępny heap po restarcie,
  // co objawia się Illegal instruction w br_rsa_public_get_default.
  client.setBufferSizes(512, 512);

  // --- Poprawka: poczekaj aż heap osiągnie bezpieczny poziom przed pierwszym TLS ---
  // Po restarcie heap bywa zfragmentowany/uszczuplony (po portalu WiFiManager).
  // BearSSL alokuje duże bloki (context, iobuf, X.509) - jeśli zabraknie RAM,
  // std::make_shared / _alloc_iobuf zwraca nullptr i crashujemy w br_ssl_client_zero.
  Serial.printf("[HEAP] Free before TLS: %u\n", ESP.getFreeHeap());
  delay(200);
}

void fetchAndDisplayServerMessage() {
  Serial.println("[HTTP] Starting to fetch message from the server...");
  
  WiFiClient httpClient; 
  HTTPClient http;

  if (http.begin(httpClient, "http://counter.senshi.pl/index.php")) {
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("[HTTP] Received response: " + payload);

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);
      if (!error) {
        const char* displayText = doc["display_text"];
#else
      DynamicJsonBuffer jsonBuffer;
      JsonObject& doc = jsonBuffer.parseObject(payload);
      if (doc.success()) {
        const char* displayText = doc["display_text"];
#endif
        
        P.displayText(displayText, PA_CENTER, scrollSpeed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
        while (!P.displayAnimate()) {
          delay(1);
        }
        
      } else {
        Serial.println("[JSON] Failed to parse server response.");
      }
    } else {
      Serial.printf("[HTTP] GET Error, error code: %d\n", httpCode);
    }
    http.end(); 
  } else {
    Serial.println("[HTTP] Failed to connect to the server.");
  }
}

String formatNumbers(String numStr) {
  String result = "";
  int len = numStr.length();
  
  for (int i = 0; i < len; i++) {
    // Dodaj apostrof co 3 znaki od końca, ale nie na samym początku liczby
    if ((len - i) % 3 == 0 && i != 0) {
      result += "'";
    }
    result += numStr[i];
  }
  return result;
}

void loop() {
  // Wywołania api zmienione na api->
  // --- Poprawka: przy pierwszym połączeniu po restarcie, jeśli heap jest za mały,
  // odczekaj zanim spróbujesz TLS (unika crashu w br_rsa_public_get_default).
  if (ESP.getFreeHeap() < 30000) {
    Serial.printf("[HEAP] Low heap %u, waiting before TLS...\n", ESP.getFreeHeap());
    delay(3000);
  }

  if(api != nullptr && api->getChannelStatistics(channelId)) {
    Serial.println("\n---------Stats---------");
    Serial.print("Subscriber Count: ");
    Serial.println(api->channelStats.subscriberCount);
    String subscriberCount = formatNumbers(String(api->channelStats.subscriberCount));

    Serial.print("View Count: ");
    Serial.println(api->channelStats.viewCount);
    String viewCount = formatNumbers(String(api->channelStats.viewCount));

    Serial.print("Video Count: ");
    Serial.println(api->channelStats.videoCount);
    String videoCount = formatNumbers(String(api->channelStats.videoCount));
    Serial.println("------------------------");

    P.displayClear();

    // --- Channel Name ---
    P.displayText(channelName, PA_CENTER, scrollSpeed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    while (!P.displayAnimate()) delay(1);

    // --- Subs ---
    P.displayText("Suby", PA_CENTER, scrollSpeed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    while (!P.displayAnimate()) delay(1);
    
    P.displayText(subscriberCount.c_str(), PA_CENTER, scrollSpeed, 0, PA_SCROLL_LEFT, PA_NO_EFFECT);
    while (!P.displayAnimate()) delay(1);
    
    delay(timeBetweenRequests/3);
    
    P.displayText(subscriberCount.c_str(), PA_CENTER, scrollSpeed, 0, PA_NO_EFFECT, PA_SCROLL_LEFT);
    while (!P.displayAnimate()) delay(1);

    // --- Vids ---
    P.displayText("Filmy", PA_CENTER, scrollSpeed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    while (!P.displayAnimate()) delay(1);
    
    P.displayText(videoCount.c_str(), PA_CENTER, scrollSpeed, 0, PA_SCROLL_LEFT, PA_NO_EFFECT);
    while (!P.displayAnimate()) delay(1);
    
    delay(timeBetweenRequests/3);
    
    P.displayText(videoCount.c_str(), PA_CENTER, scrollSpeed, 0, PA_NO_EFFECT, PA_SCROLL_LEFT);
    while (!P.displayAnimate()) delay(1);

    // --- Views ---
    P.displayText("Wyswietlenia", PA_CENTER, scrollSpeed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    while (!P.displayAnimate()) delay(1);
    
    P.displayText(viewCount.c_str(), PA_CENTER, scrollSpeed, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
    while (!P.displayAnimate()) delay(1);

    // --- Server Message ---
    fetchAndDisplayServerMessage();
  } else {
    Serial.println("Failed to fetch YouTube statistics. Retrying...");
    delay(5000); 
  }
}