/****************************************************************************
 *  PiTS-MHZ19B-WS2812 Modul                                                *
 *  ========================                                                *
 *  Dieser Sketch für den ESP8266 dient als remote Sensor für PiTS-It! zur  *
 *  Erfassung der CO2-Konzentration mit MHZ19B-Sensor und Darstellung als   *
 *  CO2-Ampel über WS2812B LED. Es werden folgende Libraries benötigt:      *
 *   WiFi (Bestandteil der Arduino IDE),                                    *
 *   NTP (https://github.com/chrismelba/NTP),                               *
 *   Time (https://github.com/PaulStoffregen/Time),                         *
 *   Timezone (https://github.com/JChristensen/Timezone),                   *
 *   Dusk2Dawn (https://github.com/dmkishi/Dusk2Dawn),                      *
 *   Adafruit_NeoPixel (https://github.com/adafruit/Adafruit_NeoPixel),     *
 *                                                                          *
 *  Die Übertragung des Messwertes erfolgt per HTTP-Get Request an das      *
 *  Webserver Modul von PiTS-It!                                            *
 *                                                                          *
 *  Homepage: http://pits.TGD-Consulting.de                                 *
 *                                                                          *
 *  Version 0.9.1                                                           *
 *  Datum 31.03.2021                                                        *
 *                                                                          *
 *  (C) 2021 TGD-Consulting , Author: Dirk Weyand                           *
 ****************************************************************************/

/*************************
 *** Globale Parameter ***
 *************************/

#define WLAN_SSID               "SSID des WLAN"          // change to your WiFi SSID 
#define WLAN_PASSPHRASE         "DAS GEHEIME PASSWORT"   // change to your passphrase
#define HOSTNAME                "PiTS-CO2Ampel-01"       // change to unique hostname 
#define RETRIES 8                                        // maximale Anzahl der Verbindungsversuche mit WLAN-AP
#define NTP_SERVER              "192.168.0.1"            // set your local NTP-Server here, or eg. "ptbtime2.ptb.de"
#define PITS_HOST               "192.168.0.25"           // PiTS-It! Webserver
#define PITS_PORT               8080                     // Port des Webservers
#define ZAEHLER_ID              "123456789"              // eindeutige ID des Sensors
#define TOKEN                   "000000453c67f0"         // Verbindungstoken (Seriennummer des RPi)
#define PST 0            // GMT/UTC - Anpassung an lokale Sommer/Winterzeit erfolgt über Timezone Library
//#define SERDEBUG 1       // Debug-Infos über Serielle Schnittstelle senden, auskommentiert = Debugging OFF  
#define PIN 2            // WS2812B wird an GPIO 2 des ESP-01 angeschlossen
#define NUMPIXELS 1      // Anzahl der am PIN angeschlossenen WS2812B (eine LED ausreichend für einfache CO2-Ampel)
#define BRIGHTNESS 125   // Helligkeit der LEDs (0 dunkel -> 255 ganz hell)
#define STRIPTEST 1      // LEDs beim Setup testen, auskommentiert = kein Test
#define MINUTEN 2        // Abtastrate, Anzahl Minuten bis zur nächsten Datenübermittlung
#define TRIGOFF 0        // Trigger Offset (0|100|200) für früheren Wechsel der Ampelfarben, bei 0 Schwellwerte gemäß DIN EN 13779
#define COLD 16          // Schwellwert unterhalb der die Raumtemperatur als unterkühlt/kalt gilt
#define NIGHTDIM 1       // Nacht-Modus aktiv, LED wird nach Sonnenuntergang gedimmt, zum Deaktiveren auskommentieren
#define DIMLVL 3         // Dimm-Level (je größer desto dunkler leuchtet die LED im Nacht-Modus)
#define LONGITUDE 9.760  // Position Längengrad muss angepasst werden, damit Dämmerungzeiten für den Standort stimmen
#define LATITUDE 54.644  // Position Breitengrad muss angepasst werden, damit Dämmerungzeiten für den Standort stimmen
//#define MWK -8           // Messwertkorrektur damit die Temperatur im Sensor mit Raumtemperatur übereinstimmt, Kommentar entfernen, damit die Ampel blau leuchtet, bei zu kalten Räumen mit hoher Raumluftqualität (ID1)
//#define NOCHECK 1 .      // Kommentar entfernen, um bei der Auswertung der Sensordaten die Checksummen-Überprüfung zu übergehen

