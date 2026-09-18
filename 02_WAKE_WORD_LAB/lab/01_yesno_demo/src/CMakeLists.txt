# Mirrors espressif/esp-tflite-micro's examples/micro_speech/main/CMakeLists.txt,
# minus the "test_data" component (host-side unit-test WAV fixtures, not
# needed for on-device wake-word detection).
#
# Populate main/ with the source files listed in Step 1 of the KR doc before
# building - this file's SRCS list is already paired with that copy command.

set(priv_reqs spi_flash driver esp_timer)
# gpio/i2s drivers live in separate components since IDF v5.3 (required on v6+)
if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
    list(APPEND priv_reqs esp_driver_gpio esp_driver_i2s)
endif()

idf_component_register(
    SRCS main.cc main_functions.cc audio_provider.cc feature_provider.cc
         no_micro_features_data.cc yes_micro_features_data.cc
         model.cc recognize_commands.cc command_responder.cc
         micro_features_generator.cc ringbuf.c i2s_setup.cc
    PRIV_REQUIRES ${priv_reqs}
    INCLUDE_DIRS "")

# Reduce the level of paranoia to be able to compile the upstream sources
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-maybe-uninitialized
    -Wno-missing-field-initializers
    -Wno-error=sign-compare
    -Wno-error=double-promotion
    -Wno-type-limits)
