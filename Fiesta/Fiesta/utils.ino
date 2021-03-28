//
//  utils.c
//  Index
//
//  Created by Marcin Kielesiński on 07/12/2019.
//

#include <OneWire.h>
#include <DallasTemperature.h>

int binatoi(char *s) {
    int i, l = 0, w = 1;
    
    for(i = 0; i < strlen(s); i++) {
        if (s [i] == '1')  {
            l += w;
            w *= 2;
        }
        if(s [i]=='0') {
            w *= 2;
        }
    }
    return(l);
}

static char binaryNum[16 + 1];
char *decToBinary(int n) {
    // array to store binary number
    int a = 0, c, k;
    
    memset(binaryNum, 0, sizeof(binaryNum));
    
    for (c = 15; c >= 0; c--) {
        k = n >> c;
        
        if (k & 1) {
            binaryNum[a++] = '1';
        } else {
            binaryNum[a++] = '0';
        }
    }
    return binaryNum;
}

unsigned char BinToBCD(unsigned char bin) {
    return ((((bin) / 10) << 4) + ((bin) % 10));
}


unsigned char reverse(unsigned char b) {
   b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
   b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
   b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
   return b;
}

void doubleToDec(double val, int *hi, int *lo) {
	int t1 = (int)val;
	if(t1 > -128) {
		if(hi != NULL) {
			*hi = t1;
		}
		int t2 = (int) (((double)val - t1) * 10);
		if(lo != NULL) {
			if(t2 >= 0) {
				*lo = t2;
			} else {
				*lo = 0;
			}
		}
	}
}

double adcToVolt(double basev, int adc) {
    return adc * (basev/1024.0);    
}

// temp. for nominal resistance (almost always 25 C)
#define TEMPERATURENOMINAL 21   
// how many samples to take and average, more takes longer
// but is more 'smooth'
#define NUMSAMPLES 6
// The beta coefficient of the thermistor (usually 3000-4000)
#define BCOEFFICIENT 3950

static int samples[NUMSAMPLES];
double ntcToTemp(int tpin, int thermistor, int r) {

    uint8_t i;
    float average;

    // take N samples in a row, with a slight delay
    for (i=0; i< NUMSAMPLES; i++) {
    samples[i] = analogRead(tpin);
    delay(10);
    }

    // average all the samples out
    average = 0;
    for (i=0; i< NUMSAMPLES; i++) {
        average += samples[i];
    }
    average /= NUMSAMPLES;

    // convert the value to resistance
    average = 1023 / average - 1;
    average = r / average;

    double steinhart;
    steinhart = average / thermistor;     // (R/Ro)
    steinhart = log(steinhart);                  // ln(R/Ro)
    steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
    steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
    steinhart = 1.0 / steinhart;                 // Invert
    steinhart -= 273.15;                         // convert absolute temp to C    

    return steinhart;
}

static bool initialized = false;
DeviceAddress tempDeviceAddress;
OneWire oneWire;
DallasTemperature sensors;

void ds18b20Init(int pin) {
    if(!initialized) {
        oneWire.begin(pin);
        sensors.setOneWire(&oneWire);
        sensors.setResolution(tempDeviceAddress, 12);
        sensors.setWaitForConversion(false);
        sensors.begin();
        initialized = true;
    }
}

double ds18b20ToTemp(int pin, int index) {
    ds18b20Init(pin);

    sensors.getAddress(tempDeviceAddress, index);
    sensors.requestTemperatures(); 
    return sensors.getTempCByIndex(index);
}

void valToPWM(unsigned char pin, unsigned char val) {
    analogWrite(pin, (unsigned char)(255 - val));
}