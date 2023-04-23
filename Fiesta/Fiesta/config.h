#ifndef T_CONFIG
#define T_CONFIG

//important: values scaled for rpi SDK ver 1.2.0

//debug i2c only
//#define I2C_SCANNER

//for debug - display values on LCD
//debugFunc() function is invoked, no regular drawings
//#define DEBUG

//for serial debug
//#define DEBUG

#define WATCHDOG_TIME 4000
#define UNSYNCHRONIZE_TIME 15
#define CORE_OPERATION_DELAY 1

//delay time right after start, before first serious alerts will show up (in seconds)
#define SERIOUS_ALERTS_DELAY_TIME 1

//how many main loop cycles between next sensor's read (high importance values)
#define HIGH_READ_CYCLES_AMOUNT 8

//in seconds
#define FIESTA_INTRO_TIME 2 

#define MINIMUM_FUEL_AMOUNT_PERCENTAGE 10

//minimum temp value for oil and coolant
#define TEMP_MIN 45

//minimum working temp value for oil and coolant
#define TEMP_OK_LO 70

//oil temp values
#define TEMP_OIL_MAX 140
#define TEMP_OIL_OK_HI 115

//coolant temp values
#define TEMP_MAX 125
#define TEMP_OK_HI 105

#define TEMP_LOWEST -100
#define TEMP_HIGHEST 170

#define TEMP_MINIMUM_FOR_GLOW_PLUGS 50
//when engine is cold, this is the multiplier of the time
//when the glow plugs are heating the cylinders
#define TEMP_HEATING_GLOW_PLUGS_MULTIPLIER 1.5

#define TEMP_EGT_OK_HI 750
#define TEMP_EGT_MAX 1600
#define TEMP_EGT_MIN 100

//temperature when fan should start
#define TEMP_FAN_START  102
//temperature when fan should stop after start
#define TEMP_FAN_STOP   95

//temperature when engine heater should stop to heat
#define TEMP_HEATER_STOP 80

//minimum voltage amount - below this value no engine heater, or heated windows
#define MINIMUM_VOLTS_AMOUNT 13.0

//how long heated windows should heats? (in seconds)
#define HEATED_WINDOWS_TIME 4
//physical pin of microcontroller for heated windows switch on/off
#define HEATED_WINDOWS_PIN 20

#define PWM_WRITE_RESOLUTION 11
#define PWM_RESOLUTION 2047

//values from ADC
#define THROTTLE_MIN 1800
#define THROTTLE_MAX 3800

//values from ADC
#define FUEL_MAX 388
#define FUEL_MIN 1660

//dividers - analog reads

#define DIVIDER_PRESSURE_BAR 912
#define DIVIDER_EGT 2.308

//RPM refresh interval (in miliseconds)
#define RPM_REFRESH_INTERVAL 150

//maximum RPM for engine
#define RPM_MAX_EVER 7000

#define NOMINAL_RPM_VALUE 880
#define COLD_RPM_VALUE 1000
#define REGEN_RPM_VALUE 1100

#define MAX_RPM_PERCENT_VALUE 100
#define MIN_RPM_PERCENT_VALUE 20
#define MAX_RPM_DIFFERENCE 30 //max difference for engine nominal RPM
#define RESET_RPM_WATCHDOG_TIME 2000

//time of display: EGT/DPF (if available)
#define DPF_SHOW_TIME_INTERVAL 5000

//max BAR turbo pressure
#define MAX_BOOST_PRESSURE 2.0
#define IDLE_BOOST_PERCENTAGE_SET 40

#endif
