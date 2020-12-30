/****************************************************************************
 *  PiTS-MHZ19B-MQTT Modul                                                  *
 *  ======================                                                  *
 *  Dieser Sketch für den ESP8266 dient als remote Sensor für PiTS-It! zur  *
 *  Erfassung der CO2-Konzentration mit MHZ19B-Sensor und Darstellung als   *
 *  CO2-Ampel über WS2812B LED. Es werden folgende Libraries benötigt:      *
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
 *  Version 0.1.0                                                           *
 *  Datum 30.12.2020                                                        *
 *                                                                          *
 *  (C) 2020 TGD-Consulting , Author: Dirk Weyand                           *
 ****************************************************************************/

/*************************
 *** Globale Parameter ***
 *************************/

#define WLAN_SSID          "SSID des WLAN"               // change to your WiFi SSID 
#define WLAN_PASSPHRASE    "DAS GEHEIME PASSWORT"        // change to your passphrase
#define HOSTNAME           "PiTS-CO2Ampel-01"            // change to unique hostname 
#define RETRIES 8                                        // maximale AnzaAhl der Verbindungsversuche mit WLAN-AP
#define MQTT_BROKER        "broker.hivemq.com"           // MQTT Broker URI oder IP
#define MQTT_PORT          1883                          // Standard MQTT Port
#define MQTT_TOPIC         "home/PiTS/CO2-Ampel/1234"    // TOPIC der Publish-Message, check @ http://www.hivemq.com/demos/websocket-client/
#define NTP_SERVER         "192.168.0.1"                 // set your local NTP-Server here, or eg. "ptbtime2.ptb.de"
#define PST 0            // GMT/UTC - Anpassung an lokale Sommer/Winterzeit erfolgt über Timezone Library
//#define SERDEBUG 1       // Debug-Infos über Serielle Schnittstelle senden, auskommentiert = Debugging OFF  
#define PIN 2            // WS2812B wird an GPIO 2 des ESP-01 angeschlossen
#define NUMPIXELS 1      // Anzahl der am PIN angeschlossenen WS2812B (eine LED ausreichend für einfache CO2-Ampel)
#define BRIGHTNESS 200   // Helligkeit der LEDs (0 dunkel -> 255 ganz hell)
#define STRIPTEST 1      // LEDs beim Setup testen, auskommentiert = kein Test
#define MINUTEN 2        // Abtastrate, Anzahl Minuten bis zur nächsten Datenübermittlung
#define TRIGOFF 0        // Trigger Offset (0|100|200) für früheren Wechsel der Ampelfarben, bei 0 Schwellwerte gemäß DIN EN 13779
#define COLD 16          // Schwellwert unterhalb der die Raumtemperatur als unterkühlt/kalt gilt
#define NIGHTDIM 1       // Nacht-Modus aktiv, LED wird nach Sonnenuntergang gedimmt, zum Deaktiveren auskommentieren
#define DIMLVL 3         // Dimm-Level (je größer desto dunkler leuchtet die LED im Nacht-Modus)
#define LONGITUDE 9.760  // Position Längengrad muss angepasst werden, damit Dämmerungzeiten für den Standort stimmen
#define LATITUDE 54.644  // Position Breitengrad muss angepasst werden, damit Dämmerungzeiten für den Standort stimmen
//#define MWK -8           // Messwertkorrektur damit die Temperatur im Sensor mit Raumtemperatur übereinstimmt, Kommentar entfernen, damit die Ampel blau leuchtet, bei zu kalten Räumen mit hoher Raumluftqualität (ID1)

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
int idle = Intervall / 5000;              // Anzahl 5Sekunden Sleeps bis zur Messung
int d = idle;                              // Wenn d größer als idle erfolgt Messung
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
uint32_t DimColor(uint32_t color) {
  uint32_t dimColor = leds.Color(Red(color) >> 1, Green(color) >> 1, Blue(color) >> 1);
  return dimColor;
}

// Farben für Nacht-Modus festlegen
#ifdef NIGHTDIM
uint32_t ID1dim = ID1;
uint32_t ID2dim = ID2;
uint32_t ID3dim = ID3;
uint32_t ID4dim = ID4;

