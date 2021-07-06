# Air Quality Monitoring System
Arduino based project for monitoring Co2 concentration, temperature and humidity

## Dependencies
  - LiquidCrystal I2C by Frank de Brabander v1.1.2
  - RTCLib by Adafruit v1.12.5
  - SD (arduino built-in)
  - DHT sensor library by Adafruit v1.4.2
  - MQ135 [Link to repository](https://github.com/GeorgK/MQ135/blob/master/MQ135.cpp)

## What to change in MQ135 hardware & library 
1. Change load resistor from 1k to 22k ohms for better resolution
  - after changing load resistor, change `RLOAD` variable in `MQ135.h`, from 10.0 to 22.0
  - change `getResistance()` formula in `MQ135.cpp` to `( (1023. / (float)val) - 1. ) * RLOAD`
  
## How to calibrate MQ135 sensor
1. Power the sensor for 24 hrs
2. After 24 hrs, take it outside and get `RZERO` readings for calibration (`MQ135.getRZero()`).
3. Let it settle and record readings for 30 minutes or 1 hour. It is much better to get average from the readings.
4. Open `MQ135.h` and change `RZERO` value with the one obtained from step #3.

### Helpful blog post
- https://blog.robberg.net/mq-135-arduino/
