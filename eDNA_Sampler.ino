/**
 * eDNA Sampler: ESP8266 based eDNA sampler / relevant sensor controller
 * January 2020
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
 * 
 *     Pump Starting Conditions
 *     i) "Target Depth" (meters)
 *        - Pump at this depth within the "Depth Band"
 *     ii) "Target Temperature" (Celcius)
 *        - Pump in this thermocline within the "Temperature Band"
 *     iii) "Wait Time" upon 2m submersion (minutes):
 *        - Depending on the deployment, it may not reach the depth, 
 *          in which case, we start pumping after "Wait Time"
 *
 *     Pump Terminate Conditions
 *     i) "Target Flow Volume" (Liter)
 *        - We will pump water until target volume has been passed 
 *          through the filter
 *     ii) "Minimum Flowrate" (Liters/min)
 *        - Stop when the filter is clogged
 *     iii) "Target Flow Duration" (minutes)
 *        - Our filter may be full before the target volume has been met. 
 *          We stop pumping after specified "Target Flow Duration"
 * 2. Start logging time, pressure, temperature, volume and flowrate at 1Hz and
 *    log it to the file created
 * 3. Control the pump according to the configuration provided
 * 4. Keep measure flow, time, pressure and temperature at 1Hz until the power is off
 * 
 *      [LED Encoding]
 * 
 * - Power on --> RED on
 * - Wait for RFID --> BLUE blink
 * - Wait for deployment config --> GREEN blink
 * - All ready -- GREEN on
 * 
 *      [SPIFFS data filesystem]
 * 
 * Filename: [RFID_UID].txt
 * Logged data: [unix timestamp],[pressure],[temperature],[volume],[flowrate]\r\n
 * There is also an intenral log file that allows users to debug any issues
 * during deployment. 
*/

// Native libraries
#include <Wire.h>
#include <Ticker.h>
#include <String.h>
#include <FS.h>
#include <NfcAdapter.h>
#include <PN532/PN532/PN532.h>
#include <PN532/PN532_I2C/PN532_I2C.h>
#include <ArduinoJson.h>
#include <limits.h>
// External libraries
#include "TimeLib.h"
#include "RTClib.h"
#include "MS5837.h"
#include "KellerLD.h"
#include "TSYS01.h"
// Custom libraries
#include "sampler.h"                // verifies conditions for pump control
#include "samplerHelper.h"          // helper functions
#include "samplerWiFi.h"            // module that handles WiFi connection/comms
#include "samplerGlobals.h"         // contains all of the global variables

/**
 *  Data variables
 */
// Flowmeter
volatile uint32_t flowCounter = 0;  // flowmeter interrupt update variable
volatile uint32_t curTicks = 0;     // Stamped number of accumulated flowmeter ticks 
volatile uint32_t curTime;          // current time
float curDepth = 0.f;               // depth in meters
float curTemperature = 0.f;         // temperature in Celcius
float curFlowrate = 0.f;            // flowrate in L/min

/**
 * Peripherals
 */
// RTC
RTC_DS3231 rtc;
// Pressure
#ifdef IS_MS5837
MS5837 pSensor;
#else // Keller Sensor
KellerLD pSensor;
#endif
// Temperature
TSYS01 tSensor;
// RFID
PN532_I2C pn532_i2c(Wire);
NfcAdapter rfidReader = NfcAdapter(pn532_i2c);
String eDNA_UID;                    // UID of the RFID tag attached to sampler
// Timer for 1Hz log / LED
Ticker ticker;
// Pump
uint32_t pumpDuration = 0;          // number of seconds after pump started
                                    // to calculate initial pump max. flowrate

/**
 *  flags
 */
uint8_t fSamplerStatus = NOT_READY;
uint8_t fPumpStatus = IDLE;
uint8_t fLogData = NOT_READY;

/**
 *  Logger
 */
String dataFilePath;
String logFilePath = "/log.txt";

/**
 *  Sampler helper instance declaration
 */
Sampler sampler = Sampler();
SamplerWiFi samplerWiFi = SamplerWiFi();

/**
 * Helper function declartions
 */
// setup relevant GPIO pins
void setupPins(void);
// Setup I2C devices
void setupI2c(void);

// Flowmeter Interrupt handler
static void ICACHE_RAM_ATTR isr_flowmeter(void);
// stamp the current flowmeter reading and time
void stampTicks(void);
// read all the peripheral data and store them in global curX variables
void readData(void);
// write the data onto the data file
void writeData(void);

