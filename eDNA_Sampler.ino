/**
 * eDNA Sampler: ESP8266 based eDNA Sampler / relevant sensor controller
 * Nov 5, 2019
 * Authors: JunSu Jang
 *        
 *      [Descriptions]
 * 
 *  This eDNA Sampler will sample eDNA at specified depth of the ocean by
 * using a off-the-shelf pump. It keeps track of the relevant data 
 * (i.e. pressure, temperature, water flow and realtime)
 * 
 * 1. Deployment configuration
 *  a) Set up I2C devices (RTC, RFID reader, pressure and temperature sensors)
 *  b) Connect to local WiFi and upload collected data if exists
 *     Delete the uploaded datafile
 *  c) Wait for a eDNA filter's RFID tag to be tagged
 *  d) Upon tagging, create a deployment on the local webserver and
 *     wait for deployment configurations
 *     i) "Target Depth" (pressure in mBar)
 *        - Ideally, we measure at this depth
 *     ii) "Wait Time" after 5m submersion (time in sec):
 *        - Depending on the deployment, it may not reach the depth, 
 *          in which case, we start pumping after "Wait Time"
 *     iii) "Target Flow Volume" (amount of water in mL)
 *        - We will pump water until target volume has been passed 
 *          through the filter
 *     iv) "Target Flow Duration" (time in sec)
 *        - Our filter may be full before the target volume has been met. 
 *          We stop pumping after specified "Target Flow Duration"
 * 2. Start measuring flow, time, pressure and temperature at 1Hz and
 *    log it to the file created
 * 3. At i) or ii), start pumping water
 * 4. Wait until iii) or iv), then stop the pump
 * 5. Keep measure flow, time, pressure and temperature at 1Hz until the
 *    end of deployment
 * 
 *      [LED Encoding]
 * 
 * 1. Until I2C is set up, all three LEDs blink.
 *    * Sometimes sensors require power-cycle, if they keep blinking,
 *      power-cycle the entire system.
 * 2. I2C setup complete --> RED on
 * 3. Wifi, data upload --> RED blink
 * 4. Wait for RFID --> BLUE blink
 * 5. Wait for deployment config --> GREEN blink
 * 6. All ready -- GREEN on
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
#include <NfcAdapter.h>
#include <PN532/PN532/PN532.h>
#include <PN532/PN532_I2C/PN532_I2C.h>
#include <ArduinoJson.h>
#include <limits.h>
#include <float.h>
#include "TimeLib.h"
#include "RTClib.h"
#include "MS5837.h"
#include "KellerLD.h"
#include "TSYS01.h"

// Define global parameters 
#define DEVICE_ID 3             // Hardcoded device ID
#define FM_PIN 14               // Flowmeter interrupt pin
#define PUMP_PIN 12             // GPIO pin to control Vpump
#define LED_PWR 13              // RED Led
#define LED_RDYB 16             // Blue LED
#define LED_RDYG 15             // Green LED
#define DEPTH_MARGIN 10         // Target depth margin
#define MAX_NUM_PUMP 64         // Maximum number of pumps on/off
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
#define DEPLOYMENT_NOT_RDY 0    // RFID was not scanned
#define DEPLOYMENT_RDY 1        // RFID was previously scanned

// WiFi Configuration
#define LOCAL_SSID "RPI"        // SSID of the RPI host
#define LOCAL_PWD "12345678"    // PWD of the RPI host
#define SERVER_IP "192.168.4.1" // for RPI; Junsu comp: "18.21.130.198"
#define WEB_PORT "5000"
#define CHUNK_SIZE 2048         // Data chunk size for uploading
#define WIFI_WAIT 20

// Uncomment if using MS5837 pressure sensor
#define IS_MS5837 1
#define MIN_DEPTH 2

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
float u_min_flowrate = 0;                    // L/min
uint32_t u_wait_pump_end = UINT_MAX;          // seconds
uint32_t u_target_flow_vol = UINT_MAX;            // ticks 
uint32_t u_wait_pump_start = UINT_MAX;        // seconds after dive
float u_temperature_band = 0.f;               // +/-100deg C pre-specified for maximum range
float u_target_temperature = -273.15;         // deg Celcius
float u_depth_band = 0;                    // meters
float u_target_depth = FLT_MAX;           // meters
uint32_t u_ticks_per_L = 0;                   // ticks per Liter

// deployment conditions
uint8_t start_conditions[N_START_COND];
uint8_t end_conditions[N_END_COND];
uint8_t start_cond_mask[N_END_COND];
uint8_t end_cond_mask[N_START_COND];

// RFID
PN532_I2C pn532_i2c(Wire);
NfcAdapter nfc = NfcAdapter(pn532_i2c);
String eDNA_uid;

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
volatile uint32_t f_data = 0;              // Stamped variable for logging
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


/**
 * Helper function declartions
 */