void setdimcol (){
   for (int i = 0; i < DIMLVL; i++) { ID1dim = DimColor(ID1dim); }
   for (int i = 0; i < DIMLVL; i++) { ID2dim = DimColor(ID2dim); }
   for (int i = 0; i < DIMLVL; i++) { ID3dim = DimColor(ID3dim); }
   for (int i = 0; i < DIMLVL; i++) { ID4dim = DimColor(ID4dim); }
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
        if (!client.connect(HOSTNAME)) {
#ifdef SERDEBUG 
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" retrying in 5 seconds");
#endif
            delay(5000);
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
  leds.setPixelColor(0, leds.Color(255, 0, 0)); // Farbe Rot setzen
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
  leds.setPixelColor(0, ID4dim); // Farbe Rot gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, ID3dim); // Farbe Gelb gedimmt setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  leds.setPixelColor(0, ID1dim); // Farbe Grün gedimmt setzen
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
  leds.setPixelColor(0, color = ID4); // Farbe Rot setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  leds.setPixelColor(0, color = ID3); // Farbe Gelb setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));   // ausdimmen
  leds.setPixelColor(0, color = ID2);  // Farbe Hellgrün setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  leds.setPixelColor(0, color = ID1); // Farbe Grün setzen
  leds.show(); //Anzeigen
  delay(1000); // warte 1s
  FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen
  
  // erfolgreichen WiFi-Connect signalisieren -> blau dimmen
  if(WiFi.status() == WL_CONNECTED){
    leds.setPixelColor(0, color = leds.Color(0, 0, 250)); // Farbe Blau setzen
    leds.show(); //Anzeigen
    delay(1000); // warte 1s
    FadeOut ((byte) Red(color), (byte) Green(color), (byte) Blue(color));  // ausdimmen

    client.setServer(MQTT_BROKER, MQTT_PORT);  // Connection zum MQTT Broker herstellen
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

  d++;             // d inkrementieren

  if (d > idle) {  // Falls d größer idle => Messung durchführen
  co2 = co2ppm();  // MH-Z19B Sensor auslesen
  
  // CO2-Ampel
  if(co2 > (1900 - TRIGOFF)){                           // ID4 sehr niedrig => rot blinken
    leds.setPixelColor(0, color = ID4);     // Rot für CO2-Ampel niedrige Raumluftqualität = ID4
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, color = ID4dim);} // gedimmtes Rot bei Nacht
#endif
    leds.show();   //Anzeigen
    for (int i = 0; i <= 10; i++) {
      FadeOutIn ((byte) Red(color), (byte) Green(color), (byte) Blue(color)); // Farbe Rot Fade out/Fade in
      delay(1000); // warte 1s
    }
  }
  else if(co2 > (1400 - TRIGOFF)){            // ID4 niedrig => rot
    leds.setPixelColor(0, ID4);   // Rot für CO2-Ampel niedrige Raumluftqualität = ID4
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, ID4dim);} // Gedimmtes rot bei Nacht
#endif
  }
  else if(co2 >= (1000 - TRIGOFF)){           // ID3 mäßig => gelb
    leds.setPixelColor(0, ID3);   // Gelb für CO2-Ampel mäßige Raumluftqualität = ID3
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, ID3dim);} // Gedimmtes gelb bei Nacht
#endif
  }
  else if(co2 >= (800 - TRIGOFF)){            // ID2 mittel => hellgrün
    leds.setPixelColor(0, ID2);   // Hellgrün für CO2-Ampel mittlere Raumluftqualität = ID2
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, ID2dim);} // Gedimmtes hellgrün bei Nacht
#endif
  }
  else if(co2 >= 400) {           // ID1 gut => grün
    leds.setPixelColor(0, ID1);   // Grün für CO2-Ampel hohe Raumluftqualität = ID1
#ifdef NIGHTDIM
    if (isNight()) {leds.setPixelColor(0, ID1dim);} // Gedimmtes grün bei Nacht
#endif
#ifdef MWK
    if (temperature <= (COLD - MWK)){
      leds.setPixelColor(0, leds.Color(0, 0, 250));   // Blau für CO2-Ampel kalter Raum, hohe Raumluftqualität = ID1
#ifdef NIGHTDIM
      if (isNight()) {leds.setPixelColor(0, leds.Color(0, 0, 31));} // Gedimmtes blau bei Nacht
#endif
    }
#endif
  } else {
    yield();
    leds.clear();            // alle LEDs ausschalten
    leds.show(); //Anzeigen
    delay(5000); // warte 5s
    return;
  }
  leds.show(); //Anzeigen
  
  if(WiFi.status() == WL_CONNECTED){                       // nur Messdaten senden, wenn erfolgreich per WiFi verbunden
    // Werte des MH-Z19B Sensors ausgelesen => Signalisierung an PITS-Server
    time_t t = CE.toLocal(now(), &tcr);      // Store the current local time in time variable t
//    time_t t = now();                      // Store the current time in time variable t
    String DateTimeString = String(day(t), DEC) + "-" + String(month(t), DEC) + "-" + String(year(t), DEC);
    DateTimeString = DateTimeString + "/" + String(hour(t), DEC) + ":" + String(minute(t), DEC) + ":" + String(second(t), DEC);
        
    if (client.connected()) {    
    // We now create the message for the request
    String msg = "";
    msg += co2;
    msg += " ";
    msg += temperature;
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
    } else {
#ifdef SERDEBUG
      Serial.println("PITS >> MQTT   >> connection failed");
#endif
      reconnect();
    }
  }
  d = 1;            // d zurücksetzen
  }
  
  client.loop();    // MQTT-Verbindung aufrecht erhalten (PingREQ alle 15s KeepAlive) 
  
  delay(5000);      // Warte 5 Sekunden
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
 //   byte abccmd[9] = {0xFF, 0x01, 0x79, 0xA0, 0x00, 0x00, 0x00, 0x00, 0xE6};  // enable ABC logic on command
 //   Serial.write(abccmd, 9);
 //   for (int i = 0; i <= 3; i++) {
 //     FadeOutIn ((byte) Red(color), (byte) Green(color), (byte) Blue(color)); // Farbe Blau Fade out/Fade in
 //     delay(1000); // warte 1s
 //   }
 //   return -1;               // Abbruch
    return co2;              // alten Messwert zurückliefern
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
      return co2;             // alter Messwert
//      return -1;              // Abbruch
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