// include requiered library header
#include <ESP8266WiFi.h> // WiFi functionality
#include <WiFiUdp.h>     // udp for network time
#include <TimeLib.h>
#include <Timezone.h>    // Anpassung an lokale Zeitzone
#include <Dusk2Dawn.h>   // Dämmerungszeiten bestimmen für Nacht-Modus
#include <ntp.h>
#include <Adafruit_NeoPixel.h>

// function pre declaration 2 avoid errors
bool startWiFi(void);
time_t getNTPtime(void);

Adafruit_NeoPixel leds(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800); // Adafruit_NeoPixel Library setup
NTP NTPclient;
Dusk2Dawn myPlace(LATITUDE, LONGITUDE, 1);  // Dusk2Dawn library setup

uint8_t count;  // Zähler für WiFi-Connect Versuche
uint32_t color; // 'Packed' 32-bit RGB Pixelcolor
int Intervall = MINUTEN * 60 * 1000;      // Sleeptime = Messinterval
int co2 = 400;                            // bisheriger co2 Messwert
int temperature = 25;                     // Temperatur des MH-Z19B
int tzo = 1;                              // time zone offset in hours from UTC
bool dst = false;                         // normal time = No Daylight Saving Time 
uint32_t ID1 = leds.Color(0, 250, 0);     // RGB Farbe Grün für CO2-Ampel hohe Raumluftqualität
uint32_t ID2 = leds.Color(140, 240, 0);   // RGB Farbe Hellgrün für CO2-Ampel mittlere Raumluftqualität
uint32_t ID3 = leds.Color(240, 220, 0);   // RGB Farbe Gelb für CO2-Ampel mäßige Raumluftqualität
uint32_t ID4 = leds.Color(250, 0, 0);     // RGB Farbe Rot für CO2-Ampel niedrige Raumluftqualität

//Central European Time (Berlin, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};    //Central European Summer Time = UTC + 2 hours
TimeChangeRule CET = {"CET", Last, Sun, Oct, 3, 60};       //Central European Standard Time = UTC + 1 hours
Timezone CE(CEST, CET);

TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev

ADC_MODE(ADC_VCC);          // Aktiviert Spannungsabfrage

// Returns the Red component of a 32-bit color
uint8_t Red(uint32_t color) {
  return (color >> 16) & 0xFF;
}
 
// Returns the Green component of a 32-bit color
uint8_t Green(uint32_t color) {
  return (color >> 8) & 0xFF;
}
 
// Returns the Blue component of a 32-bit color
uint8_t Blue(uint32_t color) {
  return color & 0xFF;
}

// Return color, dimmed 
uint32_t DimColor(uint32_t color, int shiftlevel) {
  uint32_t dimColor = leds.Color(Red(color) >> shiftlevel, Green(color) >> shiftlevel, Blue(color) >> shiftlevel);
  return dimColor;
}

// Farben für Nacht-Modus festlegen
#ifdef NIGHTDIM
uint32_t ID1dim = DimColor(ID1,DIMLVL);
uint32_t ID2dim = DimColor(ID2,DIMLVL);
uint32_t ID3dim = DimColor(ID3,DIMLVL);
uint32_t ID4dim = DimColor(ID4,DIMLVL);
#endif

