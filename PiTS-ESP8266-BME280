/****************************************************************************
 * PiTS-ESP8266 Modul                                                       *
 * ==================                                                       *
 * Dieser Sketch für den ESP8266 dient als remote Sensor für PiTS-It! zur   *
 * Temperaturerfassung mit BME280-Sensor und benötigt folgende Libraries:   *
 *  WiFi, NTP, Time, Adafruit_BME280, Adafruit_Sensors                      *
 *                                                                          *
 * Die Übertragung des Messwerte erfolgt per HTTP-Get Request an das        *
 * Webserver Modul von PiTS-It!                                             *
 *                                                                          *
 * Homepage: http://pits.TGD-Consulting.de                                  *
 *                                                                          *
 * Version 0.1.0                                                            *
 * Datum 18.06.2017                                                         *
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
#define PST +1           // MESZ
#define SERDEBUG 1       // Debug-Infos über Serielle Schnittstelle senden, bei 0 Debugging OFF  
#define GPIO_I2C_SDA 4   // Verwende GPIO4 als I2C SDA (Input)
#define GPIO_I2C_SCL 5   // Verwende GPIO5 als I2C SCL
#define MINUTEN 10       // Abtastrate, Anzahl Minuten bis zur nächsten Datenübermittlung

// include requiered library header
#include <ntp.h>
#include <ESP8266WiFi.h> // WiFi functionality
#include <WiFiUdp.h>     // udp for network time
#include <Time.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// function pre declaration 2 avoid errors
bool startWiFi(void);
time_t getNTPtime(void);

Adafruit_BME280 bme; // Note Adafruit assumes I2C adress = 0x77 my module (eBay) uses 0x76 so the library address has been changed accordingly in Adafruit_BME280.h
NTP NTPclient;

void setup() {
#ifdef SERDEBUG
   Serial.begin(115200);
   delay(10);
   Serial.println();
   Serial.println();
   Serial.println("PROG INFORMATION =========================================================");
   Serial.println("PROG >> INFO >> PiTS-ESP8266 with BOSCH BME280");
   Serial.println("PROG >> ID   >> " ZAEHLER_ID );
   Serial.println("PROG >> DATE >> " __DATE__ );
   Serial.println("PROG >> TIME >> " __TIME__ );
   Serial.println("PROG >> GCC  >> " __VERSION__ );
   Serial.println(String("PROG >> IDE  >> ") + IDEString() );
   Serial.println("CHIP INFORMATION =========================================================");
   Serial.printf("CHIP >> CORE  >> ID: %08X\r\n", ESP.getChipId());
   Serial.println(String("CHIP >> CORE  >> Free Heap: ") + ESP.getFreeHeap() / 1024 + " kB");
   Serial.println("CHIP >> CORE  >> Speed: 80 MHz");
   Serial.printf("CHIP >> FLASH >> ID : %08X\r\n", ESP.getFlashChipId());
   Serial.println(String("CHIP >> FLASH >> Size: ") + ESP.getFlashChipRealSize() / 1024 + " kB");
   Serial.println(String("CHIP >> FLASH >> Speed: ") + ESP.getFlashChipSpeed()/1000000 + " MHz");
   Serial.println("==========================================================================");
#endif

   pinMode(GPIO_I2C_SDA, INPUT_PULLUP); // Set input (SDA) pull-up resistor on

   // Open I2C Bus on defined Pins
   Wire.begin(GPIO_I2C_SDA, GPIO_I2C_SCL);
#ifdef SERDEBUG
   Serial.println("I2C-Bus initialized!");
#endif
   delay(1500);  // and give BME280 time to boot
  
   // Init BME280 on I2C
   if (!bme.begin()) {
#ifdef SERDEBUG
     Serial.println("I2C-No Sensor detected!");
#endif
   } else {
#ifdef SERDEBUG  
     Serial.println("I2C-BME280 initialized!");
#endif
   }

   // mit WLAN-AP verbinden
   while (!startWiFi()){delay(1500);}

#ifdef SERDEBUG  
   Serial.println("WiFi connected");
   Serial.println("IP address: ");
   Serial.println(WiFi.localIP());
#endif

   NTPclient.begin(NTP_SERVER, PST);
   setSyncInterval(SECS_PER_HOUR);
   setSyncProvider(getNTPtime);

   delay(1000);  // nach dem Start 1 Sekunde Zeit, für NTP-Synchronisation

}

void loop() {
    float pressure, temp, humidity, altitude;
    int loopCount;
    Intervall = MINUTEN*60*1000;

    // BME280 Sensor auslesen
    temp = bme.readTemperature();
    pressure = bme.readPressure() / 100.0F; // Luftdruck in hPa
    humidity = bme.readHumidity();
    altitude = bme.readAltitude(1013.25);   // ungefähre Höhe über NN
#ifdef SERDEBUG  
    Serial.print("Temperature: ");
    Serial.println(temp);
    Serial.print("Pressure: ");
    Serial.println(pressure);
    Serial.print("Humidity: ");
    Serial.println(humidity);
    Serial.print("Altitude: ");
    Serial.println(altitude);       
#endif

    // Werte des BME280 Sensors ausgelesen => Signalisierung an PITS-Server
    time_t t = now();                      // Store the current time in time variable t 
    String DateTimeString = String(day(t),DEC) + "-" + String(month(t),DEC) + "-" + String(year(t),DEC);
    DateTimeString = DateTimeString + "/" + String(hour(t),DEC) + ":" + String(minute(t),DEC) + ":" + String(second(t),DEC);

#ifdef SERDEBUG
    Serial.print("current temperature ");
    Serial.println(temp);       
    Serial.print("maessured @ ");
    Serial.println(DateTimeString);
    Serial.print("connecting to ");
    Serial.println(PITS_HOST);
#endif

    // Use WiFiClient class to create TCP connections
    WiFiClient client;
    if (!client.connect(PITS_HOST, PITS_PORT)) {
#ifdef SERDEBUG
       Serial.println("connection failed");
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
    url += ";";
    url += pressure;
    url += ";";
    url += humidity;
    if (timeStatus() != timeNotSet){ // Falls Zeit synchron zum NTP-Server, Zeitpunkt übermitteln
       url += "&time=";
       url += DateTimeString;        // im REBOL Time-Format
    }

#ifdef SERDEBUG  
    Serial.print("Requesting URL: ");
    Serial.println(url);
#endif
  
    // This will send the request to the server
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                "Host: " + PITS_HOST + "\r\n" +
                "Connection: close\r\n\r\n");
 
    delay(Intervall); // Abstand zwischen den Messungen
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
   Serial.print("Attempting to Connect to ");
   Serial.print(WLAN_SSID);
   Serial.print(" using password ");
   Serial.println(WLAN_PASSPHRASE);
#endif

   WiFi.persistent(false); // Reduces flash access, memory wearing
   WiFi.mode(WIFI_STA);    // Explicitly set the ESP8266 to be a WiFi-client

   if (WiFi.begin(WLAN_SSID, WLAN_PASSPHRASE) != WL_CONNECTED) {
      for (i=0;i<10;i++){
        if (WiFi.status() == WL_CONNECTED) return true;
        delay(500);
#ifdef SERDEBUG
        Serial.print(".");
#endif
      }
   }

#ifdef SERDEBUG
   Serial.print("Failed to connect to: ");
   Serial.println(WLAN_SSID);
  
   Serial.print("using pass phrase: ");
   Serial.println(WLAN_PASSPHRASE);
#endif

   return false;
}
