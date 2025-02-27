// Decomment to DEBUG
// #define DEBUG_NETATMO
// #define DEBUG_GRID
// #define DEBUG_WIFI
#define DEBUG_SERIAL
//#define FORCE_NVS

#define T5PLUS

// const definition
#ifdef T5PLUS
  #define DEF_PIN_BAT 14
  #define DEF_INIT_LINE_POS 35
  #define DEF_BAR_WIDTH 9
  #define DEF_BAR_HEIGHT 12
  #define DEF_BAT_LEFT_MARGIN 480
  #define DEF_BAT_TOP_MARGIN 10
  #define DEF_MODULE_HEIGHT 160
  #define DEF_GRID_SPACING 20
  #define DEF_RECT_LEFT_MARGIN 4
#else
  #define DEF_PIN_BAT 35
  #define DEF_INIT_LINE_POS 0
  #define DEF_BAR_WIDTH 3
  #define DEF_BAR_HEIGHT 4
  #define DEF_BAT_LEFT_MARGIN 90
  #define DEF_BAT_TOP_MARGIN 5
  #define DEF_MODULE_HEIGHT 50
  #define DEF_GRID_SPACING 10
  #define EPD_HEIGHT 122
  #define EPD_WIDTH 250
  #define DEF_RECT_LEFT_MARGIN 2
#endif

// Customize with your settings
#include "TOCUSTOMIZE.h"

// screen specific
#ifdef T5PLUS
	#include "epd_highlevel.h"
	#include "epd_driver.h"
	
	//font	
	#include "firasans_12.h"
	#include "opensans16.h"
	#include "opensans24.h"
	#include "opensans24b.h"
	#include "firasans_40.h"
#else
	#include <GxEPD.h>
	#include <GxDEPG0213BN/GxDEPG0213BN.h>
	#include <GxIO/GxIO_SPI/GxIO_SPI.h>
	#include <GxIO/GxIO.h>

	//font
	#include <Fonts/FreeSans9pt7b.h>
	#include <Fonts/FreeSansBold12pt7b.h>
	#include <Fonts/Org_01.h>
#endif

#include <WiFi.h>
#include <MyDumbWifi.h>
#include <NetatmoWeatherAPI.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <TimeLib.h>
#include <math.h>
#include <Preferences.h>

Preferences nvs;
char previous_access_token[58];
char previous_refresh_token[58];

// screen specific again
#ifdef T5PLUS
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
#else
	GxIO_Class io(SPI, /*CS=5*/ SS, /*DC=*/17, /*RST=*/16);
	GxEPD_Class display(io, /*RST=*/16, /*BUSY=*/4);
#endif

const int PIN_BAT = DEF_PIN_BAT;        // adc for bat voltage
const float VOLTAGE_100 = 4.2; // Full battery curent li-ion
const float VOLTAGE_0 = 3.5;   // Low battery curent li-ion

int currentLinePos = DEF_INIT_LINE_POS;

int batteryPercentage = 0;
float batteryVoltage = 0.0;

const char *ntpServer = "pool.ntp.org";


// put function declarations here:
String getDayOfWeekInFrench(int dayOfWeek);
String getMonthInFrench(int month);
tm getTimeWithDelta(int delta);
String getFullDateStringAddDelta(bool withTime, int delta);
bool initializeTime();

#ifdef T5PLUS
void drawString(int x, int y, String text, const EpdFont *font);
#else
void drawString(int x, int y, String text, GFXfont *font);
#endif
void drawLine(int x0, int y0, int y1, int y2);
void drawRect(int x, int y, int width, int height);
void clearScreen();
void finalizeDisplay();

void updateBatteryPercentage(int &percentage, float &voltage);
void displayLine(String text);
void drawBatteryLevel(int batteryTopLeftX, int batteryTopLeftY, int percentage);
void displayModulePluvio(module_struct module, int y);
void displayModule(module_struct module, int y);
void displayInfo(NetatmoWeatherAPI myAPI);
int getDataFromAPI(NetatmoWeatherAPI *myAPI);

void goToDeepSleepUntilNextWakeup();
void drawDebugGrid();

// Helper functions to get French abbreviations
String getDayOfWeekInFrench(int dayOfWeek)
{
  const char *daysFrench[] = {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"};
  return daysFrench[dayOfWeek % 7]; // Use modulo just in case
}

String getMonthInFrench(int month)
{
  const char *monthsFrench[] = {"Jan", "Fev", "Mar", "Avr", "Mai", "Juin", "Juil", "Aou", "Sep", "Oct", "Nov", "Dec"};
  return monthsFrench[(month - 1) % 12]; // Use modulo and adjust since tm_mon is [0,11]
}

tm getTimeWithDelta(int delta)
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Echec de récupération de la date !");
    timeinfo = {0};
  }

  timeinfo.tm_mday += delta;
  mktime(&timeinfo);
  return timeinfo;
}