// Flowmeter Interrupt handler
static void ICACHE_RAM_ATTR isr_flowmeter(void);
// stamp the current flowmeter reading and time
void data_log(void);
// Concatenate string for home url
String get_home_url(void);
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
// Wait for RFID to be read and set the eDNA_uid
void wait_RFID(void);

// check the condition of starting and ending the pump
uint8_t check_conditions(uint32_t depth, float temperature, uint32_t time_now, uint32_t ticks);
// Check validity of deployment configuration
uint8_t is_valid_user_configuration(void);
// synchornize the RTC with the time of the webserver
void synchronize_rtc(String home_url);
// Upload data file, broken into chunks, to the webserver
void upload_existing_data(String home_url);
// Check if deployment has been configured (i.e. RFID scanned)
uint8_t check_deployment_status(String home_url);
// Create new deployment on the webserver
void upload_new_deployment(String home_url);
// Query the deployment configuration from the webserver
void query_deployment_configurations(String home_url);

// log file operation
void log_line(String log_data);
void upload_log(String home_url, String uid);

/***********************************************************
 * 
 *     setup()
 * 
 *   Setup GPIO Pins
 *   Setupt I2C
 *   Connect to WiFi
 *   If there is file, send it over and delete
 *   Wait for RFID
 *   Wait for Deployment configuration
 *   Create file
 *   Start collecting data
 *   
 * 
 */
void setup() {
  // Watchdog update to 5 seconds
  ESP.wdtEnable(5000);
  #ifdef DEBUG
  Serial.begin(9600);
//  Serial.setDebugOutput(true);
  #endif
 
  // 0. setup GPIO pins, i2c, filesystem and isr
  // Blink all LEDs while hardware getting ready
//  ticker_led.attach_ms(500, blink_all);
  SPIFFS.begin();
  setup_pins();
  attachInterrupt(FM_PIN, isr_flowmeter, FALLING);

  // 1. Connect to WiFi
  ticker_led.attach_ms(500, blink_single_led, LED_PWR);
  WiFi.scanDelete();
  WiFi.begin(LOCAL_SSID, LOCAL_PWD);
  uint8_t wifi_attempt = 0;
  while ((WiFi.status() != WL_CONNECTED) && (wifi_attempt < WIFI_WAIT)) {
    delay(1000);  
    wifi_attempt++;
  }

  if (wifi_attempt == WIFI_WAIT) {
    ticker_led.detach();
    ticker_led.attach_ms(500, blink_all);
    log_line("Wifi connection failed after 10 attempts!");
    while(1) {
      delay(500);
    }
  }

  #ifdef DEBUG
  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());
  #endif
  log_line("Connected to WiFi.");

  setup_i2c();

  String home_url = get_home_url();

  // 2. Synch RTC
  synchronize_rtc(home_url);
