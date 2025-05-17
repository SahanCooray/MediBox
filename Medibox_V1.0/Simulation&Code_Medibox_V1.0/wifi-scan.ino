#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <time.h>

// Display constants
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// Pin definitions
#define BUZZER 5
#define LED 15
#define HumiLED 2
#define TempLED 4
#define PB_CANCEL 34
#define PB_OK 32
#define PB_UP 33
#define PB_DOWN 35
#define DHTPIN 12

// Network constants
#define NTP_SERVER "pool.ntp.org"
#define UTC_OFFSET_DST 0

// Initialize components
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHTesp dhtsensor;

// Time variables
int days = 0;
int hours = 0;
int minutes = 0;
int seconds = 0;
int months = 0;
int years = 0;
unsigned long timeNow = 0;
unsigned long timeLast = 0;

// Alarm system
const int MAX_ALARMS = 3;
bool alarm_enabled = true;
int alarm_hours[MAX_ALARMS] = {8, 12, 20};    // Default alarms at 8am, 12pm, 8pm
int alarm_minutes[MAX_ALARMS] = {0, 0, 0};
bool alarm_triggered[MAX_ALARMS] = {false, false, false};

// Buzzer notes
const int n_notes = 3;
const int notes[n_notes] = {262, 294, 330};  // C, D, E notes

// Menu system
enum MenuMode {
  SET_ALARM_1,
  SET_ALARM_2,
  SET_ALARM_3,
  TOGGLE_ALARMS,
  SET_TIMEZONE,
  VIEW_ALARMS,
  DELETE_ALARM
};

const int NUM_MODES = 7;
String modes[NUM_MODES] = {
  "1- Set Alarm 1",
  "2- Set Alarm 2",
  "3- Set Alarm 3",
  "4- Toggle Alarms",
  "5- Set Timezone",
  "6- View Alarms",
  "7- Delete Alarm"
};
int current_mode = 0;

void setup() {
  // Initialize hardware
  pinMode(BUZZER, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(HumiLED, OUTPUT);
  pinMode(TempLED, OUTPUT);
  pinMode(PB_CANCEL, INPUT);
  pinMode(PB_OK, INPUT);
  pinMode(PB_UP, INPUT);
  pinMode(PB_DOWN, INPUT);

  dhtsensor.setup(DHTPIN, DHTesp::DHT22);

  Serial.begin(115200);
  
  // Initialize display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    while (true);
  }

  // Connect to WiFi
// Connect to WiFi
  display.clearDisplay();
  print_line("Connecting to WiFi", 0, 0, 1);

  WiFi.begin("Wokwi-GUEST", "", 6);

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    display.clearDisplay();
    
    // Create loading dots
    String dots = "";
    int dotCount = (millis()/500) % 4;  // Cycle through 0-3 dots
    for (int i = 0; i < dotCount; i++) {
      dots += ".";
    }
    
    print_line("Connecting to WiFi" + dots, 0, 0, 1);
  }
  // Configure time
  configTime(19800, UTC_OFFSET_DST, NTP_SERVER); // Default to UTC+5:30
  print_line("Syncing time...", 0, 20, 1);
  
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo, 5000)) {
    Serial.println("Waiting for NTP time...");
    delay(500);
  }

  // Startup sequence
  display.clearDisplay();
  print_line("MediBox Ready!", 0, 20, 2);
  startup_sound();
  delay(1000);
}

void loop() {
  update_time();
  print_time_now();
  
  check_alarms();
  check_environment();
  
  if (digitalRead(PB_OK) == LOW) {
    delay(200); // Debounce
    enter_menu();
  }
  
  delay(100); // Main loop delay
}

// Helper Functions

