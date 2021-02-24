
SRC_DIR := src
LIB_DIR := lib
CFG_DIR := cfg
OBJ_DIR := obj
OUT_DIR ?= out

OUT ?= firmware
TYPE ?= debug

ELF ?= $(OUT_DIR)/$(TYPE)/$(OUT).elf
MAP ?= $(OUT_DIR)/$(TYPE)/$(OUT).map
BIN ?= $(OUT_DIR)/$(TYPE)/$(OUT).bin

################################################################################
# Source files                                                                 #
################################################################################
	# $(LIB_DIR)/loramac-node/src/mac/region/RegionUS915.c
	# $(LIB_DIR)/loramac-node/src/mac/region/RegionAU915.c
SRC_FILES += \
    $(SRC_DIR)/atci.c \
    $(SRC_DIR)/board.c \
    $(SRC_DIR)/cmd.c \
    $(SRC_DIR)/config.c \
    $(SRC_DIR)/console.c \
    $(SRC_DIR)/eeprom.c \
    $(SRC_DIR)/error.c \
    $(SRC_DIR)/fifo.c \
    $(SRC_DIR)/hw_gpio.c \
    $(SRC_DIR)/hw_rtc.c \
    $(SRC_DIR)/hw_spi.c \
    $(SRC_DIR)/irq.c \
    $(SRC_DIR)/lora.c \
    $(SRC_DIR)/main.c \
    $(SRC_DIR)/system.c \
    $(SRC_DIR)/vcom.c \
	\
	$(SRC_DIR)/mlm32l0xx_hal_msp.c \
	$(SRC_DIR)/mlm32l0xx_hw.c \
	$(SRC_DIR)/mlm32l0xx_it.c \
	$(SRC_DIR)/mlm32l07x01.c \
	\
	$(LIB_DIR)/loramac-node/src/peripherals/soft-se/aes.c \
	$(LIB_DIR)/loramac-node/src/peripherals/soft-se/cmac.c \
	$(LIB_DIR)/loramac-node/src/peripherals/soft-se/soft-se.c \
    $(LIB_DIR)/loramac-node/src/peripherals/soft-se/soft-se-hal.c \
	$(LIB_DIR)/loramac-node/src/mac/region/Region.c \
	$(LIB_DIR)/loramac-node/src/mac/region/RegionAS923.c \
	$(LIB_DIR)/loramac-node/src/mac/region/RegionCN470.c \
	$(LIB_DIR)/loramac-node/src/mac/region/RegionCN779.c \
	$(LIB_DIR)/loramac-node/src/mac/region/RegionCommon.c \
	$(LIB_DIR)/loramac-node/src/mac/region/RegionEU433.c \
	$(LIB_DIR)/loramac-node/src/mac/region/RegionEU868.c \
	$(LIB_DIR)/loramac-node/src/mac/region/RegionIN865.c \
	$(LIB_DIR)/loramac-node/src/mac/region/RegionKR920.c \
	$(LIB_DIR)/loramac-node/src/mac/region/RegionRU864.c \
	$(LIB_DIR)/loramac-node/src/mac/LoRaMac.c \
	$(LIB_DIR)/loramac-node/src/mac/LoRaMacAdr.c \
	$(LIB_DIR)/loramac-node/src/mac/LoRaMacClassB.c \
	$(LIB_DIR)/loramac-node/src/mac/LoRaMacCommands.c \
	$(LIB_DIR)/loramac-node/src/mac/LoRaMacConfirmQueue.c \
	$(LIB_DIR)/loramac-node/src/mac/LoRaMacCrypto.c \
	$(LIB_DIR)/loramac-node/src/mac/LoRaMacParser.c \
	$(LIB_DIR)/loramac-node/src/mac/LoRaMacSerializer.c \
	$(LIB_DIR)/LoRaWAN/Utilities/low_power_manager.c \
	$(LIB_DIR)/LoRaWAN/Utilities/queue.c \
	$(LIB_DIR)/LoRaWAN/Utilities/systime.c \
	$(LIB_DIR)/LoRaWAN/Utilities/timeServer.c \
	$(LIB_DIR)/LoRaWAN/Utilities/utilities.c \
	$(LIB_DIR)/LoRaWAN/Patterns/Basic/lora-test.c \
	\
	$(LIB_DIR)/stm/src/system_stm32l0xx.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_adc.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_adc_ex.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_cortex.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_dma.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_flash.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_flash_ex.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_gpio.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_pwr.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_pwr_ex.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rcc.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rcc_ex.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rtc.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_rtc_ex.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_spi.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_uart.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_uart_ex.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_hal_usart.c \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/stm32l0xx_ll_dma.c \
	\
	$(LIB_DIR)/sx1276/sx1276.c \

