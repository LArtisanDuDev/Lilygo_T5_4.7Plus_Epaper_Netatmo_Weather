// Decomment to DEBUG
// #define DEBUG_NETATMO
// #define DEBUG_GRID
#define DEBUG_WIFI
#define DEBUG_SERIAL

// Customize with your settings
#include "TOCUSTOMIZE.h"

// epd
#include "epd_highlevel.h"
#include "epd_driver.h"

// font
#include "firasans_12.h"
#include "opensans16.h"
#include "opensans24.h"
#include "opensans24b.h"
#include "firasans_40.h"

#include <WiFi.h>
#include <MyDumbWifi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TimeLib.h>
#include <math.h>

EpdiyHighlevelState hl;
// ambient temperature around device
int temperature = 20;
uint8_t *framebuffer;
enum EpdDrawError err;

#define Black 0x00
#define Grey 0x80
#define White 0xF0

#define WAVEFORM EPD_BUILTIN_WAVEFORM

// CHOOSE HERE YOU IF YOU WANT PORTRAIT OR LANDSCAPE
// both orientations possible
EpdRotation orientation = EPD_ROT_PORTRAIT;
// EpdRotation orientation = EPD_ROT_LANDSCAPE;

const int PIN_BAT = 14;        // adc for bat voltage
const float VOLTAGE_100 = 4.2; // Full battery curent li-ion
const float VOLTAGE_0 = 3.5;   // Low battery curent li-ion

int currentLinePos = 35;

int batteryPercentage = 0;
float batteryVoltage = 0.0;

struct module_struct
{
  String name = "";
  String temperature = "";
  String min = "";
  String max = "";
  String trend = "";
  int battery_percent = 0;
  String co2 = "";
  String humidity = "";
  String rain = "";
  String sum_rain_1h = "";
  String sum_rain_24h = "";
  String reachable = "";

  unsigned long timemin = 0;
  unsigned long timemax = 0;
  unsigned long timeupdate = 0;
}
// station
NAMain,
    // module extérieur
    NAModule1,
    // modules intérieurs
    NAModule4[3],
    // pluviomètre
    NAModule3;

// put function declarations here:
void drawString(int x, int y, String text, const EpdFont *font);
void drawLine(int x0, int y0, int y1, int y2);
void drawRect(int x, int y, int width, int height);
void clearScreen();

void updateBatteryPercentage(int &percentage, float &voltage);
void displayLine(String text);
void drawBatteryLevel(int batteryTopLeftX, int batteryTopLeftY, int percentage);
void displayModulePluvio(module_struct module, int y);
void displayModule(module_struct module, int y);
void displayInfo();
void dumpModule(module_struct module);
bool getStationsData();
void goToDeepSleepUntilNextWakeup();
bool getRefreshToken();
void drawDebugGrid();

void drawString(int x, int y, String text, const EpdFont *font)
{
  char *data = const_cast<char *>(text.c_str());
  EpdFontProperties font_props = epd_font_properties_default();
  epd_write_string(font, data, &x, &y, framebuffer, &font_props);
}

void drawLine(int x0, int y0, int x1, int y1)
{
  epd_draw_line(x0, y0, x1, y1, Black, framebuffer);
}

void clearScreen()
{
  // Initialize display
  epd_init(EPD_OPTIONS_DEFAULT);
  hl = epd_hl_init(WAVEFORM);
  epd_set_rotation(orientation);
  framebuffer = epd_hl_get_framebuffer(&hl);
  epd_poweron();
  epd_clear();
}

void drawRect(int x, int y, int width, int height)
{
  EpdRect rect = {
      .x = x,
      .y = y,
      .width = width,
      .height = height};
  epd_draw_rect(rect, Black, framebuffer);
}

