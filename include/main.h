#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <SoftwareSerial.h>
#include <Ticker.h>
#include <TinyGPS++.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <sys/time.h>
#include <TZ.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <sntp.h>
#include <coredecls.h>
#include <WifiUdp.h>

#include "ota.h"
#include "customizations.h"
#include "logging.h"
#include "wifi_credentials.h"

void setup();
void init_gps_file();
void upload_gps_file();
void init_assistnow();
bool download_online_blob(time_t epoch_time, struct tm *time_info);
bool download_offline_blob(time_t epoch_time, struct tm *time_info);
void load_assistnow_blob(String filename);
bool connect_wifi();
void check_incoming_commands();
void persist_location_record();
void udpBroadcast(char *);

#define FORMAT_LITTLEFS_IF_FAILED true
#define OWNER_FILENAME "OWNER.TXT"
#define LAST_KNOWN_LOCATION "LAST_KNOWN_LOCATION.TXT"
#define GPS_FILENAME "GPS.TXT"
#define GPS_COLUMN_HEADERS "timestamp;latitude;longitude;altitude;speed;sats"
#define GPS_LOG_INTERVAL 1
#define TICK_INTERVAL 0.1
#define SSD1306_NO_SPLASH 
#define UDP_LISTEN_PORT 8081
#define UDP_BROADCAST_PORT 8081

#define TIMESTAMP_FORMAT "%04d-%02d-%02dT%02d:%02d:%02dZ"
#define TIMESTAMP_ARGS gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second()

#define ASSISTNOW_ONLINE_BASE_URL "http://online-live1.services.u-blox.com/GetOnlineData.ashx"
#define ASSISTNOW_OFFLINE_BASE_URL "http://offline-live1.services.u-blox.com/GetOfflineData.ashx"