################################################################################
# Include directories                                                          #
################################################################################
INC_DIR += \
	$(SRC_DIR) \
	$(CFG_DIR) \
	$(LIB_DIR)/loramac-node/src/peripherals/soft-se \
	$(LIB_DIR)/loramac-node/src/mac/region \
	$(LIB_DIR)/loramac-node/src/mac \
	$(LIB_DIR)/loramac-node/src/radio\
	$(LIB_DIR)/LoRaWAN/Utilities \
	$(LIB_DIR)/LoRaWAN/Patterns/Basic \
	$(LIB_DIR)/stm/include \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Inc \
	$(LIB_DIR)/sx1276 \

################################################################################
# ASM sources                                                                  #
################################################################################

ASM_SOURCES ?= $(LIB_DIR)/stm/src/startup_stm32l072xx.s

################################################################################
# Linker script                                                                #
################################################################################

LINKER_SCRIPT ?= $(CFG_DIR)/STM32L072CZEx_FLASH.ld

################################################################################
# Toolchain                                                                    #
################################################################################

TOOLCHAIN ?= arm-none-eabi-
CC = $(TOOLCHAIN)gcc
GDB = $(TOOLCHAIN)gdb
AS = $(TOOLCHAIN)gcc -x assembler-with-cpp
OBJCOPY = $(TOOLCHAIN)objcopy
SIZE = $(TOOLCHAIN)size

################################################################################
# Verbose build?                                                               #
################################################################################

ifeq ("$(BUILD_VERBOSE)","1")
Q :=
ECHO = @echo
else
MAKE += -s
Q := @
ECHO = @echo
endif

################################################################################
# Compiler flags for "c" files                                                 #
################################################################################

CFLAGS += -mcpu=cortex-m0plus
CFLAGS += -mthumb
CFLAGS += -mlittle-endian
CFLAGS += -Wall
CFLAGS += -pedantic
CFLAGS += -Wextra
CFLAGS += -Wmissing-include-dirs
CFLAGS += -Wswitch-default
CFLAGS += -Wswitch-enum
CFLAGS += -D'__weak=__attribute__((weak))'
CFLAGS += -D'__packed=__attribute__((__packed__))'
CFLAGS += -D'STM32L072xx'
CFLAGS += -D'HAL_IWDG_MODULE_ENABLED'
CFLAGS += -ffunction-sections
CFLAGS += -fdata-sections
CFLAGS += -std=c11
CFLAGS_DEBUG += -g3
CFLAGS_DEBUG += -Og
CFLAGS_DEBUG += -D'DEBUG'
CFLAGS_RELEASE += -Os
CFLAGS_RELEASE += -D'RELEASE'

# CFLAGS += -D'USE_HAL_DRIVER'
CFLAGS += -DUSE_FULL_LL_DRIVER
CFLAGS += -DREGION_EU868
# CFLAGS += -DREGION_AS923
# CFLAGS += -D'REGION_AS923_DEFAULT_CHANNEL_PLAN=CHANNEL_PLAN_GROUP_AS923_1'
CFLAGS += -DSOFT_SE

################################################################################
# Compiler flags for "s" files                                                 #
################################################################################

ASFLAGS += -mcpu=cortex-m0plus
ASFLAGS += --specs=nano.specs
ASFLAGS += -mfloat-abi=soft
ASFLAGS += -mthumb
ASFLAGS += -mlittle-endian
ASFLAGS_DEBUG += -g3
ASFLAGS_DEBUG += -Og
ASFLAGS_RELEASE += -Os

################################################################################
# Linker flags                                                                 #
################################################################################

LDFLAGS += -mcpu=cortex-m0plus
LDFLAGS += -mthumb
LDFLAGS += -mlittle-endian
LDFLAGS += -T$(LINKER_SCRIPT)
LDFLAGS += -Wl,-lc
LDFLAGS += -Wl,-lm
LDFLAGS += -static
LDFLAGS += -Wl,-Map=$(MAP)
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,--print-memory-usage
LDFLAGS += -Wl,-u,__errno
LDFLAGS += --specs=nosys.specs

################################################################################
# Create list of object files and their dependencies                           #
################################################################################

OBJ_C = $(SRC_FILES:%.c=$(OBJ_DIR)/$(TYPE)/%.o)
OBJ_S = $(ASM_SOURCES:%.s=$(OBJ_DIR)/$(TYPE)/%.o)
OBJ = $(OBJ_C) $(OBJ_S)
DEP = $(OBJ:%.o=%.d)
ALLDEP = $(MAKEFILE_LIST)

################################################################################
# Debug target                                                                 #
################################################################################

.PHONY: debug
debug: $(ALLDEP)
	$(Q)$(MAKE) .clean-out
	$(Q)$(MAKE) .obj-debug
	$(Q)$(MAKE) elf
	$(Q)$(MAKE) size
	$(Q)$(MAKE) bin

