#include "main.h"
#include "ota.h"

#define LED_ON digitalWrite(LED_BUILTIN, LOW)
#define LED_OFF digitalWrite(LED_BUILTIN, HIGH)
#define IS_LED_ON digitalRead(LED_BUILTIN)==LOW?true:false
#define BLINK_LED if (IS_LED_ON) {LED_OFF;} else {LED_ON;}

unsigned long last_oled_update = 0;
#define OLED_UPDATE_INTERVAL 500L

double min_speed=0,max_speed=0,actual_speed=0;
double min_alt=0,max_alt=0,actual_alt=0;

void listDir(const char * dirname, uint8_t levels) {
    log_printf("Listing directory: %s\r\n", dirname);

    Dir root = LittleFS.openDir(dirname);
    while (root.next()) {
        File file = root.openFile("r");
        if (file.isDirectory()) {
            log_print("DIR:");
            log_println(file.name());
            if(levels) {
                listDir(file.fullName(), levels - 1);
            }
            file.close();
        } else {
            log_print("FILE:");
            log_print(file.name());
            log_print("\tSIZE:");
            log_printfln("%d",file.size());
        }
    }
}

int no_gps_lock_counter = 0;
int no_location_update_counter = 0;
bool gps_debug = false;
Ticker output_ticker;
SoftwareSerial uart_gps;
TinyGPSPlus gps;
Adafruit_SSD1306 display = Adafruit_SSD1306(128, 64, &Wire, -1);
double old_lat;
double old_lon;
WiFiUDP udp;

double last_known_lat = ASSISTNOW_START_LAT;
double last_known_lon = ASSISTNOW_START_LON;
double last_known_alt = ASSISTNOW_START_ALT;

void udpBroadcast(const char *message) {
    if (!WiFi.isConnected()) return;
    if (udp.beginPacket(WiFi.broadcastIP(),UDP_BROADCAST_PORT)) {
        udp.write(message);
        udp.endPacket();
    }
}

void oled_setup() {
    Wire.begin(14,12);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.setTextSize(1);      // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE); // Draw white text
    display.setCursor(0, 0);     // Start at top-left corner
    display.cp437(true);         // Use full 256 char 'Code Page 437' font
    display.printf(SW_NAME " v" SW_VERSION "\n");
    display.display();
}

#define DISPLAYP(x) {display.print(x);display.display();}

void setup() {
    oled_setup();
    DISPLAYP("OTA-");
    ota_setup();
    pinMode(LED_BUILTIN, OUTPUT);
    LED_OFF;
    DISPLAYP("NET-")
    time_setup();
    wifi_connect();    
    log_print("\n\n***BOOTING APP***\n\n");

    DISPLAYP("FS-")
    if(!LittleFS.begin()) {
        log_print("LittleFS Mount Failed");
        return;
    }
    listDir("/",1);
    log_printfln("BUILD: %s %s", __DATE__, __TIME__);

    // if (!LittleFS.exists(OWNER_FILENAME)) {
    //     File owner_file = LittleFS.open(OWNER_FILENAME, "w");
    //     owner_file.print(OWNER_CONTENT);
    //     owner_file.close();
    //     log_println("OWNER file written.");
    // } else {
    //     log_println("OWNER file present.");
    // }

    // if (LittleFS.exists(LAST_KNOWN_LOCATION)) {
    //     log_println("LAST_KNOWN_LOCATION file present.");
    //     File last_known_location_file = LittleFS.open(LAST_KNOWN_LOCATION, "r");
    //     last_known_lat = last_known_location_file.readStringUntil('\n').toDouble();
    //     last_known_lon = last_known_location_file.readStringUntil('\n').toDouble();
    //     last_known_alt = last_known_location_file.readStringUntil('\n').toDouble();
    //     last_known_location_file.close();
    //     log_printfln("Last known location: lat:%.6f, lon:%.6f, alt:%.6f", last_known_lat, last_known_lon, last_known_alt);
    // }

    // if (LittleFS.exists(GPS_FILENAME)) {
    //     File gpsFile = LittleFS.open(GPS_FILENAME, "r");
    //     if (gpsFile.size() > strlen(GPS_COLUMN_HEADERS) + 2) {
    //         log_printfln("GPS file with data present with %d bytes.", gpsFile.size());
    //         upload_gps_file();
    //     } else if (gpsFile.size() == strlen(GPS_COLUMN_HEADERS) + 2) {
    //         log_println("GPS file present and without data.");
    //     } else {
    //         log_println("GPS file smaller than expected - deleting it.");
    //         LittleFS.remove(GPS_FILENAME);
    //     }
    // }
    // init_gps_file();

    DISPLAYP("GPS-")
    uart_gps.begin(9600,SWSERIAL_8N1,5,4);
    log_println("GPS module connection started.");
    output_ticker.attach(TICK_INTERVAL, persist_location_record);
}

