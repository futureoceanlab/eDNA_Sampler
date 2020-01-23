// #define UNITTEST
#ifdef UNITTEST

#include<iostream>
#include <sstream>
#include "sampler.h"
#include "samplerGlobals.h"

using namespace std;

#define START_TIME 0
#define TICKS_PER_L 4600
int numFailures = 0;
int numTests = 0;

void printTestResults(string testName, uint8_t numTestCases, uint8_t* testResults); 


/**
 * User configuration 
 */



/**
 *  Pump condition test
 */
// Start: depth
// End: time
void testDepth1(Sampler sampler){
    // Normal case
    // Start cases
    float tDepth = 20.f;                                                // m
    float depthBand = 5.f;                                              // m
    // End cases
    uint32_t waitPumpEnd = 5;                                           // min

    // SCENARIO
    // between pumpStart1 and pumpStart2, the device was outside of the target depth + band
    uint32_t pumpStart1 = 30;                                           // sec
    uint32_t pumpDuration1 = waitPumpEnd/2*60;                          // sec
    uint32_t pumpDuring1 = pumpStart1 + pumpDuration1;                  // sec
    uint32_t pumpStart2 = pumpStart1 + pumpDuration1 + 100;             // sec
    uint32_t pumpDuration2 = waitPumpEnd * 60 - 1;                      // sec
    uint32_t pumpDuring2 = pumpStart2 + pumpDuration2 - pumpDuration1;  // sec
    uint32_t pumpDuration3 = waitPumpEnd * 60 + 1;                          // sec  
    uint32_t pumpDuring3 = pumpStart2 + pumpDuration3 - pumpDuration2;  // sec

    uint8_t numTestCases = 6;
    uint8_t testResults[numTestCases] = {0};

    sampler.setDeploymentConfig(0.f, tDepth, depthBand, ABS_ZERO_C, 0.f, waitPumpEnd, 0, 0, TICKS_PER_L);
    testResults[0] = sampler.isValidUserConfig() == 1;
    testResults[1] = (sampler.checkPumpTrigger(23.f, 13.f, pumpStart1, 0, 0) == PUMP_ON);
    sampler.setPumpStartTime(pumpStart1);

    testResults[2] = (sampler.checkPumpTrigger(26.f, 13.f, pumpDuring1, 4600, pumpDuration1) == PUMP_OFF);
    testResults[3] = (sampler.checkPumpTrigger(18.5f, 13.f, pumpDuring2, 4600, pumpDuration2) == PUMP_ON);
    sampler.setPumpStartTime(pumpStart2);

    testResults[4] = (sampler.checkPumpTrigger(13.f, 13.f, pumpDuring2, 4600, pumpDuration2) == PUMP_OFF);
    testResults[5] = (sampler.checkPumpTrigger(19.2f, 13.f, pumpDuring3, 4600, pumpDuration3) == PUMP_OFF);

    printTestResults("Depth1", numTestCases, testResults);
}

// Start: temperature
// End: time
void testTemperature1(Sampler sampler){
    // Normal case
    // Start cases
    float tTemp = 13.f;                                                // C
    float tempBand = 2.f;                                              // C
    // End cases
    uint32_t waitPumpEnd = 5;                                           // min

    // SCENARIO
    // between pumpStart1 and pumpStart2, the device was outside of the target depth + band
    uint32_t pumpStart1 = 30;                                           // sec
    uint32_t pumpDuration1 = waitPumpEnd/2*60;                          // sec
    uint32_t pumpDuring1 = pumpStart1 + pumpDuration1;                  // sec
    uint32_t pumpStart2 = pumpStart1 + pumpDuration1 + 100;             // sec
    uint32_t pumpDuration2 = waitPumpEnd * 60 - 1;                      // sec
    uint32_t pumpDuring2 = pumpStart2 + pumpDuration2 - pumpDuration1;  // sec
    uint32_t pumpDuration3 = waitPumpEnd * 60 + 1;                          // sec  
    uint32_t pumpDuring3 = pumpStart2 + pumpDuration3 - pumpDuration2;  // sec

    uint8_t numTestCases = 6;
    uint8_t testResults[numTestCases] = {0};

    sampler.setDeploymentConfig(0.f, 0.f, 0.f, tTemp, tempBand, waitPumpEnd, 0, 0, TICKS_PER_L);
    testResults[0] = sampler.isValidUserConfig() == 1;
    testResults[1] = (sampler.checkPumpTrigger(23.f, 13.f, pumpStart1, 0, 0) == PUMP_ON);
    sampler.setPumpStartTime(pumpStart1);

    testResults[2] = (sampler.checkPumpTrigger(26.f, 10.95f, pumpDuring1, 4600, pumpDuration1) == PUMP_OFF);
    testResults[3] = (sampler.checkPumpTrigger(18.5f, 14.f, pumpDuring2, 4600, pumpDuration2) == PUMP_ON);
    sampler.setPumpStartTime(pumpStart2);

    testResults[4] = (sampler.checkPumpTrigger(13.f, 17.f, pumpDuring2, 4600, pumpDuration2) == PUMP_OFF);
    testResults[5] = (sampler.checkPumpTrigger(19.2f, 13.f, pumpDuring3, 4600, pumpDuration3) == PUMP_OFF);

    printTestResults("Temperature1", numTestCases, testResults);
}


