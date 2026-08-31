from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    cloud = (root / "app/Core/User/Src/cloud_service.c").read_text(encoding="utf-8")
    uart_source = (root / "app/Core/Src/usart.c").read_text(encoding="utf-8")
    ioc = (root / "app/PatrolVehicleChassis_VCU_App.ioc").read_text(encoding="utf-8")
    cmake = (root / "app/CMakeLists.txt").read_text(encoding="utf-8")
    assert "Dma.USART1_RX.0.Mode=DMA_CIRCULAR" in ioc
    assert "hdma_usart1_rx.Init.Mode = DMA_CIRCULAR" in uart_source
    assert "CLOUD_RX_DMA_SIZE (EC800_AT_RAW_CHUNK_MAX + 64U)" in cloud
    assert "CLOUD_RX_RING_SIZE 4096U" in cloud
    assert "ABoxEc800Rx_OnDmaEvent(&g_ec800_rx, size)" in cloud
    assert "ABoxEc800Rx_Drain(&g_ec800_rx" in cloud
    assert "ABoxEc800At_AbortAll(&g_at_core, ABOX_EC800_RESULT_ERROR)" in cloud
    assert "EC800_AT_SetRxOverflowCount" in cloud
    assert "Core/User/Src/ec800_at.c" not in cmake
    assert "abox::ec800" in cmake
    assert not (root / "app/Core/User/Src/ec800_at.c").exists()
    print("uart1_circular_dma_contract: PASS")


if __name__ == "__main__":
    main()
