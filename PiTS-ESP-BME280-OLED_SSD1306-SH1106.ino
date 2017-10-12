/****************************************************************************
 *  PiTS-ESP8266-BME280 Modul mit OLED-Display                              *
 *  ==========================================                              *
 *  Dieser Sketch für den ESP8266 dient als remote Sensor für PiTS-It! zur  *
 *  Temperaturerfassung mit BME280-Sensor inkl. der Darstellung auf lokalem *
 *  OLED-Display und benötigt folgende Libraries:                           *
 *   WiFi (Bestandteil der Arduino IDE),                                    *
 *   NTP (https://github.com/chrismelba/NTP),                               *
 *   Time (https://github.com/PaulStoffregen/Time),                         *
 *   Timezone (https://github.com/JChristensen/Timezone)                    *
 *   Adafruit_BME280 (https://github.com/adafruit/Adafruit_BME280_Library), *
 *   Adafruit_Sensors (https://github.com/adafruit/Adafruit_Sensor),        *
 *   esp8266-oled-ssd1306 (https://github.com/squix78/esp8266-oled-ssd1306) *
 *                                                                          *
 *                                                                          *
 *  Die Übertragung des Messwerte erfolgt per HTTP-Get Request an das       *
 *  Webserver Modul von PiTS-It!                                            *
 *                                                                          *
 *  Homepage: http://pits.TGD-Consulting.de                                 *
 *                                                                          *
 *  Version 0.1.0                                                           *
 *  Datum 11.10.2017                                                        *
 *                                                                          *
 *  (C) 2017 TGD-Consulting , Author: Dirk Weyand                           *
 ****************************************************************************/

/*************************
 *** Globale Parameter ***
 *************************/

#define WLAN_SSID               "SSID des WLAN"          // change to your WiFi SSID 
#define WLAN_PASSPHRASE         "DAS GEHEIME PASSWORT"   // change to your passphrase
#define NTP_SERVER              "192.168.0.1"            // set your local NTP-Server here, or eg. "ptbtime2.ptb.de"
#define PITS_HOST               "192.168.0.25"           // PiTS-It! Webserver
#define PITS_PORT               8080                     // Port des Webservers
#define ZAEHLER_ID              "123456789"              // eindeutige ID des Sensors
#define TOKEN                   "000000003cb62dc7"       // Verbindungstoken (Seriennummer des RPi)
#define PST 0            // GMT/UTC - Anpassung an lokale Sommer/Winterzeit erfolgt über Timezone Library
#define SERDEBUG 1       // Debug-Infos über Serielle Schnittstelle senden, auskommentiert = Debugging //OFF  
#define OLED 1           // OLED-Display SSD1306 verwenden, auskommentiert = kein Display  
#define BIG 1            // bei 1 erfolgt die Darstellung der Messwerte mit groesseren Zeichen
//#define NOCLOCK 1      // Für Darstellung der Uhr in der UI diese Zeile // auskommentieren
#define GPIO_I2C_SDA 0   // Verwende GPIO4 als I2C SDA (Input)
#define GPIO_I2C_SCL 2   // Verwende GPIO5 als I2C SCL
#define MINUTEN 5       // Abtastrate, Anzahl Minuten bis zur nächsten Datenübermittlung

// include requiered library header
#include <ESP8266WiFi.h> // WiFi functionality
#include <WiFiUdp.h>     // udp for network time
#include <TimeLib.h>
#include <Timezone.h>    // Anpassung an lokale Zeitzone
#include <ntp.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Wire.h>        // fuer Ausgbabe auf I2C OLED-LED & auslesen des BME280 
//#include "SH1106.h"      // fuer Ausgabe auf 1,3" I2C OLED-Display                                                                                                                                                 OLED
#include "SSD1306.h"     // fuer Ausgbabe auf I2C OLED-Display
#include "OLEDDisplayUi.h" // Include the UI lib

// function pre declaration 2 avoid errors
bool startWiFi(void);
time_t getNTPtime(void);

Adafruit_BME280 bme; // Note Adafruit assumes I2C adress = 0x77 my module (eBay) uses 0x76 so the library address has been changed accordingly in Adafruit_BME280.h
NTP NTPclient;

