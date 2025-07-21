// Include necessary libraries
#include <DHTesp.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <sys/time.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <WiFiUdp.h>

WiFiClient espClient;
PubSubClient client(espClient);

// MQTT setup
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqttServer = "broker.emqx.io";  
const int mqttPort = 1883;


// OLED display parameters
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// WiFi connection parameters
const char* SSID = "Wokwi-GUEST";
const char* PASSWORD = "";
const int WIFI_CHANNEL = 6;

// Healthy condition thresholds for medicine storage
#define TEMP_HIGH 32    // Maximum safe temperature
#define TEMP_LOW 24     // Minimum safe temperature
#define HUMIDITY_HIGH 80  // Maximum safe humidity
#define HUMIDITY_LOW 65   // Minimum safe humidity

// Buzzer parameters 
const int NOTES[] = {262, 294, 330, 349, 392, 440, 494, 523}; 
const int NUM_NOTES = sizeof(NOTES) / sizeof(NOTES[0]);

// Pin definitions
#define DHT_PIN 12      // DHT22 sensor pin
#define LED1_PIN 15     // Red LED (for warnings)
#define LED2_PIN 2      // Green LED (for alarms)
#define BUZZER_PIN 18   // Buzzer pin
#define CANCEL 34       // Cancel button
#define OK 19           // OK button
#define UP 35           // Up button
#define DOWN 32         // Down button
#define LDR_PIN 33      // LDR pin (for light sensor)
#define SERVO_PIN 16    // Servo motor pin

// Time-related variables
const char* NTP_SERVER = "pool.ntp.org";
String UTC_OFFSET = "IST-5:30";  // Default timezone (India Standard Time)

// Available UTC offsets for timezone selection
const String UTC_OFFSETS[] = {
  "UTC-12:00","UTC-11:00","UTC-10:00","UTC-09:30","UTC-09:00",
  "UTC-08:00","UTC-07:00","UTC-06:00","UTC-05:30","UTC-04:30",
  "UTC-04:00","UTC-03:30","UTC-03:00","UTC-02:00","UTC-01:00",
  "UTC+00:00","UTC+01:00","UTC+02:00","UTC+03:00","UTC+03:30",
  "UTC+04:00","UTC+04:30","UTC+05:00","UTC+05:30","UTC+05:45",
  "UTC+06:00","UTC+06:30","UTC+07:00","UTC+08:00","UTC+08:45",
  "UTC+09:00","UTC+09:30","UTC+10:00","UTC+10:30","UTC+11:00",
  "UTC+12:00","UTC+12:45","UTC+13:00","UTC+14:00"
};
const int NUM_UTC_OFFSETS = sizeof(UTC_OFFSETS) / sizeof(UTC_OFFSETS[0]);

// Create objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHTesp dhtSensor;
WiFiUDP ntpUDP;
Servo servo;

// Time variables
int seconds;
int minutes;
int hours;

// Alarm system variables
bool alarm_enabled = false;
const int NUM_ALARMS = 2;
int alarm_hours[NUM_ALARMS] = {0, 0};
int alarm_minutes[NUM_ALARMS] = {0, 0};
bool alarm_triggered[NUM_ALARMS] = {false, false};

// Menu system variables
int current_mode = 0;  // Current selected menu option
const String MODES[] = {
  "1 - Set Time Zone",
  "2 - Set Alarm 1",
  "3 - Set Alarm 2",
  "4 - View Alarms",
  "5 - Delete Alarms"
};
const int MAX_MODE = sizeof(MODES) / sizeof(MODES[0]);

// Node-Red Dashoboard communication variables
char tempArr[8];
char humArr[8];
char ldrArr[8];

const char* topic_ts = "medibox/nodeRed/ts";
const char* topic_tu = "medibox/nodeRed/tu";
const char* topic_theta = "medibox/nodeRed/theta";
const char* topic_y = "medibox/nodeRed/y";
const char* topic_itemp = "medibox/nodeRed/itemp";

int ts = 5; // Default sampling interval
int tu = 120; // Default time constant
int theta_offset = 30; // Servo angle offset
float control_factor = 0.75; // Control factor for LDR
int Tmed = 30; // Default Tmed
float y = 0.75; // Default gamma value

int ldr_count = 0; // LDR count for averaging
float total_intensity = 0; // Total intensity for averaging
float average_intensity = 0; // Average intensity

