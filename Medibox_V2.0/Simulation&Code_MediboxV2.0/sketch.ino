#include <PubSubClient.h>
#include <WiFi.h>
#include "DHTesp.h"
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Constants for OLED display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// Pin definitions
#define BUZZER 5
#define LED_1 15
#define BUTTON_CANCEL 34
#define BUTTON_OK 32
#define BUTTON_UP 33
#define BUTTON_DOWN 35
#define DHTPIN 12
#define LDR_PIN 36
#define SERVO_PIN 2

// MQTT broker details (HiveMQ)
#define MQTT_SERVER "broker.hivemq.com"
#define MQTT_PORT 1883
#define MQTT_CLIENT_ID "ESP32-Medibox"

// Global variables
DHTesp dhtSensor;
float temperature = 0;
char tempStr[10];
int ldrValue = 0;
float normalizedLightIntensity = 0;
char lightIntensityStr[10];

// Servo control variables
Servo servoMotor;
int minimumAngle = 30;  // Default θoffset
float controllingFactor = 0.75;  // Default γ
float idealTemp = 30.0;  // Default Tmed
float servoAngle = 0;

// LDR sampling and sending intervals
unsigned long samplingInterval = 5000;  // Default 5 seconds (in ms)
unsigned long sendingInterval = 10000;  // 10 seconds for testing (revert to 120000)
unsigned long lastSamplingTime = 0;
unsigned long lastSendingTime = 0;

// Variables for LDR readings average calculation
#define MAX_READINGS 24  // 2 min / 5 sec = 24
float ldrReadings[MAX_READINGS];
int readingIndex = 0;
int totalReadings = 0;
float sumReadings = 0;

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// OLED display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_1, OUTPUT);
  pinMode(BUTTON_CANCEL, INPUT);
  pinMode(BUTTON_OK, INPUT);
  pinMode(BUTTON_UP, INPUT);
  pinMode(BUTTON_DOWN, INPUT);
  pinMode(LDR_PIN, INPUT);
  
  // Initialize WiFi
  setupWiFi();
  
  // Initialize MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(callback);
  
  // Initialize DHT sensor
  dhtSensor.setup(DHTPIN, DHTesp::DHT11);
  
  // Initialize servo motor
  servoMotor.attach(SERVO_PIN, 500, 2400);
  servoMotor.write(minimumAngle);
  
  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Medibox");
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println("Initializing...");
  display.display();
  
  connectToMQTTBroker();
  
  for (int i = 0; i < MAX_READINGS; i++) {
    ldrReadings[i] = 0;
  }
  
  lastSendingTime = millis();  // Initialize to start sending soon
  
  delay(2000);
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("Ready!");
  display.display();
  delay(1000);
}

void loop() {
  if (!mqttClient.connected()) {
    connectToMQTTBroker();
  }
  mqttClient.loop();
  
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastSamplingTime >= samplingInterval) {
    lastSamplingTime = currentMillis;
    
    readAndStoreLDRValue();
    readTemperature();
    updateServoPosition();
    updateDisplay();
    
    digitalWrite(LED_1, HIGH);
    delay(100);
    digitalWrite(LED_1, LOW);
  }
  
  if (currentMillis - lastSendingTime >= sendingInterval) {
    lastSendingTime = currentMillis;
    
    float avgLightIntensity = calculateAverageLightIntensity();
    dtostrf(avgLightIntensity, 4, 2, lightIntensityStr);
    
    bool allPublished = true;
    allPublished &= mqttClient.publish("medibox/temperature220089B", tempStr);
    allPublished &= mqttClient.publish("medibox/lightIntensity220089B", lightIntensityStr);
    allPublished &= mqttClient.publish("medibox/servoAngle", String(servoAngle).c_str());
    
    if (allPublished) {
      Serial.println("Data sent to MQTT:");
      Serial.println("Temp: " + String(tempStr));
      Serial.println("Light: " + String(lightIntensityStr));
      Serial.println("Angle: " + String(servoAngle));
      tone(BUZZER, 1000, 200);
    } else {
      Serial.println("Failed to send MQTT data");
    }
  }
  
  checkButtons();
}

void setupWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin("Wokwi-GUEST", "");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected");
  Serial.println("IP: " + WiFi.localIP().toString());
}