//Central Europe Time (Berlin, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};    //Central European Summer Time = UTC + 2 hours
TimeChangeRule CET = {"CET", Last, Sun, Oct, 3, 60};       //Central European Standard Time = UTC + 1 hours
Timezone CE(CEST, CET);

TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev

ADC_MODE(ADC_VCC);          // Aktiviert Spannungsabfrage

float Messwerte[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 100.0, -100.0}; // Array der aktuellen Messwerte [temp, press, hum, alt, abs-hum, dewpoint, tmin, tmax]

void meassure(void) { 
  float temp, pressure, humidity, altitude;

  // BME280 Sensor auslesen
  temp = bme.readTemperature();
  pressure = bme.readPressure() / 100.0F; // Luftdruck in hPa
  humidity = bme.readHumidity();
  altitude = bme.readAltitude(1013.25);   // ungefähre Höhe über NN

  // aktuelle Werte ins Array uebertragen
  Messwerte[0] = temp;
  Messwerte[1] = pressure;
  Messwerte[2] = humidity;
  Messwerte[3] = altitude;

#ifdef SERDEBUG
  Serial.print("I2C  >> BME280 >> Temperature: ");
  Serial.println(temp);
  Serial.print("I2C  >> BME280 >> Pressure: ");
  Serial.println(pressure);
  Serial.print("I2C  >> BME280 >> Humidity: ");
  Serial.println(humidity);
  Serial.print("I2C  >> BME280 >> Altitude: ");
  Serial.println(altitude);
#endif

  // Min-Max Temperaturen setzen
  if (temp < Messwerte[6]) {
      Messwerte[6] = temp;
  }
  if (temp > Messwerte[7]) {
      Messwerte[7] = temp;
  }
}

void compute(void) {        // aktuellen Taupunkt und absolute Feuchte berechnen
   float a, b;
   float r = Messwerte[2];  // relative Luftfeuchte
   float t = Messwerte[0];  // aktuelle Temperatur in °C
   if (t >= 0) {            // weitere Berechnungsparameter (siehe https://www.wetterochs.de/wetter/feuchte.html)
      a = 7.5;
      b = 237.3;
   } else {
      a = 7.6;
      b = 240.7;
   }
   float SDD = 6.1078 * pow (10, (a * t)/(b + t));          // Saettigungsdampfdruck in hPa ermitteln
   float DD = r / 100 * SDD;                                // Dampfdruck in hPa
   float v = log10 (DD / 6.1078);
   float TD = b * v / (a - v);                              // Taupunkttemperatur in °C
   float AF = 100000 * 18.016 / 8314.3 * DD / (t + 273.15); // Absolute Feuchte in g/m3 Luft
   Messwerte[4] = AF;
   Messwerte[5] = TD;
}

#ifdef OLED 
SSD1306 display(0x3c, GPIO_I2C_SDA, GPIO_I2C_SCL); //aktiviert Display, Ansteuerung erfolgt über Wire Library
//SH1106 display(0x3c, GPIO_I2C_SDA, GPIO_I2C_SCL); //aktiviert grosses Display, Ansteuerung erfolgt über Wire Library

OLEDDisplayUi ui ( &display );                     //aktiviert UI-Funktionen

int counter = 1; // Counter für Progess-Bar
char* monthnames[12] = {"Jan", "Feb", "Mär", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"}; 
char* daynames[7] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"}; 

void drawDateTime(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_10);
  time_t t = CE.toLocal(now(), &tcr);                      // Store the current time in time variable t
  char s[20];
  sprintf(s,"%02u:%02u:%02u", hour(t), minute(t), second(t));
  String date = String(daynames[weekday(t)-1]) + ", " + String(day(t), DEC) + "-" + String(month(t), DEC) + "-" + String(year(t), DEC);
  int textWidth = display->getStringWidth(date);
  display->drawString(64 + x, 22 + y, date);
  display->setFont(ArialMT_Plain_24);
  String time = String(s);
  textWidth = display->getStringWidth(time);
  display->drawString(64 + x, 32 + y, time);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
}