// Start: time
// End: time
void testTime1(Sampler sampler)
{
    // Start case
    uint32_t waitPumpStart = 2; //min
    // End case
    uint32_t waitPumpEnd = 2; //min

    // Scenario
    // depth and temperature changes upon 2min after deployment and stops pump 
    // after another 2min
    uint32_t t1 = 60; 
    uint32_t t2 = waitPumpStart*60;
    uint32_t t3 = t2 + waitPumpEnd/2;
    uint32_t t4 = t2 + waitPumpEnd + 1;
    uint32_t pumpDuration2 = t2 - t2;
    uint32_t pumpDuration3 = t3 - t2;
    uint32_t pumpDuration4 = t4 - t2;

    uint8_t numTestCases = 5;
    uint8_t testResults[numTestCases] = {0};

    sampler.setDeploymentConfig(0.f, 0.f, 0.f, ABS_ZERO_C, 0.f, waitPumpEnd, waitPumpStart, 0, TICKS_PER_L);
    
    testResults[0] = sampler.isValidUserConfig() == 1;
    testResults[1] = sampler.checkPumpTrigger(13.f, 13.f, t1, 0, 0) == PUMP_OFF;
    testResults[2] = sampler.checkPumpTrigger(26.f, 4.f, t2, 0, pumpDuration2) == PUMP_ON;
    sampler.setPumpStartTime(t2);
    testResults[3] = sampler.checkPumpTrigger(130.f, 9.f, t3, 0, pumpDuration3) == PUMP_ON;
    testResults[4] = sampler.checkPumpTrigger(600.f, 10.f, t4, 0, pumpDuration4) == PUMP_ON;

    printTestResults("Time1", numTestCases, testResults);
}


// Start: depth or wait time
// End: max volume
void testVolume1(Sampler sampler)
{
    // Start case
    float tDepth = 200.f;                                               // m
    float depthBand = 5.f;                                              // m
    uint32_t waitPumpStart = 1; //min
    uint32_t pumpRate = 1; // L/sec

    // End case
    uint32_t maxVol = 20;                                               // L

    //Scenario
    // We expected to reach 200m in 60seconds, but actually arrived in 45 seconds
    // pump until 20L and stops pumping even in the depth range
    uint32_t t1 = 20;
    uint32_t t2 = 45;
    uint32_t t3 = 60;
    uint32_t pumpDuration3 = t3 - t2;
    uint32_t pumpAmount3 = pumpRate * TICKS_PER_L; // ticks
    
    uint32_t pumpDuration4 = maxVol / pumpRate;
    uint32_t pumpAmount4 = maxVol * TICKS_PER_L; // ticks
    uint32_t t4 = t3 + pumpDuration4;
    uint32_t t5 = 200; //sec
    uint8_t numTestCases = 6;
    uint8_t testResults[numTestCases] = {0};

    sampler.setDeploymentConfig(0.f, tDepth, depthBand, ABS_ZERO_C, 0.f, 0, waitPumpStart, maxVol, TICKS_PER_L);

    testResults[0] = sampler.isValidUserConfig() == 1;
    testResults[1] = sampler.checkPumpTrigger(13.f, 13.f, t1, 0, 0) == PUMP_OFF;
    testResults[2] = sampler.checkPumpTrigger(200.f, 13.f, t2, 0, 0) == PUMP_ON;
    sampler.setPumpStartTime(t2);
    testResults[3] = sampler.checkPumpTrigger(202.f, 13.f, t3, pumpAmount3, pumpDuration3) == PUMP_ON;
    testResults[4] = sampler.checkPumpTrigger(199.5f, 13.f, t4, pumpAmount4, pumpDuration4) == PUMP_OFF;
    testResults[5] = sampler.checkPumpTrigger(199.5f, 13.f, t5, pumpAmount4, pumpDuration4) == PUMP_OFF;

    printTestResults("Volume1", numTestCases, testResults);
}

// Start: depth or time
// End: min flowrate


// Start: temperature + time
// End: temperature

/**
 * flowarte computation
 */


/**
 * maximum flowrate computation
 */

void printTestResults(string testName, uint8_t numTestCases, uint8_t* testResults) 
{
    string str;
    ostringstream temp;

    for (int i = 0; i < numTestCases; i++) {
        temp << (int) (testResults[i] == 1);
        str = temp.str();
        cout << "Test " << testName << ": CASE " << i;
        if (testResults[i] == 1) {
            cout << " PASS" << endl;
        } else {
            numFailures++;
            cout << " FAILED*" << endl;
        }
        numTests++;
    }
}

int main()
{
    cout << "Hello, these are unit tests for eDNA Sampler" << endl;
    Sampler sampler;
    sampler.setDiveStartTime(START_TIME);
    testDepth1(sampler);
    testTemperature1(sampler);
    testTime1(sampler);
    testVolume1(sampler);
    cout << "Test ended with " << numFailures << " failures out of " << numTests << " tests" << endl;
    return 0;
    
}
#endif