void FadeOut (byte red, byte green, byte blue){
  float r, g, b;

  for(int k = 255; k >= 0; k=k-2) {
    r = (k/256.0)*red;
    g = (k/256.0)*green;
    b = (k/256.0)*blue;
    leds.fill(leds.Color((uint8_t) r,(uint8_t) g, (uint8_t) b));
    leds.show();
    delay(5);
  }
}

void FadeIn (byte red, byte green, byte blue){
  float r, g, b;

  for(int k = 0; k < 256; k=k+1) {
    r = (k/256.0)*red;
    g = (k/256.0)*green;
    b = (k/256.0)*blue;
    leds.fill(leds.Color((uint8_t) r,(uint8_t) g, (uint8_t) b));
    leds.show();
    delay(10);
  }
}

void FadeOutIn (byte red, byte green, byte blue){
  FadeOut (red, green, blue);
  FadeIn  (red, green, blue);
}

// Code 4 K.I.T.T. led effect.
// Simulate a moving LED with tail.
// First LED starts at 0, and moves along a triangular function.
// The tail follows, with decreasing brightness.
// Takes approximately 1s for each direction.

const int kitt_tail = 3; // How many dimmer LEDs follow in K.I.T.T. wheel
const int trail = (NUMPIXELS + 1) / 3;

void showWaitingLED(uint32_t color) {
    delay(100);
    static uint16_t kitt_offset = 0;
    leds.clear();
    for (int j = kitt_tail; j >= 0; j--) {
      int ledNumber = abs((kitt_offset - j + NUMPIXELS) % (2 * NUMPIXELS) - NUMPIXELS) % NUMPIXELS; // Triangular function
      leds.setPixelColor(ledNumber, DimColor(color, (j + 1)));
    }
    leds.show();
    kitt_offset++;
}

void showKITTWheel(uint32_t color, uint16_t duration_s) {
  for (int i = 0; i < duration_s * NUMPIXELS; ++i) {
    showWaitingLED(color);
  }
}

void showTrailingLED(uint32_t color, bool clockw, int fillring) {
    delay(100);
    static int16_t offset = 0;
    if (0 > fillring) {                  // LED-Ring leeren 
      for (int i=0; i < NUMPIXELS; i++) {
        int ledNumber = offset;
        if (ledNumber < 0) {
          ledNumber = ledNumber + NUMPIXELS;
        }
        if (ledNumber >= NUMPIXELS) {
          ledNumber = ledNumber - NUMPIXELS;
        }
        leds.setPixelColor(ledNumber, 0x000000);
        leds.show();
        if (clockw) {
          offset++;
          if (offset >= NUMPIXELS) {
            offset = 0;
          }
        } else {
          offset--;
          if (offset < 0) {
            offset = NUMPIXELS - 1;
          }
        }
        delay(100);
      }
    }
    leds.clear();
    for (int j = 0; j <= trail; j++) {
      int ledNumber = offset - j;
      if (ledNumber < 0) {
        ledNumber = ledNumber + NUMPIXELS;
      }
      if (ledNumber >= NUMPIXELS) {
        ledNumber = ledNumber - NUMPIXELS;
      }
      if (clockw) {
        leds.setPixelColor(ledNumber, DimColor(color, (j + 1)));
      } else {
        leds.setPixelColor(ledNumber, DimColor(color, ((trail - j) + 1)));
      }
    }
    leds.show();
    if (clockw) {
      offset++;
      if (offset >= NUMPIXELS) {
        offset = 0;
      }
    } else {
      offset--;
      if (offset < 0) {
        offset = NUMPIXELS - 1;
      }
    }
    if (0 < fillring) {                  // LED-Ring auffüllen
      for (int i=0; i < NUMPIXELS; i++) {
        int ledNumber = offset;
        if (ledNumber < 0) {
          ledNumber = ledNumber + NUMPIXELS;
        }
        if (ledNumber >= NUMPIXELS) {
          ledNumber = ledNumber - NUMPIXELS;
        }
        leds.setPixelColor(ledNumber, color);
        leds.show();
        if (clockw) {
          offset++;
          if (offset >= NUMPIXELS) {
            offset = 0;
          }
        } else {
          offset--;
          if (offset < 0) {
            offset = NUMPIXELS - 1;
          }
        }
        delay(100);
      }
    }
}

