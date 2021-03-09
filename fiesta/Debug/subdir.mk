################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../adc.c \
../font.c \
../i2cEeprom.c \
../lcd.c \
../main.c \
../pcf8574a.c \
../spi.c \
../st7735.c \
../st7735_font.c \
../st7735_gfx.c \
../twi_i2c.c \
../utils.c 

OBJS += \
./adc.o \
./font.o \
./i2cEeprom.o \
./lcd.o \
./main.o \
./pcf8574a.o \
./spi.o \
./st7735.o \
./st7735_font.o \
./st7735_gfx.o \
./twi_i2c.o \
./utils.o 

C_DEPS += \
./adc.d \
./font.d \
./i2cEeprom.d \
./lcd.d \
./main.d \
./pcf8574a.d \
./spi.d \
./st7735.d \
./st7735_font.d \
./st7735_gfx.d \
./twi_i2c.d \
./utils.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: AVR Compiler'
	avr-gcc -Wall -g2 -gstabs -O2 -fpack-struct -fshort-enums -ffunction-sections -fdata-sections -std=gnu99 -funsigned-char -funsigned-bitfields -mmcu=atmega328p -DF_CPU=16000000UL -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