// Wait for RFID to be read and set the eDNA_UID
void waitRFID(void);
// Control the pump action based on the conditions
void controlPump(void);

// synchornize the RTC with the time of the webserver
void synchronizeRTC(void);
// Check if deployment has been configured (i.e. RFID scanned)
uint8_t getDeploymentStatus(void);
// Query the deployment configuration from the webserver
void getDeploymentConfiguration(void);
// Upload data file, broken into chunks, to the webserver
void uploadExistingData(void);
// Similarly, upload log file
void uploadExistingLog(void);
// Helper function to concat data file path String
String createDataFilePath(void);

// Trigger the ticker to measure data every second
void startMeasuringData(void);
// line to log internally any information
void internalLogLine(String logData);
// Wrapper for outputing the message to the correct output
void outputDebugMessage(String msg);


/**
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
    SPIFFS.begin();

    setupPins();
    turnOnLED(LED_PWR);
    outputDebugMessage("Trying to connect to WiFi");
    uint8_t online = samplerWiFi.connectWiFi();
    if (!online) {
        internalLogLine("Wifi connection failed after 10 attempts!");
        flagErrorLED();
    }
    outputDebugMessage("WiFi connected");
    outputDebugMessage("Setting up I2C...");
    
    setupI2c();
    synchronizeRTC();
    uploadExistingData();
    uploadExistingLog();
    fSamplerStatus = getDeploymentStatus();

    switch (fSamplerStatus) {
        case NOT_READY:
            rfidReader.begin();
            blinkSingleLED(LED_RDYB, 500);
            waitRFID();
            turnOnLED(LED_RDYB);
            samplerWiFi.uploadNewDeployment(eDNA_UID);
            turnOffLED(LED_RDYB);
            blinkSingleLED(LED_RDYG, 500);
            getDeploymentConfiguration();
            break;
        case READY:
            blinkSingleLED(LED_RDYG, 500);
            getDeploymentConfiguration();
            dataFilePath = createDataFilePath();
            startMeasuringData();
            outputDebugMessage("Wait for deployment...");
            break;
        default:
            break;
    }
    turnOnLED(LED_RDYG);
}

/***********************************************************
 * 
 *     loop()
 * 
 *   If log data flag is set, do the following:
 *    - read from sensors
 *    - Save the data
 *   Depending on famplerStatus, act accordingly
 *   READY:
 *      Waiting to be deployed underwater (below 2m)
 *   DEPLOYED:
 *      Control the pump until surface out of the water (above 2m)
 *   COMPLETE:
 *      Out of water, just log the data until powered off
 * 
 */
void loop() {
    // Log data and check device status/action every second
    if (fLogData == READY) {
        readData();
        writeData();
        fLogData = NOT_READY;
        switch (fSamplerStatus) {
            case READY:
                #ifdef DEBUG
                sampler.setDiveStartTime(curTime);
                fSamplerStatus = DEPLOYED;
                outputDebugMessage("Deployed");
                #else
                if (curDepth > MIN_DEPTH) {
                // Finally, the device is submerged underwater!
                    sampler.setDiveStartTime(curTime);
                    // Turn off all of the LEDs in the water
                    turnOffLED(LED_PWR);
                    turnOffLED(LED_RDYG);
                    fSamplerStatus = DEPLOYED;
                }
                #endif
                break;
            case DEPLOYED:
                controlPump();
                #ifndef DEBUG
                if (curDepth <= MIN_DEPTH) {
                // Out of the water
                    fSamplerStatus = COMPLETE;
                    // Turn the LEDs back on
                    turnOnLED(LED_PWR);
                    turnOnLED(LED_RDYG);
                    // Turn off the pump just in case
                    digitalWrite(PUMP_PIN, LOW);
                }
                #endif
                break;
            case COMPLETE:
            // Nothing to do here but wait until power-cycled to upload
            // the newly collected data
                break;
            default:
                break;
        }
    }
}

void readData() {
    sampler.updateCurrentFlowrate(curTicks);
    curFlowrate = sampler.getCurFlowrate();
    // Pressure / Depth
    pSensor.read();
    curDepth = pSensor.depth();
    // Temperature
    tSensor.read();
    curTemperature  = tSensor.temperature();
}

