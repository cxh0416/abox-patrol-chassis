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
set(ABOX_HARDWARE "stm32f105_ec800_v1" CACHE STRING "ABox MCU hardware target")

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
    if(ABOX_HARDWARE STREQUAL "gd32f105_ec800_v1")
        string(APPEND PATROL_CHASSIS_APP_VERSION "-gd")
    endif()
endif()
if(ABOX_HARDWARE STREQUAL "gd32f105_ec800_v1" AND NOT PATROL_CHASSIS_APP_VERSION MATCHES "^patrol-chassis-[0-9]+\\.[0-9]+\\.[0-9]+-gd$")
    message(FATAL_ERROR "GD32 V1 App version must be patrol-chassis-X.Y.Z-gd")
elseif(ABOX_HARDWARE STREQUAL "stm32f105_ec800_v1" AND PATROL_CHASSIS_APP_VERSION MATCHES "-gd$")
    message(FATAL_ERROR "STM32 V2 App version cannot end in -gd")
endif()
option(PATROL_CHASSIS_OTA_AUTO_SEED "Seed first stable OTA slot" ON)
option(PATROL_DRIVE_EXECUTOR_ENABLED "Compile bounded drive executor" OFF)
option(PATROL_STEERING_EXECUTOR_ENABLED "Compile bounded steering executor" OFF)
option(PATROL_CHASSIS_ACTUATION_ENABLED "Allow CANopen motion output after hardware validation" OFF)
# >>>>> CAR-PROFILE: START >>>>>
set(PATROL_ACTIVE_CAR "0" CACHE STRING "Active vehicle calibration profile: 0=disabled, 1..4=car")
if(NOT PATROL_ACTIVE_CAR MATCHES "^[0-4]$")
    message(FATAL_ERROR "PATROL_ACTIVE_CAR must be 0..4")
endif()
# <<<<< CAR-PROFILE: END <<<<<
# >>>>> CAN2-GATEWAY: START >>>>>
option(PATROL_CHASSIS_CAN2_GATEWAY_ENABLED "Bridge CAN2<->CAN1 for offline bench remote control" OFF)
if(PATROL_CHASSIS_CAN2_GATEWAY_ENABLED)
    message(FATAL_ERROR "Transparent CAN2 gateway bypasses motion arbitration; use the CAN2 keyboard command service")
endif()
# <<<<< CAN2-GATEWAY: END <<<<<
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
if(DEFINED ABOX_BOARD_VERSION)
    if((ABOX_HARDWARE STREQUAL "stm32f105_ec800_v1" AND NOT ABOX_BOARD_VERSION STREQUAL "v2") OR
       (ABOX_HARDWARE STREQUAL "gd32f105_ec800_v1" AND NOT ABOX_BOARD_VERSION STREQUAL "v1"))
        message(FATAL_ERROR "Patrol requires GD32/V1 or STM32/V2")
    endif()
endif()
add_subdirectory("${CMAKE_SOURCE_DIR}/../platform" abox_platform_build)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})
set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/STM32F105XX_FLASH.ld"
)

# Add STM32CubeMX generated sources
if(ABOX_HARDWARE STREQUAL "stm32f105_ec800_v1")
    add_subdirectory(cmake/stm32cubemx)
elseif(ABOX_HARDWARE STREQUAL "gd32f105_ec800_v1")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/Src/freertos.c Core/Src/syscalls.c
        Core/Src/sysmem.c Core/User/board/src/patrol_gd32_port.c)
else()
    message(FATAL_ERROR "Unsupported ABOX_HARDWARE: ${ABOX_HARDWARE}")
endif()

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Product source files and module-scoped include paths.
include(Core/User/modules.cmake)
if(ABOX_HARDWARE STREQUAL "stm32f105_ec800_v1")
    abox_platform_attach_standard_clock(${CMAKE_PROJECT_NAME})
endif()

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    ABOX_SERIAL_TRANSACTION_CHUNK_SIZE=32U
    ABOX_SERIAL_TRANSACTION_SLOTS=3U
    $<$<CONFIG:Debug>:PATROL_EC800_AT_DIAG_UART5=1>
    $<$<CONFIG:Debug>:PATROL_EC800_AT_TRACE=1>
    PATROL_OTA_AUTO_SEED=$<BOOL:${PATROL_CHASSIS_OTA_AUTO_SEED}>
    PATROL_CHASSIS_ACTUATION_ENABLED=$<BOOL:${PATROL_CHASSIS_ACTUATION_ENABLED}>
    # >>>>> CAR-PROFILE: START >>>>>
    PATROL_ACTIVE_CAR=${PATROL_ACTIVE_CAR}
    # <<<<< CAR-PROFILE: END <<<<<
    PATROL_STEERING_EXECUTOR_ENABLED=$<BOOL:${PATROL_STEERING_EXECUTOR_ENABLED}>
    PATROL_DRIVE_EXECUTOR_ENABLED=$<BOOL:${PATROL_DRIVE_EXECUTOR_ENABLED}>
    # >>>>> CAN2-GATEWAY: START >>>>>
    PATROL_CHASSIS_CAN2_GATEWAY_ENABLED=$<BOOL:${PATROL_CHASSIS_CAN2_GATEWAY_ENABLED}>
    # <<<<< CAN2-GATEWAY: END <<<<<
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
    abox::mqtt_digest
    abox::cloud_v4
    abox::ufs_journal
    abox::ec800_ufs_at
    # Add user defined libraries
)

if(ABOX_HARDWARE STREQUAL "stm32f105_ec800_v1")
    target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx)
    abox_platform_attach_standard_app(${CMAKE_PROJECT_NAME}
        PRODUCT_CONFIG_DIR "${ABOX_PRODUCT_CONFIG_DIR}" SCHEDULER FREERTOS)
    abox_platform_attach_serial_port(${CMAKE_PROJECT_NAME} HARDWARE ${ABOX_HARDWARE})
    abox_platform_attach_modem_port(${CMAKE_PROJECT_NAME} HARDWARE ${ABOX_HARDWARE})
else()
    abox_platform_attach_f105_app(${CMAKE_PROJECT_NAME}
        HARDWARE ${ABOX_HARDWARE} PRODUCT_CONFIG_DIR "${ABOX_PRODUCT_CONFIG_DIR}"
        SCHEDULER FREERTOS FREERTOS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source"
        FREERTOS_CONFIG_DIR "${CMAKE_SOURCE_DIR}/Core/Inc" CAN CAN_EVENTS)
    abox_platform_attach_gd32_legacy_can(${CMAKE_PROJECT_NAME})
    target_include_directories(${CMAKE_PROJECT_NAME} BEFORE PRIVATE "${CMAKE_SOURCE_DIR}/Core/User/board/gd32")
endif()
abox_platform_attach_watchdog_port(${CMAKE_PROJECT_NAME} HARDWARE ${ABOX_HARDWARE})
abox_platform_attach_serial_transaction(${CMAKE_PROJECT_NAME})

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
)