// Samples sending parameters
unsigned long lastSampleTime = 0; // Last sample time
unsigned long lastSentTime = 0; // Last sent time
unsigned long lastServoTime = 0; // Last servo time

// Function prototypes
void printLine(String text, String clearDisplay = "n", int textSize = 1, int column = 0, int row = 0);
void updateTime();
void printCurrentTime();
void triggerAlarm(int alarmIndex);
void snoozeAlarm(int alarmIndex);
void showWarning(float value, float thresholdLow, float thresholdHigh, int row, String clearStatus, String message);
void checkEnvironmentalConditions();
void updateTimeAndCheckAlarms();
int waitForButtonPress();
void setTimeUnit(int &unit, int maxValue, String message);
void configureAlarm(int alarmIndex);
void displayActiveAlarms();
void removeAlarm();
void configureTimezone();
void executeMode(int mode);
void enterMenu();
void callback(char* topic, byte* payload, unsigned int length);
float getLDR();
int calculateServoAngle(float I, float ts, float tu, float T, float theta_offset, float gamma, float Tmed);

void setup() {
  // Initialize pins
  pinMode(DHT_PIN, INPUT);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(CANCEL, INPUT);
  pinMode(OK, INPUT);
  pinMode(UP, INPUT);
  pinMode(DOWN, INPUT);
  pinMode(LDR_PIN, INPUT);

  servo.attach(SERVO_PIN);
  servo.write(0);

  Serial.begin(115200);

  // Initialize DHT22 sensor
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);

  // Initialize OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while(true); // Infinite loop if display fails
  }

  // Show initial display buffer (splash screen)
  display.display();
  delay(1000);
  display.clearDisplay();

  // Connect to WiFi
  WiFi.begin(SSID, PASSWORD, WIFI_CHANNEL);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    printLine("Connecting to WiFi", "y", 1, 0, 5);
    display.clearDisplay();
  }

  printLine("Connected to WiFi", "n", 1, 0, 5);
  delay(2000);
  display.clearDisplay();

  // Configure and sync time with NTP server
  printLine("Updating Time...", "n", 1, 0, 5);
  display.clearDisplay();
  configTzTime("IST-5:30", "pool.ntp.org");

  // Wait until time is properly synced
  while (time(nullptr) < 1510644967) {
    delay(500);
  }

  display.clearDisplay();
  printLine("Time config updated", "n", 1, 0, 5);
  delay(1000);
  display.clearDisplay();
  delay(2000);

  // Show welcome message
  printLine("Welcome to Medibox!", "y", 1, 10, 30);
  delay(2000);

  client.setServer(mqttServer, mqttPort);   //MQTT broker's address (server) and port number
  
  client.setCallback(callback);   //automatically called whenever a message is received on a subscribed topic

  client.subscribe(topic_ts);
  client.subscribe(topic_tu);
  client.subscribe(topic_theta);
  client.subscribe(topic_y);
  client.subscribe(topic_itemp);
}


void loop(){
  client.loop();
  if (!client.connected()) {
    while (!client.connect("WokwiClient")) {
      delay(500);
    }
    Serial.println("Connected to MQTT broker");
  }
  client.subscribe(topic_ts);
  client.subscribe(topic_tu);
  client.subscribe(topic_theta);
  client.subscribe(topic_y);
  client.subscribe(topic_itemp);
  client.loop();


  // Control servo motor based on LDR intensity
  if (millis() - lastSampleTime >= ts * 1000) {
    lastSampleTime = millis();
    float intensity = 1 - getLDR();
    total_intensity = intensity + total_intensity;
    ldr_count++;
  }

  if (millis() - lastSentTime >= 1000 * tu) {
      lastSentTime = millis();
      if (ldr_count > 0) {
          average_intensity = total_intensity / ldr_count;
          sprintf(ldrArr, "%.2f", average_intensity);
          client.publish("medibox/ldr", ldrArr);
          Serial.print("Average LDR Intensity: ");
          Serial.println(ldrArr);
          total_intensity = 0;
          ldr_count = 0;
      } else {
          Serial.println("Warning: ldr_count is 0");
      }
  }

  if(millis() - lastServoTime >= 500) {
    float T = dhtSensor.getTemperature();
    float I = average_intensity;
    int servoAngle = calculateServoAngle(I, ts, tu, T, theta_offset, y, Tmed);
    servo.write(servoAngle);
    lastServoTime = millis();
    Serial.print("Servo Angle: ");
    Serial.println(servoAngle);
  }

  updateTimeAndCheckAlarms();
  
  // Enter menu if OK button is pressed
  if (digitalRead(OK) == LOW) {
    delay(200);
    enterMenu();
  }
} 

