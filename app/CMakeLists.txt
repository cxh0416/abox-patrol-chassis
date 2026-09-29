cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Patrol_Vehicle_Chassis_App)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

file(STRINGS "${CMAKE_SOURCE_DIR}/../VERSION" PATROL_CHASSIS_DEFAULT_VERSION LIMIT_COUNT 1)
string(STRIP "${PATROL_CHASSIS_DEFAULT_VERSION}" PATROL_CHASSIS_DEFAULT_VERSION)
if(NOT PATROL_CHASSIS_DEFAULT_VERSION MATCHES "^patrol-chassis-[0-9]+\\.[0-9]+\\.[0-9]+([A-Za-z0-9._+-]*)?$")
    message(FATAL_ERROR "Invalid or missing patrol chassis VERSION")
endif()
set(PATROL_CHASSIS_APP_VERSION_OVERRIDE "" CACHE STRING "Override the VERSION file for this build")
if(PATROL_CHASSIS_APP_VERSION_OVERRIDE)
    if(NOT PATROL_CHASSIS_APP_VERSION_OVERRIDE MATCHES "^patrol-chassis-[0-9]+\\.[0-9]+\\.[0-9]+([A-Za-z0-9._+-]*)?$")
        message(FATAL_ERROR "Invalid patrol chassis App version override")
    endif()
    set(PATROL_CHASSIS_APP_VERSION "${PATROL_CHASSIS_APP_VERSION_OVERRIDE}")
else()
    set(PATROL_CHASSIS_APP_VERSION "${PATROL_CHASSIS_DEFAULT_VERSION}")
endif()
option(PATROL_CHASSIS_OTA_AUTO_SEED "Seed first stable OTA slot" ON)
option(PATROL_DRIVE_EXECUTOR_ENABLED "Compile bounded drive executor" OFF)
option(PATROL_STEERING_EXECUTOR_ENABLED "Compile bounded steering executor" OFF)
option(PATROL_CHASSIS_ACTUATION_ENABLED "Allow CANopen motion output after hardware validation" OFF)
option(PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED "Allow read-only CANopen and encoder polling" ON)
option(PATROL_CHASSIS_POWER_OUTPUT_ENABLED "Allow the confirmed PA0 vehicle relay-supply output" ON)
option(PATROL_CHASSIS_APP_FAULT_INJECTION_TEST "Build intentional Boot confirmation fault" OFF)
option(PATROL_CHASSIS_MQTT_LAB_TEST "Enable RAM-only MQTT runtime acceptance commands" OFF)
option(PATROL_MQTT_TRIAL_FAULT_INJECTION "Enable WP7.2 test-only MQTT trial fault injection" OFF)
set(PATROL_MQTT_TRIAL_FAULT_KIND "0" CACHE STRING "0=none, 1=stall subscribe, 2=drop proof, 3=drop proof then force restore failure")
if(PATROL_MQTT_TRIAL_FAULT_INJECTION AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "MQTT trial fault injection is permitted only in Debug builds")
endif()
if(PATROL_MQTT_TRIAL_FAULT_INJECTION AND NOT PATROL_CHASSIS_MQTT_LAB_TEST)
    message(FATAL_ERROR "MQTT trial fault injection requires the lab test command gate")
endif()
if(NOT PATROL_MQTT_TRIAL_FAULT_KIND MATCHES "^[0-3]$")
    message(FATAL_ERROR "Invalid MQTT trial fault kind")
endif()
set(PATROL_CHASSIS_APP_TEST_FAULT_KIND "0" CACHE STRING "1=HardFault, 2=IWDG")

# Credentials may be provisioned later in the protected Config page.  An
# unprovisioned Release remains cloud-off and motion-off by design.

set(ABOX_PRODUCT_CONFIG_DIR "${CMAKE_SOURCE_DIR}/../platform_config")
set(ABOX_PLATFORM_SCHEDULER "FREERTOS")
add_subdirectory("${CMAKE_SOURCE_DIR}/../platform" abox_platform_build)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/STM32F105XX_FLASH.ld"
)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Product source files and module-scoped include paths.
include(Core/User/modules.cmake)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:PATROL_EC800_AT_DIAG_UART5=1>
    $<$<CONFIG:Debug>:PATROL_EC800_AT_TRACE=1>
    PATROL_OTA_AUTO_SEED=$<BOOL:${PATROL_CHASSIS_OTA_AUTO_SEED}>
    PATROL_CHASSIS_ACTUATION_ENABLED=$<BOOL:${PATROL_CHASSIS_ACTUATION_ENABLED}>
    PATROL_STEERING_EXECUTOR_ENABLED=$<BOOL:${PATROL_STEERING_EXECUTOR_ENABLED}>
    PATROL_DRIVE_EXECUTOR_ENABLED=$<BOOL:${PATROL_DRIVE_EXECUTOR_ENABLED}>
    PATROL_CHASSIS_POWER_OUTPUT_ENABLED=$<BOOL:${PATROL_CHASSIS_POWER_OUTPUT_ENABLED}>
    PATROL_CHASSIS_MQTT_LAB_TEST=$<BOOL:${PATROL_CHASSIS_MQTT_LAB_TEST}>
    PATROL_MQTT_TRIAL_FAULT_INJECTION=$<BOOL:${PATROL_MQTT_TRIAL_FAULT_INJECTION}>
    PATROL_MQTT_TRIAL_FAULT_KIND=${PATROL_MQTT_TRIAL_FAULT_KIND}
)
if(PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED=0>
        $<$<NOT:$<CONFIG:Debug>>:PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED=1>
    )
else()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED=0
    )
endif()
if(PATROL_CHASSIS_MQTT_LAB_TEST)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        PATROL_EC800_AT_DIAG_UART5=1
        PATROL_EC800_AT_TRACE=1
    )
endif()
if(PATROL_CHASSIS_APP_FAULT_INJECTION_TEST)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/User/system/src/app_test.c)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        PATROL_CHASSIS_APP_FAULT_INJECTION_TEST=1
        PATROL_CHASSIS_APP_TEST_FAULT_KIND=${PATROL_CHASSIS_APP_TEST_FAULT_KIND}
    )
endif()
if(PATROL_CHASSIS_APP_VERSION)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE PATROL_APP_FW_VERSION="${PATROL_CHASSIS_APP_VERSION}")
endif()

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    # Add user defined libraries
)

abox_platform_attach_components(${CMAKE_PROJECT_NAME}
    SCHEDULER FREERTOS HARDWARE stm32f105_ec800_v1
    COMPONENTS core board_io cjson ec_power ec800 ec800_ufs ec800_command_port ec800_tls
               mqtt_tls mqtt_runtime mqtt_trial mqtt_v4 mqtt_ec800_rx
               mqtt_ec800 enrollment
               enrollment_ec800_http boot_v2_common https_ufs_downloader
               boot_v2_app)
abox_platform_attach_standard_port(${CMAKE_PROJECT_NAME} HARDWARE stm32f105_ec800_v1)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
)