void init_gps_file() {
    if (LittleFS.exists(GPS_FILENAME)) {
        return;
    }
    File gpsFile = LittleFS.open(GPS_FILENAME, "w");
    gpsFile.println(GPS_COLUMN_HEADERS);
    gpsFile.close();
    log_println("GPS file initialized.");
}

bool isTimeValid = false;

void timeIsValid() {
    isTimeValid = true;
    log_println("System time has been set.");
}

void time_setup() {
    settimeofday_cb(timeIsValid);
    sntp_servermode_dhcp(0);
    configTime(TZ_Etc_UTC, "pool.ntp.org", "time.nist.gov", "ntp1.inrim.it");
}

bool wifi_connect() {
    const char *found_ssid = NULL;
    int n = 0;

    for (int i = 0; i < 3; i++) {
        n = WiFi.scanNetworks();
        if (n > 0) {
            break;
        }
        delay(250);
    }

    for (int i = 0; i < n; ++i) {
        int j = 0;
        while (WIFI_CREDENTIALS[j][0] != NULL) {
            if (WiFi.SSID(i) == WIFI_CREDENTIALS[j][0]) {
                found_ssid = WIFI_CREDENTIALS[j][0];
                const char *passphrase = WIFI_CREDENTIALS[j][1];
                WiFi.begin(found_ssid, passphrase);
                break;
            }
            j++;
        }
    }

    if (found_ssid == NULL) {
        log_println("No known WiFi found.");
        WiFi.mode(WIFI_OFF);
        return false;
    }

    log_printfln("Connecting to WiFi: %s ...", found_ssid);
    WiFi.mode(WIFI_STA);
    int tries = 50;
    while (WiFi.status() != WL_CONNECTED && tries > 0) {
        delay(250);
        tries--;
    }
    if (tries == 0) {
        log_println("Failed to connect to WiFi!");
        WiFi.mode(WIFI_OFF);
        return false;
    }
    
    log_print("IP: ");
    log_println(WiFi.localIP().toString());
    return true;
}

void oled_update() {
    unsigned long now = millis();
    if (now-last_oled_update>OLED_UPDATE_INTERVAL) {
        last_oled_update = now; 
        display.clearDisplay();
        display.setCursor(0,0);
        display.setTextSize(1);
        if (gps.satellites.isValid()) display.printf("S:%02u ",gps.satellites.value());
        else display.print("S:-- ");
        if (gps.hdop.isValid()) display.printf("HDOP:%05.2f ",gps.hdop.hdop());
        else display.print("HDOP:--.-- ");
        if (gps.location.isValid() && gps.altitude.isValid() && gps.speed.isValid()) display.println("3DFIX");
        else if (gps.location.isValid()) display.println("  FIX");
        else display.println("NOFIX");
        if (gps.date.isValid()) display.printf("%04u-%02u-%02u   %02u:%02u:%02u\n",gps.date.year(),gps.date.month(),gps.date.day(),gps.time.hour(),gps.time.minute(),gps.time.second());
        else display.println("YYYY-MM-DD   HH:MM:SS");
        display.setTextSize(2);
        if (gps.speed.isValid()) {
            double actual_speed = gps.speed.kmph();
            min_speed=actual_speed<min_speed?actual_speed:min_speed;
            max_speed=actual_speed>max_speed?actual_speed:max_speed;
            display.printf("%06.2f km/h\n",max_speed);
        }
        if (gps.altitude.isValid()) {
            double actual_alt = gps.altitude.meters();
            min_alt=actual_alt<min_alt?actual_alt:min_alt;
            max_alt=actual_alt>max_alt?actual_alt:max_alt;
            display.printf("%06.1f m",max_alt);
        }
        display.display();
    }
}

void loop() {
    if (uart_gps.available() > 0) {
        int b = uart_gps.read();
        if (gps_debug) Serial.printf("%c", b);
        gps.encode(b);
    }
    check_incoming_commands();
    oled_update();
    ESP.wdtFeed();
    yield();
}

void check_incoming_commands() {
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        if (command == "g" || command == "gps") {
            gps_debug = !gps_debug;
            log_printfln("Toggling GPS debug %s...", gps_debug ? "on" : "off");
        } else if (command == "r" || command == "reset") {
            log_println("Restarting ESP...");
            ESP.restart();
        } else if (command == "u" || command == "upload") {
            log_println("Executing upload routine...");
            upload_gps_file();
        }
    }
}