void drawTemperature(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  char stringbuf[20];
  dtostrf(Messwerte[0], 5, 1, stringbuf);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  if (BIG > 0) {
     display->setFont(ArialMT_Plain_10);
     display->drawString(64 + x, 22 + y, "Temperatur");
     display->setFont(ArialMT_Plain_24);
     display->drawString(64 + x, 32 + y, String(stringbuf) + "°C");    
  } else {
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 20 + y, "Temperatur:");
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 36 + y, String(stringbuf) + "°C");
  }
}

void drawPressure(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  char stringbuf[20];
  dtostrf(Messwerte[1], 6, 1, stringbuf);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  if (BIG > 0) {
     display->setFont(ArialMT_Plain_10);
     display->drawString(64 + x, 22 + y, "Luftdruck");
     display->setFont(ArialMT_Plain_24);
     display->drawString(64 + x, 32 + y, String(stringbuf) + " hPa");
  } else {
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 20 + y, "Luftdruck:");
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 36 + y, String(stringbuf) + " hPa");
  }
}

void drawHumidity(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  char stringbuf[20];
  dtostrf(Messwerte[2], 4, 1, stringbuf);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  if (BIG > 0) {
     display->setFont(ArialMT_Plain_10);
     display->drawString(64 + x, 22 + y, "relative Feuchte");
     display->setFont(ArialMT_Plain_24);
     display->drawString(64 + x, 32 + y, String(stringbuf) + " %");
  } else {
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 20 + y, "relative Feuchte:");
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 36 + y, String(stringbuf) + " %");
  }
}

void drawMinMaxTemp(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  char stringbuf[20];
  dtostrf(Messwerte[6], 5, 1, stringbuf);
  String s = String(stringbuf) + "/";
  dtostrf(Messwerte[7], 5, 1, stringbuf);
  s += stringbuf;
  s.replace(" ", ""); // entfernt spaces
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  if (BIG > 0) {
     display->setFont(ArialMT_Plain_10);
     display->drawString(64 + x, 22 + y, "Min/Max Temperatur");
     display->setFont(ArialMT_Plain_24);
     display->drawString(64 + x, 32 + y, s + "°C");    
  } else {
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 20 + y, "Min/Max Temperatur:");
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 36 + y, s + "°C");
  }
}

void drawAbsHum(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  char stringbuf[20];
  dtostrf(Messwerte[4], 4, 1, stringbuf);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  if (BIG > 0) {
     display->setFont(ArialMT_Plain_10);
     display->drawString(64 + x, 22 + y, "absolute Feuchte");
     display->setFont(ArialMT_Plain_24);
     display->drawString(64 + x, 32 + y, String(stringbuf) + " g/m³");
  } else {
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 20 + y, "absolute Feuchte:");
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 36 + y, String(stringbuf) + " g/m³");
  }
}

void drawDewpoint(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  char stringbuf[20];
  dtostrf(Messwerte[5], 5, 1, stringbuf);
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  if (BIG > 0) {
     display->setFont(ArialMT_Plain_10);
     display->drawString(64 + x, 22 + y, "Taupunkt");
     display->setFont(ArialMT_Plain_24);
     display->drawString(64 + x, 32 + y, String(stringbuf) + "°C");    
  } else {
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 20 + y, "Taupunkt:");
     display->setFont(ArialMT_Plain_16);
     display->drawString(64 + x, 36 + y, String(stringbuf) + "°C");
  }
}

void drawHeaderOverlay(OLEDDisplay *display, OLEDDisplayUiState* state) {
  time_t t = CE.toLocal(now(), &tcr);                      // Store the current time in time variable t
  char s[20];
  sprintf(s,"%02u:%02u", hour(t), minute(t));
  display->setFont(ArialMT_Plain_16);
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->drawString(0, 0, String(String(day(t), DEC) + "." + monthnames[month(t)-1]));
  display->setTextAlignment(TEXT_ALIGN_RIGHT);
  display->drawString(128, 0, String(s));
  // Draw two lines horizontally
  display->drawHorizontalLine(0, 16, 128);
  display->drawHorizontalLine(0, 17, 128);
}

#ifdef NOCLOCK
FrameCallback frames[] = { drawTemperature, drawPressure, drawHumidity, drawMinMaxTemp, drawAbsHum, drawDewpoint }; // this array keeps function pointers to all frames
int numberOfFrames = 6;                                                   // frames are the single views that slide from right to left
#else
FrameCallback frames[] = { drawTemperature, drawPressure, drawHumidity, drawMinMaxTemp, drawAbsHum, drawDewpoint, drawDateTime }; // this array keeps function pointers to all frames
int numberOfFrames = 7;                                                                 // frames are the single views that slide from right to left
#endif