//  Serial.println("Time set");
  // 3. Upload old data if exists
  upload_existing_data(home_url);
  
  Serial.println("Uploaded data");
  ticker_led.detach();
  digitalWrite(LED_PWR, LOW);
  log_line("Uploaded data to webserver");

  // 4. Check if deployment is setup online
  // check_deployment_status() will set the eDNA_UID shared variable
  uint8_t deployment_status = check_deployment_status(home_url);

  if (deployment_status != DEPLOYMENT_RDY) {

    // 4. Scan RFID 
    nfc.begin();
    
    #ifdef DEBUG
    Serial.println("NFC connected");
    #endif
    log_line("RFID reader initialized.");

    ticker_led.attach_ms(500, blink_single_led, LED_RDYB);
    wait_RFID();
    Serial.println("RFID done");
    // 5. Create deployment log file and upload the upcoming deployment
    upload_new_deployment(home_url);
    ticker_led.detach();
    digitalWrite(LED_RDYB, LOW);

    // 6. Wait until the deployment configuration is set
    ticker_led.attach_ms(500, blink_single_led, LED_RDYG);
    query_deployment_configurations(home_url);
    
  } else {
    // Bring in the configuration information online
    ticker_led.attach_ms(500, blink_single_led, LED_RDYG);
    query_deployment_configurations(home_url);
    
    // 7. Deployment has previously been set, 
    //    configure log file and start logging!
    data_file = "/";
    data_file.concat(eDNA_uid);
    data_file.concat(".txt");
    #ifdef DEBUG
    Serial.print("Data file name: ");
    Serial.println(data_file);
    #endif

    File data_f = SPIFFS.open(data_file, "w+");
    if (!data_f) {
      #ifdef DEBUG
      Serial.println("Failed to open new file");
      #endif
      log_line("Opening data file failed!");
      ticker_led.attach_ms(500, blink_all);
      while(1);
    }
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
  }
  ticker_led.detach();
  digitalWrite(LED_RDYG, HIGH);
}


/***********************************************************
 * 
 *     loop()
 * 
 *   If log data flag is set, do the following:
 *    - read from pressure and temperature senso`rs
 *    - Save the data
 *    - save dive start time (5m deep)
 *    - Check if target depth / time is reached 
 *        --> Start the pump
 *    - Check if target volume / time is reached 
 *        --> End the pump
 *   
 * 
 */
