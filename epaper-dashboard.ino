#include "images.h"

#define USE_HSPI_FOR_EPD
#define ENABLE_GxEPD2_GFX 0
#include <GxEPD2_4C.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>

#define GxEPD2_DISPLAY_CLASS GxEPD2_4C
#define GxEPD2_DRIVER_CLASS GxEPD2_266c_GDEY0266F51H // GDEY0266F51H 184x360, JD79667 (FPC-H006 22.04.02)
#define GxEPD2_4C_IS_GxEPD2_4C true
#define MAX_DISPLAY_BUFFER_SIZE 65536ul // e.g.
#define IS_GxEPD2_4C(x) IS_GxEPD(GxEPD2_4C_IS_, x)
#define MAX_HEIGHT(EPD) 184

GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> display(GxEPD2_DRIVER_CLASS(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));
SPIClass hspi(HSPI);

#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#define BATTERYPIN 35

// Master server or 'brain' 
char config_home_ip[40] = "central.linframe.nl";

/*
* Wifi Manager
*/
WiFiManager wm;
WiFiManagerParameter custom_masterserver("node server", "Home",config_home_ip, 40);
bool shouldSaveConfig = false;

// HTTPS wifi Client
WiFiClientSecure httpsClient;

JsonDocument data;

void setup() {

  Serial.begin(115200);
  Serial.println("Start");

  hspi.begin(13, 12, 14, 15); // remap hspi for EPD (swap pins)
  display.epd2.selectSPI(hspi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200);
  display.setRotation(1);
  
  // Wifi Manager
  wm.setConnectTimeout(60);
  wm.setMinimumSignalQuality(10);
  // Custom title HTML for the WiFiManager portal
  String customTitle = "<h1>e-paper dashboard config</h1>";
  wm.setCustomHeadElement(customTitle.c_str());
  wm.setSaveConfigCallback(saveConfigCallback);
  
  bool wificonnect = wm.autoConnect("EP-HomeDashBoard", "123456789");

  httpsClient.setInsecure();

  Serial.printf("Battery level : %f v.\n", batteryLevel() );

}

void loop() {

  update_data();
  display_dashboard();
  display.hibernate();

  Serial.println("Going to sleep");

  esp_sleep_enable_timer_wakeup(5 * 60 * 1000000); // time in microseconds
  esp_deep_sleep_start();

  delay(100);
  Serial.println("Waking up.");
}

void display_dashboard() {
  display.setFullWindow();
  display.firstPage();
  
  do
  {
    display.fillScreen(GxEPD_WHITE);

    display.drawRoundRect(3, 8, 74, 74, 12, GxEPD_DARKGREY );
    display.drawRoundRect(96, 8, 74, 74, 12, GxEPD_DARKGREY);
    display.drawRoundRect(192, 8, 74, 74, 12, GxEPD_DARKGREY);
    display.drawRoundRect(285, 8, 74, 74, 12, GxEPD_DARKGREY);

    display.drawLine(1, 86, 359, 86, GxEPD_DARKGREY);
    display.drawLine(1, 176, 359, 176, GxEPD_DARKGREY);

    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(GxEPD_DARKGREY);

    // Open
    display.drawRGBBitmap( 6, 16, bmp_door, 21, 36 );
    display.setCursor(13, 72);
    display.print("Open");

    // Temp.
    display.drawRGBBitmap( 100, 16, bmp_temp, 21, 36 );
    display.setCursor(106, 72);
    display.print("Temp");

    // gas
    display.drawRGBBitmap( 198, 16, bmp_flame, 21, 36 );
    display.setCursor(195, 72);
    display.print("dm3/5m");

    // power
    display.drawRGBBitmap( 290, 16, bmp_power, 21, 36 );
    display.setCursor(310, 72);
    display.print("kWh");

    // Lightbulb
    display.drawRGBBitmap( 6, 96 , bmp_light, 21, 36 );

    // 48H power chart
    display.drawRoundRect(90, 96, 152 , 50, 0, GxEPD_DARKGREY );
    display.setCursor(88, 166);
    display.print("48h. power chart");

    display_data();

    Serial.println("Displayed");
  } while (display.nextPage());
  
}


void display_data() {

  // Open
  display.setFont(&FreeSansBold18pt7b);
  display.setTextColor(GxEPD_BLACK);
  
  int open = data["open"];
  display.setCursor(40, 44);
  if (open > 0) {
    display.setTextColor(GxEPD_RED);    
  }
  display.print(open);

  // Temp
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_BLACK);

  String avgtemp = data["avgtemp"];
  display.setCursor(122, 44);
  display.print(avgtemp);

  // Gas
  display.setFont(&FreeSansBold18pt7b);
  display.setTextColor(GxEPD_BLACK);

  String gas = data["gas5min"];
  display.setCursor(222, 50);
  display.print(gas);

  // Power
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_BLACK);

  float pwr = data["power"];
  display.setCursor(310, 44);
  if (pwr > 0.3) {
    display.setTextColor(GxEPD_RED);    
  }
  display.print(String(pwr));

  // Lights on
  display.setFont(&FreeSansBold18pt7b);
  display.setTextColor(GxEPD_BLACK);

  int lights = data["lights_on"];
  display.setCursor(30, 126);
    if (lights > 3) {
    display.setTextColor(GxEPD_RED);    
  }
  display.print("x" + String(lights));

  // Power chart
  int x = 3;
  for (int i=0; i <= 47; i++ ) {
    int y1 = data["pwr_chart"][i];
    display.fillRect(90 + x, 146 - y1, 2, y1, GxEPD_RED);
    x = x + 3;
  }

  // Battery level
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_BLACK);

  String battery = "B:" + String(batteryLevel()) + "v";
  display.setCursor(290, 148);
  display.print(battery);

  // Last update
  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  String update = data["last_update"];
  display.setCursor(270, 166);
  display.print("ls:" + update);

}

void update_data() {
  
  if ( !httpsClient.connect(config_home_ip, 443)) {
    Serial.println("Connection failed");
    return;
  }

  // Send HTTP GET request
  httpsClient.println("GET /dashboard_info HTTP/1.1");
  httpsClient.println("Host: " + String(config_home_ip));
  httpsClient.println("Connection: close");
  httpsClient.println();

  // Wait for response
  while (httpsClient.connected()) {
    String line = httpsClient.readStringUntil('\n');
    if (line == "\r") break; // Headers done
  }

  // Print response body
  String response = httpsClient.readString();
  Serial.println("Response body:");
  Serial.println(response);

  deserializeJson(data, response);

}

float batteryLevel() {
  float pinValue = float(analogRead(BATTERYPIN));
  float tmpLevel = (pinValue * 2) / 1000;
  float battery = round(tmpLevel * 100) / 100.0;
  return battery;
}

void saveConfigCallback() {
  shouldSaveConfig = true;
}
