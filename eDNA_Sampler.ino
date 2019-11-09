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
#include "TimeLib.h"
#include "RTClib.h"
#include "MS5837.h"
#include "KellerLD.h"
#include "TSYS01.h"

// Define global parameters 
#define DEVICE_ID 2             // Hardcoded device ID
#define FM_PIN 14               // Flowmeter interrupt pin
#define PUMP_PIN 12             // GPIO pin to control Vpump
#define LED_PWR 13              // RED Led
#define LED_RDYB 16             // Blue LED
#define LED_RDYG 15             // Green LED
#define DEPTH_MARGIN 10         // Target depth margin

// Pump state variables
#define PUMP_READY 0            // Pump has not yet started
#define PUMP_RUNNING 1          // Pump is running
#define PUMP_ENDED 2            // Pump has finished

#define DEPLOYMENT_NOT_RDY 0    // RFID was not scanned
#define DEPLOYMENT_RDY 1        // RFID was previously scanned
// WiFi Configuration
#define LOCAL_SSID "MIT"      
#define LOCAL_PWD ""
#define SERVER_IP "18.21.135.5"
#define WEB_PORT "5000"
#define CHUNK_SIZE 2048         // Data chunk size for uploading

// Uncomment if using MS5837 pressure sensor
//#define IS_MS5837 1
// Uncomment if want serial output for debugging
#define DEBUG 1
// Uncomment if using FTB431 flowmeter (wire one) 
// Comment if using FTB2003 flowmeter
#define IS_FTB431 1

// Deployment configuration parameters
int target_depth=0, target_pump_wait=0;
int target_flow_vol=0, target_flow_duration=0;

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
volatile uint32_t flow_counter = 0;
volatile uint32_t f_data = 0;

// Flash
String data_file;

// Timer for 1Hz log / LED
Ticker ticker;
Ticker ticker_led;

//flags
volatile uint8_t fLogData = 0;
volatile uint8_t pump_state = PUMP_READY;

// bookkeeping parameters
uint32_t dive_start, pump_start;
uint8_t submerged = 0;


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
  #ifdef DEBUG
  Serial.begin(9600);
  #endif
  // 0. setup GPIO pins, i2c, filesystem and isr
  // Blink all LEDs while hardware getting ready
  ticker_led.attach_ms(500, blink_all);

  setup_pins();
  setup_i2c();

  SPIFFS.begin();
  attachInterrupt(FM_PIN, isr_flowmeter, FALLING);
  ticker_led.detach();
  // Turn off all LEDs
  digitalWrite(LED_PWR, LOW);
  digitalWrite(LED_RDYB, LOW);
  digitalWrite(LED_RDYG, LOW);
  
  // 1. Connect to WiFi
  ticker_led.attach_ms(500, blink_single_led, LED_PWR);
  WiFi.begin(LOCAL_SSID, LOCAL_PWD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000); }
  #ifdef DEBUG
  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());
  #endif

  String home_url = get_home_url();

  // 2. Synch RTC
  synchronize_rtc(home_url);

  // 3. Upload old data if exists
  upload_existing_data(home_url);
  ticker_led.detach();
  digitalWrite(LED_PWR, LOW);

  // 4. Check if deployment is setup online
  // check_deployment_status() will set the eDNA_UID shared variable
  uint8_t deployment_status = check_deployment_status(home_url);

  if (deployment_status != DEPLOYMENT_RDY) {
    // 4. Scan RFID 
    nfc.begin();
    #ifdef DEBUG
    Serial.println("NFC connected");
    #endif
    ticker_led.attach_ms(500, blink_single_led, LED_RDYB);
    wait_RFID();
    
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
    }
    // First line of the file [time, device id, uid, num_entries]
    char f_header[100];
    sprintf(f_header, "%d,%d,%s,%d", \
        rtc.now().unixtime(), DEVICE_ID, eDNA_uid.c_str(), 0);
    data_f.println(f_header);
    data_f.close();
    
    // 1Hz data logging
    ticker.attach_ms(1000, data_log);
    #ifdef DEBUG
    dive_start = rtc.now().unixtime(); // Debugging purpose
    #endif
  }
  ticker_led.detach();
  digitalWrite(LED_RDYG, HIGH);
}


