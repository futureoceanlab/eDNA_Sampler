/**
 * eDNA Sampler: ESP8266 based eDNA sampler / relevant sensor controller
 * samplerHelper.h
 * January 2020
 * Authors: JunSu Jang
 *        
 *      [Descriptions]
 * These are helper functions that do not play integral role in the main 
 * program.
 */

#ifndef SamplerHelper_h
#define SamplerHelper_h

#include "Arduino.h"
#include "samplerGlobals.h"
#include <String.h>

// LED control functions
void blinkAllLEDs(uint32_t period);
void blinkSingleLED(int led, uint32_t period);
void turnOnLED(int led);
void turnOffLED(int led);
// When there is an error, flag it by blinking all LEDs
void flagErrorLED();
// Retreive string concatenation for file path for data file
String createDataFilePath(String uid);

#endif