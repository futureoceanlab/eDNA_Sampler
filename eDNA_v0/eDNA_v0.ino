/**
 * eDNA Sampler: ESP8266 based eDNA Sampler / relevant sensor controller
 * Version 0.0
 * Dec 7, 2019
 * Authors: JunSu Jang
 *        
 *      [Descriptions]
 * 
 *  This eDNA Sampler will sample eDNA at specified depth of the ocean by
 * using a off-the-shelf pump. It keeps track of the relevant data 
 * (i.e. pressure, temperature, water flow and realtime)
 * 
 * 
 *      [SPIFFS data filesystem]
 * 
 * Filename: [RFID_UID].txt
 * First line: [unix timestamp],[device ID],[RFID UID],[Target Depth]\r\n
 * Logged data: [unix timestamp],[pressure],[flow],[temperature]\r\n
 *    * pressure and temperature are multipied by 100 to convert float to int
*/
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <Ticker.h>
#include <String.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <limits.h>
#include <float.h>
#include "TimeLib.h"
#include "RTClib.h"
#include "MS5837.h"
#include "KellerLD.h"
#include "TSYS01.h"

// Define global parameters 
#define DEVICE_ID 1             // Hardcoded device ID

// Pin configuration
#define FM_PIN 14               // Flowmeter interrupt pin
#define PUMP_PIN 12             // GPIO pin to control Vpump
#define LED_PWR 13              // RED Led
#define LED_RDYB 16             // Blue LED
#define LED_RDYG 15             // Green LED
#define NUM_FLOW_LOGS 5         // Num data for computing derivate of ticks
#define ABS_ZERO_C -273.15f

// Pump condition global variables
#define N_START_COND 3          // Number of pump starting conditions
#define N_END_COND 3            // Number of pump ending conditions

// Pump state variables
#define PUMP_OFF 0              // Pump is off
#define PUMP_ON 1               // Pump is on
#define PUMP_IDLE 2
#define PUMP_RUNNING 3

// Device status
#define SUBMERGED 1             // Sampler submerged
#define COMMUNICATION_MODE 2    // Communication with user
#define DEPLOYMENT_MODE 3       // Deployment mode

// Uncomment if using MS5837 pressure sensor
#define IS_MS5837 1
#define MIN_DEPTH 2.f              // meter
#define MAX_VOL 100

// Sensor minimuma and maximum boundaries
#ifdef IS_MS5837
#define MAX_DEPTH 300.f
#else
#define MAX_DEPTH 975.f
#endif
#define MAX_TEMP 125.0f
#define MIN_FLOWRATE 0.1f       // Stop at 0.1L/min flowrate regardless of conditions

// Uncomment if want serial output for debugging
#define DEBUG 1

// user defined deployment configuration variables

// ****************************************************************************
// * User Configuration START
// ****************************************************************************

// PUMP START CONDITIONS
float u_target_depth = FLT_MAX;           // meters (default: FLT_MAX if not using)
float u_depth_band = 0;                   // meters (default: 0 if not using)

float u_target_temperature = -273.15;     // deg Celcius (default: ABS_ZERO_C if not using)
float u_temperature_band = 0.f;           // deg Celcius (default: 0 if not using)

uint32_t u_wait_pump_start = 0;          // seconds (default: 0 if not using)

// PUMP END CONDITIONS
float u_min_flowrate = 0.f;                 // Liter/min (default: 0 if not using)
uint32_t u_wait_pump_end = UINT_MAX;            // seconds (default: UINT_MAX if not using)
uint32_t u_target_flow_vol = MAX_VOL;    // Liter (default: 100 if not using) 

// FTB431 (not potted): 2724 ticks/L
// FTB2003 (potted): 4600 ticks/L 
uint32_t u_ticks_per_L = 0;               // ticks per Liter 
       
// Deployment ID 
String eDNA_uid = "ABCDEFGH";             // Deployment UID 8-bit char like RFID tag UID

// ****************************************************************************
// * User Configuration END
// ****************************************************************************

// deployment conditions
uint8_t start_conditions[N_START_COND];
uint8_t end_conditions[N_END_COND];
uint8_t start_cond_mask[N_END_COND];
uint8_t end_cond_mask[N_START_COND];

