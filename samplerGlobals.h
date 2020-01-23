/**
 * eDNA Sampler: ESP8266 based eDNA sampler / relevant sensor controller
 * samplerGlobals.h
 * January 2020
 * Authors: JunSu Jang
 *        
 *      [Descriptions]
 * This is a file that contains all of the relevant global variables.
 * These variables are exposed in this manner primarily so that
 * another developer can change them easily if he/she needs to.
 * For example, other details might be needed for different sensors 
 */

// **IMPORTANT**
// Please comment out DEBUG when out for deployment!! 
#define DEBUG 1
// Uncomment if using MS5837 pressure sensor
#define IS_MS5837


// Hardcoded device ID
#define DEVICE_ID 5             
// Pinouts
#define FM_PIN 14               // Flowmeter interrupt pin
#define PUMP_PIN 12             // GPIO pin to control Vpump
#define LED_PWR 15              // RED Led
#define LED_RDYB 16             // Blue LED
#define LED_RDYG 13             // Green LED

// WiFi Configuration
#define LOCAL_SSID "MIT"   //"RPI"        // SSID of the RPI host
#define LOCAL_PWD "" //"12345678"    // PWD of the RPI host
#define SERVER_IP "18.21.176.213" //"18.20.160.82" //"192.168.4.1" //for RPI; Junsu comp: 
#define WEB_PORT "5000"
#define CHUNK_SIZE 2048         // Data chunk size for uploading

// status global for sampler status
#define NOT_READY 0
#define READY 1
#define DEPLOYED 2
#define COMPLETE 3

// Pump state variables
#define IDLE 4
#define RUNNING 5
#define PUMP_OFF 7
#define PUMP_ON 8
// 10 seconds wait till max flowrate calculation
#define NUM_PUMP_COUNTS 10      

// Sensors
#ifdef IS_MS5837
#define MAX_DEPTH 300.f         // meters
#else // Keller Sensor
#define MAX_DEPTH 975.f         // meters
#endif
#define MIN_DEPTH 1.f           // meters
// specified by the temperature sensor
#define MAX_TEMPERATURE 125.0f  // Celcius
// Stop at 0.2L/min flowrate regardless of conditions
#define MIN_FLOWRATE 0.2f       // L/min 
// hardcoded name of the log file
#define LOG_FILE_PATH "/log.txt"
