// Decomment to DEBUG
#define DEBUG_NETATMO
// #define DEBUG_GRID
//#define DEBUG_WIFI
#define DEBUG_SERIAL
//#define FORCE_NVS

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
#include <NetatmoWeatherAPI.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TimeLib.h>
#include <math.h>
#include <Preferences.h>

Preferences nvs;
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
void displayInfo(NetatmoWeatherAPI myAPI);
void goToDeepSleepUntilNextWakeup();
void sendDataToDebugServer(String str_message);
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
  for (int i = 0; i < 20; i++)
  {
    Serial.println(i);
    delay(1000);
  }
#endif

  Serial.println("Starting...\n");

  // Gathering battery level
  updateBatteryPercentage(batteryPercentage, batteryVoltage);

  Serial.println("MAC Adress:");
  Serial.println(WiFi.macAddress().c_str());
  Serial.println("Battery:");

  char line[24];
  sprintf(line, "%5.3fv (%d%%)", batteryVoltage, batteryPercentage);
  Serial.println(line);
  // Connect to WiFi
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
    char previous_access_token[58];
    char previous_refresh_token[58];

    nvs.begin("netatmoCred", false);
    NetatmoWeatherAPI myAPI;
    sendDataToDebugServer("setup Init");

#ifdef FORCE_NVS
    nvs.putString("access_token", access_token);
    nvs.putString("refresh_token", refresh_token);
#endif
    if(!nvs.isKey("access_token") || !nvs.isKey("refresh_token")) {
      Serial.println("NVS : init namespace");
      nvs.putString("access_token", access_token);
      nvs.putString("refresh_token", refresh_token);
    } else {
      Serial.println("NVS : get value from namespace");
      nvs.getString("access_token", access_token, 58);
      nvs.getString("refresh_token", refresh_token, 58);     
    }
    Serial.print("access_token : ");
    Serial.println(access_token);
    Serial.print("refresh_token : ");
    Serial.println(refresh_token); 
    memcpy(previous_access_token, access_token, 58);
    memcpy(previous_refresh_token, refresh_token, 58); 

    sendDataToDebugServer("setup NVS Access=" + String(access_token));   
    sendDataToDebugServer("setup NVS Refresh=" + String(refresh_token));   

    #ifdef DEBUG_NETATMO
      myAPI.setDebug(true);
    #endif

    int result = myAPI.getStationsData(access_token, device_id, DELAYUTC_YOURTIMEZONE);
    if (result == EXPIRED_ACCESS_TOKEN) {
      sendDataToDebugServer("setup Expired Access Token");
    } 
    if (result == INVALID_ACCESS_TOKEN) {
      sendDataToDebugServer("setup Invalid Access Token");
    }   

    if (result == VALID_ACCESS_TOKEN) {
      sendDataToDebugServer("setup Valid Access Token");
    }

    if (result == EXPIRED_ACCESS_TOKEN || result == INVALID_ACCESS_TOKEN) {
      if (myAPI.getRefreshToken(access_token, refresh_token, client_secret, client_id))
      {
        sendDataToDebugServer("setup getRefreshToken Access=" + String(access_token));   
        sendDataToDebugServer("setup getRefreshToken Refresh=" + String(refresh_token));   

        // compare cred with previous
        if(strncmp(previous_access_token, access_token, 58) != 0){
          nvs.putString("access_token", access_token);
          Serial.println("NVS : access_token updated");
          sendDataToDebugServer("setup getRefreshToken Access Token Updated");   
        } else {
          Serial.println("NVS : same access_token");
          sendDataToDebugServer("setup getRefreshToken Same Access Token"); 
        }

        if(strncmp(previous_refresh_token, refresh_token, 58) != 0){
          nvs.putString("refresh_token", refresh_token);
          Serial.println("NVS : refresh_token updated");
          sendDataToDebugServer("setup getRefreshToken Refresh Token Updated");   
        } else {
          Serial.println("NVS : same refresh_token");
          sendDataToDebugServer("setup getRefreshToken Same Refresh Token"); 
        }

        result = myAPI.getStationsData(access_token, device_id, DELAYUTC_YOURTIMEZONE);
        sendDataToDebugServer("setup getStationsData result=" + result); 
      } else {
        sendDataToDebugServer("setup getRefreshToken False");   
      }
    }

    // Gathering Netatmo datas
    if (result == VALID_ACCESS_TOKEN)
    {
        Serial.println("Start display");
        sendDataToDebugServer("setup Start Display"); 
        clearScreen();
        #ifdef DEBUG_GRID
        drawDebugGrid();
        #endif
        displayInfo(myAPI);
        sendDataToDebugServer("setup End displayInfo"); 
        
      }
      else
      {
        clearScreen();
        displayLine("GetStationsData Error");
      
    }
  }

  sendDataToDebugServer("setup End Display"); 
        
  err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
  Serial.print("Update Result : ");
  Serial.println(err);
  sendDataToDebugServer("setup End"); 
  
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

void displayInfo(NetatmoWeatherAPI myAPI)
{
  // 6 modules sur 960 de haut = 160 par module
  // 5 marge à gauche 5 marge à droite => reste 530
  const int battery_left_margin = 480;
  const int battery_top_margin = 10;
  const int module_height = 160;

  int y = 0;
  displayModule(myAPI.NAMain, y);
  // esp32 batterie level
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, batteryPercentage);
  y += module_height;

  displayModule(myAPI.NAModule1, y);
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, myAPI.NAModule1.battery_percent);
  y += module_height;

  displayModule(myAPI.NAModule4[0], y);
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, myAPI.NAModule4[0].battery_percent);
  y += module_height;

  displayModule(myAPI.NAModule4[1], y);
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, myAPI.NAModule4[1].battery_percent);
  y += module_height;

  displayModule(myAPI.NAModule4[2], y);
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, myAPI.NAModule4[2].battery_percent);
  y += module_height;

  displayModulePluvio(myAPI.NAModule3, y);
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, myAPI.NAModule3.battery_percent);
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

void sendDataToDebugServer(String str_message)
{

  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    String debugServerPayLoad = str_message;
    
    Serial.println(debugServerPayLoad);

    http.begin("http://192.168.0.216/esp32/message=" + str_message);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpCode = http.GET();
    http.end();
  }
}