// RTC
RTC_DS3231 rtc;
volatile uint32_t t_data;

// Pressure
#ifdef IS_MS5837
MS5837 p_sensor;
#else // Keller Sensor
KellerLD p_sensor;
#endif

// Temperature
TSYS01 c_sensor;

// Flowmeter
volatile uint32_t flow_counter = 0;        // flowmeter interrupt update variable
volatile uint32_t f_data = 0;              //                                              Stamped variable for logging
uint32_t flow_log[NUM_FLOW_LOGS] = {0};    // log of flow values for derivative calculation
float max_flowrate = 0.f;
float cur_flowrate = 0.f;
uint8_t cur_flow_idx = 0;

// Flash
String data_file;
String log_file = "/log.txt";;

// Timer for 1Hz log / LED
Ticker ticker;
Ticker ticker_led;

//flags
volatile uint8_t fLogData = 0;
uint8_t fSubmerged = !SUBMERGED;

// bookkeeping parameters
uint32_t dive_start = UINT_MAX, pump_start = UINT_MAX;
uint8_t pump_status = PUMP_IDLE;
uint8_t counter = 0;
uint8_t device_mode = COMMUNICATION_MODE;


/**
 * Helper function declartions
 */
// Setup the deployment data file
void setup_deployment(void);
// Log the data onto the data file
void write_data(uint32_t time, float depth, float temperature, uint32_t flow, float flowrate);
// Flowmeter Interrupt handler
static void ICACHE_RAM_ATTR isr_flowmeter(void);
// stamp the current flowmeter reading and time
void data_log(void);
// Convert milliliter to ticks
uint32_t convert_ml_to_ticks(uint32_t ml);
// blink all leds
void blink_all(void);
// blink a single led of specified led
void blink_single_led(int led);
// setup relevant GPIO pins
void setup_pins(void);
// Setup I2C devices
void setup_i2c(void);

// Communication related functions
void serial_output_user_prompt(void);
void output_all_files(void);
void delete_all_files(void);

// check the condition of starting and ending the pump
uint8_t check_conditions(uint32_t depth, float temperature, uint32_t time_now, uint32_t ticks);
// Check validity of deployment configuration
uint8_t is_valid_user_configuration(void);

// log file operation
void log_line(String log_data);

/***********************************************************
 * 
 *     setup()
 * 
 *   Setup GPIO Pins and interrupts
 *   Setupt I2C
 */
void setup() {
  Serial.begin(115200);
  
  // Setup GPIO pins, i2c, filesystem and isr
  SPIFFS.begin();
  setup_pins();
  digitalWrite(LED_PWR, HIGH); // Power LED indicator
  attachInterrupt(FM_PIN, isr_flowmeter, FALLING);
  setup_i2c();

  // Blink blue LED to indicate communication ready
  ticker_led.attach_ms(500, blink_single_led, LED_RDYB);
  serial_output_user_prompt();
}


/***********************************************************
 *     loop()
 * 
 *   CASE: Communication
 *      download: Dump all files
 *      delete: Delete all files 
 *      deploy: The device is to be deployed
 * 
 *   CASE: Deployment
 *      1. Log the data
 *      2. Check that the device is submerged
 *      3. if submerged, control the pump
 */
