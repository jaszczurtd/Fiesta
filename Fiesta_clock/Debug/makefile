################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
LSS += \
Fiesta_clock.lss \

FLASH_IMAGE += \
Fiesta_clock.hex \

EEPROM_IMAGE += \
Fiesta_clock.eep \

SIZEDUMMY += \
sizedummy \

AVRDUDEDUMMY += \
avrdudedummy \


# All Target
all: Fiesta_clock.elf secondary-outputs

# Tool invocations
Fiesta_clock.elf: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: AVR C Linker'
	avr-gcc -Wl,-Map,Fiesta_clock.map -s -Wl,-gc-sections -mmcu=atmega328p -o "Fiesta_clock.elf" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

Fiesta_clock.lss: Fiesta_clock.elf
	@echo 'Invoking: AVR Create Extended Listing'
	-avr-objdump -h -S Fiesta_clock.elf  >"Fiesta_clock.lss"
	@echo 'Finished building: $@'
	@echo ' '

Fiesta_clock.hex: Fiesta_clock.elf
	@echo 'Create Flash image (ihex format)'
	-avr-objcopy -R .eeprom -R .fuse -R .lock -R .signature -O ihex Fiesta_clock.elf  "Fiesta_clock.hex"
	@echo 'Finished building: $@'
	@echo ' '

Fiesta_clock.eep: Fiesta_clock.elf
	@echo 'Create eeprom image (ihex format)'
	-avr-objcopy -j .eeprom --no-change-warnings --change-section-lma .eeprom=0 -O ihex Fiesta_clock.elf  "Fiesta_clock.eep"
	@echo 'Finished building: $@'
	@echo ' '

sizedummy: Fiesta_clock.elf
	@echo 'Invoking: Print Size'
	-avr-size --format=avr --mcu=atmega328p Fiesta_clock.elf
	@echo 'Finished building: $@'
	@echo ' '

avrdudedummy: Fiesta_clock.elf
	@echo 'Invoking: AVRDude'
	C:\development\WINAVR\bin\avrdude -pm328p -cusbasp -Uflash:w:Fiesta_clock.hex:a
	@echo 'Finished building: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(FLASH_IMAGE)$(ELFS)$(OBJS)$(ASM_DEPS)$(EEPROM_IMAGE)$(AVRDUDEDUMMY)$(S_DEPS)$(SIZEDUMMY)$(S_UPPER_DEPS)$(LSS)$(C_DEPS) Fiesta_clock.elf
	-@echo ' '

secondary-outputs: $(LSS) $(FLASH_IMAGE) $(EEPROM_IMAGE) $(SIZEDUMMY) $(AVRDUDEDUMMY)

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets
