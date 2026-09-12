# Milk-V Duo FreeRTOS DMA

This project provides a hardware abstraction layer (HAL) for the DMA controller used by FreeRTOS running on the small C906 core (C906 @ 700 MHz) of the `Milk-V Duo`.

The DMAC in the SG2002 SoC supports two different transfer modes: Basic mode and Linked List mode. It supports data transfers between RAM and RAM, Memory and Device, Device and Memory, Device and Device, and more. See the SG2002 datasheet for more details.

# Quick Start

## 1. Environment

Refer to the official SDK provided by Milk-V Duo, [duo-buildroot-sdk](https://github.com/milkv-duo/duo-buildroot-sdk), to set up the environment for building the SDK.

## 2. Add the Project to the SDK

### 1. Find the HAL Directory

Find the `hal` directory under FreeRTOS.

Copy the `dma` directory from this repository to:

```text
duo-buildroot-sdk-1.1.4/freertos/cvitek/hal/cv181x
```

or the corresponding directory in your SDK version. The path may vary between different SDK versions.

Then, update the contents of the two files under the `config` directory in your SDK.

### 2. Demo

The mailbox interface can be used to test the DMA driver by sending a command request from Linux to FreeRTOS.

See `mailbox_dma.c` under the `demo` directory.

To run the DMA test, add the corresponding command to the `prvCmdQuRunTask` function in:

```text
duo-buildroot-sdk-1.1.4/freertos/cvitek/task/comm/src/riscv64/comm_main.c
```

# Test

This project has been tested on the `Milk-V Duo 256M` with the official `duo-buildroot-sdk` version 1.1.4.

# Note

Linux also has its own DMA driver. Therefore, if both Linux and FreeRTOS may access the DMAC at the same time, the DMA driver must be modified to coordinate access between the two systems.

This project currently does not provide a mutex to protect shared DMA resources. A suitable synchronization mechanism should be added to the product-level implementation when the DMAC may be accessed by both Linux and FreeRTOS.