// Display text on OLED screen
void printLine(String text, String clearDisplay, int textSize, int column, int row) {
  if (clearDisplay == "y") {
    display.clearDisplay();
  }
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(column, row);
  display.println(text);
  display.display();
}

// Update time variables from system time
void updateTime() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {
    printLine("Failed to obtain time", "y");
    return;
  }

  seconds = timeInfo.tm_sec;
  minutes = timeInfo.tm_min;
  hours = timeInfo.tm_hour;
}

// Display current time on screen
void printCurrentTime() {
  updateTime();
  // Format time with leading zeros for single-digit values
  String timeStr = String(hours < 10 ? "0" : "") + String(hours) + ":" + 
                   String(minutes < 10 ? "0" : "") + String(minutes) + ":" + 
                   String(seconds < 10 ? "0" : "") + String(seconds);
  printLine("Time: " + timeStr, "y", 1, 15, 30);
}


// Trigger alarm sequence
void triggerAlarm(int alarmIndex) {
  printLine("MEDICINE TIME", "y", 1, 0, 10);
  printLine("OK-Snooze/CANCEL-Stop", "n", 1, 0, 50);
  
  bool alarmActive = true;
  
  while (alarmActive && digitalRead(CANCEL) == HIGH) {
    for(int i = 0; i < NUM_NOTES; i++) {
      // Check for snooze button
      if (digitalRead(OK) == LOW) {
        snoozeAlarm(alarmIndex);
        alarmActive = false; // Exit the loop after snoozing
        break;
      }
      
      // Check for cancel button
      if (digitalRead(CANCEL) == LOW) {
        alarmActive = false;
        break;
      }
      
      // Play note and flash LED
      tone(BUZZER_PIN, NOTES[i]);
      digitalWrite(LED2_PIN, HIGH);
      delay(500);
      noTone(BUZZER_PIN);
      digitalWrite(LED2_PIN, LOW);
      delay(200);
    }
  }
  
  // Clean up after alarm
  digitalWrite(LED2_PIN, LOW);
  noTone(BUZZER_PIN);
  alarm_triggered[alarmIndex] = !alarmActive;
}

void snoozeAlarm(int alarmIndex) {
  delay(200);
  alarm_minutes[alarmIndex] += 5; // Add 5 minutes to the alarm time
  if (alarm_minutes[alarmIndex] >= 60) {
    alarm_minutes[alarmIndex] -= 60; 
    alarm_hours[alarmIndex] = (alarm_hours[alarmIndex] + 1) % 24; 
  }
  
  alarm_triggered[alarmIndex] = false;  // Reset alarm trigger flag
  alarm_enabled = true;  // Ensure alarm system is active

  printLine("Alarm snoozed for 5 mins", "y", 1, 10, 30);
  delay(1000);
}


// Display warning message if value is outside thresholds
void showWarning(float value, float thresholdLow, float thresholdHigh, 
                int row, String clearStatus, String message) {
  if (value < thresholdLow) {
    message += " LOW";
    printLine(message, clearStatus, 1, 10, row);
  } else if (value > thresholdHigh) {
    message += " HIGH";
    printLine(message, clearStatus, 1, 10, row);
  }
}

// Check temperature and humidity conditions
void checkEnvironmentalConditions() {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  int counter = 0;

  // Send values to Node-Red dashboard
  String(data.temperature, 1).toCharArray(tempArr, 6);
  String(data.humidity, 1).toCharArray(humArr, 6);
  // mqttClient.publish("medibox/temperature", tempArr);
  // mqttClient.publish("medibox/humidity", humArr);

  // Continue checking while conditions are outside safe ranges
  while(data.temperature < TEMP_LOW || data.temperature > TEMP_HIGH || 
        data.humidity < HUMIDITY_LOW || data.humidity > HUMIDITY_HIGH) {
    // Show warnings for temperature and humidity
    showWarning(data.temperature, TEMP_LOW, TEMP_HIGH, 10, "y", "TEMP "); 
    showWarning(data.humidity, HUMIDITY_LOW, HUMIDITY_HIGH, 30, "y", "HUMID ");
  
    counter++;
  
    // Blink LED and sound buzzer every other iteration
    if (counter % 2 == 0) {
      digitalWrite(LED1_PIN, HIGH);
      tone(BUZZER_PIN, NOTES[0]);
      delay(500);
    } else {
      digitalWrite(LED1_PIN, LOW);
      noTone(BUZZER_PIN);
      delay(500);
    }

    // Get updated readings
    data = dhtSensor.getTempAndHumidity();
    String(data.temperature, 1).toCharArray(tempArr, 6);
    String(data.humidity, 1).toCharArray(humArr, 6);
    if (!client.connected()) {
      Serial.println("Reconnecting to MQTT...");
    }
    client.loop();
    client.publish("medibox/temperature", tempArr);
    client.publish("medibox/humidity", humArr);
  }

  // Turn off warning indicators when conditions return to normal
  digitalWrite(LED1_PIN, LOW);
  noTone(BUZZER_PIN);
}

