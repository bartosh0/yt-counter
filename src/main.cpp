#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <LittleFS.h>

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>

#include <YoutubeApi.h>

#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4
#define CLK_PIN   14  // or SCK
#define DATA_PIN  13  // or MOSI
#define CS_PIN    15  // or SS

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

// Hardware SPI connection
// MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
MD_Parola P = MD_Parola(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

//define your default values here, if there are different values in config.json, they are overwritten.
char ytApiV3Key[40] = "YOUR_YT_API_KEY";                // YouTube Data API v3 key generated here: https://console.developers.google.com
char channelId[25] = "UCCJhqj2M7sY8vbuid1DjS_g";   // YT channel id

//flag for saving data
bool shouldSaveConfig = false;

//counter setup
WiFiClientSecure client;
YoutubeApi api(ytApiV3Key, client);

unsigned long timeBetweenRequests = 60 * 1000;  // 60 seconds, in milliseconds

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  P.begin();
  // P.displayClear();

  //clean FS, for testing
  //LittleFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");
  // P.displayScroll("FS...", PA_CENTER, PA_SCROLL_LEFT, 100);

  if (LittleFS.begin()) {
    Serial.println("mounted file system");
    if (LittleFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

 #if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
#else
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
#endif
          Serial.println("\nparsed json");
          strcpy(ytApiV3Key, json["ytApiV3Key"]);
          strcpy(channelId, json["channelId"]);
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
    // P.displayScroll("FS error", PA_CENTER, PA_SCROLL_LEFT, 100 );
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_ytApiV3Key("ytkey", "YouTube API v3 key", ytApiV3Key, 40);
  WiFiManagerParameter custom_channelId("channelid", "YouTube channel id", channelId, 25);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10, 0, 1, 99), IPAddress(10, 0, 1, 1), IPAddress(255, 255, 255, 0));

  //add all your parameters here
  wifiManager.addParameter(&custom_ytApiV3Key);
  wifiManager.addParameter(&custom_channelId);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  // P.displayScroll("WiFi ok", PA_CENTER, PA_SCROLL_LEFT, 100 );

  //read updated parameters
  strcpy(ytApiV3Key, custom_ytApiV3Key.getValue());
  strcpy(channelId, custom_channelId.getValue());
  Serial.println("The values in the file are: ");
  Serial.println("\tytApiV3Key : " + String(ytApiV3Key));
  Serial.println("\tchannelId : " + String(channelId));

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
 #if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
    DynamicJsonDocument json(1024);
#else
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
#endif
    json["ytApiV3Key"] = ytApiV3Key;
    json["channelId"] = channelId;

    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
    serializeJson(json, Serial);
    serializeJson(json, configFile);
#else
    json.printTo(Serial);
    json.printTo(configFile);
#endif
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  // P.displayScroll(WiFi.localIP().toString().c_str(), PA_CENTER, PA_SCROLL_LEFT, 100 );
  client.setInsecure();
  YoutubeApi api(ytApiV3Key, client);
}

void loop() {
  // put your main code here, to run repeatedly:
	if(api.getChannelStatistics(channelId)) {
		Serial.println("\n---------Stats---------");

    Serial.print("Subscriber Count: ");
    Serial.println(api.channelStats.subscriberCount);
    String subscriberCount = String(api.channelStats.subscriberCount);

		Serial.print("View Count: ");
		Serial.println(api.channelStats.viewCount);
    String viewCount = String(api.channelStats.viewCount);

		Serial.print("Video Count: ");
		Serial.println(api.channelStats.videoCount);
    String videoCount = String(api.channelStats.videoCount);

		// Probably not needed :)
		//Serial.print("hiddenSubscriberCount: ");
		//Serial.println(api.channelStats.hiddenSubscriberCount);

		Serial.println("------------------------");

    // P.displayScroll(subscriberCount.c_str(), PA_CENTER, PA_SCROLL_LEFT, 0 );
    // P.displayText("Test", PA_CENTER, 100, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);


    if (P.displayAnimate()) {
      P.displayText("Test", PA_CENTER, 100, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
      P.displayScroll("Test2", PA_CENTER, PA_SCROLL_LEFT, 100);
      P.displayReset(); // Restart animation when done
    }
	}
	delay(timeBetweenRequests);
}