from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    cloud = "\n".join((root / "app/Core/User/cloud" / name).read_text(encoding="utf-8")
                      for name in ("src/cloud_service.c", "private/cloud_internal.h"))
    uart_source = (root / "app/Core/Src/usart.c").read_text(encoding="utf-8")
    ioc = (root / "app/Patrol_Vehicle_Chassis.ioc").read_text(encoding="utf-8")
    cmake = (root / "app/CMakeLists.txt").read_text(encoding="utf-8")
    service = (root / "platform/components/cloud/src/abox_app_service.c").read_text(encoding="utf-8")
    assert "Dma.USART1_RX.0.Mode=DMA_CIRCULAR" in ioc
    assert "hdma_usart1_rx.Init.Mode = DMA_CIRCULAR" in uart_source
    assert "CLOUD_RX_DMA_SIZE (EC800_AT_RAW_CHUNK_MAX + 64U)" in cloud
    assert "CLOUD_RX_RING_SIZE 4096U" in cloud
    assert "ABoxEc800Rx_OnDmaEvent(&g_cloud.ec800_rx, size)" in cloud
    assert "ABoxAppService_Init(&g_app_service, &g_cloud.at_core, &g_cloud.ec800_rx" in cloud
    assert ".restart_rx = app_restart_rx" in cloud
    assert "ABoxEc800Rx_Drain(service->rx" in service
    assert "ABoxEc800At_AbortAll(service->at, ABOX_EC800_RESULT_ERROR)" in service
    assert "ABoxEc800At_SetRxOverflowCount" in service
    assert "abox_platform_attach_standard_app(" in cmake
    assert not list((root / "app/Core/User").glob("*/src/ec800_at.c"))
    print("uart1_circular_dma_contract: PASS")


if __name__ == "__main__":
    main()