// Main time and alarm checking function
void updateTimeAndCheckAlarms() {
  printCurrentTime();
  if (alarm_enabled) {
    for (int i = 0; i < NUM_ALARMS; i++) {
      if (alarm_hours[i] == hours && alarm_minutes[i] == minutes) {
        if (!alarm_triggered[i]) {
          triggerAlarm(i);
        }
      }
    }
  }
  // Check temperature and humidity
  checkEnvironmentalConditions();
}

// Wait for and return which button was pressed
int waitForButtonPress() {
  const int BUTTONS[] = {UP, DOWN, OK, CANCEL};
  const int NUM_BUTTONS = sizeof(BUTTONS)/sizeof(BUTTONS[0]);

  while(true) {
    for(int i = 0; i < NUM_BUTTONS; i++) {
      if(digitalRead(BUTTONS[i]) == LOW) {
        delay(200); // Debounce delay
        return BUTTONS[i];
      }
    }
    delay(10); 
  }
}

// Set a time unit (hours/minutes) with up/down buttons
void setTimeUnit(int &unit, int maxValue, String message) {
  int tempUnit = unit;

  while (true) {
    printLine(message + String(tempUnit), "y");

    int pressed = waitForButtonPress();

    switch(pressed) {
      case UP:
        tempUnit = (tempUnit + 1) % maxValue;
        break;
      case DOWN:
        tempUnit = (tempUnit - 1 + maxValue) % maxValue;
        break;
      case OK:
        unit = tempUnit;
        return;
      case CANCEL:
        return;
    }
  }
}

// Configure an alarm (set hours and minutes)
void configureAlarm(int alarmIndex) {
  setTimeUnit(alarm_hours[alarmIndex], 24, "Enter hour: ");
  setTimeUnit(alarm_minutes[alarmIndex], 60, "Enter minute: ");

  display.clearDisplay();
  printLine("OK to set alarm", "n", 1, 0, 5);
  printLine("CANCEL to exit", "n", 1, 0, 25);

  int button = waitForButtonPress();
  if (button == CANCEL) {
    return;
  } else if (button == OK) {
    alarm_enabled = true;
    printLine("Alarm set", "y");
  }
  delay(1000);
}

// Display all active alarms
void displayActiveAlarms() {
  display.clearDisplay();
  bool hasActiveAlarms = false;
  int yPos = 10; // Starting Y position for first alarm
  
  // Check both alarms
  for (int i = 0; i < NUM_ALARMS; i++) {
    if (alarm_hours[i] != 0 || alarm_minutes[i] != 0) {
      hasActiveAlarms = true;
      // Format with leading zeros for single-digit values
      String alarmText = "Alarm " + String(i+1) + ": " + 
                        (alarm_hours[i] < 10 ? "0" : "") + String(alarm_hours[i]) + ":" + 
                        (alarm_minutes[i] < 10 ? "0" : "") + String(alarm_minutes[i]);
      printLine(alarmText, "n", 1, 20, yPos);
      yPos += 20; // Move down for next alarm
    }
  }

  if (!hasActiveAlarms) {
    printLine("No active alarms", "y", 1, 10, 20);
  }
  
  printLine("Press CANCEL to exit", "n", 1, 0, 50);
  display.display();

  // Wait for cancel button press
  while (digitalRead(CANCEL) == HIGH) {
    delay(50);
  }
  delay(200); // Debounce delay
}

