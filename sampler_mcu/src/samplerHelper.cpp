#include <Ticker.h>
#include "samplerHelper.h"

Ticker ticker_led;


void _toggleLED(int led) 
{
    digitalWrite(led, !(digitalRead(led)));
}


void _toggleAllLEDs() 
{
  digitalWrite(LED_PWR, !(digitalRead(LED_PWR)));
  digitalWrite(LED_RDYB, !(digitalRead(LED_RDYB)));
  digitalWrite(LED_RDYG, !(digitalRead(LED_RDYG)));
}


void blinkAllLEDs(uint32_t period) 
{
    ticker_led.detach();
    ticker_led.attach_ms(period, _toggleAllLEDs);
}


void blinkSingleLED(int led, uint32_t period) 
{
    ticker_led.detach();
    ticker_led.attach_ms(period, _toggleLED, (int)led);
}


void turnOnLED(int led) 
{
    ticker_led.detach();
    digitalWrite(led, HIGH);
}


void turnOffLED(int led) 
{
    digitalWrite(led, LOW);
}


void flagErrorLED()
{
    ticker_led.detach();
    ticker_led.attach_ms(500, _toggleAllLEDs);
    while(1) {
        delay(500);
    }
}