void loop() {
  if (device_mode == COMMUNICATION_MODE) {
    if (Serial.available()) {
      String user_input = Serial.readStringUntil('\n');
      Serial.print("User: ");
      Serial.println(user_input);
      if (user_input == "download") {
        output_all_files();
        Serial.println("Please write the next command.");
      } else if (user_input == "delete") {
        delete_all_files();
        Serial.println("Deleted all files successfully.");
        Serial.println("Please write the next command.");
      } else if (user_input == "deploy") {
        setup_deployment();
        device_mode = DEPLOYMENT_MODE;
        Serial.println("Please remove the serial device and enclose me safely!");
        Serial.println("Sea ya later!");
      }
    }
  } else if (device_mode == DEPLOYMENT_MODE) {
    if (fLogData == 1) {
      // 1. Log data
        flow_log[cur_flow_idx] = f_data;
        cur_flow_idx = (cur_flow_idx + 1) % NUM_FLOW_LOGS;
        // average the flow across 5 seconds of accumulated ticks
        cur_flowrate = (f_data - flow_log[cur_flow_idx]) * 12.f; // ticks per minute
        p_sensor.read();
        float depth = p_sensor.depth();
        float p_data = p_sensor.pressure();
        c_sensor.read();
        float c_data = c_sensor.temperature();
        write_data(t_data, depth, c_data, f_data, cur_flowrate);
      // 2. Check submersion
        #ifndef DEBUG
        // At 5m, we turn off LEDs
        if (fSubmerged != SUBMERGED && depth >= MIN_DEPTH) {
          log_line("START DEPLOYMENT - SEE YOU!")
          digitalWrite(LED_RDYG, LOW);
          dive_start = t_data;
          fSubmerged = SUBMERGED;
        }
        if (fSubmerged == SUBMERGED && depth <= MIN_DEPTH) {
          // Stop the pump if about to come out of the water
          log_line("END DEPLOYMENT - HELLO!")
          digitalWrite(LED_RDYG, HIGH);
          digitalWrite(PUMP_PIN, LOW);
          fSubmerged = !SUBMERGED;
        }
        #endif
      // 3. Control pump if submerged 
        if (fSubmerged == SUBMERGED) {
          uint8_t pump_action = check_conditions(depth, c_data, t_data, f_data);
          if (pump_status == PUMP_IDLE && pump_action == PUMP_ON) {
            // Measure the maximum flowrate after 5 seconds of pump start
            // on the first pump start (in ticks/min)
            pump_start = t_data;
            log_line("PUMP ON!");
            #ifdef DEBUG
            Serial.println("PUMP ON!");
            #endif
            digitalWrite(PUMP_PIN, HIGH);
            pump_status = PUMP_RUNNING;
          } else if (pump_status == PUMP_RUNNING && pump_action == PUMP_OFF) {
            digitalWrite(PUMP_PIN, LOW);
            log_line("PUMP OFF!");
            #ifdef DEBUG
            Serial.println("PUMP OFF!");
            #endif
            pump_status = PUMP_IDLE;
          }
          // After 10 seconds, measure the maximum flowrate
          if (pump_status == PUMP_RUNNING && counter < 10) {
            counter++;
            if (counter == 10) {
              max_flowrate = (f_data - flow_log[cur_flow_idx]) * 12;
            }
          }
        }
      fLogData = 0; 
    }
  }
}


void setup_deployment() {
  if (is_valid_user_configuration()) {
    Serial.print("Deployment configuration valid");
  } else {
    Serial.println("WARNING");
    Serial.println("Deployment is not configured properly. Please re-configure");
    Serial.println("WARNING");
    ticker_led.attach_ms(500, blink_all);
    while(1) {
      delay(500);
    }
  }
  data_file = "/";
  data_file.concat(eDNA_uid);
  data_file.concat("_");
  data_file.concat(rtc.now().unixtime());
  data_file.concat(".txt");
  #ifdef DEBUG
  Serial.print("Data file name: ");
  Serial.println(data_file);
  #endif

  File data_f = SPIFFS.open(data_file, "w+");

  // First line of the file [time, device id, uid, num_entries]
  char f_header[100], f_deployment[100];
  sprintf(f_header, "%d,%d,%s,%d", \
      rtc.now().unixtime(), DEVICE_ID, eDNA_uid.c_str(), 0);
  data_f.println(f_header);
  data_f.close();
  
  // 1Hz data logging
  ticker.attach_ms(1000, data_log);
  #ifdef DEBUG
  dive_start = rtc.now().unixtime(); // Debugging purpose
  fSubmerged = SUBMERGED;
  #endif
  log_line("Setup complete - Start logging data!");
  ticker_led.detach();
  digitalWrite(LED_RDYG, HIGH);
}


