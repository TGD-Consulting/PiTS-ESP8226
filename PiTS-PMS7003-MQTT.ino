/****************************************************************************
 *  PiTS-PMS7003-MQTT Modul                                                 *
 *  =======================                                                 *
 *  Dieser Sketch für den ESP8266 dient als remote Sensor für PiTS-It! zur  *
 *  Erfassung der Feinstaubkonzentration mit PMS7003-Sensor und Darstellung *
 *  als AQI-Ampel über WS2812B LED. Es werden folgende Libraries benötigt:  *
 *   WiFi (Bestandteil der Arduino IDE),                                    *
 *   PubSubClient (https://github.com/knolleary/pubsubclient)               *
 *   NTP (https://github.com/chrismelba/NTP),                               *
 *   Time (https://github.com/PaulStoffregen/Time),                         *
 *   Timezone (https://github.com/JChristensen/Timezone),                   *
 *   Dusk2Dawn (https://github.com/dmkishi/Dusk2Dawn),                      *
 *   Adafruit_NeoPixel (https://github.com/adafruit/Adafruit_NeoPixel),     *
 *                                                                          *
 *  Die Übertragung des Messwertes erfolgt hier per MQTT Publish-Request    *
 *  an einen MQTT-Broker und kann darüber auch vom aktuellen Release der    *
 *  PiTS-It! Management Software empfangen und weiter verarbeitet werden.   *
 *                                                                          *
 *  Homepage: http://pits.TGD-Consulting.de                                 *
 *                                                                          *
 *  Version 0.1.1                                                           *
 *  Datum 05.01.2021                                                        *
 *                                                                          *
 *  (C) 2021 TGD-Consulting , Author: Dirk Weyand                           *
 ****************************************************************************/

/*************************
 *** Globale Parameter ***
 *************************/

#define WLAN_SSID          "SSID des WLAN"               // change to your WiFi SSID 
#define WLAN_PASSPHRASE    "DAS GEHEIME PASSWORT"        // change to your passphrase
#define HOSTNAME           "PiTS-PMAmpel-01"            // change to unique hostname 
#define RETRIES 8                                        // maximale AnzaAhl der Verbindungsversuche mit WLAN-AP
#define MQTT_BROKER        "broker.hivemq.com"           // MQTT Broker URI oder IP
#define MQTT_PORT          1883                          // Standard MQTT Port
#define MQTT_TOPIC         "home/PiTS/PM-Ampel/1234"     // TOPIC der Publish-Message, check @ http://www.hivemq.com/demos/websocket-client/
#define MQTT_KEEPALIVE     30                            // 30 Sekunden Timeout/KeepAlive der MQTT Verbindung 
#define NTP_SERVER         "192.168.0.1"                 // set your local NTP-Server here, or eg. "ptbtime2.ptb.de"
#define PST 0            // GMT/UTC - Anpassung an lokale Sommer/Winterzeit erfolgt über Timezone Library
//#define SERDEBUG 1       // Debug-Infos über Serielle Schnittstelle senden, auskommentiert = Debugging OFF  
#define PIN 2            // WS2812B wird an GPIO 2 des ESP-01 angeschlossen
#define NUMPIXELS 1      // Anzahl der am PIN angeschlossenen WS2812B (eine LED ausreichend für einfache PM-Ampel)
#define BRIGHTNESS 200   // Helligkeit der LEDs (0 dunkel -> 255 ganz hell)
#define STRIPTEST 1      // LEDs beim Setup testen, auskommentiert = kein Test
#define CONNECTEST 1     // Publish Message nach MQTT-Connect, auskommentiert = kein Test-Announcement
#define MINUTEN 2        // Abtastrate, Anzahl Minuten bis zur nächsten Datenübermittlung
#define TRIGOFF 0        // Trigger Offset (0|20|40) für früheren Wechsel der Ampelfarben, bei 0 Schwellwerte gemäß DIN EN 13779
#define NIGHTDIM 1       // Nacht-Modus aktiv, LED wird nach Sonnenuntergang gedimmt, zum Deaktiveren auskommentieren
#define DIMLVL 4         // Dimm-Level (je größer desto dunkler leuchtet die LED im Nacht-Modus)
#define LONGITUDE 9.760  // Position Längengrad muss angepasst werden, damit Dämmerungzeiten für den Standort stimmen
#define LATITUDE 54.644  // Position Breitengrad muss angepasst werden, damit Dämmerungzeiten für den Standort stimmen
#define PMS_READ_DELAY 30000  // 30 Sekunden benötigt der PMS7003 bis vernünftige Messwerte vorhanden sind