void setup()
{

  setlocale(LC_TIME, "fr_FR.UTF-8");

  Serial.begin(115200);
#ifdef DEBUG_SERIAL
  // time to plug serial
  for (int i = 0; i < 10; i++)
  {
    Serial.println(i);
    delay(1000);
  }
#endif

  Serial.println("Starting...\n");

  // Gathering battery level
  updateBatteryPercentage(batteryPercentage, batteryVoltage);

  Serial.println("Starting...");
  Serial.println("MAC Adress:");
  Serial.println(WiFi.macAddress().c_str());
  Serial.println("Battery:");

  char line[24];
  sprintf(line, "%5.3fv (%d%%)", batteryVoltage, batteryPercentage);
  Serial.println(line);
  // Connecter au WiFi
  MyDumbWifi mdw;
#ifdef DEBUG_WIFI
  mdw.setDebug(true);
#endif

  if (!mdw.connectToWiFi(wifi_ssid, wifi_key))
  {
    clearScreen();
    displayLine("Error connecting wifi");
  }
  else
  {

    // Gathering Netatmo datas
    if (getRefreshToken())
    {
      if (getStationsData())
      {
        Serial.println("Start display");
        clearScreen();
#ifdef DEBUG_GRID
        drawDebugGrid();
#endif
        displayInfo();
      }
      else
      {
        clearScreen();
        displayLine("GetStationsData Error");
      }
    }
    else
    {
      clearScreen();
      displayLine("Refresh token Error");
    }
  }

  err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
  Serial.print("Update Result : ");
  Serial.println(err);

  goToDeepSleepUntilNextWakeup();
}

void updateBatteryPercentage(int &percentage, float &voltage)
{
  // Lire la tension de la batterie
  voltage = analogRead(PIN_BAT) / 4096.0 * 7.05;
  percentage = 0;
  if (voltage > 1)
  { // Afficher uniquement si la lecture est valide
    percentage = static_cast<int>(2836.9625 * pow(voltage, 4) - 43987.4889 * pow(voltage, 3) + 255233.8134 * pow(voltage, 2) - 656689.7123 * voltage + 632041.7303);
    // Ajuster le pourcentage en fonction des seuils de tension
    if (voltage >= VOLTAGE_100)
    {
      percentage = 100;
    }
    else if (voltage <= VOLTAGE_0)
    {
      percentage = 0;
    }
  }
}

void displayLine(String text)
{
  drawString(10, currentLinePos, text + "  " + currentLinePos, &FiraSans_12);
  currentLinePos += 35;
}

void drawBatteryLevel(int batteryTopLeftX, int batteryTopLeftY, int percentage)
{
  // Draw battery Level
  const int nbBars = 4;
  const int barWidth = 9;
  const int batteryWidth = (barWidth + 1) * nbBars + 2;
  const int barHeight = 12;
  const int batteryHeight = barHeight + 4;

  // Horizontal
  drawLine(batteryTopLeftX, batteryTopLeftY, batteryTopLeftX + batteryWidth, batteryTopLeftY);
  drawLine(batteryTopLeftX, batteryTopLeftY + batteryHeight, batteryTopLeftX + batteryWidth, batteryTopLeftY + batteryHeight);
  // Vertical
  drawLine(batteryTopLeftX, batteryTopLeftY, batteryTopLeftX, batteryTopLeftY + batteryHeight);
  drawLine(batteryTopLeftX + batteryWidth, batteryTopLeftY, batteryTopLeftX + batteryWidth, batteryTopLeftY + batteryHeight);
  // + Pole
  drawLine(batteryTopLeftX + batteryWidth + 1, batteryTopLeftY + 1, batteryTopLeftX + batteryWidth + 1, batteryTopLeftY + (batteryHeight - 1));
  drawLine(batteryTopLeftX + batteryWidth + 2, batteryTopLeftY + 1, batteryTopLeftX + batteryWidth + 2, batteryTopLeftY + (batteryHeight - 1));

  int i, j;
  int nbBarsToDraw = round(percentage / 25.0);
  for (j = 0; j < nbBarsToDraw; j++)
  {
    for (i = 0; i < barWidth; i++)
    {
      drawLine(batteryTopLeftX + 2 + (j * (barWidth + 1)) + i, batteryTopLeftY + 2, batteryTopLeftX + 2 + (j * (barWidth + 1)) + i, batteryTopLeftY + 2 + barHeight);
    }
  }
}

void displayModulePluvio(module_struct module, int y)
{
  // 540 x 156
  const int rectWidth = 532;
  const int rectHeight = 156;

  const int nameOffsetX = 10;
  const int nameOffsetY = 40;

  const int mmOffsetX = 20;
  const int mmOffsetY = 100;

  const int mmSum24HOffsetX = 240;
  const int mmSum24HOffsetY = 100;

  drawRect(4, y, rectWidth, rectHeight);
  drawString(nameOffsetX, y + nameOffsetY, module.name, &OpenSans24);

  drawString(mmSum24HOffsetX, y + mmSum24HOffsetY, module.sum_rain_24h + "mm", &FiraSans_40);
  drawString(mmOffsetX, y + mmOffsetY, "1H : " + module.rain + "mm", &OpenSans16);
}

