#include <Arduino.h>

// ==================================
//           CUSTOMIZE BEGIN
// ==================================

// Create an account and your app here : https://dev.netatmo.com/apps/
String client_secret = "";
String client_id = "";

RTC_DATA_ATTR char access_token[58] = "";
RTC_DATA_ATTR char refresh_token[58] = "";

String device_id = "";

const char* wifi_ssid = "";
const char* wifi_key = "";

// board wake up interval in seconds
const int WAKEUP_INTERVAL = 600;

// delay in seconds between your place and UTC time
const unsigned long DELAYUTC_YOURTIMEZONE = 3600;

// ==================================
//           CUSTOMIZE END
// ==================================