/* * * * * * * * * * * * * * * * * * *
 * PMS7003-TX  >> ESP-01-RX     Pin7 *
 * PMS7003-RX  >> ESP-01-TX     Pin2 *
 * PMS7003-SET >> ESP-01-GPIO-0 Pin5 *
 * PMS7003-GND >> ESP-01-GND    Pin1 *
 * PMS7003-VCC >> USB 5V             *
 * * * * * * * * * * * * * * * * * * */

// include requiered library header
#include <ESP8266WiFi.h> // WiFi functionality
#include <WiFiUdp.h>     // udp for network time
#include <PubSubClient.h> // für MQTT Message
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
WiFiClient espClient;
PubSubClient client(espClient);

uint8_t count;  // Zähler für WiFi-Connect Versuche
uint32_t color; // 'Packed' 32-bit RGB Pixelcolor
int Intervall = MINUTEN * 60 * 1000;      // Messinterval
unsigned long lastMsg = 0;                // Zeitstempel der letzten Messung
int AQI = 0;                              // bisheriger AQI Messwert
uint16_t PM1 = 0;                         // bisheriger Messwert für PM1 Konzentration
uint16_t PM25 = 0;                        // bisheriger Messwert für PM2.5 Konzentration
uint16_t PM10 = 0;                        // bisheriger Messwert für PM10 Konzentration
uint16_t nPM1 = 0;                        // Anzahl PM1 Partikel
uint16_t nPM25 = 0;                       // Anzahl PM2.5 Partikel
uint16_t nPM10 = 0;                       // Anzahl PM10 Partikel
int tzo = 1;                              // time zone offset in hours from UTC
bool dst = false;                         // normal time = No Daylight Saving Time
bool pms_disable = false;                 // Status Ventilator PMS7003 (false=LOW/true=HIGH) 
uint32_t AQI1 = leds.Color(0, 250, 0);    // RGB Farbe Grün für AQI 0-50    gute Luftqualität
uint32_t AQI2 = leds.Color(250, 220, 0);  // RGB Farbe Gelb für AQI 0-51    mäßige Luftqualität
uint32_t AQI3 = leds.Color(250, 140, 0);  // RGB Farbe Orange   AQI 100-150 leicht ungesunde Luftqualität
uint32_t AQI4 = leds.Color(250, 0, 0);    // RGB Farbe Rot für  AQI 150-200 ungesunde Luftqualität
uint32_t AQI5 = leds.Color(120, 0, 160);  // RGB Farbe Violett  AQI 201-300 äußerst ungesund
uint32_t AQI6 = leds.Color(140, 0, 33);   // RGB Farbe Maron    AQI 301-400 gefärlich, ab 400 blinkend extrem gefährlich

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
uint32_t DimColor(uint32_t color) {
  uint32_t dimColor = leds.Color(Red(color) >> 1, Green(color) >> 1, Blue(color) >> 1);
  return dimColor;
}

// Farben für Nacht-Modus festlegen
#ifdef NIGHTDIM
uint32_t AQI1dim = AQI1;
uint32_t AQI2dim = AQI2;
uint32_t AQI3dim = AQI3;
uint32_t AQI4dim = AQI4;
uint32_t AQI5dim = AQI5;
uint32_t AQI6dim = AQI6;

void setdimcol (){
   for (int i = 0; i < DIMLVL; i++) { AQI1dim = DimColor(AQI1dim); }
   for (int i = 0; i < DIMLVL; i++) { AQI2dim = DimColor(AQI2dim); }
   for (int i = 0; i < DIMLVL; i++) { AQI3dim = DimColor(AQI3dim); }
   for (int i = 0; i < DIMLVL; i++) { AQI4dim = DimColor(AQI4dim); }
   for (int i = 0; i < DIMLVL; i++) { AQI5dim = DimColor(AQI5dim); }
   for (int i = 0; i < DIMLVL; i++) { AQI6dim = DimColor(AQI6dim); }
}
#endif

void FadeOut (byte red, byte green, byte blue){
  float r, g, b;

  for(int k = 255; k >= 0; k=k-2) {
    r = (k/256.0)*red;
    g = (k/256.0)*green;
    b = (k/256.0)*blue;
    leds.setPixelColor(0, (uint8_t) r,(uint8_t) g, (uint8_t) b);
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
    leds.setPixelColor(0, (uint8_t) r,(uint8_t) g, (uint8_t) b);
    leds.show();
    delay(10);
  }
}