void loop() {
  if (fLogData == 1) {
    flow_log[cur_flow_idx] = f_data;
    cur_flow_idx = (cur_flow_idx + 1) % NUM_FLOW_LOGS;
    // average the flow across 5 seconds of accumulated ticks
    cur_flowrate = (f_data - flow_log[cur_flow_idx]) * 12.f; // ticks per minute
    p_sensor.read();
    float depth = p_sensor.depth();
    float p_data = p_sensor.pressure();
    c_sensor.read();
    float c_data = c_sensor.temperature();

    // Write data to the data log file
    if (SPIFFS.exists(data_file)) {
      // TODO: Need to update number of entries
      File f = SPIFFS.open(data_file, "a+");
      f.print(t_data);
      f.print(',');
      f.print(p_data);
      f.print(',');
      f.print(c_data);
      f.print(',');
      f.print(f_data);
      f.print(',');
      f.println(cur_flowrate);
      f.close();
      #ifdef DEBUG
      Serial.print(t_data);
      Serial.print(", ");
      Serial.print(p_data);
      Serial.print(", ");
      Serial.print(c_data);
      Serial.print(", ");
      Serial.print(f_data);
      Serial.print(", ");
      Serial.println(cur_flowrate);
      #endif
    }

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
  end_cond_mask[0] = u_target_flow_vol > 0 && u_target_flow_vol < UINT_MAX;
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
  end_conditions[0] = ticks > u_target_flow_vol;
  // 1. Pump duration 
  end_conditions[1] = (pump_start <= time_now) && ((time_now - pump_start) > u_wait_pump_end);
  // 2. Flowrate: flowrate below 10% of original flowrate
  end_conditions[2] = ((max_flowrate > 0.f) &&
                      ((u_min_flowrate * u_ticks_per_L) > cur_flowrate )
                      || (max_flowrate > 10 * cur_flowrate)
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


String get_home_url() {
  String home_url = "http://";
  home_url.concat(SERVER_IP);
  home_url.concat(':');
  home_url.concat(WEB_PORT);
  return home_url;
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
    while(1);
  }
  #else
  p_sensor.init();
  #endif
  log_line("Pressure sensor setup successful.");
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
    while (1);
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
  log_line("I2C setup complete!");

}


void wait_RFID() {
  #ifdef DEBUG
  Serial.println("RFID Waiting");
  #endif
  // Block until there is a tag present
  while (!(nfc.tagPresent())) {}

  NfcTag tag = nfc.read();
  // Set UID parameter
  eDNA_uid = tag.getUidString();
  eDNA_uid.replace(" ", "");
  #ifdef DEBUG
  Serial.println(eDNA_uid);
  #endif
}


void synchronize_rtc(String home_url) {
  if (WiFi.status() == WL_CONNECTED) {
    time_t t;
    HTTPClient http;
    String time_url = home_url + "/deployment/datetime/now";
    int httpCode = 0;
    // Persist to get the time on the webserver. 
    while (httpCode != 200) { 
      http.begin(time_url);
      // Json preparation
      const size_t bufferSize = JSON_OBJECT_SIZE(2) + \
        JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(8) + 370;
      DynamicJsonDocument jsonBuffer(bufferSize);
      // send GET request
      httpCode = http.GET();
      deserializeJson(jsonBuffer, http.getString());
      t = (time_t) jsonBuffer["now"];

      #ifdef DEBUG
      Serial.print("Time");
      Serial.println(t);
      #endif
      log_line("Synchronized time with the server!");

      http.end();
      delay(1000);
    }
    // Adjust RTC with the webserver time
    DateTime new_now = DateTime(year(t), month(t), \
      day(t), hour(t), minute(t), second(t));
    rtc.adjust(new_now);
  }
}


void upload_existing_data(String home_url) {
  Dir dir = SPIFFS.openDir("/");
  char temp_bytes[CHUNK_SIZE];
  Serial.println("Hello");
  while (dir.next()) {
    // Assumption: We only have relevant data
    String f_name = dir.fileName();
    #ifdef DEBUG
    Serial.println(f_name);
    #endif
    int idx = f_name.indexOf('.');
    // We assume that the proper data file name is simply
    // the uid which has 8 characters (e.g. "/abcdef12.txt")
    if (idx != 9) continue;
    // Only upload data of [uid].txt format
    String uid = f_name.substring(1, idx);    
    String upload_url = home_url + "/deployment/upload/" + uid;
    
    File cur_file = dir.openFile("r");
    uint16_t f_size = dir.fileSize();
    // We need to turn data into multiple chunks
    uint8_t n_chunks = f_size / CHUNK_SIZE + 1;
    
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(upload_url);
      http.addHeader("Content-Type", "text/plain");
      http.addHeader("Chunks", String(n_chunks));

      uint16_t bytes_read = 0;
      // bookkeeping parameter to let the webserver know how much is left
      uint8_t nth_chunk = 1;
      // Upload data until the file is EOF
      while (cur_file.available()) {
        // Upload chunk by chunk
        while (cur_file.available() && bytes_read < CHUNK_SIZE) {
          temp_bytes[bytes_read] = cur_file.read();
          bytes_read++;
        }
        http.addHeader("Data-Bytes", String(bytes_read));
        http.addHeader("Nth", String(nth_chunk));
        int httpCode = http.POST(temp_bytes);

        // Persistent request to upload the file
        while (httpCode != 200) {
          httpCode = http.POST(temp_bytes);
          delay(1000);
        }
        // Reset/update the local parameters
        bytes_read = 0;
        nth_chunk++;
      }
      http.end();
    }
    if (SPIFFS.exists(log_file)) {
      upload_log(home_url, uid);
    }
    // Delete the uploaded file
    SPIFFS.remove(f_name);
  }
}


void upload_new_deployment(String home_url) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String create_url = home_url + "/deployment/create/" + String(DEVICE_ID);
    http.begin(create_url);
    http.addHeader("Content-Type", "text/plain");
    int httpCode = http.POST(eDNA_uid);
    // Persistent POST request to create the deployment
    while (httpCode != 200) {
      httpCode = http.POST(eDNA_uid);
    }
    #ifdef DEBUG
    Serial.println("Uploaded a new deployment successfully!");
    #endif
    http.end();
  }
}