void displayModule(module_struct module, int y)
{

  // 540 x 156
  const int rectWidth = 532;
  const int rectHeight = 156;

  const int nameOffsetX = 10;
  const int nameOffsetY = 40;

  const int tempOffsetX = 320;
  const int tempOffsetY = 90;

  const int humidityOffsetX = 400;
  const int humidityOffsetY = 140;

  const int minTempOffsetX = 15;
  const int minTempOffsetY = 140;

  const int maxTempOffsetX = 15;
  const int maxTempOffsetY = 100;

  const int trendTempOffsetX = 270;
  const int trendTempOffsetY = 90;

  const int reachableOffsetX = 420;
  const int reachableOffsetY = 40;

  drawRect(4, y, rectWidth, rectHeight);
  drawString(nameOffsetX, y + nameOffsetY, module.name, &OpenSans24);

  drawString(tempOffsetX, y + tempOffsetY, module.temperature + "°", &FiraSans_40);

  drawString(humidityOffsetX, y + humidityOffsetY, module.humidity + "%", &OpenSans24);

  char buff[6];
  sprintf(buff, "%02d:%02d", hour(module.timemin), minute(module.timemin));
  drawString(minTempOffsetX, y + minTempOffsetY, "⏬ " + module.min + "° (" + String(buff) + ")", &OpenSans16);

  sprintf(buff, "%02d:%02d", hour(module.timemax), minute(module.timemax));
  drawString(maxTempOffsetX, y + maxTempOffsetY, "⏫ " + module.max + "° (" + String(buff) + ")", &OpenSans16);

  if (module.trend == "up")
  {
    drawString(trendTempOffsetX, y + trendTempOffsetY, "▲", &OpenSans24B);
  }

  if (module.trend == "down")
  {
    drawString(trendTempOffsetX, y + trendTempOffsetY, "▼", &OpenSans24B);
  }

  if (!module.reachable)
  {
    drawString(reachableOffsetX, y + reachableOffsetY, "⚠", &OpenSans24B);
  }
}

void displayInfo()
{
  // 6 modules sur 960 de haut = 160 par module
  // 5 marge à gauche 5 marge à droite => reste 530
  const int battery_left_margin = 480;
  const int battery_top_margin = 10;
  const int module_height = 160;

  int y = 0;
  displayModule(NAMain, y);
  // esp32 batterie level
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, batteryPercentage);
  y += module_height;

  displayModule(NAModule1, y);
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, NAModule1.battery_percent);
  y += module_height;

  displayModule(NAModule4[0], y);
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, NAModule4[0].battery_percent);
  y += module_height;

  displayModule(NAModule4[1], y);
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, NAModule4[1].battery_percent);
  y += module_height;

  displayModule(NAModule4[2], y);
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, NAModule4[2].battery_percent);
  y += module_height;

  displayModulePluvio(NAModule3, y);
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, NAModule3.battery_percent);
}

void dumpModule(module_struct module)
{
  Serial.println("Name :" + module.name);
  Serial.println("Temperature :" + module.temperature);
  Serial.println("Min :" + module.min);
  Serial.println("Max :" + module.max);
  Serial.println("Trend :" + module.trend);
  Serial.print("Battery :");
  Serial.println(module.battery_percent);
  Serial.println("CO2 :" + module.co2);
  Serial.println("Humidity :" + module.humidity);
  Serial.print("Time Min :");
  Serial.println(module.timemin);
  Serial.print("Time Max :");
  Serial.println(module.timemax);
  Serial.print("Time Update :");
  Serial.println(module.timeupdate);

  Serial.print("Rain :" + module.rain);
  Serial.print("Sum Rain 1h :" + module.sum_rain_1h);
  Serial.print("Sum Rain 24h :" + module.sum_rain_24h);
  Serial.println("Reachable :" + module.reachable);
}