OverlayCallback overlays[] = { drawHeaderOverlay };
int numberOfOverlays = 1;

void drawHeadline(String msg = String());

void drawHeadline(String msg) {
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  if (msg.length() > 0) {
     display.drawString(64, 0, String(msg));
  } else {
     display.drawString(64, 0, String("PiTS-ESP8266"));
  }
}

void drawDate(String cDate, String cTime) {
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, String(cDate));
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(128, 0, String(cTime));
  // Draw two lines horizontally
  display.drawHorizontalLine(0, 16, 128);
  display.drawHorizontalLine(0, 17, 128);
}

void drawValues(float t, float h, float p) {
  char stringbuf[20];
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  dtostrf(t, 5, 1, stringbuf);
  display.drawString(0, 24, String(String("T:") + stringbuf + "°C"));
  dtostrf(p, 6, 1, stringbuf);
  display.drawString(0, 48, String(String("P:") + stringbuf + " hPa"));
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  dtostrf(h, 4, 1, stringbuf);
  display.drawString(128, 24, String(String("H:") + stringbuf + "%"));
}

void drawProgressBar() {
  int progress = (counter / 5) % 100;
  // draw the progress bar
  display.drawProgressBar(0, 40, 120, 10, progress);

  // draw the percentage as String
  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 25, String(progress) + "%");
}

void displayProgress(String message = String());

void displayProgress(String message) {
  counter++;
  if (message.indexOf (" connected ") > 0) {  // Message enthält "connected" -> IP-Adresse in Headline ausgeben
    drawHeadline(String (WiFi.localIP().toString()));
  } else {
    drawHeadline();    
  }
  drawProgressBar();
  // draw the message
  if (message.length() > 0) {
    display.drawString(64, 52, String(message));
  }
  display.display();              // write the buffer to the display
}
#endif

