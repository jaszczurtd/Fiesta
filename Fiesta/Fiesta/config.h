#ifndef T_CONFIG
#define T_CONFIG

//debug i2c only
//#define I2C_SCANNER

//for debug - display values on LCD
//debugFunc() function is invoked, no regular drawings
//#define DEBUG

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
#define NUMSAMPLES 7
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

#define PWM_WRITE_RESOLUTION 16
#define PWM_RESOLUTION 65535

#if defined(ARDUINO_ARCH_RP2040)
#define PI_PICO_RESOLUTION_MULTIPLER 4.0
#else
#define PI_PICO_RESOLUTION_MULTIPLER 1.0
#endif

#define THROTTLE_MIN (50 * PI_PICO_RESOLUTION_MULTIPLER)
#define THROTTLE_MAX (970 * PI_PICO_RESOLUTION_MULTIPLER)

#define FUEL_MAX (97 * PI_PICO_RESOLUTION_MULTIPLER)
#define FUEL_MIN (403 * PI_PICO_RESOLUTION_MULTIPLER)

//dividers - analog reads

#define DIVIDER_VOLTS (53.157142 * PI_PICO_RESOLUTION_MULTIPLER)
#define DIVIDER_PRESSURE_BAR (228.0 * PI_PICO_RESOLUTION_MULTIPLER)
#define DIVIDER_EGT (0.577 * PI_PICO_RESOLUTION_MULTIPLER)

#endif
