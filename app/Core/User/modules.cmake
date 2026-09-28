# Product modules. Keep source order stable to avoid unnecessary link-layout changes.
set(PATROL_PRODUCT_SOURCES
    Core/User/board/src/abox_product_port.c
    Core/User/motion/src/absolute_encoder.c
    Core/User/common/src/app_log.c
    Core/User/system/src/patrol_config.c
    Core/User/system/src/mqtt_config_adapter.c
    Core/User/system/src/app_version.c
    Core/User/cloud/src/cloud_service.c
    Core/User/cloud/src/patrol_enrollment.c
    Core/User/cloud/src/cloud_commands.c
    Core/User/cloud/src/cloud_messages.c
    Core/User/cloud/src/patrol_mqtt_profile.c
    Core/User/cloud/src/cloud_tx.c
    Core/User/cloud/src/cloud_protocol.c
    Core/User/cloud/src/chassis_cloud_protocol.c
    Core/User/motion/src/chassis_control.c
    Core/User/common/src/fixed_point.c
    Core/User/mqtt/src/mqtt_core_adapter.c
    Core/User/mqtt/src/mqtt_runtime.c
    Core/User/mqtt/src/mqtt_trial_manager.c
    Core/User/ota/src/ota_info.c
    Core/User/ota/src/ota_service.c
    Core/User/motion/src/patrol_canopen.c
    Core/User/motion/src/patrol_diagnostics.c
    Core/User/cloud/src/remote_service.c
    Core/User/motion/src/steering_angle.c
    Core/User/motion/src/bench_steering.c
    Core/User/motion/src/bench_drive.c
    Core/User/common/src/time_base.c
    Core/User/board/src/vehicle_power_output.c
    Core/User/board/src/vehicle_power_port.c
    Core/User/motion/src/patrol_runtime.c
    Core/User/motion/src/center_calibration.c
    Core/User/system/src/patrol_rtos.c
    ${CMAKE_SOURCE_DIR}/../shared/boot_state_compat.c
    ${CMAKE_SOURCE_DIR}/../shared/fault_mailbox_compat.c
)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${PATROL_PRODUCT_SOURCES})

# Public interfaces may be used by other product modules and generated entry points.
foreach(module IN ITEMS system motion cloud mqtt ota board common)
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/User/${module}/include")
    # Internal headers are visible only while compiling their owning module.
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Core/User/${module}/private")
        foreach(source IN LISTS PATROL_PRODUCT_SOURCES)
            if(source MATCHES "^Core/User/${module}/src/")
                set_property(SOURCE "${source}" APPEND PROPERTY INCLUDE_DIRECTORIES
                    "${CMAKE_CURRENT_SOURCE_DIR}/Core/User/${module}/private")
            endif()
        endforeach()
    endif()
endforeach()
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../shared/include")