void showWheel(uint32_t color, uint16_t duration_ms, bool clockwise) {
  unsigned long t0 = millis();
  while (millis() - t0 < duration_ms) {
    showTrailingLED(color, clockwise, 0);
  }
}

void showRainbowWheel(uint16_t duration_ms, uint16_t hue_increment, bool clockwise) {
  static uint16_t wheel_offset = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < duration_ms) {
    for (int i = 0; i < NUMPIXELS; i++) {
      leds.setPixelColor(i, leds.ColorHSV(i * 65535 / NUMPIXELS + wheel_offset));
      if (clockwise) {
        wheel_offset -= hue_increment;
      } else {
        wheel_offset += hue_increment;
      }
    }
    leds.show();
    delay(10);
  }
}

void setup() {
#ifdef SERDEBUG
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println();
  Serial.println("PROG INFORMATION =========================================================");
  Serial.println("PROG >> INFO >> PiTS-ESP8266 with MH-Z19B and WS2812");
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

  leds.begin();
  leds.setBrightness(BRIGHTNESS); //die Helligkeit des LED-Strips setzen 0 dunkel -> 255 ganz hell
  leds.show();

#ifdef SERDEBUG
  Serial.print("PITS >> LEDS >> Brightness: ");
  Serial.println(String(leds.getBrightness()));
#endif

#ifdef STRIPTEST
  // Test LED-Strip (Farben der LED setzen)
  if (NUMPIXELS >= 8){ 
    showRainbowWheel(5000, 50, false);       // Regenbogeneffect
  } else {
    leds.fill(leds.Color(255, 255, 255));    // alle LEDs des Strips leuchten weiss
    leds.show();
    delay(1000);    // warte 1s
  }
  showRainbowWheel(5000, 50, true);            // Regenbogeneffect im Uhrzeigersinn
  leds.clear();   // alle LEDs ausschalten
  leds.show();
  delay(1000);    // warte 1s
  if (NUMPIXELS >= 8){ 
    showWheel(0xFF0000, 5000, false);
    showWheel(0xFFFF00, 5000, false);
    showWheel(0x00FF00, 5000, false);
    showWheel(0x0000FF, 5000, false);
  } else {
  leds.fill(leds.Color(255, 0, 0)); // Farbe Rot setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.fill(leds.Color(255, 255, 0)); // Farbe Gelb setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.fill(leds.Color(0, 255, 0)); // Farbe Grün setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.fill(leds.Color(0, 0, 255)); // Farbe Blau setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  }
#ifdef NIGHTDIM
  leds.fill(ID4dim); // Farbe Rot gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.fill(ID3dim); // Farbe Gelb gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.fill(ID1dim); // Farbe Grün gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.fill(leds.Color(0, 0, 31)); // Farbe Blau gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
#endif
#endif
  leds.fill(leds.Color(0, 0, 255)); // Farbe Blau setzen zum Dimmen bei erfolglosen WLAN-Connect Versuchen

  // mit WLAN-AP verbinden
  count = 0;
  while (count < RETRIES && !startWiFi()) {
    count++;
    leds.fill(DimColor(leds.getPixelColor(0),1)); // dunkler dimmen
    leds.show();
  }

  if(WiFi.status() == WL_CONNECTED){
#ifdef SERDEBUG
    Serial.println("WiFi connected");
    Serial.print("WIFI >> IP address: ");
    Serial.println(WiFi.localIP());
#endif

    NTPclient.begin(NTP_SERVER, PST);
    setSyncProvider(getNTPtime);
    setSyncInterval(SECS_PER_HOUR);  // jede Stunde aktualisieren

    delay(1000);  // nach dem Start 1 Sekunde Zeit, für NTP-Synchronisation
  }

#ifdef SERDEBUG
  Serial.flush();
  Serial.end();
#endif

  Serial.begin(9600);      // richtige Geschwindigkeit der seriellen Schnittstelle für MH-Z19B setzen

  // Ampel Farben durchlaufen = gleich geht's los mit der Messung 
  if (NUMPIXELS >= 8){ 
    showWheel(ID4, 5000, true);
    showWheel(ID3, 5000, true);
    showWheel(ID2, 5000, true);
    showWheel(ID1, 5000, true);
    if(WiFi.status() == WL_CONNECTED){
       showWheel(0x0000FA, 5000, true);
    }
  } else {
  leds.fill(color = ID4); // Farbe Rot setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  leds.fill(color = ID3); // Farbe Gelb setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));   // ausdimmen
  leds.fill(color = ID2);  // Farbe Hellgrün setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  leds.fill(color = ID1); // Farbe Grün setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  
  // erfolgreichen WiFi-Connect signalisieren -> blau dimmen
  if(WiFi.status() == WL_CONNECTED){
    leds.fill(color = leds.Color(0, 0, 250)); // Farbe Blau setzen
    leds.show(); //Anzeigen
    delay(1000); // warte 1s
    FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  }
  }
  leds.clear();            // alle LEDs ausschalten
  leds.show(); //Anzeigen
}
  
void loop() {
#ifdef NIGHTDIM
    if (CE.locIsDST(CE.toLocal(now(), &tcr))) { // Daylight Saving Time ?
       tzo = 2;
       dst = true;
    }
    Dusk2Dawn myPlace(LATITUDE, LONGITUDE, tzo); // für exakte Dämmerungszeiten müssen die Geokoordinaten oben angepasst werden
#endif

  static int old = 400;   // bisheriger co2 Messwert
  co2 = co2ppm();         // MH-Z19B Sensor auslesen
  
  // CO2-Ampel
  if(co2 > (1400 - TRIGOFF)){                 // ID4 niedrig => rot
    color = ID4;                              // Rot für CO2-Ampel niedrige Raumluftqualität = ID4
#ifdef NIGHTDIM
    if (isNight()) {color = ID4dim;}          // Gedimmtes rot bei Nacht
#endif
  }
  else if(co2 >= (1000 - TRIGOFF)){           // ID3 mäßig => gelb
    color = ID3;                              // Gelb für CO2-Ampel mäßige Raumluftqualität = ID3
#ifdef NIGHTDIM
    if (isNight()) {color = ID3dim;}          // Gedimmtes gelb bei Nacht
#endif    
  }
  else if(co2 >= (800 - TRIGOFF)){            // ID2 mittel => hellgrün
    color = ID2;                              // Hellgrün für CO2-Ampel mittlere Raumluftqualität = ID2
#ifdef NIGHTDIM
    if (isNight()) {color = ID2dim;}          // Gedimmtes hellgrün bei Nacht
#endif
  }
  else if(co2 >= 400) {                       // ID1 gut => grün
    color = ID1;                              // Grün für CO2-Ampel hohe Raumluftqualität = ID1
#ifdef NIGHTDIM
    if (isNight()) {color = ID1dim;}          // Gedimmtes grün bei Nacht
#endif
#ifdef MWK
    if (temperature <= (COLD - MWK)){
      color = 0x0000FA;                       // Blau für CO2-Ampel kalter Raum, hohe Raumluftqualität = ID1
#ifdef NIGHTDIM
      if (isNight()) {color = 0x00001F;}      // Gedimmtes blau bei Nacht
#endif
    }
#endif
  } else {
    if (co2 == -10) {                            // Sensor hat ungültigen Response zurückgeschickt
      if (NUMPIXELS >= 8){
         showKITTWheel(0x8000FA, 8);             // bei LED-Ring lila K.I.T.T.Wheel Effekt zeigen                                    
      } else {
        leds.setPixelColor(0, color = 0x8000FA); // ansonsten Farbe lila setzen
        for (int i = 0; i <= 4; i++) {
           FadeOutIn ((byte) Red(color), (byte) Green(color), (byte) Blue(color)); // Farbe Blau Fade out/Fade in
           delay(1000); // warte 1s
        }
      }
      co2 = old;             // alter Messwert
    }
    if (co2 == -1) {         // Checksum-Error beim Sensor-Response
      showRainbowWheel(5000, 50, true);               // Regenbogeneffect im Uhrzeigersinn
      co2 = old;             // alter Messwert
    }
    yield();
    leds.clear();            // bei obigen Fehlern nach Signalisierung alle LEDs ausschalten
    leds.show(); //Anzeigen
    delay(5000); // warte 5s
    return;
  }
  // Effekt bei kleiner oder größer als vorheriger Messwert
  if (NUMPIXELS >= 8){ 
    if (co2 > old) {
        showTrailingLED(color, true, -1);   // LED-Ring im Uhrzeigersinn leeren
        showWheel(color, 5000, true);
        showTrailingLED(color, true, 1);    // LED-Ring mit Farbe im Uhrzeigersinn auffüllen
    } else if (old > co2) {
        showTrailingLED(color, false, -1);  // LED-Ring im Gegenuhrzeigersinn leeren
        showWheel(color, 5000, false);
        showTrailingLED(color, false, 1);   // LED-Ring mit Farbe im Gegenuhrzeigersinn auffüllen
    } else { 
        leds.fill(color);                   // LED-Ring in einer Farbe bei gleich bleibendem Wert
    }
  } else {
      leds.fill(color);
  }
  leds.show(); //Anzeigen
  
  if(co2 > (1900 - TRIGOFF)){                  // ID4 sehr niedrig => rot blinken
    for (int i = 0; i <= 10; i++) {
      FadeOutIn ((byte) Red(color), (byte) Green(color), (byte) Blue(color)); // Farbe Rot Fade out/Fade in
      delay(1000); // warte 1s
    } 
  }

  old = co2;   //neuer alter Messwert
  
  if(WiFi.status() == WL_CONNECTED){                       // nur Messdaten senden, wenn erfolgreich per WiFi verbunden
    // Werte des MH-Z19B Sensors ausgelesen => Signalisierung an PITS-Server
    time_t t = CE.toLocal(now(), &tcr);      // Store the current local time in time variable t
//    time_t t = now();                      // Store the current time in time variable t
    String DateTimeString = String(day(t), DEC) + "-" + String(month(t), DEC) + "-" + String(year(t), DEC);
    DateTimeString = DateTimeString + "/" + String(hour(t), DEC) + ":" + String(minute(t), DEC) + ":" + String(second(t), DEC);
    
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
    url += co2;
    url += ";";
    url += temperature;
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
  }
  
  delay(Intervall); // Abstand zwischen den Messungen
}

bool isNight() {
   time_t t = CE.toLocal(now(), &tcr);   // Store the current local time in time variable t
   int Sunrise = myPlace.sunrise(year(t), month(t), day(t), dst);     // Sonnenaufgang
   int Sunset  = myPlace.sunset(year(t), month(t), day(t), dst);      // Sonnenuntergang
   int elapsedMins = (60 * hour(t)) + minute(t);                      // minutes elapsed since midnight
   if (elapsedMins < Sunrise || elapsedMins > Sunset) {return true;}  // es ist Nacht!
   return false;                                                      // ansonsten Tag...
}

int co2ppm() {         // original code @ https://github.com/jehy/arduino-esp8266-mh-z19-serial
  static byte cmd[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};  // Befehl zum Abfragen des Sensors
  static byte response[9] = {0};                                                // Response-Buffer mit 0-Bytes initialisieren
  
  Serial.write(cmd, 9);
  
  // The serial stream can get out of sync. The response starts with 0xff, if not try to resync.
  while (Serial.available() > 0 && (unsigned char)Serial.peek() != 0xFF) {
    Serial.read();           // liest ein Byte von der seriellen Scnittstelle zum resyncen
  }
  
  memset(response, 0, 9);    // Response-Buffer mit 0-Bytes initialisieren
  Serial.readBytes(response, 9);
  
  if (response[1] != 0x86){  // ungültige Antwort -> serieller Verbindung zurücksetzen //, ABC logic on command an Sensor senden, LED blau 6x blinken lassen
    Serial.flush();
    Serial.end();
    Serial.begin(9600); 
//   leds.setPixelColor(0, color = leds.Color(0, 0, 250)); // Farbe Blau setzen
//   for (int i = 0; i <= 3; i++) {
//     FadeOutIn ((byte) Red(color), (byte) Green(color), (byte) Blue(color)); // Farbe Blau Fade out/Fade in
//     delay(1000); // warte 1s
//   }
    delay(11);
    byte abccmd[9] = {0xFF, 0x01, 0x79, 0xA0, 0x00, 0x00, 0x00, 0x00, 0xE6};  // enable ABC logic on command
    Serial.write(abccmd, 9);
//   for (int i = 0; i <= 3; i++) {
//     FadeOutIn ((byte) Red(color), (byte) Green(color), (byte) Blue(color)); // Farbe Blau Fade out/Fade in
//     delay(1000); // warte 1s
//   }
    return -10;               // Abbruch
//    return co2;              // alten Messwert zurückliefern
  } else {
    // Checksumme berechnen
    byte crc = 0;  
    for (int i = 1; i < 8; i++) {
      crc += response[i];
    }
    crc = 255 - crc + 1;
     
    if (response[8] == crc) { // Checksummen stimmen überein => CO2-Konzentration bestimmen
      unsigned int responseHigh = (unsigned int) response[2];
      unsigned int responseLow = (unsigned int) response[3];
      temperature = -40 + (unsigned int) response[4];
      return (256 * responseHigh) + responseLow;
    } else {
#ifdef NOCHECK
      unsigned int responseHigh = (unsigned int) response[2];
      unsigned int responseLow = (unsigned int) response[3];
      temperature = -40 + (unsigned int) response[4];
      return (256 * responseHigh) + responseLow;
#endif 
//      return co2;             // alter Messwert
      return -1;              // Abbruch
    }
  }
}

String uptime() {
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

String IDEString() {
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

time_t getNTPtime(void) {
  time_t retVal = 0;

  for ( int i = 0; i < NTP_RETRIES && retVal == 0; i++ )
  {
    retVal = NTPclient.getNtpTime();
  }
  return ( retVal );
}

bool startWiFi(void) {
  uint8_t i;

#ifdef SERDEBUG
  Serial.print("WIFI >> Attempting to connect to ");
  Serial.print(WLAN_SSID);
  Serial.print(" using password ");
  Serial.println(WLAN_PASSPHRASE);
#endif

  WiFi.persistent(false);  // Reduces flash access, memory wearing
  WiFi.mode(WIFI_STA);     // Explicitly set the ESP8266 to be a WiFi-client
#ifdef HOSTNAME
  WiFi.hostname(HOSTNAME); // Hostname 4 this ESP8266
#endif

  if (WiFi.begin(WLAN_SSID, WLAN_PASSPHRASE) != WL_CONNECTED) {
#ifdef SERDEBUG
    Serial.print("WIFI >> ");
#endif
    for (i = 0; i < 10; i++) {
      if (WiFi.status() == WL_CONNECTED) return true;
      delay(600);
#ifdef SERDEBUG
      Serial.print(".");
#endif
    }
  }

#ifdef SERDEBUG
  Serial.print("Failed to connect to: ");
  Serial.println(WLAN_SSID);

  Serial.print("WIFI >> using pass phrase: ");
  Serial.println(WLAN_PASSPHRASE);
#endif

  return false;
}