void write_data(uint32_t time, float depth, float temperature, uint32_t flow, float flowrate) {
  // Write data to the data log file
  if (SPIFFS.exists(data_file)) {
    // TODO: Need to update number of entries
    File f = SPIFFS.open(data_file, "a+");
    f.print(time);
    f.print(',');
    f.print(depth);
    f.print(',');
    f.print(temperature);
    f.print(',');
    f.print(flow);
    f.print(',');
    f.println(flowrate);
    f.close();
    #ifdef DEBUG
    Serial.print(time);
    Serial.print(", ");
    Serial.print(depth);
    Serial.print(", ");
    Serial.print(temperature);
    Serial.print(", ");
    Serial.print(flow);
    Serial.print(", ");
    Serial.println(flowrate);
    #endif
  }
}

uint8_t is_valid_user_configuration() {
  // 0. depth
  start_cond_mask[0] = ((u_target_depth >= MIN_DEPTH) 
                     && (u_target_depth < MAX_DEPTH) 
                     && (u_depth_band > 0.f));
  // 1. temperature
  start_cond_mask[1] = ((u_target_temperature > ABS_ZERO_C) 
              && (u_target_temperature < MAX_TEMP) 
              && (u_temperature_band > 0.f));
  // 2. Wait duration after dive started
  start_cond_mask[2] = u_wait_pump_start > 0 && u_wait_pump_start < UINT_MAX;

  // 0. Pump volume
  end_cond_mask[0] = u_target_flow_vol > 0 && u_target_flow_vol < MAX_VOL;
  // 1. Pump duration
  end_cond_mask[1] = u_wait_pump_end > 0 && u_wait_pump_end < UINT_MAX;
  // 2. Minimum Flowrate until stop
  end_cond_mask[2] = u_min_flowrate > 0.f;

  uint8_t is_valid_fm = u_ticks_per_L > 0;
  uint8_t is_valid_start = 0, is_valid_end = 0;
  for (uint8_t i = 0; i < N_START_COND; i++) {
    is_valid_start |= start_cond_mask[i];
    is_valid_end |= end_cond_mask[i];
  }
  return is_valid_fm & is_valid_start & is_valid_end; 
}

uint8_t check_conditions(float depth, float temperature, uint32_t time_now, uint32_t ticks) {
  // 0. Depth
  start_conditions[0] = (abs(depth - u_target_depth)) < u_depth_band;
  // 1. Temperature
  start_conditions[1] = (abs(temperature - u_target_temperature)) < u_temperature_band;
  // 2. Time elapsed since dive start
  start_conditions[2] = (dive_start <= time_now) && ((time_now - dive_start) > u_wait_pump_start);

  // 0. Volume pumped
  end_conditions[0] = ticks > u_target_flow_vol* u_ticks_per_L;
  // 1. Pump duration 
  end_conditions[1] = (pump_start <= time_now) && ((time_now - pump_start) > u_wait_pump_end);
  // 2. Flowrate: flowrate below 10% of original flowrate
  end_conditions[2] = ((max_flowrate > 0.f) &&
                      ((u_min_flowrate * u_ticks_per_L) > cur_flowrate )
                      || (cur_flowrate < MIN_FLOWRATE)); 

  uint8_t pump_on = 0;
  for (uint8_t j = 0; j < N_START_COND; j++) {
    pump_on |= start_cond_mask[j] * start_conditions[j];
  }
  for (uint8_t k = 0; k < N_END_COND; k++) {
    pump_on &= !(end_cond_mask[k] * end_conditions[k]);
  }
  pump_on = pump_on == 0 ? PUMP_OFF : PUMP_ON;
  return pump_on;
}


void data_log() {
  cli();
  f_data = flow_counter;
  t_data = rtc.now().unixtime();
  sei();
  fLogData = 1;
}


static void ICACHE_RAM_ATTR isr_flowmeter() {
  flow_counter++;
}


void blink_all() {
  digitalWrite(LED_PWR, !(digitalRead(LED_PWR)));
  digitalWrite(LED_RDYB, !(digitalRead(LED_RDYB)));
  digitalWrite(LED_RDYG, !(digitalRead(LED_RDYG)));
}


void blink_single_led(int led) {
  digitalWrite(led, !(digitalRead(led)));
}