bool getStationsData()
{
  String tmp_device_id = device_id;
  tmp_device_id.replace(":", "%3A");

  bool retour = false;
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;

    String netatmoGetStationsData = "https://api.netatmo.com/api/getstationsdata?device_id=" + tmp_device_id + "&get_favorites=false";
    Serial.println(netatmoGetStationsData);

    http.begin(netatmoGetStationsData);

    char bearer[66] = "Bearer ";
    strcat(bearer, access_token);

    http.addHeader("Authorization", bearer); // Adding Bearer token as HTTP header
    int httpCode = http.GET();

    if (httpCode > 0)
    {
      DynamicJsonDocument doc(12288);
      String payload = http.getString();
#ifdef DEBUG_NETATMO
      Serial.println("body :");
      Serial.println(payload);
#endif
      deserializeJson(doc, payload);

      JsonArray stations = doc["body"]["devices"].as<JsonArray>();
      for (JsonObject station : stations)
      {
        NAMain.name = station["module_name"].as<String>();
        NAMain.min = station["dashboard_data"]["min_temp"].as<String>();
        NAMain.max = station["dashboard_data"]["max_temp"].as<String>();
        NAMain.temperature = station["dashboard_data"]["Temperature"].as<String>();
        NAMain.trend = station["dashboard_data"]["temp_trend"].as<String>();
        NAMain.timemin = DELAYUTC_YOURTIMEZONE + station["dashboard_data"]["date_min_temp"].as<unsigned long>();
        NAMain.timemax = DELAYUTC_YOURTIMEZONE + station["dashboard_data"]["date_max_temp"].as<unsigned long>();
        NAMain.timeupdate = DELAYUTC_YOURTIMEZONE + station["dashboard_data"]["time_utc"].as<unsigned long>();
        NAMain.humidity = station["dashboard_data"]["Humidity"].as<String>();
        JsonArray modules = station["modules"].as<JsonArray>();
        int module4counter = 0;
        for (JsonObject module : modules)
        {
          if (module["type"].as<String>() == "NAModule1")
          {
            NAModule1.name = module["module_name"].as<String>();
            NAModule1.battery_percent = module["battery_percent"].as<int>();
            NAModule1.min = module["dashboard_data"]["min_temp"].as<String>();
            NAModule1.max = module["dashboard_data"]["max_temp"].as<String>();
            NAModule1.temperature = module["dashboard_data"]["Temperature"].as<String>();
            NAModule1.trend = module["dashboard_data"]["temp_trend"].as<String>();
            NAModule1.timemin = DELAYUTC_YOURTIMEZONE + module["dashboard_data"]["date_min_temp"].as<unsigned long>();
            NAModule1.timemax = DELAYUTC_YOURTIMEZONE + module["dashboard_data"]["date_max_temp"].as<unsigned long>();
            NAModule1.timeupdate = DELAYUTC_YOURTIMEZONE + station["dashboard_data"]["time_utc"].as<unsigned long>();
            NAModule1.humidity = module["dashboard_data"]["Humidity"].as<String>();
            NAModule1.reachable = module["reachable"].as<String>();
          }
          if (module["type"].as<String>() == "NAModule4")
          {
            NAModule4[module4counter].name = module["module_name"].as<String>();
            NAModule4[module4counter].battery_percent = module["battery_percent"].as<int>();
            NAModule4[module4counter].min = module["dashboard_data"]["min_temp"].as<String>();
            NAModule4[module4counter].max = module["dashboard_data"]["max_temp"].as<String>();
            NAModule4[module4counter].temperature = module["dashboard_data"]["Temperature"].as<String>();
            NAModule4[module4counter].trend = module["dashboard_data"]["temp_trend"].as<String>();
            NAModule4[module4counter].timemin = DELAYUTC_YOURTIMEZONE + module["dashboard_data"]["date_min_temp"].as<unsigned long>();
            NAModule4[module4counter].timemax = DELAYUTC_YOURTIMEZONE + module["dashboard_data"]["date_max_temp"].as<unsigned long>();
            NAModule4[module4counter].timeupdate = DELAYUTC_YOURTIMEZONE + station["dashboard_data"]["time_utc"].as<unsigned long>();
            NAModule4[module4counter].humidity = module["dashboard_data"]["Humidity"].as<String>();
            NAModule4[module4counter].co2 = module["dashboard_data"]["temp_trend"].as<String>();
            NAModule4[module4counter].reachable = module["reachable"].as<String>();
            module4counter++;
          }
          if (module["type"].as<String>() == "NAModule3")
          {
            NAModule3.name = module["module_name"].as<String>();
            NAModule3.rain = module["dashboard_data"]["Rain"].as<String>();
            NAModule3.sum_rain_1h = module["dashboard_data"]["sum_rain_1"].as<String>();
            NAModule3.sum_rain_24h = module["dashboard_data"]["sum_rain_24"].as<String>();
            NAModule3.battery_percent = module["battery_percent"].as<int>();
            NAModule3.reachable = module["reachable"].as<String>();
          }
        }
      }
#ifdef DEBUG_NETATMO
      dumpModule(NAMain);
      dumpModule(NAModule1);
      dumpModule(NAModule4[0]);
      dumpModule(NAModule4[1]);
      dumpModule(NAModule4[2]);
      dumpModule(NAModule3);
#endif
      retour = true;
    }
    else
    {
      Serial.println("getStationsData Error : " + http.errorToString(httpCode));
    }
    http.end();
  }
  return retour;
}

