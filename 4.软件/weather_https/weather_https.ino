
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

#include <TimeLib.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

/*--------------------== HTTPS Setting ==-------------------------*/
// Fingerprint for demo URL, expires on June 2, 2019, needs to be updated well before this date
const uint8_t fingerprint[20] = {0xe3, 0x3A, 0x34, 0x79, 0x6F, 0xC8, 0x93, 0x40, 0x69, 0x3E, 0x18, 0x6F, 0xF0, 0x43, 0x72, 0x12, 0xA7, 0x47, 0x75, 0xA6};
ESP8266WiFiMulti WiFiMulti;

/*--------------------== Weather Station Setting ==-------------------------*/
String url = "https://free-api.heweather.net/s6/weather/now?location=*********&key=************&lang=en";
//const char* host = "free-api.heweather.net";
//const int httpPort = 443;

/*--------------------== Display Setting ==-------------------------*/
//#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
//#include "SH1106.h" // alias for `#include "SSD1306Wire.h"`
// Initialize the OLED display using SPI
// D5 -> CLK
// D7 -> MOSI (DOUT)
// D0 -> RES
// D2 -> DC
// D8 -> CS
#include "SSD1306Spi.h"
SSD1306Spi  display(0, 2, 8);
//#include "SH1106.h"
//SH1106 display(0x3c, D3, D5);
#include "images.h"
#include "Fonts.h"

/*--------------------== WIFI Setting ==-------------------------*/
char ssid[] = "******";  //  your network SSID (name)
char pass[] = "*******";       // your network password

/*--------------------== UDP Setting ==-------------------------*/
WiFiUDP Udp;
unsigned int localPort = 8888;

/*--------------------== NTP Setting ==-------------------------*/
static const char ntpServerName1[] = "ntp.ntsc.ac.cn";// NTP Servers:
static const char ntpServerName2[] = "ntp1.aliyun.com";
const int timeZone = 8;     // Central European Time
time_t getNtpTime();

/*--------------------== Function ==-------------------------*/
void digitalClockDisplay();
void sendNTPpacket(IPAddress &address);
const String WDAY_NAMES[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

/*--------------------== JSON Setting ==-------------------------*/
int Weather_now_cond;
const char* Weather_now_txt;
int Weather_now_tmp;
int Weather_now_hum;

void setup() {
  Serial.begin(9600);
  delay(250);
  display.init();
  display.flipScreenVertically();
  show_strat_logo();
  show_connect_wifi();
  Serial.println("[SYS]System Start");
  Serial.print("[SYS]Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(ssid, pass);
  while (WiFiMulti.run() != WL_CONNECTED)
  {
    show_connect_wifi();
  }
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 16, "IP assigned : ");
  display.drawString(0, 32, WiFi.localIP().toString());
  display.drawString(0, 48, "get weather info");
  display.display();
  delay(3000);
  Serial.print("[SYS]IP assigned");
  Serial.println(WiFi.localIP());
  Serial.println("[SYS]Starting UDP");
  Udp.begin(localPort);
  Serial.print("[SYS]Local port: ");
  Serial.println(Udp.localPort());
  Serial.println("[SYS]waiting for sync");
  setSyncProvider(getNtpTime);
  setSyncInterval(300);
  get_weather_info();
}

time_t prevDisplay = 0; // when the digital clock was displayed

void loop() {
  if (timeStatus() != timeNotSet) {
    if (minute() == 35 && second() == 25) {
      get_weather_info();
      Serial.println("[SYS]Updateing Weather info");
    }
    if (now() != prevDisplay) { //update the display only if time has changed
      prevDisplay = now();
      digitalClockDisplay();
    }
  }
}

void digitalClockDisplay()
{
  display.clear();
  display.drawXbm( 112, 0, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits[2]);
  display.drawLine(0, 14, 127, 14);
  // digital clock display of the time
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  String DayString = String(year()) + "-" + String(month()) + "-" + String(day());
  String TimeString = String(hour()) + ( minute() > 9 ? ":" : ":0") + String(minute()) + ( second() > 9 ? ":" : ":0" ) + String(second());
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, DayString);
  display.drawString(60, 0, WDAY_NAMES[weekday() - 1]);
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(127, 17, TimeString);
  display.setFont(Meteocons_Plain_36);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(30, 18, String(get_weather_icon(Weather_now_cond)));
  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  String temp = String(Weather_now_tmp) + "°C" ;
  display.drawString(70, 34, temp);
  display.drawLine(0, 62, 127, 62);
  /* display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawXbm(0, 32, T_16_width, T_16_height, tian_bits);
    display.drawXbm(17, 32, T_16_width, T_16_height, qi_bits);
    display.setFont(ArialMT_Plain_16);
    display.drawString(34, 32, ":");
    display.drawString(37, 32, Weather_now_txt );
    display.drawXbm(0, 48, T_16_width, T_16_height, wen_bits);
    display.drawXbm(17, 48, T_16_width, T_16_height, du_bits);
    display.drawString(34, 48, ":");
    display.drawString(37, 48, Weather_now_tmp);
  */
  display.display();
  // Serial.println(DayString + TimeString);
}

/*-------- NTP code ----------*/
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("[SYS]Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName1, ntpServerIP);
  Serial.print(ntpServerName1);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("[SYS]Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("[SYS]No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress & address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
void show_strat_logo() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawXbm( 0, 25, logo2_width, logo2_height, logo2_2_bits);
  display.display();
  delay(2000);
}
void show_connect_wifi() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 16, "connecting to wifi");
  for (int i = 0 ; i < 3; i++) {
    display.drawXbm(56, 40, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits[i]);
    display.display();
    delay(500);
  }
}

