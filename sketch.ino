#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include "occupancy_xgboost.h"

// Network / CLoud Configuration
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com"; 
String thingSpeakApiKey = "1V1XMVNREZ25YN2C"; 

// Hardware
#define DHTPIN 15      
#define DHTTYPE DHT22  
#define SERVOPIN 13    
#define LDRPIN 34      
#define LEDPIN 12      

DHT dht(DHTPIN, DHTTYPE);
Servo ventServo;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long lastMeasureTime = 0;
bool ledState = false; 

void setup() {
  Serial.begin(115200);
  dht.begin();
  ventServo.attach(SERVOPIN, 500, 2400); 
  pinMode(LDRPIN, INPUT);
  pinMode(LEDPIN, OUTPUT);
  
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.println("\nWiFi Connected!");
  
  mqttClient.setServer(mqtt_server, 1883); 
  mqttClient.setCallback(mqttCallback);    
}

void reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected.");
      mqttClient.subscribe("warehouse/servo/secure"); 
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 5 seconds.");
      delay(5000);
    }
  }
}

// Security and Control
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) { 
    message += (char)payload[i]; 
  }
  
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Payload: ");
  Serial.println(message);
  
  int angle = message.toInt();
  if (angle >= 0 && angle <= 180) {
    ventServo.write(angle);
    Serial.print("Manual override. Servo rotated to: ");
    Serial.println(angle);
  } else {
    Serial.println("Invalid command rejected."); 
  }
}

void loop() {
  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop(); 

  // Polling every 20 seconds 
  if (millis() - lastMeasureTime >= 20000) {
    lastMeasureTime = millis();
    
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    int ldr = analogRead(LDRPIN);
    
    if (isnan(temp) || isnan(hum)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    // Automation 1 - Closed Loop Ventilation
    if (temp > 30.0) {
      ventServo.write(90); 
    } else if (temp <= 28.0) {
      ventServo.write(0);  
    }

    // Automation 2 - Open-Loop Lighting
    if (ldr >= 2000) { 
      ledState = true;
      digitalWrite(LEDPIN, HIGH); 
    } else if (ldr <= 1600) { 
      ledState = false;
      digitalWrite(LEDPIN, LOW);  
    }

    // Edge AI
    double features[] = {static_cast<double>(ldr), temp};
    double probabilities[2];
    xgboost_predict(features, probabilities);
    const char* occupancy = probabilities[1] >= probabilities[0] ? "Occupied" : "Empty";
    
    Serial.print("AI Prediction: ");
    Serial.println(occupancy);

    // ThingSpeak Upload
    HTTPClient http;
    String url = "https://api.thingspeak.com/update?api_key=" + thingSpeakApiKey + "&field1=" + String(temp) + "&field2=" + String(hum);
    http.begin(url);
    int httpCode = http.GET(); 
    http.end(); 
    Serial.print("ThingSpeak Update HTTP Code: ");
    Serial.println(httpCode);

    // Node-RED Dashboard
    String livePayload = "{\"temp\":" + String(temp) + ",\"hum\":" + String(hum) + ",\"occupancy\":\"" + String(occupancy) + "\"}";
    mqttClient.publish("warehouse/sensors/live", livePayload.c_str()); 
  }
}