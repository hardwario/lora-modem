
SRC_DIR := src
LIB_DIR := lib
CFG_DIR := cfg
OBJ_DIR := obj
OUT_DIR ?= out

OUT ?= firmware
TYPE ?= debug
BAUDRATE ?= 9600

# Select the regional parameter files that you wish to have included in the
# firmware. By default the full set is included. Please note that the full set
# may not fit into the flash memory in the debug mode (it does fit in the
# release mode).
ENABLE_REGIONS ?= AS923 AU915 CN470 CN779 EU433 EU868 IN865 KR920 RU864 US915

ELF ?= $(OUT_DIR)/$(TYPE)/$(OUT).elf
MAP ?= $(OUT_DIR)/$(TYPE)/$(OUT).map
BIN ?= $(OUT_DIR)/$(TYPE)/$(OUT).bin


################################################################################
# Source files                                                                 #
################################################################################

# Include only the following selected sources from the STM HAL and everything
# from stm/src
stm_hal = \
	stm32l0xx_hal.c \
	stm32l0xx_hal_adc.c \
	stm32l0xx_hal_adc_ex.c \
	stm32l0xx_hal_cortex.c \
	stm32l0xx_hal_dma.c \
	stm32l0xx_hal_flash.c \
	stm32l0xx_hal_flash_ex.c \
	stm32l0xx_hal_gpio.c \
	stm32l0xx_hal_pwr.c \
	stm32l0xx_hal_pwr_ex.c \
	stm32l0xx_hal_rcc.c \
	stm32l0xx_hal_rcc_ex.c \
	stm32l0xx_hal_rtc.c \
	stm32l0xx_hal_rtc_ex.c \
	stm32l0xx_hal_spi.c \
	stm32l0xx_hal_uart.c \
	stm32l0xx_hal_uart_ex.c \
	stm32l0xx_hal_usart.c \
	stm32l0xx_ll_dma.c
SRC_FILES += $(patsubst %.c,$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Src/%.c,$(stm_hal))
SRC_DIR += $(LIB_DIR)/stm/src

# Include all source code from rtt, LoRaWAN
SRC_DIR += $(LIB_DIR)/rtt
SRC_DIR += $(LIB_DIR)/LoRaWAN/Utilities

# Include the core LoRa MAC stack with only the base regional files
SRC_DIR += \
	$(LIB_DIR)/loramac-node/src/peripherals/soft-se \
	$(LIB_DIR)/loramac-node/src/radio/sx1276 \
	$(LIB_DIR)/loramac-node/src/mac
SRC_FILES += \
	$(LIB_DIR)/loramac-node/src/mac/region/Region.c \
	$(LIB_DIR)/loramac-node/src/mac/region/RegionCommon.c