void setup_pins() {
  // GPIO for LED Indicators (OUTPUT)
  pinMode(LED_PWR, OUTPUT);
  pinMode(LED_RDYB, OUTPUT);
  pinMode(LED_RDYG, OUTPUT);

  digitalWrite(LED_PWR, LOW);
  digitalWrite(LED_RDYB, LOW);
  digitalWrite(LED_RDYG, LOW);
  
  // GPIO for controlling Power to the pump
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  // Flowmeter
  pinMode(FM_PIN, INPUT_PULLUP);
  #ifdef DEBUG
  Serial.println("Pins set");
  #endif
  log_line("Pins setup comlete!");
}


void setup_i2c() {
  Wire.begin();
  #ifdef DEBUG
  Serial.println("Setting up I2C...");
  #endif
  log_line("Start settup up I2C");

  // Temperature sensor
  c_sensor.init();
  log_line("Temperature sensor setup successful.");

  // Setup RTC
  if (!rtc.begin()) {
    #ifdef DEBUG
    Serial.println("Couldn't find RTC.");
    #endif
    log_line("RTC setup failed.");
    ticker_led.attach_ms(500, blink_all);
    while (1) {
      delay(500);
    };
  }
  log_line("RTC setup successful.");

  if (rtc.lostPower()) {
    #ifdef DEBUG
    Serial.println("RTC lost power, lets set the time!");
    #endif
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  #ifdef DEBUG
  Serial.println("done I2C");
  #endif
  // Pressure sensor
  #ifdef IS_MS5837
  uint8_t p_init_attempts = 0;
  while (!p_sensor.init() && p_init_attempts < 5) {
    #ifdef DEBUG
    Serial.println("pressure waiting...");
    #endif
    delay(1000); 
    p_init_attempts++;
  }
  if (p_init_attempts == 5) {
   log_line("Sensor MS5837 setup failed after 5 attempts.");
   ticker_led.attach_ms(500, blink_all);
   while (1) {
     delay(500);
   };
  }
  #else
  p_sensor.init();
  #endif
  log_line("Pressure sensor setup successful.");
  log_line("I2C setup complete!");

}

void log_line(String log_data) {
  File log_f = SPIFFS.open(log_file, "a+");
  log_f.print(rtc.now().unixtime());
  log_f.print(": ");
  log_f.println(log_data);
  log_f.close();
}

void serial_output_user_prompt() {
  Serial.println(" ** eDNA Sampler ** ");
  Serial.print("Current Deployment Configurations for");
  Serial.print(eDNA_uid);
  
  Serial.print("-- Flowmeter (ticks/L): ");
  Serial.println(u_ticks_per_L);

  Serial.println("--Pump start conditions:");
  Serial.print("Depth (m): ");
  Serial.println(u_target_depth);
  Serial.print("Depth band (m): ");
  Serial.println(u_depth_band);
  Serial.print("Temperature (C): ");
  Serial.println(u_target_temperature);
  Serial.print("Temperature band (C): ");
  Serial.println(u_temperature_band);
  Serial.print("Pump wait (sec): ");
  Serial.println(u_wait_pump_start);

  Serial.println("--Pump end conditions:");
  Serial.print("Minimum flowrate (ticks/min): ");
  Serial.println(u_min_flowrate);
  Serial.print("Pump duration (sec): ");
  Serial.println(u_wait_pump_end);
  Serial.print("Target volume (ticks): ");
  Serial.println(u_target_flow_vol);


  Serial.println("Available commands:");
  Serial.println("download : dump all files onto the serial monitor");
  Serial.println("delete : delete all files");
  Serial.println("deploy : will enter deployment mode. Did you configure in the code?");
  Serial.println("-------------------------------------------------------------------");
  Serial.println("Please type the commands below...!");
}

void output_all_files() {
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    String f_name = dir.fileName();
    uint16_t f_size = dir.fileSize();
    Serial.println(f_name);
    Serial.print("File size: ");
    Serial.println(f_size);

    File cur_file = dir.openFile("r");
    while(cur_file.available()){
        Serial.write(cur_file.read());
    }
    cur_file.close();
  }
}

void delete_all_files() {
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    String f_name = dir.fileName();
    Serial.print("Delete: ");
    Serial.println(f_name);
    SPIFFS.remove(f_name);
  }
}