void setup() {
#ifdef OLED
  display.init();
  display.flipScreenVertically();
  drawHeadline();                 // Prog-Info in erste Zeile
  display.drawString(64, 24, String("©2017 TGD"));
  display.drawString(64, 48, String("Vcc: ") + ESP.getVcc() / 1000.0f + "V"); // Supply Voltage of ESP
  display.display();              // write the buffer to the display
#endif

#ifdef SERDEBUG
  Serial.begin(115200);
  while (!Serial) {               // Wait for serial port to connect
#ifdef OLED     
    counter++;                    // and display Progress-Bar
    displayProgress();            // 4 Weicheier
#else
    ;
#endif
  }; //delay(100);
  Serial.println();
  Serial.println();
  Serial.println("PROG INFORMATION =========================================================");
#ifdef OLED
  Serial.println("PROG >> INFO >> PiTS-ESP8266 with BOSCH BME280 & OLED-Display");
#else
  Serial.println("PROG >> INFO >> PiTS-ESP8266 with BOSCH BME280");
#endif
  Serial.println("PROG >> ID   >> " ZAEHLER_ID );
  Serial.println("PROG >> DATE >> " __DATE__ );
  Serial.println("PROG >> TIME >> " __TIME__ );
  Serial.println("PROG >> GCC  >> " __VERSION__ );
  Serial.println(String("PROG >> IDE  >> ") + IDEString() );
  Serial.println("CHIP INFORMATION =========================================================");
  Serial.printf("CHIP >> CORE  >> ID: %08X\r\n", ESP.getChipId());
  Serial.println(String("CHIP >> CORE  >> Free Heap: ") + ESP.getFreeHeap() / 1024 + " kB");
  Serial.println("CHIP >> CORE  >> Speed: 80 MHz");
  Serial.println(String("CHIP >> CORE  >> Voltage: ") + ESP.getVcc() / 1000.0f + " V");
  Serial.printf("CHIP >> FLASH >> ID : %08X\r\n", ESP.getFlashChipId());
  Serial.println(String("CHIP >> FLASH >> Size: ") + ESP.getFlashChipRealSize() / 1024 + " kB");
  Serial.println(String("CHIP >> FLASH >> Speed: ") + ESP.getFlashChipSpeed() / 1000000 + " MHz");
  Serial.println("RUNTIME INFORMATION========================================================");
  Serial.print("PITS >> SENSOR >> ID ");
  Serial.println(ZAEHLER_ID);
#endif

//  pinMode(GPIO_I2C_SDA, INPUT_PULLUP); // Set input (SDA) pull-up resistor on
//  use real hardware pull-up 4k7-resistor instead !!!

#ifndef OLED
  // Open I2C Bus on defined Pins
  Wire.begin(GPIO_I2C_SDA, GPIO_I2C_SCL); 
#endif
#ifdef SERDEBUG
  Serial.println("I2C  >> I2C-Bus initialized!");
#endif
  delay(2000);  // and give BME280 time to boot

  // Init BME280 on I2C
  if (!bme.begin(0x76)) {
#ifdef SERDEBUG
    Serial.println("I2C  >> I2C-No Sensor detected!");  
#endif
  } else {
#ifdef SERDEBUG
    Serial.println("I2C  >> I2C-BME280 initialized!");
#endif
  }

  // mit WLAN-AP verbinden
  while (!startWiFi()) {
    delay(1500);
  }

#ifdef SERDEBUG
  Serial.println("WiFi connected");
  Serial.print("WIFI >> IP address: ");
  Serial.println(WiFi.localIP());
#endif
#ifdef OLED     
    displayProgress(String("WIFI >> connected !")); // display connecet
#endif

  NTPclient.begin(NTP_SERVER, PST);
  setSyncProvider(getNTPtime);
  setSyncInterval(SECS_PER_HOUR * 12);  // alle 12 Stunden aktualisieren

  meassure();   // Messwerte mit aktuellen Werten des BME280 initialisieren
  compute();    // Taupunkt und absolute Luftfeuchte berechnen

  delay(2000);  // nach dem Start 2 Sekunden Zeit, für NTP-Synchronisation

#ifdef OLED     // Parameter des Scrolling UI 
  ui.setTargetFPS(30);

//  ui.setActiveSymbol(activSymbol);
//  ui.setInactiveSymbol(inactivSymbol);

  ui.setIndicatorPosition(BOTTOM);         // You can change this to TOP, LEFT, BOTTOM, RIGHT

  ui.setIndicatorDirection(LEFT_RIGHT); // Defines where the first frame is located in the bar.

  ui.setFrameAnimation(SLIDE_LEFT);     // You can change the transition that is used SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN

  ui.setFrames(frames, numberOfFrames); 

  ui.setOverlays(overlays, numberOfOverlays);

  ui.init();                            // Inital UI takes care of initalising the display too.

  display.flipScreenVertically();
#endif
}

long timeSinceLastMeassurement = 0;
int Intervall = MINUTEN * 60 * 1000;

void loop() {
  int loopCount; //, Intervall;
  bool reset = true;
//  Intervall = MINUTEN * 60 * 1000;

#ifdef OLED
  int remainingTimeBudget = ui.update();

  if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
    if (millis() - timeSinceLastMeassurement > Intervall) {
      meassure();  // BME280 Sensor auslesen
      compute();   // Taupunkt und absolute Luftfeuchte berechnen

  // Werte des BME280 Sensors ausgelesen => Signalisierung an PITS-Server
  time_t t = CE.toLocal(now(), &tcr); //now();                      // Store the current time in time variable t
  String DateTimeString = String(day(t), DEC) + "-" + String(month(t), DEC) + "-" + String(year(t), DEC);
  DateTimeString = DateTimeString + "/" + String(hour(t), DEC) + ":" + String(minute(t), DEC) + ":" + String(second(t), DEC);

  // Min/Max Temperatur Werte nach Mitternacht reseten
  if (not reset and 0 == hour(t)) {
     Messwerte[6] = 100.0;
     Messwerte[7] = -100.0;
     reset = true;
  } else if (reset and hour(t) > 0) {
    reset = false;   // Trigger wieder scharf schalten
  }
  
#ifdef SERDEBUG
  Serial.print("PITS >> SENSOR >> current temperature ");
  Serial.println(Messwerte[0]);
  Serial.print("PITS >> SENSOR >> maessured @ ");
  Serial.println(DateTimeString);
  Serial.print("PITS >> HTTP   >> connecting to ");
  Serial.println(PITS_HOST);
#endif

  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  if (!client.connect(PITS_HOST, PITS_PORT)) {
#ifdef SERDEBUG
    Serial.println("PITS >> HTTP   >> connection failed");
#endif
    return;
  }

  // We now create a URI for the request
  String url = "/cgi-bin/import.html?id=";
  url += ZAEHLER_ID;
  url += "&token=";
  url += TOKEN;
  url += "&data=";
  url += Messwerte[0]; // Temperatur
  url += ";";
  url += Messwerte[1]; // Luftdruck
  url += ";";
  url += Messwerte[2]; // Luftfeuchte
  url += "&run=";
  url += uptime();
  if (timeStatus() != timeNotSet) { // Falls Zeit synchron zum NTP-Server, Zeitpunkt übermitteln
    url += "&time=";
    url += DateTimeString;        // im REBOL Time-Format
  }
  
#ifdef SERDEBUG
  Serial.print("PITS >> HTTP   >> Requesting URL: ");
  Serial.println(url);
#endif

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + PITS_HOST + "\r\n" +
               "Connection: close\r\n\r\n");

      timeSinceLastMeassurement = millis();
    }
    delay(remainingTimeBudget);
  }