String getFullDateStringAddDelta(bool withTime, int delta)
{
  struct tm timeinfo = getTimeWithDelta(delta);
  String dayOfWeek = getDayOfWeekInFrench(timeinfo.tm_wday);
  String month = getMonthInFrench(timeinfo.tm_mon + 1); // tm_mon is months since January - [0,11]
  char dayBuffer[3];
  snprintf(dayBuffer, sizeof(dayBuffer), "%02d", timeinfo.tm_mday);

  String result = dayOfWeek + " " + String(dayBuffer) + " " + month;
  if (withTime)
  {
    char timeBuffer[9];
    snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    result = result + " " + String(timeBuffer);
  }
  return result;
}

bool initializeTime()
{
  // If connected to WiFi, attempt to synchronize time with NTP
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Tentative de synchronisation NTP...");
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", ntpServer); // Configure time zone to adjust for daylight savings

    const int maxNTPAttempts = 5;
    int ntpAttempts = 0;
    time_t now;
    struct tm timeinfo;
    while (ntpAttempts < maxNTPAttempts)
    {
      time(&now);
      localtime_r(&now, &timeinfo);

      if (timeinfo.tm_year > (2016 - 1900))
      { // Check if the year is plausible
        Serial.println("NTP time synchronized!");
        return true;
      }

      ntpAttempts++;
      Serial.println("Attente de la synchronisation NTP...");
      delay(2000); // Delay between attempts to prevent overloading the server
    }

    Serial.println("Échec de synchronisation NTP, utilisation de l'heure RTC.");
  }

  // Regardless of WiFi or NTP sync, try to use RTC time
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0))
  { // Immediately return the RTC time without waiting
    if (timeinfo.tm_year < (2016 - 1900))
    { // If year is not plausible, RTC time is not set
      Serial.println("Échec de récupération de l'heure RTC, veuillez vérifier si l'heure a été définie.");
      return false;
    }
  }

  Serial.println("Heure RTC utilisée.");
  return true;
}



#ifdef T5PLUS
void drawString(int x, int y, String text, const EpdFont *font)
{
  char *data = const_cast<char *>(text.c_str());
  EpdFontProperties font_props = epd_font_properties_default();
  epd_write_string(font, data, &x, &y, framebuffer, &font_props);
}

#else
void drawString(int x, int y, String text, GFXfont *font)
{
  display.setFont(font);
  display.setCursor(x, y);
  display.print(text);
}
#endif


void drawLine(int x0, int y0, int x1, int y1)
{
#ifdef T5PLUS
  epd_draw_line(x0, y0, x1, y1, Black, framebuffer);
#else
  display.drawLine(x0, y0, x1, y1, GxEPD_BLACK);
#endif
}

void clearScreen()
{
  // Initialize display
#ifdef T5PLUS    
  epd_init(EPD_OPTIONS_DEFAULT);
  hl = epd_hl_init(WAVEFORM);
  epd_set_rotation(orientation);
  framebuffer = epd_hl_get_framebuffer(&hl);
  epd_poweron();
  epd_clear();
#else
	display.init();
	display.fillScreen(GxEPD_WHITE);
	display.setTextColor(GxEPD_BLACK);
#endif
}

void finalizeDisplay()
{
#ifdef T5PLUS
  err = epd_hl_update_screen(&hl, MODE_GC16, temperature);
  Serial.print("Update Result : ");
  Serial.println(err);
#else
	display.update();
#endif
}

void drawRect(int x, int y, int width, int height)
{
#ifdef T5PLUS
  EpdRect rect = {
      .x = x,
      .y = y,
      .width = width,
      .height = height};
  epd_draw_rect(rect, Black, framebuffer);
#else
	display.drawRoundRect(x, y, width, height, 8, GxEPD_BLACK);
#endif
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
    initializeTime();
	  
    nvs.begin("netatmoCred", false);
    NetatmoWeatherAPI myAPI;
	
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

    
    #ifdef DEBUG_NETATMO
      myAPI.setDebug(true);
    #endif
    int res = getDataFromAPI(&myAPI); 
    clearScreen();

    if (res == VALID_ACCESS_TOKEN)
    {
      Serial.println("Start display");
#ifdef DEBUG_GRID
      drawDebugGrid();
#endif
      displayInfo(myAPI);
    } else {
      displayLine("API Error");
      switch (res)
        {
          case 3:
            displayLine("Expired Token");
          break;
          case 2:
            displayLine("Invalid Token");
          break;
          case 1:
            displayLine("Other");
          break;
          case 0:
            displayLine("Ok ? WTF ?");
          break;
        }
        displayLine("HttpCodes " + myAPI.allHttpCodes);
        displayLine("Msg " + myAPI.errorMessage);
        displayLine("LastBody " + myAPI.lastBody);
    }    
  }

  finalizeDisplay();
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