void FadeOutIn (byte red, byte green, byte blue){
  FadeOut (red, green, blue);
  FadeIn  (red, green, blue);
}

void reconnect() {
    while (!client.connected()) {
#ifdef SERDEBUG      
        Serial.print("Reconnecting...");
#endif
        // Create a random MQTT-client ID
       String clientId = String(HOSTNAME);
       clientId += "-";
       clientId += String(random(0xffff), HEX);
       if (client.connect(clientId.c_str())) {                                        // ReConnect with random MQTT-client ID
#ifdef CONNECTEST
          client.publish("home/PiTS/MQTT/PM-Ampel/ESPclient", clientId.c_str());     // Once reconnected, publish an announcement 4 test...  
#endif
       } else {
#ifdef SERDEBUG 
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" retrying in 5 seconds");
#endif
            delay(5000);   // Wait 5 seconds totally before retrying
       }
    }
}

void setup() {
#ifdef SERDEBUG
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println();
  Serial.println("PROG INFORMATION =========================================================");
  Serial.println("PROG >> INFO >> PiTS-ESP8266 with MH-Z19B and MQTT");
  Serial.println("PROG >> TOP  >> " MQTT_TOPIC );
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
  Serial.print("PITS >> SENSOR >> MQTT >> TOPIC ");
  Serial.println(MQTT_TOPIC);
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
  leds.fill(leds.Color(255, 255, 255));    // alle LEDs des Strips leuchten weiss
  leds.show();
  delay(1000);    // warte 1s
  leds.clear();   // alle LEDs ausschalten
  leds.show();
  delay(1000);    // warte 1s
  leds.setPixelColor(0, AQI6); // Farbe Maroon setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, AQI5); // Farbe Violett setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, leds.Color(255, 0, 0)); // Farbe Rot setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, AQI3); // Farbe Orange gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, leds.Color(255, 255, 0)); // Farbe Gelb setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, leds.Color(0, 255, 0)); // Farbe Grün setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, leds.Color(0, 0, 255)); // Farbe Blau setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
#ifdef NIGHTDIM
  setdimcol ();
  leds.setPixelColor(0, AQI6dim); // Farbe Maroon gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, AQI5dim); // Farbe Violett gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, AQI4dim); // Farbe Rot gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, AQI3dim); // Farbe Orange gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, AQI2dim); // Farbe Gelb gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, AQI1dim); // Farbe Grün gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, leds.Color(0, 0, 31)); // Farbe Blau gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
#endif
#endif
  leds.setPixelColor(0, leds.Color(0, 0, 255)); // Farbe Blau setzen zum Dimmen bei erfolglosen WLAN-Connect Versuchen

  // mit WLAN-AP verbinden
  count = 0;
  while (count < RETRIES && !startWiFi()) {
    count++;
    leds.setPixelColor(0, DimColor(leds.getPixelColor(0))); // dunkler dimmen
    leds.show();
  }

  if(WiFi.status() == WL_CONNECTED){
#ifdef SERDEBUG
    Serial.println("WiFi connected");
    Serial.print("WIFI >> IP address: ");
    Serial.println(WiFi.localIP());
#endif

    randomSeed(micros());   // "zufälligen" Salt für RNG bestimmen

    pinMode(0, OUTPUT);     // PMS7003 deaktivieren
    digitalWrite(0, LOW);   // Turn of ventilator
    
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
  leds.setPixelColor(0, color = AQI6); // Farbe Rot setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  leds.setPixelColor(0, color = AQI5); // Farbe Rot setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  leds.setPixelColor(0, color = AQI4); // Farbe Rot setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  leds.setPixelColor(0, color = AQI3); // Farbe Gelb setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));   // ausdimmen
  leds.setPixelColor(0, color = AQI2);  // Farbe Hellgrün setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  leds.setPixelColor(0, color = AQI1); // Farbe Grün setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  
  // erfolgreichen WiFi-Connect signalisieren -> blau dimmen
  if(WiFi.status() == WL_CONNECTED){
#ifdef MQTT_KEEPALIVE
    client.setKeepAlive (MQTT_KEEPALIVE);      // KeepAlive 4 Connection
    client.setSocketTimeout(MQTT_KEEPALIVE);   // TCP-Socket Timeout
#endif    
    client.setServer(MQTT_BROKER, MQTT_PORT);  // Connection zum MQTT Broker herstellen

    leds.setPixelColor(0, color = leds.Color(0, 0, 250)); // Farbe Blau setzen
    leds.show(); //Anzeigen
    delay(1000); // warte 1s
    FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
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

  if(WiFi.status() == WL_CONNECTED){             // nur Messdaten senden, wenn erfolgreich per WiFi verbunden
    if (!client.connected()) {
      reconnect();
    }
    client.loop();                    // MQTT-Verbindung aufrecht erhalten (PingREQ alle 15s bzw. entsprechend KeepAlive)
  }
  
  unsigned long jetzt = millis();
  
  if (jetzt - lastMsg > Intervall - PMS_READ_DELAY) {  // Falls Zeitpunkt der letzten Mesung größer als Messintervall abzgl. "Vorglühzeit" => PMS7003 aktivieren Messung durchführen
    if (!pms_disable) {
      digitalWrite(0, HIGH);   // Turn on ventilator
      pms_disable = true;
    } 
    else if (jetzt - lastMsg > Intervall) {
      lastMsg = jetzt;
      AQI = pms();            // PMS7003 Sensor auslesen
      digitalWrite(0, LOW);   // Turn off ventilator
      pms_disable = false;

    if(WiFi.status() == WL_CONNECTED){         // nur Messdaten senden, wenn erfolgreich per WiFi verbunden
      // Werte des MH-Z19B Sensors ausgelesen => Signalisierung an PITS-Server über MQTT-Broker
      time_t t = CE.toLocal(now(), &tcr);      // Store the current local time in time variable t
//      time_t t = now();                      // Store the current time in time variable t
      String DateTimeString = String(day(t), DEC) + "-" + String(month(t), DEC) + "-" + String(year(t), DEC);
      DateTimeString = DateTimeString + "/" + String(hour(t), DEC) + ":" + String(minute(t), DEC) + ":" + String(second(t), DEC);
        
    // Messwert übertragen
    if (client.connected()) {    
       // We now create the message for the request
       String msg = "";
       msg += AQI;
       msg += " ";
       msg += PM1;
       msg += " ";
       msg += PM25;
       msg += " ";
       msg += PM10;
       msg += " ";
       msg += nPM1;
       msg += " ";
       msg += nPM25;
       msg += " ";
       msg += nPM10;
       if (timeStatus() != timeNotSet) { // Falls Zeit synchron zum NTP-Server, Zeitpunkt übermitteln
         msg += " ";
         msg += DateTimeString;        // im REBOL Time-Format
       }
       msg += " ";
       msg += uptime();

#ifdef SERDEBUG
       Serial.print("PITS >> MQTT   >> Publish Payload: ");
       Serial.println(msg);
#endif
    
       // This will send the request to the broker
       client.publish(MQTT_TOPIC, msg.c_str());
    }
    }
    
  // AQI-Ampel
  if(AQI > (400 - TRIGOFF)){                           // AQI6 sehr gefährlich => maroon blinken
    leds.setPixelColor(0, color = AQI6);     // Maroon für AQI-Ampel sehr gefahrliche Luftqualität = AQI6
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, color = AQI6dim);} // gedimmtes maroon bei Nacht
#endif
    leds.show();   //Anzeigen
    for (int i = 0; i <= 10; i++) {
      FadeOutIn ((byte) Red(color), (byte) Green(color), (byte) Blue(color)); // Farbe maroon Fade out/Fade in
      delay(1000); // warte 1s
    }
  }
  else if(AQI > (300 - TRIGOFF)){            // AQI6 gefährlich => maroon
    leds.setPixelColor(0, AQI6);   // Maroon für AQI-Ampel gefährliche Luftqualität = AQI6
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, AQI6dim);} // Gedimmtes maroon bei Nacht
#endif
  }
  else if(AQI > (200 - TRIGOFF)){           // AQI5 äußerst ungesund => violett
    leds.setPixelColor(0, AQI5);   // Violett für AQI-Ampel äußerst ungesund Luftqualität = AQI5
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, AQI5dim);} // Gedimmtes violett bei Nacht
#endif
  }
  else if(AQI > (150 - TRIGOFF)){           // AQI4 ungsund => rot
    leds.setPixelColor(0, AQI4);   // Rot für AQI-Ampel ungesunde Luftqualität = AQI4
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, AQI3dim);} // Gedimmtes rot bei Nacht
#endif
  }
  else if(AQI > (100 - TRIGOFF)){           // AQI3 leicht ungesund => orange
    leds.setPixelColor(0, AQI3);   // Orange für AQI-Ampel leicht ungesunde Luftqualität = AQI3
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, AQI3dim);} // Gedimmtes orange bei Nacht
#endif
  }
  else if(AQI > (50 - TRIGOFF)){            // AQI2 mäßig => gelb
    leds.setPixelColor(0, AQI2);   // gelb für AQI-Ampel mäßige Luftqualität = AQI2
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, AQI2dim);} // Gedimmtes gelb bei Nacht
#endif
  }
  else if(AQI >= 0) {           // AQI1 gut => grün
    leds.setPixelColor(0, AQI1);   // Grün für AQI-Ampel hohe Raumluftqualität = AQI1
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, AQI1dim);} // Gedimmtes grün bei Nacht
#endif
  } else {   // Ausnahmezustand signalisieren => Ampel aus, ESP LED an 
    yield();
    leds.clear();            // alle LEDs ausschalten
    leds.show(); //Anzeigen
    delay(5000); // warte 5s
    return;
  }
  leds.show(); //Anzeigen

  }

  delay(1000);      // Warte 1 Sekunde
 }
}