void print_line(String text, int column, int row, int text_size) {
  display.setTextSize(text_size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(column, row);
  display.println(text);
  display.display();
}

void startup_sound() {
  for (int i = 0; i < n_notes; i++) {
    tone(BUZZER, notes[i], 200);
    delay(250);
  }
  noTone(BUZZER);
}

void update_time() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  char buffer[20];
  
  // Get hours
  strftime(buffer, sizeof(buffer), "%H", &timeinfo);
  hours = atoi(buffer);
  
  // Get minutes
  strftime(buffer, sizeof(buffer), "%M", &timeinfo);
  minutes = atoi(buffer);
  
  // Get seconds
  strftime(buffer, sizeof(buffer), "%S", &timeinfo);
  seconds = atoi(buffer);
  
  // Get date
  strftime(buffer, sizeof(buffer), "%d", &timeinfo);
  days = atoi(buffer);
  
  strftime(buffer, sizeof(buffer), "%m", &timeinfo);
  months = atoi(buffer);
  
  strftime(buffer, sizeof(buffer), "%Y", &timeinfo);
  years = atoi(buffer);
}

void print_time_now() {
  display.clearDisplay();
  
  // Format time as HH:MM:SS
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", hours, minutes, seconds);
  print_line(timeStr, 0, 0, 2);
  
  // Format date as DD/MM/YYYY
  char dateStr[11];
  sprintf(dateStr, "%02d/%02d/%04d", days, months, years);
  print_line(dateStr, 0, 25, 1);
}

// Alarm Functions

void check_alarms() {
  if (!alarm_enabled) return;

  for (int i = 0; i < MAX_ALARMS; i++) {
    if (!alarm_triggered[i] && 
        alarm_hours[i] == hours && 
        alarm_minutes[i] == minutes) {
      trigger_alarm(i);
    }
  }
}

void trigger_alarm(int alarm_idx) {
  unsigned long alarmStart = millis();
  bool alarmActive = true;
  bool snoozed = false;

  while (alarmActive && (millis() - alarmStart < 30000)) { // 30s timeout
    // Visual indicator
    digitalWrite(LED, !digitalRead(LED));
    
    // Audible alarm
    for (int i = 0; i < n_notes; i++) {
      tone(BUZZER, notes[i], 200);
      delay(250);
      noTone(BUZZER);
      
      // Check for button presses
      if (digitalRead(PB_OK) == LOW) {
        // Snooze for 5 minutes
        snooze_alarm(alarm_idx);
        snoozed = true;
        alarmActive = false;
        break;
      }
      
      if (digitalRead(PB_CANCEL) == LOW) {
        // Cancel alarm
        alarm_triggered[alarm_idx] = true;
        alarmActive = false;
        break;
      }
    }
  }
  
  digitalWrite(LED, LOW);
  if (!snoozed) {
    alarm_triggered[alarm_idx] = true;
  }
}

void snooze_alarm(int alarm_idx) {
  alarm_minutes[alarm_idx] += 5;
  if (alarm_minutes[alarm_idx] >= 60) {
    alarm_minutes[alarm_idx] -= 60;
    alarm_hours[alarm_idx] = (alarm_hours[alarm_idx] + 1) % 24;
  }
  alarm_triggered[alarm_idx] = false;
  
  display.clearDisplay();
  print_line("Snoozed for 5 min", 0, 20, 1);
  delay(2000);
}

// Environment Monitoring

void check_environment() {
  TempAndHumidity data = dhtsensor.getTempAndHumidity();
  
  // Check temperature
  bool tempWarning = (data.temperature < 24) || (data.temperature > 32);
  digitalWrite(TempLED, tempWarning);
  
  // Check humidity
  bool humWarning = (data.humidity < 65) || (data.humidity > 80);
  digitalWrite(HumiLED, humWarning);
  
  // Display warnings if needed
  if (tempWarning || humWarning) {
    String warning = "";
    if (tempWarning) warning += "TEMP ";
    if (humWarning) warning += "HUM ";
    warning += "WARNING!";
    
    display.setCursor(0, 40);
    display.setTextSize(1);
    display.println(warning);
    display.display();
  }
}

// Menu System

