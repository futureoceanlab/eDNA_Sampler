/**
 * eDNA Sampler: ESP8266 based eDNA sampler / relevant sensor controller
 * samplerWiFi.h
 * January 2020
 * Authors: JunSu Jang
 * email: junsuj@mit.edu
 *        
 *      [Descriptions]
 * SamplerWiFi handles all of the actions that require WiFi connection.
 * These are mainly configuration related. 
 */
#ifndef SamplerWiFi_h
#define SamplerWiFi_h


#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <String.h>
#include <FS.h>
#include <ArduinoJson.h>
#include "samplerGlobals.h"


class SamplerWiFi
{
    public:
        SamplerWiFi(void);
        // Make connection to the WiFi server. Flag failure if 
        // not able to connect in 10 seconds
        uint8_t connectWiFi(void);
        // Query deployment configuration data
        DynamicJsonDocument queryDeploymentConfiguration(String uid);
        // Let the server know that there is a new deployment upcoming
        void uploadNewDeployment(String uid);
        // Check if the device should be looking for a new deployment
        // or waiting for deployment
        DynamicJsonDocument checkDeploymentStatus(void);
        // get current time online
        time_t getTimeOnline(void);
        // upload the existing data
        void uploadData(String uid, File curFile, uint8_t nChunks);
        // upload the existing intenral log file
        void uploadInternalLog(String uid, File logFile, uint8_t nChunks);
    private:
        const char* _ssid;
        const char* _pwd;
        int _deviceID;
        String _homeUrl;

        // When uploading data, never stop trying to upload data
        // until it receives 200 (OK) from the server
        void _persistChunkUpload(String url, File curFile, uint8_t nChunks);

};

#endif