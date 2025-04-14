#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <max6675.h>
#include "RunningAverage.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

void updateTemperature();
void updateSolarHeater();

// ====== WiFi Configuration ======
const char* ssid = "bluedirt_IOT_2G";
const char* password = "allthethings!";

// ====== SPI Pin Definitions for MAX6675 on VSPI ======
const int thermoSCK = 18;
const int thermoCS  = 5;
const int thermoSO  = 19;
const int SOLAR_RELAY_PIN = 26;
const int SUN_PANEL_PIN = GPIO_NUM_36;
const float  SUN_ON_LEVEL = 2900.0;
const int TEMP_UPDATE_INTERVAL_MILLIS = 5000;
const int NUM_SUN_SAMPLES=50;

//local vars
int heater_on = 0;
float tempC = 0.0;
float tempF = 0.0;
int sunLevel = 0;
unsigned long lastTempUpdate = 0;
unsigned long lastSolarUpdate = 0;


MAX6675 thermocouple(thermoSCK, thermoCS, thermoSO);
AsyncWebServer  server(80);
RunningAverage sunLevelAvg(NUM_SUN_SAMPLES);

// ====== Read Temperature Helper ======
float readTemperatureC() {
  float tempC = thermocouple.readCelsius();
  if (isnan(tempC) || tempC < -100.0) {
    Serial.println("Error: Invalid temperature reading");
    return NAN;
  }
  return tempC;
}


// ====== Convert Celsius to Fahrenheit ======
float celsiusToFahrenheit(float tempC) {
  return (tempC * 9.0 / 5.0) + 32.0;
}


// ====== Setup and Loop ======
void setup() {
  Serial.begin(115200);
  delay(1000); // let serial settle
  pinMode(SOLAR_RELAY_PIN,OUTPUT);

  // Start WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }


  Serial.println("\nConnected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // ====== HTTP Handlers ======
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    char html[512];

    if (isnan(tempC)) {
      snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>ESP32 Pool Controller</title></head><body>"
        "<h1>ESP32 Pool Controller</h1>"
        "<p>Error reading temperature.</p></body></html>"
      );
    } else {
      snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><title>ESP32 Pool Controller</title></head><body>"
        "<h1>ESP32 Pool Controller</h1>"
        "<p>Current Temperature: %.2f&deg;C (%.2f&deg;F)</p></body></html>",
        tempC, tempF
      );
    }

    request->send(200, "text/html", html);
  });

  server.on("/gettemperature", HTTP_GET, [](AsyncWebServerRequest *request){
    char json[512];
    snprintf(json, sizeof(json),
      "{\"temperature_c\": %.2f, \"temperature_f\": %.2f, \"sun_level\": %d, \"heater\": %s, \"heap_free\" : %d }",
      tempC, tempF, sunLevel, heater_on ? "true" : "false", ESP.getFreeHeap()
    );
    request->send(200, "application/json", json);
  });  

  server.begin();

  Serial.println("HTTP server started");
  sunLevelAvg.clear();

  
}

void updateTemperature(){
  tempC = readTemperatureC();
  tempF = celsiusToFahrenheit(tempC);

  if (!isnan(tempC)) {
    Serial.print("Current Temperature: ");
    Serial.print(tempC);
    Serial.print("°C / ");
    Serial.print(tempF);
    Serial.println("°F");
  }  
}

void updateSolarHeater(){
  sunLevel = analogRead(SUN_PANEL_PIN);
  sunLevelAvg.addValue(sunLevel);
  float currentSunLevelAverage = sunLevelAvg.getAverage();
  if ( currentSunLevelAverage > SUN_ON_LEVEL) {
    heater_on = 1;
  }
  else{
    heater_on = 0;
  }
  digitalWrite(SOLAR_RELAY_PIN,heater_on);
  Serial.print("SunLevel: ");Serial.print(currentSunLevelAverage);
  Serial.print(" Heater: ");Serial.print(heater_on);
  Serial.print("  Free heap: ");  Serial.print(ESP.getFreeHeap());  
  Serial.printf("Min free heap: %u\n", esp_get_minimum_free_heap_size());
}

void loop() {

  unsigned long now = millis();
  if (now - lastTempUpdate >= TEMP_UPDATE_INTERVAL_MILLIS) {
    lastTempUpdate = now;
    updateTemperature();
  }
  if (now - lastSolarUpdate >= TEMP_UPDATE_INTERVAL_MILLIS) {
    lastSolarUpdate = now;
    updateSolarHeater();
  }

  if (ESP.getFreeHeap() < 10000) {
    Serial.println("Heap too low, restarting...");
    ESP.restart();
  }

  
  delay(1);


}