################################################################################
# Release target                                                               #
################################################################################

.PHONY: release
release: $(ALLDEP)
	$(Q)$(MAKE) clean TYPE=release
	$(Q)$(MAKE) .obj-release TYPE=release
	$(Q)$(MAKE) elf TYPE=release
	$(Q)$(MAKE) size TYPE=release
	$(Q)$(MAKE) bin TYPE=release
	$(Q)$(MAKE) .clean-obj TYPE=release

################################################################################
# Clean target                                                                 #
################################################################################

.PHONY: clean
clean: $(ALLDEP)
	$(Q)$(MAKE) .clean-obj
	$(Q)$(MAKE) .clean-out

.PHONY: .clean-obj
.clean-obj: $(ALLDEP)
	$(Q)$(ECHO) "Removing object directory..."
	$(Q)rm -rf $(OBJ_DIR)/$(TYPE)

.PHONY: .clean-out
.clean-out: $(ALLDEP)
	$(Q)$(ECHO) "Clean output ..."
	$(Q)rm -f "$(ELF)" "$(MAP)" "$(BIN)"

################################################################################
# J-Link                                          #
################################################################################

.PHONY: flash
flash: $(ALLDEP)
ifeq ($(OS),Windows_NT)
	JLink -device stm32l072cz -CommanderScript tools/jlink/flash.jlink
else
	JLinkExe -device stm32l072cz -CommanderScript tools/jlink/flash.jlink
endif

.PHONY: gdbserver
gdbserver: $(ALLDEP)
ifeq ($(OS),Windows_NT)
	JLinkGDBServerCL -singlerun -device stm32l072cz -if swd -speed 4000 -localhostonly -reset
else
	JLinkGDBServer -singlerun -device stm32l072cz -if swd -speed 4000 -localhostonly -reset
endif

.PHONY: jlink
jlink: $(ALLDEP)
	$(Q)$(MAKE) jlink-flash
	$(Q)$(MAKE) jlink-gdbserver

.PHONY: ozone
ozone: debug $(ALLDEP)
	$(Q)$(ECHO) "Launching Ozone debugger..."
	$(Q)Ozone tools/ozone/ozone.jdebug


################################################################################
# git submodule                                                                #
################################################################################

$(LIB_DIR)/loramac-node/LICENSE:
	@git submodule update --init lib/loramac-node

################################################################################
# Link object files                                                            #
################################################################################

.PHONY: elf
elf: $(ELF) $(ALLDEP)

$(ELF): $(OBJ) $(ALLDEP)
	$(Q)$(ECHO) "Linking object files..."
	$(Q)mkdir -p $(OUT_DIR)/$(TYPE)
	$(Q)$(CC) $(LDFLAGS) $(OBJ) -o $(ELF)

################################################################################
# Print information about size of sections                                     #
################################################################################

.PHONY: size
size: $(ELF) $(ALLDEP)
	$(Q)$(ECHO) "Size of sections:"
	$(Q)$(SIZE) $(ELF)

################################################################################
# Create binary file                                                           #
################################################################################

.PHONY: bin
bin: $(BIN) $(ALLDEP)

$(BIN): $(ELF) $(ALLDEP)
	$(Q)$(ECHO) "Creating $(BIN) from $(ELF)..."
	$(Q)$(OBJCOPY) -O binary $(ELF) $(BIN)
	$(Q)rm -f $(OUT).bin
	$(Q)cp $(BIN) $(OUT).bin

################################################################################
# Compile source files                                                         #
################################################################################

.PHONY: .obj-debug
.obj-debug: CFLAGS += $(CFLAGS_DEBUG)
.obj-debug: ASFLAGS += $(ASFLAGS_DEBUG)
.obj-debug: $(OBJ) $(ALLDEP)

.PHONY: .obj-release
.obj-release: CFLAGS += $(CFLAGS_RELEASE)
.obj-release: ASFLAGS += $(ASFLAGS_RELEASE)
.obj-release: $(OBJ) $(ALLDEP)

################################################################################
# Compile "c" files                                                            #
################################################################################

$(OBJ_DIR)/$(TYPE)/%.o: %.c $(ALLDEP)
	$(Q)$(ECHO) "Compiling: $<"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) -MMD -MP -MT "$@ $(@:.o=.d)" -c $(CFLAGS) $(foreach d,$(INC_DIR),-I$d) $< -o $@

################################################################################
# Compile "s" files                                                            #
################################################################################

$(OBJ_DIR)/$(TYPE)/%.o: %.s $(ALLDEP)
	$(Q)$(ECHO) "Compiling: $<"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) -MMD -MP -MT "$@ $(@:.o=.d)" -c $(ASFLAGS) $< -o $@