int getDataFromAPI(NetatmoWeatherAPI *myAPI) {
  #ifdef DEBUG_NETATMO
      myAPI->setDebug(true);
    #endif

    int result = myAPI->getStationsData(access_token, device_id, DELAYUTC_YOURTIMEZONE); 

    if (result == EXPIRED_ACCESS_TOKEN || result == INVALID_ACCESS_TOKEN) {
      if (myAPI->getRefreshToken(access_token, refresh_token, client_secret, client_id))
      {
        
        // compare cred with previous
        if(strncmp(previous_access_token, access_token, 58) != 0){
          nvs.putString("access_token", access_token);
          Serial.println("NVS : access_token updated");   
        } else {
          Serial.println("NVS : same access_token");
          
        }

        if(strncmp(previous_refresh_token, refresh_token, 58) != 0){
          nvs.putString("refresh_token", refresh_token);
          Serial.println("NVS : refresh_token updated");
            
        } else {
          Serial.println("NVS : same refresh_token");
          
        }
        result = myAPI->getStationsData(access_token, device_id, DELAYUTC_YOURTIMEZONE);
        
      }
    }
    return result;
}

void displayLine(String text)
{
#ifdef T5PLUS
	drawString(10, currentLinePos, text, &FiraSans_12);
  currentLinePos += 35;
#else
if (currentLinePos > 150)
  {
    currentLinePos = 0;
    clearScreen();
  }
  display.setCursor(10, currentLinePos);
  display.print(text);
  currentLinePos += 10;
#endif  
}

void drawBatteryLevel(int batteryTopLeftX, int batteryTopLeftY, int percentage)
{
  // Draw battery Level
  const int nbBars = 4;
  const int barWidth = DEF_BAR_WIDTH;
  const int batteryWidth = (barWidth + 1) * nbBars + 2;
  const int barHeight = DEF_BAR_HEIGHT;
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

  drawRect(DEF_RECT_LEFT_MARGIN, y, rectWidth, rectHeight);
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

  const int maxTempOffsetX = 15;
  const int maxTempOffsetY = 100;

  const int humidityOffsetX = 400;
  const int humidityOffsetY = 140;

  const int minTempOffsetX = 15;
  const int minTempOffsetY = 140;

  
  const int trendTempOffsetX = 270;
  const int trendTempOffsetY = 90;

  const int reachableOffsetX = 420;
  const int reachableOffsetY = 40;

  drawRect(DEF_RECT_LEFT_MARGIN, y, rectWidth, rectHeight);
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
  // T5PLUS :
  // 6 modules sur 960 de haut = 160 par module
  // 5 marge à gauche 5 marge à droite => reste 530
  const int battery_left_margin = DEF_BAT_LEFT_MARGIN;
  const int battery_top_margin = DEF_BAT_TOP_MARGIN;
  const int module_height = DEF_MODULE_HEIGHT;

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

#ifdef T5PLUS
  displayModulePluvio(myAPI.NAModule3, y);
  drawBatteryLevel(battery_left_margin, y + battery_top_margin, myAPI.NAModule3.battery_percent);

  String dateRefresh = getFullDateStringAddDelta(true,0);
  drawString(15, y + 140, "↻ " + dateRefresh, &OpenSans16);
#endif
}

void drawDebugGrid()
{
  int gridSpacing = DEF_GRID_SPACING; // Espacement entre les lignes de la grille
  int screenWidth = EPD_HEIGHT; // T5PLUS 540 // 122
  int screenHeight = EPD_WIDTH; // T5PLUS 960 // 250

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
#ifdef T5PLUS
  // pause time to permit upload
  delay(20000);
#endif

  time_t sleepDuration = WAKEUP_INTERVAL;
  Serial.print("Sleeping duration (seconds): ");
  Serial.println(sleepDuration);

  // Configure wake up
#ifdef T5PLUS  
  epd_poweroff();
  delay(400);
#endif

  esp_sleep_enable_timer_wakeup(sleepDuration * 1000000ULL);
  esp_deep_sleep_start();
}

void loop()
{
  // put your main code here, to run repeatedly:
}

