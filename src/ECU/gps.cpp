
#include "gps.h"


NOINIT char gpsDate[GPS_TIME_DATE_BUFFER_SIZE];
NOINIT char gpsTime[GPS_TIME_DATE_BUFFER_SIZE];

static bool isGPSInitialized = false;
void initGPS(void) {
  if(!isGPSInitialized) {
    hal_gps_init(SERIAL_RX_GPIO, SERIAL_TX_GPIO, 9600, HAL_UART_CFG_8N1);
    isGPSInitialized = true;
  }
}

void getGPSData(void) {

  hal_gps_update();

  if(hal_gps_location_is_valid()) {
    if (hal_gps_location_is_updated()){

      deb("Lat=%f Long=%f date:%s hour:%s", 
        hal_gps_latitude(), hal_gps_longitude(),
        getGPSDate(), getGPSTime());
    }
  } else {
    deb("GPS is not available");
  }

  deb("GPS: valid:%d updated:%d age:%lu", hal_gps_location_is_valid(), hal_gps_location_is_updated(),
    (unsigned long)hal_gps_location_age());
}

void initGPSDateAndTime(void) {
  memset(gpsDate, 0, GPS_TIME_DATE_BUFFER_SIZE);
  memset(gpsTime, 0, GPS_TIME_DATE_BUFFER_SIZE);
}

static void getAdjustedDateTime(int *year, int *month, int *day,
                                int *hour, int *minute, int *second) {
  *year   = hal_gps_date_year();
  *month  = hal_gps_date_month();
  *day    = hal_gps_date_day();
  *hour   = hal_gps_time_hour();
  *minute = hal_gps_time_minute();
  *second = hal_gps_time_second();
  adjustTime(year, month, day, hour, minute);
}

const char *getGPSDate(void) {
  if(hal_gps_location_is_valid()) {
    int year, month, day, hour, minute, second;
    getAdjustedDateTime(&year, &month, &day, &hour, &minute, &second);
    memset(gpsDate, 0, GPS_TIME_DATE_BUFFER_SIZE);
    snprintf(gpsDate, GPS_TIME_DATE_BUFFER_SIZE - 1, "%d/%02d/%02d", 
      year, month, day);
  }
  return gpsDate;
}

const char *getGPSTime(void) {
  if(hal_gps_location_is_valid()) {
    int year, month, day, hour, minute, second;
    getAdjustedDateTime(&year, &month, &day, &hour, &minute, &second);
    memset(gpsTime, 0, GPS_TIME_DATE_BUFFER_SIZE);
    snprintf(gpsTime, GPS_TIME_DATE_BUFFER_SIZE - 1, "%02d:%02d:%02d", 
      hour, minute, second);
  }
  return gpsTime;
}

float getCurrentCarSpeed(void) {
  if(isGPSAvailable()) {
    double s = hal_gps_speed_kmph();
    if(s < GPS_MIN_KMPH_SPEED) {
      return 0.0f;
    }
    return float(s);
  }
  return 0.0;
}

bool isGPSAvailable(void) {
  bool isavail = hal_gps_location_is_valid();
  if(isavail) {
    if(hal_gps_location_age() > MAX_GPS_AGE) {
      isavail = false;
    }
  }
  setGlobalValue(F_GPS_IS_AVAILABLE, isavail);
  return isavail;
}


