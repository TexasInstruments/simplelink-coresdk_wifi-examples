SIMPLELINK_WIFI_SDK_INSTALL_DIR ?= $(abspath ../../../../../../..)

include $(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/imports.mak

CC = "$(TICLANG_ARMCOMPILER)/bin/tiarmclang"
LNK = "$(TICLANG_ARMCOMPILER)/bin/tiarmclang"

SYSCONFIG_GUI_TOOL = $(dir $(SYSCONFIG_TOOL))sysconfig_gui$(suffix $(SYSCONFIG_TOOL))
SYSCFG_CMD_STUB = $(SYSCONFIG_TOOL) --compiler ticlang --product $(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/.metadata/product.json --product $(SIMPLELINK_WIFI_TOOLBOX_INSTALL_DIR)/.metadata/product.json
SYSCFG_GUI_CMD_STUB = $(SYSCONFIG_GUI_TOOL) --compiler ticlang --product $(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/.metadata/product.json --product $(SIMPLELINK_WIFI_TOOLBOX_INSTALL_DIR)/.metadata/product.json
SYSCFG_FILES := $(shell $(SYSCFG_CMD_STUB) --listGeneratedFiles --listReferencedFiles --output . ../../freertos/psaRawKeyAgreement.syscfg)

SYSCFG_C_FILES = $(filter %.c,$(SYSCFG_FILES))
SYSCFG_H_FILES = $(filter %.h,$(SYSCFG_FILES))
SYSCFG_OPT_FILES = $(filter %.opt,$(SYSCFG_FILES))

# Enable verbose output by setting VERBOSE=1
V := @
ifeq ($(VERBOSE), 1)
  V :=
endif

OBJECTS = psaRawKeyAgreement.obj freertos_main_freertos.obj $(patsubst %.c,%.obj,$(notdir $(SYSCFG_C_FILES)))

NAME = psaRawKeyAgreement


CFLAGS += -I../.. \
    -I. \
    $(addprefix @,$(SYSCFG_OPT_FILES)) \
    -Oz \
    -DDeviceFamily_CC35XX \
    "-I$(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/source" \
    -DMBEDTLS_CONFIG_FILE="<configs/config-hsm.h>" \
    -DMBEDTLS_PSA_CRYPTO_CONFIG_FILE="<configs/config-psa-crypto-hsm.h>" \
    "-I$(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/source/third_party/mbedtls/include" \
    "-I$(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/source/third_party/mbedtls/ti" \
    "-I$(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/source/third_party/mbedtls/ti/port" \
    "-I$(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/kernel/freertos" \
    "-I$(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/source/ti/posix/ticlang" \
    "-I$(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/source/third_party/freertos/include" \
    "-I$(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/source/third_party/freertos/portable/GCC/ARM_CM33_NTZ/non_secure" \
    -gdwarf-3 \
    -mcpu=cortex-m33 \
    -mfloat-abi=hard \
    -mfpu=fpv5-sp-d16 \
    -Wall

LFLAGS += "-L$(SIMPLELINK_WIFI_SDK_INSTALL_DIR)/source" \
    ti_utils_build_linker.cmd.genlibs \
    ../../freertos/ticlang/cc35xx_freertos.cmd \
    "-Wl,-m,$(NAME).map" \
    -Wl,--rom_model \
    -Wl,--warn_sections \
    "-L$(TICLANG_ARMCOMPILER)/lib" \
    -llibc.a

all: $(NAME).out

.INTERMEDIATE: syscfg
$(SYSCFG_FILES): syscfg
	@ echo generation complete

syscfg: ../../freertos/psaRawKeyAgreement.syscfg
	@ echo Generating configuration files...
	$(V) $(SYSCFG_CMD_STUB) --output $(@D) $<


# Helpful hint that the user needs to use a standalone SysConfig installation
$(SYSCONFIG_GUI_TOOL):
	$(error $(dir $(SYSCONFIG_TOOL)) does not contain the GUI framework \
        necessary to launch the SysConfig GUI.  Please set SYSCONFIG_TOOL \
        (in your SDK's imports.mak) to a standalone SysConfig installation \
        rather than one inside CCS)

syscfg-gui: ../../freertos/psaRawKeyAgreement.syscfg $(SYSCONFIG_GUI_TOOL)
	@ echo Opening SysConfig GUI
	$(V) $(SYSCFG_GUI_CMD_STUB) $<


define C_RULE
$(basename $(notdir $(1))).obj: $(1) $(SYSCFG_H_FILES)
	@ echo Building $$@
	$(V) $(CC) $(CFLAGS) -c $$< -o $$@
endef

$(foreach c_file,$(SYSCFG_C_FILES),$(eval $(call C_RULE,$(c_file))))

psaRawKeyAgreement.obj: ../../psaRawKeyAgreement.c $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) -c $< -o $@

freertos_main_freertos.obj: ../../freertos/main_freertos.c $(SYSCFG_H_FILES)
	@ echo Building $@
	$(V) $(CC) $(CFLAGS) -c $< -o $@

$(NAME).out: $(OBJECTS)
	@ echo linking $@
	$(V) $(LNK) -Wl,-u,_c_int00 $(OBJECTS)  $(LFLAGS) -o $(NAME).out

clean:
	@ echo Cleaning...
	$(V) $(RM) $(OBJECTS) > $(DEVNULL) 2>&1
	$(V) $(RM) $(NAME).out > $(DEVNULL) 2>&1
	$(V) $(RM) $(NAME).map > $(DEVNULL) 2>&1
	$(V) $(RM) $(call SLASH_FIXUP,$(SYSCFG_FILES)) > $(DEVNULL) 2>&1
