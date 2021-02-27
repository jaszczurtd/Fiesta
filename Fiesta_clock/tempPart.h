//
//  tempPart.h
//  Index
//
//  Created by Marcin Kielesinski on 25/02/2020.
//

#ifndef tempPart_h
#define tempPart_h

#include <stdio.h>
#include "main.h"

#define TEMP_DELAY_LOOPS 20

extern char s[];

void temp_initial_read(void);
void temp_read_display(void);

#endif /* tempPart_h */