void get_weather_info() {
  const size_t capacity = JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(8) + JSON_OBJECT_SIZE(13) + 370;
  DynamicJsonBuffer jsonBuffer(capacity);
  String json;
  /*
    Serial.print("[SYS]Setting time using SNTP");
    configTime(8 * 3600, 0, ntpServerName1, ntpServerName2);
    time_t now = now();
    while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    }
    Serial.println("");
  */
  time_t now_time = now();
  struct tm timeinfo;
  gmtime_r(&now_time, &timeinfo);
  if ((WiFiMulti.run() == WL_CONNECTED)) {
    std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
    client->setFingerprint(fingerprint);
    HTTPClient https;
    //Serial.print("[HTTPS] begin...\n");
    if (https.begin(*client, url)) {  // HTTPS
      //Serial.print("[HTTPS] GET...\n");
      // start connection and send HTTP header
      int httpCode = https.GET();
      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled
        // Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
          json = https.getString();
          // Serial.println(json);
        }
      } else {
        // Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
      }
      https.end();
    } else {
      //Serial.printf("[HTTPS] Unable to connect\n");
    }
  }
  JsonObject& root = jsonBuffer.parseObject(json);
  JsonObject& HeWeather6_0 = root["HeWeather6"][0];
  JsonObject& HeWeather6_0_now = HeWeather6_0["now"];
  Weather_now_cond = HeWeather6_0_now["cond_code"]; // "100"
  Weather_now_txt = HeWeather6_0_now["cond_txt"]; // "Clear"
  Weather_now_tmp = HeWeather6_0_now["tmp"]; // "21"
  Weather_now_hum = HeWeather6_0_now["hum"]; // "59"
}

char get_weather_icon(int i) {
  int is_night = 0;
  if (hour() > 18 || hour() < 5 ) {
    is_night = 1;
  }
  char icon_code;
  switch (i) {
    case  100 : is_night == 0 ? icon_code = 66 : icon_code = 50; break;
    case  101 : icon_code = 89  ; break;
    case  102 : icon_code = 78  ; break;
    case  103 : icon_code = 72  ; break;
    case  104 : is_night == 0 ? icon_code = 68 : icon_code = 73; break;
    case  200 : icon_code = 83  ; break;
    case  201 : icon_code = 83  ; break;
    case  202 : icon_code = 83  ; break;
    case  203 : icon_code = 83  ; break;
    case  204 : icon_code = 83  ; break;
    case  205 : icon_code = 83  ; break;
    case  206 : icon_code = 83  ; break;
    case  207 : icon_code = 83  ; break;
    case  208 : icon_code = 83  ; break;
    case  209 : icon_code = 83  ; break;
    case  210 : icon_code = 83  ; break;
    case  211 : icon_code = 83  ; break;
    case  212 : icon_code = 83  ; break;
    case  213 : icon_code = 83  ; break;
    case  300 : icon_code = 83  ; break;
    case  301 : icon_code = 84  ; break;
    case  302 : icon_code = 79  ; break;
    case  303 : icon_code = 80  ; break;
    case  304 : icon_code = 80  ; break;
    case  305 : icon_code = 81  ; break;
    case  306 : icon_code = 82  ; break;
    case  307 : icon_code = 82  ; break;
    case  308 : icon_code = 82  ; break;
    case  309 : icon_code = 82  ; break;
    case  310 : icon_code = 82  ; break;
    case  311 : icon_code = 82  ; break;
    case  312 : icon_code = 82  ; break;
    case  313 : icon_code = 82  ; break;
    case  314 : icon_code = 82  ; break;
    case  315 : icon_code = 82  ; break;
    case  316 : icon_code = 82  ; break;
    case  317 : icon_code = 82  ; break;
    case  318 : icon_code = 82  ; break;
    case  399 : icon_code = 82  ; break;
    case  400 : icon_code = 85  ; break;
    case  401 : icon_code = 35  ; break;
    case  402 : icon_code = 35  ; break;
    case  403 : icon_code = 35  ; break;
    case  404 : icon_code = 35  ; break;
    case  405 : icon_code = 35  ; break;
    case  406 : icon_code = 35  ; break;
    case  407 : icon_code = 86  ; break;
    case  408 : icon_code = 87  ; break;
    case  409 : icon_code = 88  ; break;
    case  410 : icon_code = 88  ; break;
    case  499 : icon_code = 71  ; break;
    case  500 : icon_code = 69  ; break;
    case  501 : icon_code = 77  ; break;
    case  502 : icon_code = 74  ; break;
    case  503 : icon_code = 74  ; break;
    case  504 : icon_code = 74  ; break;
    case  507 : icon_code = 70  ; break;
    case  508 : icon_code = 77  ; break;
    case  509 : icon_code = 77  ; break;
    case  510 : icon_code = 77  ; break;
    case  511 : icon_code = 77  ; break;
    case  512 : icon_code = 77  ; break;
    case  513 : icon_code = 77  ; break;
    case  514 : icon_code = 77  ; break;
    case  515 : icon_code = 77  ; break;
    case  999 : icon_code = 41  ; break;
    default: icon_code = 41;
  }
  return icon_code;
}
