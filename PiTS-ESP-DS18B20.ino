/****************************************************************************
 * PiTS-ESP8266-DS18B20 Modul                                               *
 * ==========================                                               *
 * Dieser Sketch für den ESP8266 dient als remote Sensor für PiTS-It! zur   *
 * Temperaturerfassung mit DS18B20-Sensor und benötigt folgende Libraries:  *
 *   WiFi (Bestandteil der Arduino IDE),                                    *
 *   NTP (https://github.com/chrismelba/NTP),                               *
 *   Time (https://github.com/PaulStoffregen/Time),                         *
 *   Timezone (https://github.com/JChristensen/Timezone)                    *
 *   OneWire (https://github.com/PaulStoffregen/OneWire)                    *
 *   DallasTemperature (https://github.com/milesburton/                     *
 *                               Arduino-Temperature-Control-Library)       *
 *                                                                          *
 * Die Übertragung des Messwerte erfolgt per HTTP-Get Request an das        *
 * Webserver Modul von PiTS-It!                                             *
 *                                                                          *
 * Homepage: http://pits.TGD-Consulting.de                                  *
 *                                                                          *
 * Version 0.2.0                                                            *
 * Datum 21.08.2017                                                         *
 *                                                                          *
 * (C) 2017 TGD-Consulting , Author: Dirk Weyand                            *
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
#define TOKEN                   "000000453c67f0"         // Verbindungstoken (Seriennummer des RPi)
#define PST 0            // GMT/UTC - Anpassung an lokale Sommer/Winterzeit erfolgt über Timezone Library
#define SERDEBUG 1       // Debug-Infos über Serielle Schnittstelle senden, auskommentiert = Debugging OFF  
#define ONE_WIRE_BUS 5   // DS18B20 an GPIO 5
#define MAX_TRIES 5      // maximal fünf Versuche zum Auslesen des DS18B20
#define MINUTEN 10       // Abtastrate, Anzahl Minuten bis zur nächsten Datenübermittlung

// include requiered library header
#include <ESP8266WiFi.h> // WiFi functionality
#include <WiFiUdp.h>     // udp for network time
#include <TimeLib.h>
#include <ntp.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// function pre declaration 2 avoid errors
bool startWiFi(void);
time_t getNTPtime(void);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
NTP NTPclient;

//Central Europe Time (Berlin, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};    //Central European Summer Time = UTC + 2 hours
TimeChangeRule CET = {"CET", Last, Sun, Oct, 3, 60};       //Central European Standard Time = UTC + 1 hours
Timezone CE(CEST, CET);

TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev

ADC_MODE(ADC_VCC);          // Aktiviert Spannungsabfrage

void setup() {
#ifdef SERDEBUG
   Serial.begin(115200);
   delay(10);
   Serial.println();
   Serial.println();
   Serial.println("PROG INFORMATION =========================================================");
   Serial.println("PROG >> INFO >> PiTS-ESP8266 with DS18B20");
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

   // mit WLAN-AP verbinden
   while (!startWiFi()){delay(1500);}

#ifdef SERDEBUG  
   Serial.println("WiFi connected");
   Serial.print("WIFI >> IP address: ");
   Serial.println(WiFi.localIP());
#endif

   NTPclient.begin(NTP_SERVER, PST);
   setSyncProvider(getNTPtime);
   setSyncInterval(SECS_PER_HOUR);

   delay(1000);  // nach dem Start 1 Sekunden Zeit, für NTP-Synchronisation

}

void loop() {
    float temp;
    int loopCount, Intervall;
    Intervall = MINUTEN * 60 * 1000;

    // DS18B20 Sensor auslesen
    do
    {
       delay(1000);
       DS18B20.requestTemperatures();
       delay(1000); // Pause, damit der ermittelte Wert im Eeprom des DS18B20 abgelegt werden kann
       temp = DS18B20.getTempCByIndex(0);
#ifdef SERDEBUG  
       Serial.print("1Wire>> DS18B20>> Temperature: ");
       Serial.println(temp);
#endif
    } while( temp == 85.0 && loopCount++ < MAX_TRIES );

    if( temp != 85.0 )
    {
      // Temperatur ausgelesen => Signalisierung an PITS-Server
      time_t t = CE.toLocal(now(), &tcr); //now();                      // Store the current time in time variable t
      String DateTimeString = String(day(t),DEC) + "-" + String(month(t),DEC) + "-" + String(year(t),DEC);
      DateTimeString = DateTimeString + "/" + String(hour(t),DEC) + ":" + String(minute(t),DEC) + ":" + String(second(t),DEC);

#ifdef SERDEBUG
    Serial.print("PITS >> SENSOR >> current temperature ");
    Serial.println(temp);
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
    url += temp;
    url += "&run=";
    url += uptime();
    if (timeStatus() != timeNotSet){ // Falls Zeit synchron zum NTP-Server, Zeitpunkt übermitteln
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

   for( int i = 0; i < NTP_RETRIES && retVal == 0; i++ )
   {
     retVal = NTPclient.getNtpTime();
   }
   return( retVal );
}

bool startWiFi(void)
{
   uint8_t i;

#ifdef SERDEBUG
   Serial.print("WIFI >> Attempting to connect to ");
   Serial.print(WLAN_SSID);
   Serial.print(" using password ");
   Serial.println(WLAN_PASSPHRASE);
#endif

   WiFi.persistent(false); // Reduces flash access, memory wearing
   WiFi.mode(WIFI_STA);    // Explicitly set the ESP8266 to be a WiFi-client

   if (WiFi.begin(WLAN_SSID, WLAN_PASSPHRASE) != WL_CONNECTED) {
#ifdef SERDEBUG
      Serial.print("WIFI >> ");
#endif
      for (i=0;i<10;i++){
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
