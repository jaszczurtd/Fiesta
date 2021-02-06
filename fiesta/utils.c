//
//  utils.c
//  Index
//
//  Created by Marcin Kielesiński on 07/12/2019.
//

#include "utils.h"

void delay_ms(int ms) {
    while (0 < ms) {
        _delay_ms(1);
        --ms;
    }
}

int binatoi(char *s) {
    int i,l=0,w=1;
    
    for(i=0; i < strlen(s); i++)
    {
        if (s [i]=='1')
        {
            l+=w;
            w*=2;
        }
        if(s [i]=='0')
        {
            w*=2;
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
