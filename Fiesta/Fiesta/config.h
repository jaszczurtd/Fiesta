#ifndef T_CONFIG
#define T_CONFIG

//debug i2c only
//#define I2C_SCANNER

//for debug - display values on LCD
//debugFunc() function is invoked, no regular drawings
//#define DEBUG

//for serial debug
#define DEBUG

//delay time right after start, before first serious alerts will show up (in seconds)
#define SERIOUS_ALERTS_DELAY_TIME 1

//how many main loop cycles between next sensor's read (low importance values)
#define LOW_READ_CYCLES_AMOUNT 20

//how many main loop cycles between next sensor's read (high importance values)
#define HIGH_READ_CYCLES_AMOUNT 8

//in seconds
#define FIESTA_INTRO_TIME 2 

// temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 21   
// how many samples to take and average, more takes longer
// but is more 'smooth'
#define NUMSAMPLES 8
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 3600

#define MINIMUM_FUEL_AMOUNT_PERCENTAGE 10

#define TEMP_OIL_MAX 155
#define TEMP_OIL_OK_HI 115

#define TEMP_MAX 120
#define TEMP_MIN 45

#define TEMP_OK_LO 70
#define TEMP_OK_HI 105

#define TEMP_LOWEST -100
#define TEMP_HIGHEST 170

#define TEMP_MINIMUM_FOR_GLOW_PLUGS 50

//temperature when fan should start
#define TEMP_FAN_START  102
//temperature when fan should stop after start
#define TEMP_FAN_STOP   94

//temperature when engine heater should stop to heat
#define TEMP_HEATER_STOP 80

//minimum voltage amount - below this value no engine heater, or heated windows
#define MINIMUM_VOLTS_AMOUNT 13.0

//how long heated windows should heats? (in seconds)
#define HEATED_WINDOWS_TIME (60 * 4)
//time between switch left side / right side
#define HEATED_WINDOWS_SWITCH_TIME 30
//physical pin of microcontroller for heated windows switch on/off
#define HEATED_WINDOWS_PIN 20

//RPM refresh interval (in miliseconds)
#define RPM_REFRESH_INTERVAL 400
//minimum RPM for dependencies to operate on
#define RPM_MIN 300

#define PWM_WRITE_RESOLUTION 16
#define PWM_RESOLUTION 65535

#define THROTTLE_MIN 1610
#define THROTTLE_MAX 3400

#define FUEL_MAX 388
#define FUEL_MIN 1612

//dividers - analog reads

#define DIVIDER_VOLTS 212.628568
#define DIVIDER_PRESSURE_BAR 912
#define DIVIDER_EGT 2.308

#endif
