#include "sampler.h"
#include <math.h>

Sampler::Sampler()
{
}

void Sampler::setDeploymentConfig(float minFlowrate, 
                             float targetDepth,
                             float depthBand,
                             float targetTemperature,
                             float temperatureBand,
                             uint32_t waitPumpEnd,
                             uint32_t waitPumpStart,
                             uint32_t targetFlowVol,
                             uint32_t ticksPerLiter)
{
    // Parameters for deployment configuration
    _ticksPerLiter = ticksPerLiter;

    _targetDepth = targetDepth > 0 ? targetDepth : FLT_MAX;
    _depthBand = depthBand;
    _targetTemperature = targetTemperature > ABS_ZERO_C ? targetTemperature : ABS_ZERO_C;
    _temperatureBand = temperatureBand;
    _waitPumpStart = waitPumpStart * 60; // seconds
    
    _minFlowrate = minFlowrate*ticksPerLiter; // ticks/min
    _waitPumpEnd = waitPumpEnd > 0 ? (waitPumpEnd * 60) : UINT_MAX; // sec
    _targetFlowVol = targetFlowVol > 0 ? (uint32_t)(targetFlowVol * _ticksPerLiter) : UINT_MAX;
}

uint8_t Sampler::isValidUserConfig()
{
  // 0. depth
  _startConditionMask[0] = ((_targetDepth >= MIN_DEPTH) 
                     && (_targetDepth < MAX_DEPTH) 
                     && (_depthBand > 0.f));
  // 1. temperature
  _startConditionMask[1] = ((_targetTemperature > ABS_ZERO_C) 
              && (_targetTemperature < MAX_TEMPERATURE) 
              && (_temperatureBand > 0.f));
  // 2. Wait duration after dive started
  _startConditionMask[2] = _waitPumpStart > 0 && _waitPumpStart < UINT_MAX;

  // 0. Pump volume
  _endConditionMask[0] = _targetFlowVol > 0 && _targetFlowVol < UINT_MAX;
  // 1. Pump duration
  _endConditionMask[1] = _waitPumpEnd > 0 && _waitPumpEnd < UINT_MAX;
  // 2. Minimum Flowrate until stop
  _endConditionMask[2] = _minFlowrate >= (MIN_FLOWRATE * _ticksPerLiter);
  
  uint8_t isValidFlowmeter = _ticksPerLiter > 0;
  uint8_t isPumpTrigger = 0, isPumpStop = 0;
  for (uint8_t i = 0; i < N_START_COND; i++) {
    isPumpTrigger |= _startConditionMask[i];
    isPumpStop |= _endConditionMask[i];
  }
  return isValidFlowmeter & isPumpTrigger & isPumpStop; 
}

uint8_t Sampler::checkPumpTrigger(float depth, float temperature, uint32_t time_now, uint32_t ticks, uint32_t pumpDuration)
{
  // 0. Depth
  _startConditions[0] = (fabs(depth - _targetDepth)) <= _depthBand;
  // 1. Temperature
  _startConditions[1] = (fabs(temperature - _targetTemperature)) <= _temperatureBand;
  // 2. Time elapsed since dive start
  _startConditions[2] = (_diveStartTime <= time_now) && ((time_now - _diveStartTime) >= _waitPumpStart);

  // 0. Volume pumped
  _endConditions[0] = ticks >= _targetFlowVol;
  // 1. Pump duration 
  _endConditions[1] = (_pumpStartTime <= time_now) && (pumpDuration >= _waitPumpEnd);
  // 2. Flowrate: flowrate below 10% of original flowrate
  _endConditions[2] = ((_maxFlowrate > 0.f) &&
                      (_curFlowrate <= _minFlowrate)); 

  uint8_t pumpOn = 0;ㅡ 
  for (uint8_t j = 0; j < N_START_COND; j++) {
    pumpOn |= _startConditionMask[j] * _startConditions[j];
  }
  for (uint8_t k = 0; k < N_END_COND; k++) {
    pumpOn &= !(_endConditionMask[k] * _endConditions[k]);
  }
  pumpOn = pumpOn == 0 ? PUMP_OFF : PUMP_ON;
  return pumpOn;
}

void Sampler::updateCurrentFlowrate(uint32_t flowData)
{
    _flowLog[_curFlowIdx] = flowData;
    _curFlowIdx = (_curFlowIdx + 1) % NUM_FLOW_LOGS;
    // average the flow across 5 seconds of accumulated ticks
    _curFlowrate = (flowData - _flowLog[_curFlowIdx]) * 12.f; // ticks per minute
}

float Sampler::getCurFlowrate()
{
  return _curFlowrate;
}

void Sampler::computeMaxFlowrate(uint32_t flowData) 
{
  // Recall that curFlowIdx is actually pointing to one idx after,
  // which holds the value from 5 seconds ago
  _maxFlowrate = (flowData - _flowLog[_curFlowIdx]) * 12;
}


void Sampler::setDiveStartTime(uint32_t diveStartTime)
{
    _diveStartTime = diveStartTime;
}

void Sampler::setPumpStartTime(uint32_t pumpStartTime)
{
    _pumpStartTime = pumpStartTime;
}
