
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

//-------------------------------------------------------------------------------------------------
// gps
//-------------------------------------------------------------------------------------------------

static bool gps_drawOnce = true; 
void redrawGPS(void) {
    gps_drawOnce = true;
}

const int gps_getBaseX(void) {
    return 0;
}

const int gps_getBaseY(void) {
    return BIG_ICONS_HEIGHT + (BIG_ICONS_OFFSET * 2); 
}

static int lastGPSSpeed = C_INIT_VAL;
void showGPSStatus(void) {

    if(gps_drawOnce) {
        drawImage(gps_getBaseX(), gps_getBaseY(), SMALL_ICONS_WIDTH, SMALL_ICONS_HEIGHT, ICONS_BG_COLOR, (unsigned short*)gpsIcon);
        gps_drawOnce = false;
    } else {
      int currentVal = (int)getCurrentCarSpeed();
      if(lastGPSSpeed != currentVal) {
          lastGPSSpeed = currentVal;

          drawTextForMiddleIcons(gps_getBaseX(), gps_getBaseY(), 1, 
                                 TEXT_COLOR, MODE_M_KILOMETERS, (const char*)F("%d"), currentVal);
      }

      int x, y, color;
      TFT tft = returnTFTReference();

      if(isGPSAvailable()) {
        color = COLOR(GREEN);
      } else {
        bool blink = alertSwitch();
        color = (blink) ? COLOR(RED) : ICONS_BG_COLOR;
      }

      int posOffset = 10;
      int radius = 4;

      x = gps_getBaseX() + SMALL_ICONS_WIDTH - posOffset - radius;
      y = gps_getBaseY() + posOffset - 1;

      tft.fillCircle(x, y, radius, color);
    }
}