void persist_location_record() {
    static size_t tick_counter = 0;
    bool isLocationValid = gps.location.isValid();
    bool isLocationUpdated = gps.location.isUpdated();
    
    tick_counter++;

    if (isLocationValid) {
        if (tick_counter % int(1/TICK_INTERVAL) == 0) { BLINK_LED }
    }
    else {
        BLINK_LED
    }

    if (tick_counter % int(GPS_LOG_INTERVAL / TICK_INTERVAL) == 0) { //do checks every 1 second
        if (!isLocationValid) {
            if (no_gps_lock_counter++ % int(60.0 / GPS_LOG_INTERVAL) == 0) {
                log_printfln(TIMESTAMP_FORMAT ": No GPS lock: %u satellites.",
                            TIMESTAMP_ARGS,
                            gps.satellites.value());
            }
            return;
        }
        no_gps_lock_counter = 0;

        if (!isLocationUpdated) {
            if (no_location_update_counter++ % int(60.0 / GPS_LOG_INTERVAL) == 0) {
                log_printfln(TIMESTAMP_FORMAT ": Last GPS location update was %.0f seconds ago.",
                            TIMESTAMP_ARGS,
                            gps.location.age() / 1000.0);
            }
            return;
        }
        no_location_update_counter = 0;

        double new_lat = gps.location.lat();
        double new_lon = gps.location.lng();
        double distance_travelled = gps.distanceBetween(old_lat, old_lon, new_lat, new_lon);
        if (distance_travelled < 5.0) {
            log_printfln(TIMESTAMP_FORMAT ": Only travelled %.2f meters since last update - skipping.",
                        TIMESTAMP_ARGS,
                        distance_travelled);
            return;
        } else {
            old_lat = new_lat;
            old_lon = new_lon;
        }

        char record[80];
        snprintf(record, sizeof(record), TIMESTAMP_FORMAT ";%.6f;%.6f;%.2f;%.2f;%d",
                TIMESTAMP_ARGS,
                new_lat, new_lon, gps.altitude.meters(), gps.speed.mps(), gps.satellites.value());

        // File gpsFile = LittleFS.open(GPS_FILENAME, "a");
        // gpsFile.println(record);
        // gpsFile.close();
        udpBroadcast(record);

        // if (tick_counter % (300 / TICK_INTERVAL) == 0) {
        //     File last_known_location_file = LittleFS.open(LAST_KNOWN_LOCATION, "w");
        //     snprintf(record, sizeof(record), "%.6f", last_known_lat);
        //     last_known_location_file.println(record);
        //     snprintf(record, sizeof(record), "%.6f", last_known_lon);
        //     last_known_location_file.println(record);
        //     snprintf(record, sizeof(record), "%.6f", last_known_alt);
        //     last_known_location_file.println(record);
        //     last_known_location_file.close();
        // }

        Serial.println(record);
    }
}

void upload_gps_file() {
    if (!WiFi.isConnected()) return;
    log_printfln("Connecting to upload server at %s://%s:%d ...", (USE_SERVER_TLS ? "https" : "http"), UPLOAD_SERVER_HOST, UPLOAD_SERVER_PORT);

#if USE_SERVER_TLS
    WiFiClientSecure client;
    client.setFingerprint(UPLOAD_SERVER_TLS_FINGERPRINT);
    log_printfln("Set upload server HTTPS fingerprint: %s", UPLOAD_SERVER_TLS_FINGERPRINT);
#else
    WiFiClient client;
#endif
    if (!client.connect(UPLOAD_SERVER_HOST, UPLOAD_SERVER_PORT)) {
        log_printfln("Failed to connect to upload server!");
        return;
    }

    HTTPClient http;
    http.begin(client, UPLOAD_SERVER_HOST, UPLOAD_SERVER_PORT, UPLOAD_SERVER_PATH, USE_SERVER_TLS);

    log_printfln("Uploading GPS file to server...");

    http.useHTTP10();
    http.addHeader(UPLOAD_SERVER_MAGIC_HEADER, UPLOAD_SERVER_MAGIC_HEADER_VALUE);
    http.addHeader("Content-Type", "text/plain");

    File gpsFile = LittleFS.open(GPS_FILENAME, "r");
    int http_code = http.sendRequest("POST", &gpsFile, gpsFile.size());
    gpsFile.close();
    client.stop();

    if (http_code == HTTP_CODE_OK) {
        log_println("GPS file successfully uploaded!");

        char newFilename[64];
        time_t t = time(nullptr);
        snprintf(newFilename, sizeof(newFilename), "GPS-%lld.TXT", t);
        LittleFS.rename(GPS_FILENAME, newFilename);
        log_printfln("GPS file renamed to %s", newFilename);

        init_gps_file();
    } else {
        log_printfln("Upload failed with unexpected %d HTTP status code.", http_code);
    }

    return;
}