void writeData() {
    // Write data to the data log file
    File dataFile = SPIFFS.open(dataFilePath, "a+");
    dataFile.print(curTime);
    dataFile.print(',');
    dataFile.print(curDepth);
    dataFile.print(',');
    dataFile.print(curTemperature);
    dataFile.print(',');
    dataFile.print(curTicks);
    dataFile.print(',');
    dataFile.println(curFlowrate);
    dataFile.close();
    #ifdef DEBUG
    Serial.print(curTime);
    Serial.print(", ");
    Serial.print(curDepth);
    Serial.print(", ");
    Serial.print(curTemperature);
    Serial.print(", ");
    Serial.print(curTicks);
    Serial.print(", ");
    Serial.println(curFlowrate);
    #endif
}

void controlPump() {
    uint8_t pumpAction = sampler.checkPumpTrigger(curDepth, curTemperature, curTime, curTicks, pumpDuration);
    if (fPumpStatus == IDLE && pumpAction == PUMP_ON) {
        sampler.setPumpStartTime(curTime);
        digitalWrite(PUMP_PIN, HIGH);
        fPumpStatus = RUNNING;
        outputDebugMessage("PUMP ON");
    } else if (fPumpStatus == RUNNING && pumpAction == PUMP_OFF) {
        digitalWrite(PUMP_PIN, LOW);
        fPumpStatus = IDLE;
        outputDebugMessage("PUMP OFF");
    }
    // After 10 seconds since pumping, measure the maximum flowrate
    if (fPumpStatus == RUNNING) {
        if (pumpDuration == NUM_PUMP_COUNTS) {
            sampler.computeMaxFlowrate(curTicks);
        }
        pumpDuration++;
    }
}


String createDataFilePath() {
    String dataFile = "/";
    dataFile.concat(eDNA_UID);
    dataFile.concat(".txt");
    #ifdef DEBUG
    Serial.print("Data file name: ");
    Serial.println(dataFile);
    #endif
    return dataFile;
}


void setupPins() {
    // GPIO for LED Indicators (OUTPUT)
    pinMode(LED_PWR, OUTPUT);
    pinMode(LED_RDYB, OUTPUT);
    pinMode(LED_RDYG, OUTPUT);

    turnOffLED(LED_PWR);
    turnOffLED(LED_RDYB);
    turnOffLED(LED_RDYG);

    // GPIO for controlling Power to the pump
    pinMode(PUMP_PIN, OUTPUT);
    digitalWrite(PUMP_PIN, LOW);

    // Flowmeter
    pinMode(FM_PIN, INPUT_PULLUP);
    attachInterrupt(FM_PIN, isr_flowmeter, FALLING);

    outputDebugMessage("Pins set");
}