uint8_t check_deployment_status(String home_url) {
  uint8_t deployment_status = DEPLOYMENT_NOT_RDY;
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String check_url = home_url + "/deployment/has_deployment/" + String(DEVICE_ID);
    http.begin(check_url);
    const size_t buffer_size = JSON_OBJECT_SIZE(2) + \
        JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(8) + 370;
    DynamicJsonDocument jsonBuffer(buffer_size);
    int httpCode = http.GET();
    if (httpCode > 0) {
      deserializeJson(jsonBuffer, http.getString());
      deployment_status = jsonBuffer["status"];
      if (deployment_status == DEPLOYMENT_RDY) {
        eDNA_uid = jsonBuffer["eDNA_UID"].as<String>();
        log_line(eDNA_uid);
        #ifdef DEBUG
        Serial.print("uid:");
        Serial.println(eDNA_uid);
        #endif
      }
    }
  }
  return deployment_status;
}


void query_deployment_configurations(String home_url) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String info_url = home_url + "/deployment/get_config/" + eDNA_uid;
    // Keep trying at 1Hz until all of the parameters are configured for deployment
    while (!is_valid_user_configuration()) {
      http.begin(info_url);
      // Deserialize Json file
      const size_t bufferSize = JSON_OBJECT_SIZE(2) + \
        JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(8) + 370;
      DynamicJsonDocument jsonBuffer(bufferSize);
      int httpCode = http.GET();
      if (httpCode > 0) {
        deserializeJson(jsonBuffer, http.getString());

        #ifdef DEBUG
        Serial.println(http.getString());
        #endif

        // Parameters for deployment configuration
        u_ticks_per_L = jsonBuffer["ticks_per_L"];

        float temp_depth = jsonBuffer["depth"];
        u_target_depth = temp_depth > 0 ? temp_depth : FLT_MAX;
        u_depth_band = jsonBuffer["depth_band"];
        float temp_temperature = jsonBuffer["temperature"];
        u_target_temperature = temp_temperature > ABS_ZERO_C ? temp_temperature : ABS_ZERO_C;
        u_temperature_band = jsonBuffer["temp_band"];
        u_wait_pump_start = jsonBuffer["wait_pump_start"]; // min
        u_wait_pump_start *= 60; // seconds
        
        u_min_flowrate = jsonBuffer["min_flowrate"]; // L/min
        uint32_t temp_wait_pump_end = jsonBuffer["wait_pump_end"]; // min
        u_wait_pump_end = temp_wait_pump_end > 0 ? (temp_wait_pump_end * 60) : UINT_MAX; // sec
        float temp_flow_vol = jsonBuffer["flow_volume"]; // ticks
        u_target_flow_vol = temp_flow_vol > 0 ? (uint32_t)(temp_flow_vol * u_ticks_per_L) : UINT_MAX;
       
        http.end();
        delay(1000);
      }
    }
  }
}


void log_line(String log_data) {
  File log_f = SPIFFS.open(log_file, "a+");
  log_f.print(rtc.now().unixtime());
  log_f.print(": ");
  log_f.println(log_data);
  log_f.close();
}

void upload_log(String home_url, String uid) {
  String upload_log_url = home_url + "/deployment/upload-log/" + uid;
  char temp_bytes[CHUNK_SIZE];

  File cur_file = SPIFFS.open(log_file, "r");;
  uint16_t f_size = cur_file.size();
  // We need to turn data into multiple chunks
  uint8_t n_chunks = f_size / CHUNK_SIZE + 1;
  
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(upload_log_url);
    http.addHeader("Content-Type", "text/plain");
    http.addHeader("Chunks", String(n_chunks));

    uint16_t bytes_read = 0;
    // bookkeeping parameter to let the webserver know how much is left
    uint8_t nth_chunk = 1;
    // Upload data until the file is EOF
    while (cur_file.available()) {
      // Upload chunk by chunk
      while (cur_file.available() && bytes_read < CHUNK_SIZE) {
        temp_bytes[bytes_read] = cur_file.read();
        bytes_read++;
      }
      http.addHeader("Data-Bytes", String(bytes_read));
      http.addHeader("Nth", String(nth_chunk));
      int httpCode = http.POST(temp_bytes);

      // Persistent request to upload the file
      while (httpCode != 200) {
        httpCode = http.POST(temp_bytes);
        delay(1000);
      }
      // Reset/update the local parameters
      bytes_read = 0;
      nth_chunk++;
    }
    http.end();
  }
  // Delete the uploaded log
  SPIFFS.remove(log_file);
}
