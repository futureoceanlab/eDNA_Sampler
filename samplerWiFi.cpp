#include "samplerWiFi.h"

#define WIFI_WAIT 20

const size_t bufferSize = JSON_OBJECT_SIZE(2) + \
    JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(5) + JSON_OBJECT_SIZE(8) + 370;

SamplerWiFi::SamplerWiFi() 
{
    _ssid = LOCAL_SSID;
    _pwd = LOCAL_PWD;
    _deviceID = DEVICE_ID;
    _homeUrl = "http://";
    _homeUrl.concat(SERVER_IP);
    _homeUrl.concat(':');
    _homeUrl.concat(WEB_PORT);
}

uint8_t SamplerWiFi::connectWiFi() 
{
    WiFi.scanDelete();
    WiFi.begin(_ssid, _pwd);
    uint8_t wifi_attempt = 0;
    while ((WiFi.status() != WL_CONNECTED) && (wifi_attempt < WIFI_WAIT)) {
        delay(1000);  
        wifi_attempt++;
        #ifdef DEBUG
        Serial.print(".");
        #endif
    }
    if (wifi_attempt == WIFI_WAIT) {
        return 0;
    }
    return 1;
}


DynamicJsonDocument SamplerWiFi::queryDeploymentConfiguration(String uid) {
    HTTPClient http;
    String infoUrl = _homeUrl + "/deployment/get_config/" + uid;
    // Keep trying at 1Hz until all of the parameters are configured for deployment
    http.begin(infoUrl);
    // Deserialize Json file
    DynamicJsonDocument jsonBuffer(bufferSize);
    int httpCode = http.GET();
    while (httpCode != 200) {
        delay(1000);
        httpCode = http.GET();
    }
    deserializeJson(jsonBuffer, http.getString());
    http.end();
    return jsonBuffer;
}

void SamplerWiFi::uploadNewDeployment(String uid) {
    HTTPClient http;
    String createUrl = _homeUrl + "/deployment/create/" + String(_deviceID);
    http.begin(createUrl);
    http.addHeader("Content-Type", "text/plain");
    int httpCode = http.POST(uid);
    // Persistent POST request to create the deployment
    while (httpCode != 200) {
        delay(1000);
        httpCode = http.POST(uid);
        #ifdef DEBUG
        Serial.println("Uploading failed...");
        #endif
    }
    http.end();
}

DynamicJsonDocument SamplerWiFi::checkDeploymentStatus() {
    uint8_t deploymentStatus = NOT_READY;
    HTTPClient http;
    String checkUrl = _homeUrl + "/deployment/has_deployment/" + String(_deviceID);
    http.begin(checkUrl);
    DynamicJsonDocument jsonBuffer(bufferSize);
    int httpCode = http.GET();
    while (httpCode != 200) {
        delay(1000);
        httpCode = http.GET();
    }
    deserializeJson(jsonBuffer, http.getString());
    http.end();
    return jsonBuffer;
}

// synchornize the RTC with the time of the webserver
time_t SamplerWiFi::getTimeOnline() {
    time_t t;
    HTTPClient http;
    String timeUrl = _homeUrl + "/deployment/datetime/now";
    http.begin(timeUrl);
    DynamicJsonDocument jsonBuffer(bufferSize);

    int httpCode = http.GET();
    // Persist to get the time on the webserver. 
    while (httpCode != 200) {
        delay(1000);
        httpCode = http.GET();
    }
    deserializeJson(jsonBuffer, http.getString());
    http.end();
    t = (time_t) jsonBuffer["now"];
    return t;
}

void SamplerWiFi::uploadData(String uid, File curFile, uint8_t nChunks)
{
    String uploadUrl = _homeUrl + "/deployment/upload/" + uid;
    _persistChunkUpload(uploadUrl, curFile, nChunks);
}

void SamplerWiFi::uploadInternalLog(String uid, File curFile, uint8_t nChunks)
{
    String uploadLogUrl = _homeUrl + "/deployment/upload-log/" + uid;
    _persistChunkUpload(uploadLogUrl, curFile, nChunks);

}

// Upload data file, broken into chunks, to the webserver
void SamplerWiFi::_persistChunkUpload(String url, File curFile, uint8_t nChunks) {
    char tempBytes[CHUNK_SIZE];
    // Upload to the server
    HTTPClient http;

    http.begin(url);
    http.addHeader("Content-Type", "text/plain");
    http.addHeader("Chunks", String(nChunks));

    uint16_t bytesRead = 0;
    // bookkeeping parameter to let the webserver know how much is left
    uint8_t nthChunk = 1;
    // Upload data until the file is EOF
    while (curFile.available()) {
        // Upload chunk by chunk
        while (curFile.available() && bytesRead < CHUNK_SIZE) {
            tempBytes[bytesRead] = curFile.read();
            bytesRead++;
        }
        http.addHeader("Data-Bytes", String(bytesRead));
        http.addHeader("Nth", String(nthChunk));
        int httpCode = http.POST(tempBytes);

        // Persistent request to upload the file
        while (httpCode != 200) {
            delay(1000);
            httpCode = http.POST(tempBytes);
        }
        // Reset/update the local parameters
        bytesRead = 0;
        nthChunk++;
    }
    http.end();

}