bool isNight() {
   time_t t = CE.toLocal(now(), &tcr);   // Store the current local time in time variable t
   int Sunrise = myPlace.sunrise(year(t), month(t), day(t), dst);     // Sonnenaufgang
   int Sunset  = myPlace.sunset(year(t), month(t), day(t), dst);      // Sonnenuntergang
   int elapsedMins = (60 * hour(t)) + minute(t);                      // minutes elapsed since midnight
   if (elapsedMins < Sunrise || elapsedMins > Sunset) {return true;}  // es ist Nacht!
   return false;                                                      // ansonsten Tag...
}

int calcAQI() {
   int val10 = 0;
   // Index aus PM10 ableiten
   if (PM10 < 55) {
     val10 = (int) (50 / 55) * PM10;
   } else if (PM10 < 355) {
     val10 = (int) (PM10 / 2) + 22.5;
   } else if (PM10 < 425) {
     val10 = (int) -307,2 + ((100 / 70) * PM10);
   } else {
    val10 = (int) PM10 - 105;
   }
   int val25 = 0;
   // Index aus PM25 ableiten
   if (PM25 < 12) {
      val25 = (int) (50 / 12) * PM25;
   } else if (PM25 < 35.5) {
      val25 = (int) 24,47 + ((50 / 23.5) * PM25); 
   } else if (PM25 < 55.5) {
      val25 = (int) 11,25 + ((50 / 20) * PM25);
   } else if (PM25 < 150.5) {
      val25 = (int) 120,79 + ((50 / 95) * PM25);
   } else if (PM25 < 350.5) {
      val25 = (int) 49,5 + PM25;
   } else {
      val25 = (int) 166,334 + ((100 / 150) * PM25);
   }
   if (val10 > val25) {
      return val10;
   } else {
      return val25;
   }
}

