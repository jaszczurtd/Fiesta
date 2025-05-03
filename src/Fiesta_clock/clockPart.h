//
//  clockPart.h
//  Index
//
//  Created by Marcin Kielesiński on 27/01/2020.
//

#ifndef clockPart_h
#define clockPart_h

#include <stdio.h>
#include "main.h"
#include "font.h"

extern int rc5Code, switchCode;
extern bool powerIsOn;
extern char s[];

void setClockSetMode(bool enabled);
bool isClockSetMode(void);
void manageSetMode(void);

void clockMainFunction(void);

void getTime(void);
void printClockHour(unsigned char x, unsigned char y);

void startTimerForSeconds(unsigned char seconds);
bool checkIfTimerReached(void);


#endif /* clockPart_h */