void enter_menu() {
  int selected_item = 0;
  int first_visible_item = 0; // Track which item is at the top
  
  while (digitalRead(PB_CANCEL) == HIGH) {
    display.clearDisplay();
    print_line("MediBox Menu", 0, 0, 1);
    
    // Display up to 3 menu items at a time
    for (int i = 0; i < min(3, NUM_MODES - first_visible_item); i++) {
      int item_idx = first_visible_item + i;
      display.setCursor(5, 15 + i * 10);
      if (item_idx == selected_item) {
        display.print("> ");
      } else {
        display.print("  ");
      }
      display.print(modes[item_idx]);
    }
    
    // Show scroll indicators if there are more items
    if (first_visible_item > 0) {
      display.setCursor(120, 15);
      display.print("^");
    }
    if (first_visible_item + 3 < NUM_MODES) {
      display.setCursor(120, 35);
      display.print("v");
    }
    
    display.display();
    
    // Handle navigation
    if (digitalRead(PB_UP) == LOW) {
      selected_item = max(0, selected_item - 1);
      // Adjust first visible item if needed
      if (selected_item < first_visible_item) {
        first_visible_item = selected_item;
      }
      delay(200);
    }
    else if (digitalRead(PB_DOWN) == LOW) {
      selected_item = min(NUM_MODES - 1, selected_item + 1);
      // Adjust first visible item if needed
      if (selected_item >= first_visible_item + 3) {
        first_visible_item = selected_item - 2;
      }
      delay(200);
    }
    else if (digitalRead(PB_OK) == LOW) {
      execute_menu_option(selected_item);
      delay(200);
      return;
    }
  }
}
void execute_menu_option(int option) {
  switch (option) {
    case SET_ALARM_1:
    case SET_ALARM_2:
    case SET_ALARM_3:
      set_alarm(option); // 0, 1, or 2 for alarm index
      break;
      
    case TOGGLE_ALARMS:
      alarm_enabled = !alarm_enabled;
      display.clearDisplay();
      print_line(alarm_enabled ? "Alarms Enabled" : "Alarms Disabled", 0, 20, 2);
      delay(1500);
      break;
      
    case SET_TIMEZONE:
      set_timezone();
      break;
      
    case VIEW_ALARMS:
      view_alarms();
      break;
      
    case DELETE_ALARM:
      delete_alarm();
      break;
  }
}

void set_alarm(int alarm_idx) {
  bool setting_hour = true;
  int temp_hour = alarm_hours[alarm_idx];
  int temp_minute = alarm_minutes[alarm_idx];
  
  while (true) {
    display.clearDisplay();
    print_line(String("Set Alarm ") + (alarm_idx + 1), 0, 0, 1);
    
    if (setting_hour) {
      print_line("Hour: " + String(temp_hour), 0, 20, 2);
      print_line("Press OK for minutes", 0, 40, 1);
    } else {
      print_line("Minute: " + String(temp_minute), 0, 20, 2);
      print_line("Press OK to confirm", 0, 40, 1);
    }
    
    int pressed = wait_for_button();
    
    if (pressed == PB_UP) {
      if (setting_hour) temp_hour = (temp_hour + 1) % 24;
      else temp_minute = (temp_minute + 1) % 60;
    }
    else if (pressed == PB_DOWN) {
      if (setting_hour) temp_hour = (temp_hour - 1 + 24) % 24;
      else temp_minute = (temp_minute - 1 + 60) % 60;
    }
    else if (pressed == PB_OK) {
      if (setting_hour) {
        setting_hour = false;
      } else {
        // Save the alarm
        alarm_hours[alarm_idx] = temp_hour;
        alarm_minutes[alarm_idx] = temp_minute;
        alarm_triggered[alarm_idx] = false;
        
        display.clearDisplay();
        print_line("Alarm " + String(alarm_idx + 1) + " set!", 0, 20, 2);
        delay(1500);
        return;
      }
    }
    else if (pressed == PB_CANCEL) {
      return;
    }
  }
}

