# Automatically generated build file. Do not edit.
COMPONENT_INCLUDES += /home/cedric/esp/esp-adf/components/audio_hal/include /home/cedric/esp/esp-adf/components/audio_hal/driver/es8388 /home/cedric/esp/esp-adf/components/audio_hal/driver/es8374 /home/cedric/esp/esp-adf/components/audio_hal/driver/es8311 /home/cedric/esp/esp-adf/components/audio_hal/driver/es7243 /home/cedric/esp/esp-adf/components/audio_hal/driver/zl38063 /home/cedric/esp/esp-adf/components/audio_hal/driver/zl38063/api_lib /home/cedric/esp/esp-adf/components/audio_hal/driver/zl38063/example_apps /home/cedric/esp/esp-adf/components/audio_hal/driver/zl38063/firmware
COMPONENT_LDFLAGS += -L$(BUILD_DIR_BASE)/audio_hal -laudio_hal -L/home/cedric/esp/esp-adf/components/audio_hal/driver/zl38063/firmware -lfirmware
COMPONENT_LINKER_DEPS += 
COMPONENT_SUBMODULES += 
COMPONENT_LIBRARIES += audio_hal
COMPONENT_LDFRAGMENTS += 
component-audio_hal-build: 
