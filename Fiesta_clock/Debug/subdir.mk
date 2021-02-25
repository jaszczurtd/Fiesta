################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../PCF8563.c \
../clockPart.c \
../ds18b20.c \
../font.c \
../lcd.c \
../main.c \
../tempPart.c \
../twi_i2c.c \
../utils.c 

OBJS += \
./PCF8563.o \
./clockPart.o \
./ds18b20.o \
./font.o \
./lcd.o \
./main.o \
./tempPart.o \
./twi_i2c.o \
./utils.o 

C_DEPS += \
./PCF8563.d \
./clockPart.d \
./ds18b20.d \
./font.d \
./lcd.d \
./main.d \
./tempPart.d \
./twi_i2c.d \
./utils.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: AVR Compiler'
	avr-gcc -Wall -g2 -gstabs -O2 -fpack-struct -fshort-enums -ffunction-sections -fdata-sections -std=gnu99 -funsigned-char -funsigned-bitfields -mmcu=atmega328p -DF_CPU=16000000UL -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