void set_timezone() {
  float timezone = 5.5; // Default to UTC+5:30
  int whole_hours = 5;
  int half_hours = 1; // 0.5 hours
  
  while (true) {
    display.clearDisplay();
    print_line("Set Timezone", 0, 0, 1);
    print_line(String("UTC") + (timezone >= 0 ? "+" : "") + String(timezone), 0, 20, 2);
    print_line("UP/DN: Adjust", 0, 35, 1);
    print_line("OK: Confirm", 0, 45, 1);
    
    int pressed = wait_for_button();
    
    if (pressed == PB_UP) {
      half_hours = (half_hours + 1) % 2;
      if (half_hours == 0) {
        whole_hours = (whole_hours + 1) % 24;
      }
      timezone = whole_hours + (half_hours * 0.5);
    }
    else if (pressed == PB_DOWN) {
      half_hours = (half_hours - 1 + 2) % 2;
      if (half_hours == 1) {
        whole_hours = (whole_hours - 1 + 24) % 24;
      }
      timezone = whole_hours + (half_hours * 0.5);
    }
    else if (pressed == PB_OK) {
      long offset = timezone * 3600;
      configTime(offset, UTC_OFFSET_DST, NTP_SERVER);
      
      display.clearDisplay();
      print_line("Timezone set!", 0, 20, 2);
      delay(1500);
      return;
    }
    else if (pressed == PB_CANCEL) {
      return;
    }
  }
}

void view_alarms() {
  display.clearDisplay();
  print_line("Active Alarms:", 0, 0, 1);
  
  for (int i = 0; i < MAX_ALARMS; i++) {
    String alarmStr = String(i + 1) + ": ";
    alarmStr += String(alarm_hours[i]).length() < 2 ? "0" + String(alarm_hours[i]) : String(alarm_hours[i]);
    alarmStr += ":";
    alarmStr += String(alarm_minutes[i]).length() < 2 ? "0" + String(alarm_minutes[i]) : String(alarm_minutes[i]);
    alarmStr += alarm_triggered[i] ? " (TRIG)" : " (ACTV)";
    
    print_line(alarmStr, 0, 15 + i * 10, 1);
  }
  
  print_line("Press any button", 0, 50, 1);
  wait_for_button();
}

void delete_alarm() {
  int selected = 0;
  
  while (true) {
    display.clearDisplay();
    print_line("Delete Alarm:", 0, 0, 1);
    
    for (int i = 0; i < MAX_ALARMS; i++) {
      String alarmStr = (i == selected ? "> " : "  ");
      alarmStr += String(i + 1) + ": ";
      alarmStr += String(alarm_hours[i]).length() < 2 ? "0" + String(alarm_hours[i]) : String(alarm_hours[i]);
      alarmStr += ":";
      alarmStr += String(alarm_minutes[i]).length() < 2 ? "0" + String(alarm_minutes[i]) : String(alarm_minutes[i]);
      
      print_line(alarmStr, 0, 15 + i * 10, 1);
    }
    
    int pressed = wait_for_button();
    
    if (pressed == PB_UP) {
      selected = (selected - 1 + MAX_ALARMS) % MAX_ALARMS;
    }
    else if (pressed == PB_DOWN) {
      selected = (selected + 1) % MAX_ALARMS;
    }
    else if (pressed == PB_OK) {
      // Delete the selected alarm
      alarm_hours[selected] = 0;
      alarm_minutes[selected] = 0;
      alarm_triggered[selected] = false;
      
      display.clearDisplay();
      print_line("Alarm " + String(selected + 1) + " deleted", 0, 20, 2);
      delay(1500);
      return;
    }
    else if (pressed == PB_CANCEL) {
      return;
    }
  }
}

int wait_for_button() {
  while (true) {
    if (digitalRead(PB_UP) == LOW) {
      delay(200);
      return PB_UP;
    }
    if (digitalRead(PB_DOWN) == LOW) {
      delay(200);
      return PB_DOWN;
    }
    if (digitalRead(PB_OK) == LOW) {
      delay(200);
      return PB_OK;
    }
    if (digitalRead(PB_CANCEL) == LOW) {
      delay(200);
      return PB_CANCEL;
    }
  }
}