/***********************************************************
 * 
 *     loop()
 * 
 *   If log data flag is set, do the following:
 *    - read from pressure and temperature sensors
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
    p_sensor.read();
    uint32_t p_data = (uint32_t) (p_sensor.pressure());
    c_sensor.read();
    uint32_t c_data = (uint32_t) (c_sensor.temperature() * 100);
    // Write data to the data log file
    if (SPIFFS.exists(data_file)) {
      // TODO: Need to update number of entries
      File f = SPIFFS.open(data_file, "a+");
      f.print(t_data);
      f.print(',');
      f.print(p_data);
      f.print(',');
      f.print(f_data);
      f.print(',');
      f.println(c_data);
      f.close();
    }

   // At 5m, we turn off LEDs
   if (!submerged && p_sensor.depth() > 5) {
      digitalWrite(LED_RDYG, LOW);
     dive_start = t_data;
     submerged = 1;
   }

    #ifdef DEBUG
    // Pump control given based on depth, time and water volume
    if ((pump_state == PUMP_READY) && 
       (dive_start + target_pump_wait < t_data)) {
      // TODO: Record the time at which the pump start 
      Serial.println("Pump ON");
//      digitalWrite(PUMP_PIN, HIGH);
      pump_start = t_data;
      pump_state = PUMP_RUNNING;
    } else if ((pump_state == PUMP_RUNNING) && 
      (pump_start + target_flow_duration < t_data)) {
      Serial.println("Pump OFF");
      // TODO: Record the time at which the pump ended
//      digitalWrite(PUMP_PIN, LOW);
      pump_state = PUMP_ENDED;
    }
    #else
   // Pump control given based on depth, time and water volume
    if ((pump_state == PUMP_READY) && 
       ((dive_start + target_pump_wait < t_data) ||
       (abs(p_data - target_depth) < DEPTH_MARGIN)) {
      // TODO: Record the time at which the pump start 
//      digitalWrite(PUMP_PIN, HIGH);
      pump_start = t_data;
      pump_state = PUMP_RUNNING;
    } else if ((pump_state == PUMP_RUNNING) && 
      ((pump_start + target_flow_duration < t_data) ||
      (abs(p_data - target_depth) > DEPTH_MARGIN) || 
      (target_flow_vol > f_data)) {
      // TODO: Record the time at which the pump ended
//      digitalWrite(PUMP_PIN, LOW);
      pump_state = PUMP_ENDED;
    }
    #endif
    fLogData = 0; 
  }
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

uint32_t convert_ml_to_ticks(uint32_t ml) {
  #ifdef IS_FTB431
  uint32_t ticks = (uint32_t)((float) ml / 0.36705f);
  #else // FTB2003
  uint32_t ticks = (uint32_t)((float) ml) / 4600);
  #endif
  return ticks;
}


void setup_pins() {
  // GPIO for LED Indicators (OUTPUT)
  pinMode(LED_PWR, OUTPUT);
  pinMode(LED_RDYB, OUTPUT);
  pinMode(LED_RDYG, OUTPUT);

  // GPIO for controlling Power to the pump
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  // Flowmeter
  pinMode(FM_PIN, INPUT_PULLUP);
  #ifdef DEBUG
  Serial.println("Pins set");
  #endif
}


void setup_i2c() {
  Wire.begin();
  #ifdef DEBUG
  Serial.println("Setting up I2C...");
  #endif

  // Pressure sensor
  #ifdef IS_MS5837
  while (!p_sensor.init()) {
    #ifdef DEBUG
    Serial.println("pressure waiting...");
    #endif

    delay(1000); 
  }  
  #else
  p_sensor.init();
  #endif
  
  // Temperature sensor
  c_sensor.init();
  
  // Setup RTC
  if (!rtc.begin()) {
    #ifdef DEBUG
    Serial.println("Couldn't find RTC");
    #endif

    while (1);
  }
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
      Serial.println(t);
      #endif

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

  while (dir.next()) {
    // Assumption: We only have relevant data
    String f_name = dir.fileName();
    #ifdef DEBUG
    Serial.println(f_name);
    #endif
    int idx = f_name.indexOf('.');
    if (idx != 9) continue;

    // Only upload data of [uid].txt format
    String uid = f_name.substring(1, idx);
    String upload_url = home_url + "/deployment/upload/" + uid;
    
    File cur_file = dir.openFile("r");
    uint16_t f_size = dir.fileSize();
    // We need to turn data into multiple chunks
    uint8_t n_chunks = f_size / CHUNK_SIZE + 1;
    Serial.println(CHUNK_SIZE);
    Serial.println(f_size);
    Serial.println(n_chunks);
    
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
    // Delete the uploaded file
    SPIFFS.remove(f_name);
  }
}


void upload_new_deployment(String home_url) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String create_url = home_url + "/deployment/create/" + String(DEVICE_ID);
    Serial.println(DEVICE_ID);
    http.begin(create_url);
    http.addHeader("Content-Type", "text/plain");
    int httpCode = http.POST(eDNA_uid);
    // Persistent POST request to create the deployment
    while (httpCode != 200) {
      httpCode = http.POST(eDNA_uid);
    }
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
      Serial.println(http.getString());
      deserializeJson(jsonBuffer, http.getString());
      deployment_status = jsonBuffer["status"];
      Serial.println(deployment_status);
      if (deployment_status == DEPLOYMENT_RDY) {
        eDNA_uid = jsonBuffer["eDNA_UID"].as<String>();
        Serial.print("ujid:");
        Serial.println(eDNA_uid);
      }
    }
  }
  return deployment_status;
}

void query_deployment_configurations(String home_url) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String info_url = home_url + "/deployment/get_depth/" + eDNA_uid;
    // Keep trying at 1Hz until all of the parameters are configured for deployment
    while (target_depth == 0 || target_pump_wait == 0 \
    || target_flow_vol == 0 || target_flow_duration == 0 ) {
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
        target_depth = jsonBuffer["depth"];
        target_pump_wait = jsonBuffer["pump_wait"];
        target_flow_vol = convert_ml_to_ticks(jsonBuffer["flow_volume"]);
        target_flow_duration = jsonBuffer["flow_duration"];
        http.end();
        delay(1000);
      }
    }
  }
}
