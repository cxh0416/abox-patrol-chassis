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

set(PATROL_CHASSIS_APP_VERSION "" CACHE STRING "Override patrol chassis App semantic version")
set(PATROL_CHASSIS_MQTT_USERNAME "" CACHE STRING "Production MQTT username")
set(PATROL_CHASSIS_MQTT_PASSWORD "" CACHE STRING "Production MQTT password")
option(PATROL_CHASSIS_OTA_AUTO_SEED "Seed first stable OTA slot" ON)
option(PATROL_CHASSIS_ACTUATION_ENABLED "Allow CANopen motion output after bench approval" OFF)
option(PATROL_CHASSIS_READONLY_DIAGNOSTICS_ENABLED "Allow read-only CANopen and encoder polling" ON)
option(PATROL_CHASSIS_POWER_OUTPUT_ENABLED "Allow the confirmed PA0 vehicle relay-supply output" ON)
option(PATROL_CHASSIS_APP_FAULT_INJECTION_TEST "Build intentional Boot confirmation fault" OFF)
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

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/User/Src/abox_product_port.c
    Core/User/Src/absolute_encoder.c
    Core/User/Src/app_log.c
    Core/User/Src/vcu_config.c
    Core/User/Src/app_version.c
    Core/User/Src/cloud_service.c
    Core/User/Src/cloud_protocol.c
    Core/User/Src/chassis_cloud_protocol.c
    Core/User/Src/chassis_control.c
    Core/User/Src/chassis_power.c
    Core/User/Src/fixed_point.c
    Core/User/Src/mqtt_core.c
    Core/User/Src/mqtt_core_parser.c
    Core/User/Src/mqtt_core_port.c
    Core/User/Src/mqtt_qmtrecv.c
    Core/User/Src/ota_info.c
    Core/User/Src/ota_service.c
    Core/User/Src/patrol_canopen.c
    Core/User/Src/patrol_diagnostics.c
    Core/User/Src/remote_service.c
    Core/User/Src/time_base.c
    Core/User/Src/vehicle_power_output.c
    Core/User/Src/vehicle_power_port.c
    Core/User/Src/vcu_rtos.c
    ${CMAKE_SOURCE_DIR}/../shared/boot_state_compat.c
    ${CMAKE_SOURCE_DIR}/../shared/fault_mailbox_compat.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Core/User/Inc
    ${CMAKE_SOURCE_DIR}/../shared/include
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:VCU_EC800_AT_DIAG_UART5=1>
    $<$<CONFIG:Debug>:VCU_EC800_AT_TRACE=1>
    VCU_OTA_AUTO_SEED=$<BOOL:${PATROL_CHASSIS_OTA_AUTO_SEED}>
    PATROL_CHASSIS_ACTUATION_ENABLED=$<BOOL:${PATROL_CHASSIS_ACTUATION_ENABLED}>
    PATROL_CHASSIS_POWER_OUTPUT_ENABLED=$<BOOL:${PATROL_CHASSIS_POWER_OUTPUT_ENABLED}>
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
if(PATROL_CHASSIS_APP_FAULT_INJECTION_TEST)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE Core/User/Src/app_test.c)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        PATROL_CHASSIS_APP_FAULT_INJECTION_TEST=1
        PATROL_CHASSIS_APP_TEST_FAULT_KIND=${PATROL_CHASSIS_APP_TEST_FAULT_KIND}
    )
endif()
if(PATROL_CHASSIS_MQTT_USERNAME)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE VCU_CONFIG_DEFAULT_MQTT_USER="${PATROL_CHASSIS_MQTT_USERNAME}")
endif()
if(PATROL_CHASSIS_MQTT_PASSWORD)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE VCU_CONFIG_DEFAULT_MQTT_PASS="${PATROL_CHASSIS_MQTT_PASSWORD}")
endif()
if(PATROL_CHASSIS_APP_VERSION)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE VCU_APP_FW_VERSION="${PATROL_CHASSIS_APP_VERSION}")
endif()

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    abox::ec800

    # Add user defined libraries
)

abox_platform_attach(${CMAKE_PROJECT_NAME})
abox_platform_attach_boot_v2_app(${CMAKE_PROJECT_NAME})

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_PROJECT_NAME}.bin
)
