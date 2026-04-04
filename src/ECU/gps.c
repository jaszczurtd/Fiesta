#include "gps.h"
#include <hal/hal_time.h>

typedef struct {
  bool isGPSInitialized;
} gps_runtime_state_t;

typedef struct {
  char gpsDate[GPS_TIME_DATE_BUFFER_SIZE];
  char gpsTime[GPS_TIME_DATE_BUFFER_SIZE];
} gps_persistent_state_t;

static gps_runtime_state_t s_gpsRuntime = {
  .isGPSInitialized = false
};

NOINIT static gps_persistent_state_t s_gpsPersistent;

static void getAdjustedDateTime(int *year, int *month, int *day,
                                int *hour, int *minute, int *second);

void initGPS(void) {
  if(!s_gpsRuntime.isGPSInitialized) {
    hal_gps_init(SERIAL_RX_GPIO, SERIAL_TX_GPIO, 9600, HAL_UART_CFG_8N1);
    s_gpsRuntime.isGPSInitialized = true;
  }
}

void getGPSData(void) {

  hal_gps_update();

  bool gpsAvailable = isGPSAvailable();
  if(gpsAvailable) {
    int year, month, day, hour, minute, second;
    getAdjustedDateTime(&year, &month, &day, &hour, &minute, &second);
    setGlobalValue(F_LATITUDE, (float)hal_gps_latitude());
    setGlobalValue(F_LONGITUDE, (float)hal_gps_longitude());
    setGlobalValue(F_GPS_TIME, (float)(hour * 100 + minute));
    setGlobalValue(F_GPS_DATE, (float)(((year % 100) * 10000) + (month * 100) + day));
  } else {
    setGlobalValue(F_LATITUDE, 0.0f);
    setGlobalValue(F_LONGITUDE, 0.0f);
    setGlobalValue(F_GPS_TIME, 0.0f);
    setGlobalValue(F_GPS_DATE, 0.0f);
  }

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
  memset(s_gpsPersistent.gpsDate, 0, GPS_TIME_DATE_BUFFER_SIZE);
  memset(s_gpsPersistent.gpsTime, 0, GPS_TIME_DATE_BUFFER_SIZE);
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
    memset(s_gpsPersistent.gpsDate, 0, GPS_TIME_DATE_BUFFER_SIZE);
    snprintf(s_gpsPersistent.gpsDate, GPS_TIME_DATE_BUFFER_SIZE - 1, "%d/%02d/%02d",
      year, month, day);
  }
  return s_gpsPersistent.gpsDate;
}

const char *getGPSTime(void) {
  if(hal_gps_location_is_valid()) {
    int year, month, day, hour, minute, second;
    getAdjustedDateTime(&year, &month, &day, &hour, &minute, &second);
    memset(s_gpsPersistent.gpsTime, 0, GPS_TIME_DATE_BUFFER_SIZE);
    snprintf(s_gpsPersistent.gpsTime, GPS_TIME_DATE_BUFFER_SIZE - 1, "%02d:%02d:%02d",
      hour, minute, second);
  }
  return s_gpsPersistent.gpsTime;
}

float getCurrentCarSpeed(void) {
  if(isGPSAvailable()) {
    double s = hal_gps_speed_kmph();
    if(s < GPS_MIN_KMPH_SPEED) {
      return 0.0f;
    }
    return (float)(s);
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

uint32_t gpsGetEpoch(void) {
  if(!isGPSAvailable()) {
    return 0;
  }

  int year, month, day, hour, minute, second;
  getAdjustedDateTime(&year, &month, &day, &hour, &minute, &second);

  if(year < 2020 || year > 2099) {
    return 0;
  }

  return hal_time_from_components(year, month, day, hour, minute, second);
}