static byte buffer[32] = {0};                                         // Serial-Buffer mit 0-Bytes initialisieren
inline uint16_t buff2word(uint8_t n) { return (buffer[n] << 8) | buffer[n + 1]; }

int pms() { // Feinstaubsensor auslesen
  //Serial.flush(); // Serial-Buffer löschen, nächstes Datagram auswerten überflüssig, da der Buffer nur 64Bytes= 2 Datagramme enthalten kann.
  
  // The serial stream can get out of sync. The datagram starts with 0x42, if not try to resync.
  while (Serial.available() > 0 && (unsigned char)Serial.peek() != 0x42) {
    Serial.read();           // liest ein Byte von der seriellen Scnittstelle zum resyncen
  }

  memset(buffer, 0, 32);     // Serial-Buffer mit 0-Bytes initialisieren
  Serial.readBytes(buffer, 32);

  if (buff2word(0) == 0x424D && buff2word(2) == 0x001C) {  // gültiges Datagram begint mit BM28
     uint16_t chksum = buff2word(30);                      // Checksumme
     for (uint8_t n = 0; n < 30; n++) {                    // berechnen
        chksum -= buffer[n];      
     }
     if (chksum == 0) {                                    // und üerprüfen
         PM1 = buff2word(10);                              // dann Feinstaubwerte bestimmen
         PM25 = buff2word(12);                             // für Konzentration
         PM10 = buff2word(14);                             // und
         nPM1 = buff2word(16);                             // Partikelanzahl
         nPM25 = buff2word(18);
         nPM10 = buff2word(20);              
     }
  }
  return AQI = calcAQI();             // alter Messwert
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
