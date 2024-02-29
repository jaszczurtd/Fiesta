
#include "gps.h"


static SoftwareSerial gpsSerial(SERIAL_RX_GPIO, SERIAL_TX_GPIO);
static TinyGPSPlus gps;

void serialTalks(void);

static bool isGPSInitialized = false;
void initGPS(void) {
  if(!isGPSInitialized) {
    attachInterrupt(SERIAL_RX_GPIO, serialTalks, FALLING);  
    gpsSerial.begin(9600, SERIAL_7N1);
    isGPSInitialized = true;
  }
}

void serialTalks(void) {
  if(gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
}

bool getGPSData(void *arg) {

  if(isGPSAvailable()) {
    if (gps.location.isUpdated()){

      deb("Lat=%f Long=%f date:%s hour:%s", 
        gps.location.lat(), gps.location.lng(),
        getGPSDate(), getGPSTime());
    }
  } else {
    deb("GPS is not available");
  }

  return true;
}

NOINIT char gpsDate[GPS_TIME_DATE_BUFFER_SIZE];
NOINIT char gpsTime[GPS_TIME_DATE_BUFFER_SIZE];

void initGPSDateAndTime(void) {
  memset(gpsDate, 0, GPS_TIME_DATE_BUFFER_SIZE);
  memset(gpsTime, 0, GPS_TIME_DATE_BUFFER_SIZE);
}

const char *getGPSDate(void) {
  if(isGPSAvailable()) {
    memset(gpsDate, 0, GPS_TIME_DATE_BUFFER_SIZE);
    snprintf(gpsDate, GPS_TIME_DATE_BUFFER_SIZE - 1, "%d/%02d/%02d", 
      gps.date.year(), gps.date.month(), gps.date.day());
  }
  return gpsDate;
}

const char *getGPSTime(void) {
  if(isGPSAvailable()) {
    int year = gps.date.year();
    int month = gps.date.month();
    int day = gps.date.day();
    int hour = gps.time.hour();
    int minute = gps.time.minute();

    adjustTime(&year, &month, &day, &hour, &minute);

    memset(gpsTime, 0, GPS_TIME_DATE_BUFFER_SIZE);
    snprintf(gpsTime, GPS_TIME_DATE_BUFFER_SIZE - 1, "%02d:%02d:%02d", 
      hour, minute, gps.time.second());
  }
  return gpsTime;
}

float getCurrentCarSpeed(void) {
  double s = gps.speed.kmph();
  if(s < GPS_MIN_KMPH_SPEED) {
    return 0.0f;
  }
  return float(s);
}

bool isGPSAvailable(void) {
  return gps.location.isValid();
}