void setupI2c() {
    Wire.begin();

    // RTC
    if (!rtc.begin()) {
        outputDebugMessage("Couldn't find RTC.");
        flagErrorLED();
    }
    // RTC has a new/replaced battery
    if (rtc.lostPower()) {
        outputDebugMessage("RTC lost power, lets set the time!");
        // following line sets the RTC to the date & time this sketch was compiled 
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    outputDebugMessage("RTC setup successful.");
    // Temperature 
    tSensor.init();
    outputDebugMessage("Temperature setup SUCCESS");

    // Pressure sensor
    #ifdef IS_MS5837
    uint8_t p_init_attempts = 0;
    while (!pSensor.init() && p_init_attempts < 5) {
        delay(1000); 
        p_init_attempts++;
    }
    if (p_init_attempts == 5) {
        outputDebugMessage("Sensor MS5837 setup failed after 5 attempts.");
        flagErrorLED();
    }
    #else
    pSensor.init();
    #endif

    outputDebugMessage("Pressure sensor setup successful.");
    outputDebugMessage("I2C setup complete!");
}


void waitRFID(void) {
  outputDebugMessage("RFID Waiting");
  // Block until there is a tag present
  while (!(rfidReader.tagPresent())) {}

  NfcTag tag = rfidReader.read();
  // Set UID parameter
  eDNA_UID = tag.getUidString();
  eDNA_UID.replace(" ", "");
  outputDebugMessage(eDNA_UID);
}


static void ICACHE_RAM_ATTR isr_flowmeter() {
  flowCounter++;
}

void startMeasuringData() {
    outputDebugMessage("**START LOGGING DATA");
    ticker.attach_ms(1000, stampTicks);
}

void stampTicks() {
  cli();
  curTicks = flowCounter;
  curTime = rtc.now().unixtime();
  sei();
  fLogData = READY;
}


void internalLogLine(String logData) {
  File logFile = SPIFFS.open(logFilePath, "a+");
  logFile.print(rtc.now().unixtime());
  logFile.print(": ");
  logFile.println(logData);
  logFile.close();
}

void getDeploymentConfiguration() {
    while (!sampler.isValidUserConfig()) {
        DynamicJsonDocument jsonBuffer = samplerWiFi.queryDeploymentConfiguration(eDNA_UID);

        // Parameters for deployment configuration
        float minFlowrate = jsonBuffer["min_flowrate"]; // L/min
        float targetDepth = jsonBuffer["depth"];
        float depthBand = jsonBuffer["depth_band"];
        float targetTemperature = jsonBuffer["temperature"];
        float temperatureBand = jsonBuffer["temp_band"];
        uint32_t waitPumpEnd = jsonBuffer["wait_pump_end"]; // min
        uint32_t waitPumpStart = jsonBuffer["wait_pump_start"]; // min
        uint32_t targetFlowVol = jsonBuffer["flow_volume"]; // ticks
        uint32_t ticksPerLiter = jsonBuffer["ticks_per_L"];
        #ifdef DEBUG
        Serial.print(minFlowrate);
        Serial.print(", ");
        Serial.print(targetDepth);
        Serial.print(", ");
        Serial.print(depthBand);
        Serial.print(", ");
        Serial.print(targetTemperature);
        Serial.print(", ");
        Serial.print(temperatureBand);
        Serial.print(", ");
        Serial.print(waitPumpEnd);
        Serial.print(", ");
        Serial.print(waitPumpStart);
        Serial.print(", ");
        Serial.print(targetFlowVol);
        Serial.print(", ");
        Serial.println(ticksPerLiter);
        #endif
        sampler.setDeploymentConfig(minFlowrate, 
                                    targetDepth,
                                    depthBand,
                                    targetTemperature,
                                    temperatureBand,
                                    waitPumpEnd,
                                    waitPumpStart,
                                    targetFlowVol,
                                    ticksPerLiter);  
        delay(1000);

    }

}

uint8_t getDeploymentStatus() {
    DynamicJsonDocument jsonBuffer = samplerWiFi.checkDeploymentStatus();

    uint8_t deploymentStatus = jsonBuffer["status"];
    if (deploymentStatus == READY) {
        eDNA_UID = jsonBuffer["eDNA_UID"].as<String>();
        outputDebugMessage(eDNA_UID);
    }
    return deploymentStatus;
}

void synchronizeRTC() {
    time_t tOnline = samplerWiFi.getTimeOnline();
    #ifdef DEBUG
    Serial.print("Time: ");
    Serial.println(tOnline);
    #endif
    outputDebugMessage("Synchronized time with the server!");
    // Adjust RTC with the webserver time
    DateTime new_now = DateTime(year(tOnline), month(tOnline), \
        day(tOnline), hour(tOnline), minute(tOnline), second(tOnline));
    rtc.adjust(new_now);
}

// Upload data file, broken into chunks, to the webserver
void uploadExistingData() {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
        // Assumption: We only have relevant data
        String fileName = dir.fileName();
        int idx = fileName.indexOf('.');
        // We assume that the proper data file name is simply
        // the uid which has 8 characters (e.g. "/abcdef12.txt")
        if (idx == 9){
            // Only upload data of [uid].txt format
            eDNA_UID = fileName.substring(1, idx);    
    
            File curFile = dir.openFile("r");
            uint16_t fileSize = dir.fileSize();
            // We need to turn data into multiple chunks
            uint8_t nChunks = fileSize / CHUNK_SIZE + 1;
            samplerWiFi.uploadData(eDNA_UID, curFile, nChunks);
            // Delete the uploaded file
            SPIFFS.remove(fileName);
        }
    }
}


void uploadExistingLog() {
    if ((SPIFFS.exists(logFilePath)) && (eDNA_UID.length() != 0)) {        
        File logFile = SPIFFS.open(logFilePath, "r");
        uint16_t fileSize = logFile.size();
        // We need to turn data into multiple chunks
        uint8_t nChunks = fileSize / CHUNK_SIZE + 1;
        samplerWiFi.uploadInternalLog(eDNA_UID, logFile, nChunks);
        logFile.close();
        SPIFFS.remove(logFilePath);
    }
}

void outputDebugMessage(String msg) {
    #ifdef DEBUG
    Serial.println(msg);
    #endif
    internalLogLine(msg);
}