bool getRefreshToken()
{
  String tmp_refresh_token = refresh_token;
  tmp_refresh_token.replace("|", "%7C");

  bool retour = false;
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    String netatmoRefreshTokenPayload = "client_secret=" + client_secret + "&grant_type=refresh_token&client_id=" + client_id + "&refresh_token=" + tmp_refresh_token;

#ifdef DEBUG_NETATMO
    Serial.println(netatmoRefreshTokenPayload);
#endif

    http.begin("https://api.netatmo.com/oauth2/token");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.POST(netatmoRefreshTokenPayload);
    if (httpCode > 0)
    {
      DynamicJsonDocument doc(1024);
      String payload = http.getString();
#ifdef DEBUG_NETATMO
      Serial.println("body :");
      Serial.println(payload);
#endif
      deserializeJson(doc, payload);
      if (doc.containsKey("access_token"))
      {
        String str_access_token = doc["access_token"].as<String>();
#ifdef DEBUG_NETATMO
        char buffer_token[77] = "Old access token :";
        strcat(buffer_token, access_token);
        Serial.println(buffer_token);
        Serial.println("New access token :" + str_access_token);
#endif
        strcpy(access_token, str_access_token.c_str());
        retour = true;
      }
      else
      {
        Serial.println("No access_token");
      }

      if (doc.containsKey("refresh_token"))
      {
        String str_refresh_token = doc["refresh_token"].as<String>();
#ifdef DEBUG_NETATMO
        char buffer_token[79] = "Old refresh token : ";
        strcat(buffer_token, refresh_token);
        Serial.println(buffer_token);
        Serial.println("New refresh token : " + str_refresh_token);
#endif
        strcpy(refresh_token, str_refresh_token.c_str());
      }
      else
      {
        Serial.println("No refresh_token");
        retour = false;
      }
    }
    else
    {
      Serial.println("refreshToken Error : " + http.errorToString(httpCode));
    }
    http.end();
  }
  return retour;
}

void drawDebugGrid()
{
  int gridSpacing = 20;         // Espacement entre les lignes de la grille
  int screenWidth = EPD_HEIGHT; // 540
  int screenHeight = EPD_WIDTH; // 960

  Serial.print("Width : ");
  Serial.print(screenWidth);
  Serial.print(" Eight : ");
  Serial.println(screenHeight);

  // Dessiner des lignes verticales
  for (int x = 0; x <= screenWidth; x += gridSpacing)
  {
    drawLine(x, 0, x, screenHeight);
  }

  // Dessiner des lignes horizontales
  for (int y = 0; y <= screenHeight; y += gridSpacing)
  {
    drawLine(0, y, screenWidth, y);
  }
}

void goToDeepSleepUntilNextWakeup()
{
  // pause time to permit upload
  delay(60000);

  time_t sleepDuration = WAKEUP_INTERVAL;
  Serial.print("Sleeping duration (seconds): ");
  Serial.println(sleepDuration);

  // Configure wake up
  epd_poweroff();
  delay(400);

  esp_sleep_enable_timer_wakeup(sleepDuration * 1000000ULL);
  esp_deep_sleep_start();
}

void loop()
{
  // put your main code here, to run repeatedly:
}
