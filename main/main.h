#define MAIN_H

#define INSTITUTE

#define WITH_DISPLAY
#define SEND_DELAY 10 // Seconds

#ifdef INSTITUTE
#define WIFI_SSID "CAPS-Seminar-Room"
#define WIFI_PASS "REDACTED"

#define SNTP_SERVER "ntp1.in.tum.de"

#endif

// #define SNTP_SERVER "ntp1.t-online.de"

// #define WIFI_SSID "AndroidAP"
// #define WIFI_PASS "REDACTED"

// #define WIFI_SSID "FRITZ!Box 7590 IY"
// #define WIFI_PASS "REDACTED"

// IoT Platform information
#define USER_NAME "group2"
#define USERID 31   // to be found in the platform
#define DEVICEID 78 // to be found in the platform
#define SENSOR_NAME "s2"
#define TOPIC "31/78/data" // userID_deviceID

#define JWT_TOKEN "REDACTED"

#define MQTT_SERVER "mqtt.caps-platform.live"