// Delete an alarm
void removeAlarm() {
  int selectedAlarm = 0;
  bool selecting = true;
  
  while(selecting) {
    printLine("Select alarm to delete:", "y");
    printLine(String(selectedAlarm + 1) + ": " + 
              String(alarm_hours[selectedAlarm]) + ":" + 
              String(alarm_minutes[selectedAlarm]), "n", 1, 0, 30);
              
    int pressed = waitForButtonPress();
    
    switch(pressed) {
      case UP: 
        selectedAlarm = (selectedAlarm + 1) % NUM_ALARMS;
        break;
      case DOWN:
        selectedAlarm = (selectedAlarm - 1 + NUM_ALARMS) % NUM_ALARMS;
        break;
      case OK:
        // Clear the selected alarm
        alarm_hours[selectedAlarm] = 0;
        alarm_minutes[selectedAlarm] = 0;
        alarm_triggered[selectedAlarm] = false;
        selecting = false;
        break;
      case CANCEL:
        return;
    }
  }
  
  printLine("Alarm deleted", "y");
  delay(1000);
}

// Configure timezone
void configureTimezone() {
  int index = 8; // Default UTC offset index (IST)
  int indexMax = NUM_UTC_OFFSETS;

  while (true) {
    printLine("Enter UTC offset  ", "y");
    printLine(UTC_OFFSETS[index], "n", 1, 0, 15);

    int pressed = waitForButtonPress();

    if (pressed == UP) {
      index = (index + 1) % indexMax;
    } else if (pressed == DOWN) {
      index = (index - 1 + indexMax) % indexMax;
    } else if (pressed == OK) {
      UTC_OFFSET = UTC_OFFSETS[index];
      configTzTime(UTC_OFFSET.c_str(), NTP_SERVER);
      printLine("Time zone is set", "y", 1, 10, 30);
      delay(1000);
      break;
    } else if (pressed == CANCEL) {
      break;
    }
  }
}

// Execute selected menu mode
void executeMode(int mode) {
  switch(mode) {
    case 0:  // Set timezone
      configureTimezone();
      break;
    case 1:  // Set alarm 1
    case 2:  // Set alarm 2
      configureAlarm(mode - 1);
      break;
    case 3:  // View alarms
      displayActiveAlarms();
      break;
    case 4:  // Delete alarms
      removeAlarm();
      break;
  }
}

// Enter and navigate menu system
void enterMenu() {
  while(digitalRead(CANCEL) == HIGH) {
    printLine(MODES[current_mode], "y", 1, 0, 30); 

    int pressed = waitForButtonPress();
    delay(200);

    switch(pressed) {
      case UP:
        current_mode = (current_mode + 1) % MAX_MODE;
        break;
      case DOWN:
        current_mode = (current_mode - 1 + MAX_MODE) % MAX_MODE;
        break;
      case OK:
        executeMode(current_mode);
        break;
      case CANCEL:
        return;
    }
  }
} 


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  String messageTemp;
  for (int i = 0; i < length; i++) {
    messageTemp += (char)payload[i];
  }
  Serial.println(messageTemp);

  if (strcmp(topic, topic_ts) == 0) {
    if(messageTemp.toInt() > 0) {
      ts = messageTemp.toInt();
      Serial.print("Updated ts: ");
      Serial.println(ts);
    }
  } else if (strcmp(topic, topic_tu) == 0) {
      tu = messageTemp.toInt();
      Serial.print("Updated tu: ");
      Serial.println(tu);
  } else if (strcmp(topic, topic_theta) == 0) {
      theta_offset = messageTemp.toInt();
      Serial.print("Updated theta: ");
      Serial.println(theta_offset);
  } else if (strcmp(topic, topic_y) == 0) {
      y = messageTemp.toFloat();
      Serial.print("Updated y: ");
      Serial.println(y);
  } else if (strcmp(topic, topic_itemp) == 0) {
      Tmed = messageTemp.toInt();
      Serial.print("Updated itemp: ");
      Serial.println(Tmed);
  }
}

float getLDR() {
  int LDRvalue = analogRead(LDR_PIN);
  // Map the LDR value to a range of 0 to 1
  return LDRvalue / 4095.0;
}

int calculateServoAngle(float I, float ts, float tu, float T, float theta_offset, float gamma, float Tmed) {
  if (ts <= 0 || tu <= 0 || Tmed == 0) {
    return theta_offset;  // fallback to safe value
  }

  float lnRatio = log(ts / tu);
  float theta = theta_offset + (180.0 - theta_offset) * I * gamma * lnRatio * (T / Tmed);

  theta = constrain(theta, 0, 180);

  return (int)theta;
}