void connectToMQTTBroker() {
  while (!mqttClient.connected()) {
    Serial.println("Connecting to HiveMQ...");
    
    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      Serial.println("Connected to HiveMQ");
      
      mqttClient.subscribe("medibox/samplingInterval");
      mqttClient.subscribe("medibox/sendingInterval");
      mqttClient.subscribe("medibox/minimumAngle");
      mqttClient.subscribe("medibox/controllingFactor");
      mqttClient.subscribe("medibox/idealTemp");
    } else {
      Serial.println("Failed, rc=" + String(mqttClient.state()));
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  
  Serial.println("Received [" + String(topic) + "]: " + String(message));
  
  if (strcmp(topic, "medibox/samplingInterval") == 0) {
    samplingInterval = atol(message) * 1000;
  } else if (strcmp(topic, "medibox/sendingInterval") == 0) {
    sendingInterval = atol(message) * 1000;
  } else if (strcmp(topic, "medibox/minimumAngle") == 0) {
    minimumAngle = atoi(message);
  } else if (strcmp(topic, "medibox/controllingFactor") == 0) {
    controllingFactor = atof(message);
  } else if (strcmp(topic, "medibox/idealTemp") == 0) {
    idealTemp = atof(message);
  }
}

void readAndStoreLDRValue() {
  ldrValue = analogRead(LDR_PIN);
  normalizedLightIntensity = normalizeValue(ldrValue);
  
  if (totalReadings < MAX_READINGS) {
    totalReadings++;
  }
  
  sumReadings -= ldrReadings[readingIndex];
  ldrReadings[readingIndex] = normalizedLightIntensity;
  sumReadings += ldrReadings[readingIndex];
  
  readingIndex = (readingIndex + 1) % MAX_READINGS;
  
  Serial.println("LDR: " + String(ldrValue) + ", Norm: " + String(normalizedLightIntensity));
}

float normalizeValue(int value) {
  if (value < 0 || value > 4095) return 0.0;
  return 1.0 - (value / 4095.0);  // 1 = brightest, 0 = darkest
}

float calculateAverageLightIntensity() {
  return totalReadings == 0 ? 0.0 : sumReadings / totalReadings;
}

void readTemperature() {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  temperature = data.temperature;
  dtostrf(temperature, 4, 2, tempStr);
  
  Serial.println("Temp: " + String(temperature) + "°C");
}

void updateServoPosition() {
  float tsInSeconds = samplingInterval / 1000.0;
  float tuInSeconds = sendingInterval / 1000.0;
  
  float logTerm = (tuInSeconds > 0 && tsInSeconds > 0 && tsInSeconds / tuInSeconds > 0) ? log(tsInSeconds / tuInSeconds) : 1.0;
  float tempRatio = temperature / idealTemp;
  
  servoAngle = minimumAngle + (180 - minimumAngle) * normalizedLightIntensity * 
               controllingFactor * logTerm * tempRatio;
  
  servoAngle = constrain(servoAngle, 0, 180);
  servoMotor.write(servoAngle);
  
  Serial.println("Servo angle: " + String(servoAngle));
}

void updateDisplay() {
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Temp: " + String(temperature) + " C");
  
  display.setCursor(0, 10);
  display.print("Light: " + String(normalizedLightIntensity));
  
  display.setCursor(0, 20);
  display.print("Angle: " + String(servoAngle));
  
  display.setCursor(0, 30);
  display.print("Samp: " + String(samplingInterval / 1000) + " s");
  
  display.setCursor(0, 40);
  display.print("Send: " + String(sendingInterval / 1000) + " s");
  
  display.display();
}

void checkButtons() {
  if (digitalRead(BUTTON_OK) == LOW) {
    delay(200);
    Serial.println("OK button pressed");
    tone(BUZZER, 800, 100);
  }
  if (digitalRead(BUTTON_CANCEL) == LOW) {
    delay(200);
    Serial.println("Cancel button pressed");
    tone(BUZZER, 600, 100);
  }
  if (digitalRead(BUTTON_UP) == LOW) {
    delay(200);
    Serial.println("Up button pressed");
    tone(BUZZER, 1000, 100);
  }
  if (digitalRead(BUTTON_DOWN) == LOW) {
    delay(200);
    Serial.println("Down button pressed");
    tone(BUZZER, 400, 100);
  }
}