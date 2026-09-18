# Adapts Edge Impulse's official ESP32 CMake integration pattern
# (github.com/edgeimpulse/example-standalone-inferencing-espressif-esp32,
# main/CMakeLists.txt) to this project's PlatformIO src/ layout - the
# official example globs an SDK folder that sits one level ABOVE main/;
# here everything lives directly inside src/ instead.
#
# Before building, export your Impulse from the Edge Impulse Studio as a
# "C++ library" (Deployment tab), unzip it, and copy ONLY these three
# folders into THIS src/ directory (see Step 2 of the KR doc):
#   edge-impulse-sdk/
#   tflite-model/
#   model-parameters/
# Do not copy the exported main/, CMakeLists.txt, sdkconfig, partitions.csv,
# etc. - this project already provides equivalents adapted to PlatformIO.

set(MODEL_FOLDER .)
set(EI_SDK_FOLDER edge-impulse-sdk)

# Friendly failure instead of a cryptic "include() could not find" error
# when the SDK hasn't been copied in yet.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${EI_SDK_FOLDER}/cmake/utils.cmake")
    message(FATAL_ERROR
        "src/edge-impulse-sdk/ not found.\n"
        "Export your Impulse from the Edge Impulse Studio as a 'C++ library', "
        "unzip it, and copy the edge-impulse-sdk/, tflite-model/, and "
        "model-parameters/ folders into this src/ directory before building. "
        "See Step 2 of the KR doc.")
endif()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    # Enable Espressif's ESP-NN accelerated kernels (generic + S3-specific).
    add_definitions(-DEI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=1)
    add_definitions(-DEIDSP_USE_ESP_DSP=1)
    if(${IDF_TARGET} STREQUAL "esp32s3")
        add_definitions(-DEI_CLASSIFIER_TFLITE_ENABLE_ESP_NN_S3=1)
    endif()
endif()

set(include_dirs
    ${MODEL_FOLDER}
    ${MODEL_FOLDER}/tflite-model
    ${MODEL_FOLDER}/model-parameters
    ${EI_SDK_FOLDER}
    ${EI_SDK_FOLDER}/porting/espressif/esp-dsp/modules/fft/include
    ${EI_SDK_FOLDER}/porting/espressif/esp-dsp/modules/common/include
)

# utils.cmake (part of the SDK you just copied in) provides
# RECURSIVE_FIND_FILE_EXCLUDE_DIR, used below to glob every SDK/model source
# file instead of listing them by hand (the SDK contains hundreds of files
# and differs slightly between Edge Impulse versions).
include(${EI_SDK_FOLDER}/cmake/utils.cmake)

RECURSIVE_FIND_FILE_EXCLUDE_DIR(SDK_CPP_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.cpp")
RECURSIVE_FIND_FILE_EXCLUDE_DIR(MODEL_FILES "tflite-model" "CMSIS" "*.cpp")
RECURSIVE_FIND_FILE_EXCLUDE_DIR(SDK_CC_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.cc")
RECURSIVE_FIND_FILE_EXCLUDE_DIR(SDK_C_FILES "${EI_SDK_FOLDER}" "CMSIS" "*.c")
RECURSIVE_FIND_FILE_EXCLUDE_DIR(S_FILES_ESP_NN "${EI_SDK_FOLDER}/porting/espressif/ESP-NN" "CMSIS" "*.s")
RECURSIVE_FIND_FILE_EXCLUDE_DIR(S_FILES_ESP_DSP "${EI_SDK_FOLDER}/porting/espressif/esp-dsp/modules/fft" "CMSIS" "*.s")

set(EI_SOURCE_FILES
    ${SDK_C_FILES}
    ${SDK_CC_FILES}
    ${SDK_CPP_FILES}
    ${MODEL_FILES}
    ${S_FILES_ESP_NN}
    ${S_FILES_ESP_DSP}
)

# gpio/i2s drivers live in separate components since IDF v5.3 (required on
# v6+); before that they are part of the monolithic "driver" component.
# (Same version check as 01_yesno_demo/src/CMakeLists.txt.)
set(priv_reqs driver esp_timer)
if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
    list(APPEND priv_reqs esp_driver_gpio esp_driver_i2s)
endif()

idf_component_register(
    SRCS "main.cc" ${EI_SOURCE_FILES}
    PRIV_REQUIRES ${priv_reqs}
    INCLUDE_DIRS "${include_dirs}"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++17
    -Wno-maybe-uninitialized
    -Wno-missing-field-initializers)
