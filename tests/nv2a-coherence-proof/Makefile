XBOXRECOMP_DIR ?= ../../game/build/xboxrecomp
DEVKITPRO ?= /opt/devkitpro
DEVKITA64 ?= $(DEVKITPRO)/devkitA64
CC := $(DEVKITA64)/bin/aarch64-none-elf-gcc
ELF2NRO := $(DEVKITPRO)/tools/bin/elf2nro
NACPTOOL := $(DEVKITPRO)/tools/bin/nacptool
UAM := $(DEVKITPRO)/tools/bin/uam
TARGET := nv2a-coherence-proof
APP_TITLE := Manhunt NV2A coherence proof
APP_AUTHOR := Manhunt Recompilation Team
APP_VERSION := 0.0.1
BUILD := build
ROMFS := $(BUILD)/romfs
SHADERS := $(ROMFS)/shaders/attribute_vsh.dksh $(ROMFS)/shaders/diffuse_fsh.dksh $(ROMFS)/shaders/texture_fsh.dksh
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -Wall -Wextra -Werror -O2 -ffunction-sections $(ARCH) -D__SWITCH__ -I$(DEVKITPRO)/libnx/include -I$(XBOXRECOMP_DIR)/src/kernel -I$(XBOXRECOMP_DIR)/src -MMD -MP -ffile-prefix-map=$(CURDIR)=.
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -L$(DEVKITPRO)/libnx/lib -Wl,--gc-sections

all: $(TARGET).nro

$(TARGET).nro: $(TARGET).elf $(TARGET).nacp $(SHADERS)
	$(ELF2NRO) $< $@ --nacp=$(TARGET).nacp --romfsdir=$(ROMFS)

$(TARGET).nacp:
	$(NACPTOOL) --create "$(APP_TITLE)" "$(APP_AUTHOR)" "$(APP_VERSION)" $@

RUNTIME := nv2a_pb_exec nv2a_vsh_interp nv2a_combiner nv2a_flip_hold
OBJECTS := $(BUILD)/main.o $(addprefix $(BUILD)/,$(addsuffix .o,$(RUNTIME)))

$(BUILD)/%.o: $(XBOXRECOMP_DIR)/src/kernel/%.c
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-error -ffp-contract=off -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -ldeko3d -lnx -lm -o $@

$(BUILD)/main.o: source/main.c
	mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(ROMFS)/shaders/%_vsh.dksh: ../nv2a-backend-proof/source/%_vsh.glsl
	mkdir -p $(@D)
	$(UAM) -s vert -o $@ $<

$(ROMFS)/shaders/diffuse_fsh.dksh: source/diffuse_fsh.glsl
	mkdir -p $(@D)
	$(UAM) -s frag -o $@ $<

$(ROMFS)/shaders/%_fsh.dksh: ../nv2a-backend-proof/source/%_fsh.glsl
	mkdir -p $(@D)
	$(UAM) -s frag -o $@ $<

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp

-include $(OBJECTS:.o=.d)

.PHONY: all clean