# Activate the regional parameter files explicitly enabled by the developer in
# the variable ENABLE_REGIONS.
SRC_FILES += $(foreach reg,$(ENABLE_REGIONS),$(wildcard $(LIB_DIR)/loramac-node/src/mac/region/*$(reg)*.c))
CFLAGS += $(foreach reg,$(ENABLE_REGIONS),-DREGION_$(reg))

# The US915 regional file depends on RegionBaseUS which will not be matched by
# the wildcard pattern above, so we need to include it explicitly if the region
# is enabled.
ifneq (,$(findstring US,$(ENABLE_REGIONS)))
SRC_FILES += $(LIB_DIR)/loramac-node/src/mac/region/RegionBaseUS.c
endif

################################################################################
# Include directories                                                          #
################################################################################

INC_DIR += \
	$(SRC_DIR) \
	$(CFG_DIR) \
	$(LIB_DIR)/stm/STM32L0xx_HAL_Driver/Inc \
	$(LIB_DIR)/rtt \
	$(LIB_DIR)/LoRaWAN/Utilities \
	$(LIB_DIR)/stm/include \
	$(LIB_DIR)/loramac-node/src/peripherals/soft-se \
	$(LIB_DIR)/loramac-node/src/radio \
	$(LIB_DIR)/loramac-node/src/radio/sx1276 \
	$(LIB_DIR)/loramac-node/src/mac \
	$(LIB_DIR)/loramac-node/src/mac/region

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

ALLDEP := $(MAKEFILE_LIST)

################################################################################
# Are we runnign a target that builds?                                         #
################################################################################

# A list of make targets for which version and dependency files should not be
# generated and included. That's generally any target that does not build
# firmware.
NOBUILD := clean .clean-obj .clean-out flash gdbserver jlink ozone

# We only need to generate dependency files if the make target is empty or if it
# is not one of the targets in NOBUILD
ifeq (,$(MAKECMDGOALS))
building=1
else
ifneq (,$(filter-out $(NOBUILD),$(MAKECMDGOALS)))
building=1
endif
endif

################################################################################
# Version and build date information                                           #
################################################################################

# Only generate version and build time information if we are actually running a
# target that builds something.
ifeq (1,$(building))

build_date := $(shell date "+%Y-%b-%d %H:%M:%S %Z")

# In order to properly re-build the firmware when the version string changes,
# e.g., as a result of a git tag begin added or removed, the output of git
# describe needs to be written to a file other project files can depend on, and
# the generated file only needs to be updated when the version string really
# changes. This is what the shell script below implements. The version string
# generated by git describe is written to a file in $(OBJ_DIR) which is then
# pulled in as a dependency.
#
# And since we have no way of telling which source files depend on the version
# string(s), we add the generated version files to ALLDEP which all other
# targets and files depend on. This will rebuild the entire project whenever any
# of the version strings change.

git_describe := git describe --abbrev=8 --always --tags --dirty=' (modified)'

tmp := $(shell \
	mkdir -p $(OBJ_DIR); \
	f=$(OBJ_DIR)/version; \
	cur=`$(git_describe) 2>/dev/null`; \
	[ -r $$f ] && prev=`cat $$f`; \
	[ -n "$$prev" -a "$$prev" = "$$cur" ] && exit 0; \
	echo "$$cur" > $$f)
version := $(strip $(shell cat $(OBJ_DIR)/version))

ALLDEP += $(OBJ_DIR)/version

tmp := $(shell \
	mkdir -p $(OBJ_DIR); \
	f=$(OBJ_DIR)/loramac_version; \
	cur=`(cd lib/loramac-node; $(git_describe) 2>/dev/null)`; \
	[ -r $$f ] && prev=`cat $$f`; \
	[ -n "$$prev" -a "$$prev" = "$$cur" ] && exit 0; \
	echo "$$cur" > $$f)
loramac_version := $(strip $(shell cat $(OBJ_DIR)/loramac_version))

ALLDEP += $(OBJ_DIR)/loramac_version

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

CFLAGS_RELEASE += -D'UART_BAUDRATE=${BAUDRATE}'
# CFLAGS += -D'USE_HAL_DRIVER'
CFLAGS += -DUSE_FULL_LL_DRIVER
CFLAGS += -DSOFT_SE
#CFLAGS += -DSECURE_ELEMENT_PRE_PROVISIONED
CFLAGS += -DLORAMAC_CLASSB_ENABLED

CFLAGS += -DREGION_AS923_DEFAULT_CHANNEL_PLAN=CHANNEL_PLAN_GROUP_AS923_1
CFLAGS += -DREGION_CN470_DEFAULT_CHANNEL_PLAN=CHANNEL_PLAN_20MHZ_TYPE_A

CFLAGS += -DBUILD_DATE='"$(build_date)"'
CFLAGS += -DVERSION='"$(version)"'
CFLAGS += -DLORAMAC_VERSION='"$(loramac_version)"'

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

SRC_FILES += $(foreach dir,$(SRC_DIR),$(wildcard $(dir)/*.c))
OBJ_C = $(SRC_FILES:%.c=$(OBJ_DIR)/$(TYPE)/%.o)
OBJ_S = $(ASM_SOURCES:%.s=$(OBJ_DIR)/$(TYPE)/%.o)
OBJ = $(OBJ_C) $(OBJ_S)
DEP = $(OBJ:%.o=%.d)

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

################################################################################
# Include dependencies                                                         #
################################################################################

ifeq (1,$(building))
-include $(DEP)
endif