/**
 * eDNA Sampler: ESP8266 based eDNA sampler / relevant sensor controller
 * sampler.h
 * January 2020
 * Authors: JunSu Jang
 *        
 *      [Descriptions]
 * Sampler is a class that that mainly handles the behavior of the device.
 * It verifies that the input deployment configuration is appropriate,
 * and lets the main system know when to trigger/pause the pump.
 */

#ifndef Sampler_h
#define Sampler_h

#include <cstdlib>
#include <stdint.h>
#include <limits.h>
#include <float.h>
#include "samplerGlobals.h"

// Pump condition global variables
#define N_START_COND 3          // Number of pump starting conditions
#define N_END_COND 3            // Number of pump ending conditions
#define NUM_FLOW_LOGS 5         // Num data for computing derivate of ticks
#define ABS_ZERO_C -273.15f     // default temperature value


class Sampler
{
    public:
        Sampler(void);
        // verifies that the user configuration provided is indeed valid
        uint8_t isValidUserConfig();
        // check to see if pump should be on/off
        uint8_t checkPumpTrigger(float depth, 
                                float temperature, 
                                uint32_t time_now, 
                                uint32_t ticks,
                                uint32_t pumpDuration);
        // set the deployment configuration variables
        void setDeploymentConfig(float minFlowrate, 
                                 float targetDepth,
                                 float depthBand,
                                 float targetTemperature,
                                 float temperatureBand,
                                 uint32_t waitPumpEnd,
                                 uint32_t waitPumpStart,
                                 uint32_t targetFlowVol,
                                 uint32_t ticksPerLiter);
        // set the dive start time (submerge below 2m)
        void setDiveStartTime(uint32_t diveStartTime);
        // set when the pump started
        void setPumpStartTime(uint32_t pumpStartTime);
        // keep track of the flowrate
        void updateCurrentFlowrate(uint32_t flowData);
        // compute the inital flowrate to help track the flowrate decline
        void computeMaxFlowrate(uint32_t flowData);
        // getter for the flowrate
        float getCurFlowrate(void);

    private:
        // user defined deployment configuration variables
        float _minFlowrate = MIN_FLOWRATE;         // L/min
        uint32_t _waitPumpEnd = UINT_MAX;          // seconds
        uint32_t _targetFlowVol = UINT_MAX;        // ticks 
        uint32_t _waitPumpStart = UINT_MAX;        // seconds after dive
        float _temperatureBand = 0.f;              // +/-100deg C pre-specified for maximum range
        float _targetTemperature = ABS_ZERO_C;     // deg Celcius
        float _depthBand = 0;                      // meters
        float _targetDepth = FLT_MAX;              // meters
        
        // Flowmeter information provided by the user
        uint32_t _ticksPerLiter = 0;               // ticks per Liter
        float _maxFlowrate = 0.f;                  // maximum flowrate

        // deployment pump trigger condition flags array
        // specifies which conditions are to be checked for in order to 
        // start and pause the pump
        uint8_t _startConditions[N_START_COND];    
        uint8_t _endConditions[N_END_COND];
        // masks are used to determine if any pump control condition has been met
        uint8_t _startConditionMask[N_END_COND];
        uint8_t _endConditionMask[N_START_COND];

        // unixtime to keep track of when the dive and pump started
        uint32_t _diveStartTime = UINT_MAX;
        uint32_t _pumpStartTime = UINT_MAX;

        // log of flow values for derivative calculation
        uint32_t _flowLog[NUM_FLOW_LOGS] = {0};
        // index of the currently accumulated ticks
        uint8_t _curFlowIdx = 0;
        // keep track of the flowrate internally
        float _curFlowrate = 0.f; 
};

#endif