#endif

}

String uptime()
{
  //long days = 0;
  long hours = 0;
  long mins = 0;
  long secs = 0;
  secs = millis () / 1000;     // convert current milliseconds from ESP to seconds
  mins = secs / 60;            // convert seconds to minutes
  hours = mins / 60;           // convert minutes to hours
  //days = hours / 24;           // convert hours to days
  secs = secs - (mins * 60);
  mins = mins - (hours * 60);
  //hours = hours - (days * 24);
  String rc = "";
  rc += String(hours);
  rc += ":";
  rc += String(mins);
  rc += ":";
  rc += String(secs); 
  return rc;
}

String IDEString(){
  uint16_t IDE = ARDUINO;
  String tmp = "";
  tmp += String(IDE/10000);
  IDE %= 10000;
  tmp += ".";
  tmp += String(IDE/100);
  IDE %= 100;
  tmp += ".";
  tmp += String(IDE);
  return tmp;
}

#define NTP_RETRIES 3 // Anzahl Versuche, die Uhrzeit vom NTP zu bekommen

time_t getNTPtime(void)
{
  time_t retVal = 0;

  for ( int i = 0; i < NTP_RETRIES && retVal == 0; i++ )
  {
    retVal = NTPclient.getNtpTime();
  }
  return ( retVal );
}

bool startWiFi(void)
{
  uint8_t n, i;

#ifdef SERDEBUG
  Serial.print("WIFI >> Attempting to connect to ");
  Serial.print(WLAN_SSID);
  Serial.print(" using password ");
  Serial.println(WLAN_PASSPHRASE);
#endif

#ifdef OLED     
    displayProgress(String("WIFI >> ") + WLAN_SSID); // and display Progress-Bar
#endif

  WiFi.persistent(false); // Reduces flash access, memory wearing
  WiFi.mode(WIFI_STA);    // Explicitly set the ESP8266 to be a WiFi-client

  if (WiFi.begin(WLAN_SSID, WLAN_PASSPHRASE) != WL_CONNECTED) {
#ifdef SERDEBUG
    Serial.print("WIFI >> ");
#endif
    for (i = 0; i < 10; i++) {
      if (WiFi.status() == WL_CONNECTED) return true;
#ifdef OLED
      for (n = 0; n < 60; n++) {
         displayProgress(String("WiFi >> ") + WLAN_SSID); // display Progress-Bar 4 Weicheier
         delay(10);
      }
#else
      delay(600);
#endif
#ifdef SERDEBUG
      Serial.print(".");
#endif
    }
  }

#ifdef OLED     
    displayProgress(String("WIFI >> failed !!!"));        // display failed status @ Progress-Bar
#endif

#ifdef SERDEBUG
  Serial.print("Failed to connect to: ");
  Serial.println(WLAN_SSID);

  Serial.print("WIFI >> using pass phrase: ");
  Serial.println(WLAN_PASSPHRASE);
#endif

  return false;
}
