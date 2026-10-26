DEVKITPRO ?= /opt/devkitpro
DEVKITA64 ?= $(DEVKITPRO)/devkitA64
CC := $(DEVKITA64)/bin/aarch64-none-elf-gcc
ELF2NRO := $(DEVKITPRO)/tools/bin/elf2nro
NACPTOOL := $(DEVKITPRO)/tools/bin/nacptool
UAM := $(DEVKITPRO)/tools/bin/uam
TARGET := deko3d-proof
APP_TITLE := Manhunt deko3d proof
APP_AUTHOR := Manhunt Recompilation Team
APP_VERSION := 0.0.1
BUILD := build
ROMFS := $(BUILD)/romfs
SHADERS := $(ROMFS)/shaders/triangle_vsh.dksh $(ROMFS)/shaders/red_fsh.dksh
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -Wall -O2 -ffunction-sections $(ARCH) -D__SWITCH__ -I$(DEVKITPRO)/libnx/include -MMD -MP -ffile-prefix-map=$(CURDIR)=.
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -L$(DEVKITPRO)/libnx/lib -Wl,--gc-sections

all: $(TARGET).nro

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp $(SHADERS)
	$(ELF2NRO) $< $@ --nacp=$(TARGET).nacp --romfsdir=$(ROMFS)

$(TARGET).nacp:
	$(NACPTOOL) --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

$(TARGET).elf: $(BUILD)/main.o
	$(CC) $(LDFLAGS) $< -ldeko3d -lnx -o $@

$(BUILD)/main.o: source/main.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(ROMFS)/shaders/%_vsh.dksh: source/%_vsh.glsl
	mkdir -p $(@D)
	$(UAM) -s vert -o $@ $<

$(ROMFS)/shaders/%_fsh.dksh: source/%_fsh.glsl
	mkdir -p $(@D)
	$(UAM) -s frag -o $@ $<

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp

-include $(BUILD)/main.d

.PHONY: all clean
