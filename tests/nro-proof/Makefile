DEVKITPRO ?= /opt/devkitpro
DEVKITA64 ?= $(DEVKITPRO)/devkitA64
CC := $(DEVKITA64)/bin/aarch64-none-elf-gcc
ELF2NRO := $(DEVKITPRO)/tools/bin/elf2nro
NACPTOOL := $(DEVKITPRO)/tools/bin/nacptool
TARGET := nro-proof
APP_TITLE := Manhunt NRO proof
APP_AUTHOR := Manhunt Recompilation Team
APP_VERSION := 0.0.1
BUILD := build
CFLAGS := -g -Wall -O2 -ffunction-sections -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -I$(DEVKITPRO)/libnx/include -MMD -MP -ffile-prefix-map=$(CURDIR)=.
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -L$(DEVKITPRO)/libnx/lib -Wl,--gc-sections

all: $(TARGET).nro

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp
	$(ELF2NRO) $< $@ --nacp=$(TARGET).nacp

$(TARGET).nacp:
	$(NACPTOOL) --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).elf: $(BUILD)/main.o
	$(CC) $(LDFLAGS) $< -lnx -o $@

$(BUILD)/main.o: source/main.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp

-include $(BUILD)/main.d

.PHONY: all clean
