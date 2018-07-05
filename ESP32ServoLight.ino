/*

   ESP32 Servo + LEDRing Webcontrol

  ´ (c) 23.02.2017 Luca Zimmermann
        git@sndstrm.de

   Libraries:
   ESP32Servo
    - funktionierender Fork: github.com/soundstorm/ESP32Servo
*/

#include <WiFi.h>
#include <ESP32_Servo.h>
#include "esp32_digital_led_lib.h"

#define SERVO_PIN 19
#define LED_PIN   17 // Nicht aendern!

#define LEDS      16 // Anzahl der LEDs im Ring
#define MAX_BRIGHTNESS 255

// Millisekunden zwischen 1 Bit Farbwechseln
#define FADE_INTERVAL 30

// WLAN Daten an das eigene Netzwerk anpassen
// Alternativ den ESP einen Accesspoint starten lassen
const char* ssid     = "Netzwerk";
const char* password = "Passwort";


// IP Adresse ebenfalls an Netzwerk anpassen
IPAddress ip(192, 168, 0, 111);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

WiFiServer server(80);
Servo myservo;

int color_is[3];
int color_set[3];
uint32_t nextfade;
uint8_t servopos;

strand_t pixels[] = {{.rmtChannel = 1, .gpioNum = LED_PIN, .ledType = LED_WS2812B_V3, .brightLimit = MAX_BRIGHTNESS, .numPixels =  LEDS,
                   .pixels = nullptr, ._stateVars = nullptr
}};


#ifndef max
#define max(x, y) (((x) > (y)) ? (x) : (y))
#endif
#ifndef min
#define min(x, y) (((x) < (y)) ? (x) : (y))
#endif

int getMaxMalloc(int min_mem, int max_mem) {
  int prev_size = min_mem;
  int curr_size = min_mem;
  int max_free = 0;
  while (1) {
    void * foo1 = malloc(curr_size);
    if (foo1 == nullptr) {  // Back off
      max_mem = min(curr_size, max_mem);
      curr_size = (int)(curr_size - (curr_size - prev_size) / 2.0);
    }
    else {  // Advance
      free(foo1);
      max_free = curr_size;
      prev_size = curr_size;
      curr_size = min(curr_size * 2, max_mem);
    }
    if (abs(curr_size - prev_size) == 0) {
      break;
    }
  }
  Serial.print("checkmem: max free heap = ");
  Serial.print(esp_get_free_heap_size());
  Serial.print(" bytes, max allocable = ");
  Serial.print(max_free);
  Serial.println(" bytes");
  return max_free;
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  delay(1000);
  getMaxMalloc(1 * 1024, 16 * 1024 * 1024);
  digitalLeds_initStrands(pixels, 1);
  // Je nach Servo die us-Werte angepassen
  // SG90 nutzen etwa 600 bis 2400us
  // MG995 und viele andere Typen liegen bei 1000 und 2000us
  myservo.attach(SERVO_PIN, 600, 2400);

  Serial.println();
  Serial.println();
  
  // Kann weggelassen werden, wenn IP Adresse per DHCP zugewiesen werden soll
  if (!WiFi.config(ip, gateway, subnet)) {
    Serial.println("Statische IP konnte nicht zugewiesen werden");
  }
  Serial.print("Verbindung wird hergestellt zu ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  uint8_t counter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    int16_t puls = (millis() / 10) % 509;
    puls = 255 - puls;
    for (uint8_t i = 0; i < LEDS; i++) {
      // Licht pulsieren lassen, so lange noch keine Verbindung zum WLAN besteht
      pixels[0].pixels[i] = pixelFromRGB(abs(puls), 0, 0);
    }
    delay(10);
    digitalLeds_updatePixels(&pixels[0]);
    counter++;
    // Alle 500ms einen Punkt auf der Konsole ausgeben
    if (counter == 50) {
      Serial.print(".");
      counter = 0;
    }
  }
  for (uint8_t i = 0; i < LEDS; i++) {
    // Signalisieren, dass die Verbindung hergestellt wurde
    pixels[0].pixels[i] = pixelFromRGB(0, 127, 0);
  }
  delay(10);
  digitalLeds_updatePixels(&pixels[0]);

  Serial.println("");
  Serial.println("WLAN verbunden.");
  Serial.println("IP Addresse: ");
  Serial.println(WiFi.localIP());
  server.begin();
  delay(1000);
  for (uint8_t i = 0; i < LEDS; i++) {
    pixels[0].pixels[i] = pixelFromRGB(0, 0, 0);
  }
  digitalLeds_updatePixels(&pixels[0]);
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    Serial.println("Neue Verbindung");
    String currentLine = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        if (c == '\n') {
          // GET Pfad extrahieren
          if (currentLine.startsWith("GET /")) {
            // Pfad hat servo/... ?
            int strpos = currentLine.indexOf("/servo/");
            String substr;
            if (strpos != -1) {
              // Alles nach /servo/ (7 Zeichen) extrahieren
              substr = currentLine.substring(strpos + 7);
              // Mittels atoi alle nachfolgenden Ziffern in eine Zahl wandeln
              // Sobald ein anderes Zeichen als 0-9 kommt, bricht atoi automatisch ab
              // Mittels Absolutwert und Modulo 181 den Zahlenbereich auf 0-180 begrenzen
              servopos = abs(atoi(substr.c_str())) % 181;
              // Gelesenen Wert an den Servo schreiben
              myservo.write(servopos);
            }
            strpos = currentLine.indexOf("/red/");
            if (strpos != -1) {
              // /red/ ist nur 5 Zeichen lang
              substr = currentLine.substring(strpos + 5);
              // & 0xFF ist identisch zu % 256, also Zahlenbereich auf 0-255 stutzen
              color_set[0] = abs(atoi(substr.c_str())) & 0xFF;
            }
            strpos = currentLine.indexOf("/green/");
            if (strpos != -1) {
              substr = currentLine.substring(strpos + 7);
              color_set[1] = abs(atoi(substr.c_str())) & 0xFF;
            }
            strpos = currentLine.indexOf("/blue/");
            if (strpos != -1) {
              substr = currentLine.substring(strpos + 6);
              color_set[2] = abs(atoi(substr.c_str())) & 0xFF;
            }
            Serial.print("R=");
            Serial.print(color_set[0]);
            Serial.print("; G=");
            Serial.print(color_set[1]);
            Serial.print("; B=");
            Serial.print(color_set[2]);
            Serial.print("; Servo=");
            Serial.println(servopos);
          }

          if (currentLine.length() == 0) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println();
            client.print("R=");
            client.print(color_set[0]);
            client.print("; G=");
            client.print(color_set[1]);
            client.print("; B=");
            client.print(color_set[2]);
            client.print("; Servo=");
            client.println(servopos);
            client.println();
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    client.stop();
    Serial.println("Verbindung getrennt.");
  }
  // Pruefen, ob ueberblendet werden soll
  if (millis() > nextfade) {
    for (int i = 0; i < 3; i++) {
      // Farben anpassen
      if (color_is[i] < color_set[i]) {
        color_is[i]++;
      } else if (color_is[i] > color_set[i]) {
        color_is[i]--;
      }
    }
    nextfade = millis() + FADE_INTERVAL;
  }
  for (int i = 0; i < LEDS; i++) {
    pixels[0].pixels[i] = pixelFromRGB(color_is[0], color_is[1], color_is[2]);
  }
  digitalLeds_updatePixels(&pixels[0]);
}
