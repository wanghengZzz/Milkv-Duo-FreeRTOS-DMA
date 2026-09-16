# Milk-V Duo FreeRTOS DMA

This project provides a hardware abstraction layer (HAL) for the DMA controller used by FreeRTOS running on the small C906 core (C906 @ 700 MHz) of the `Milk-V Duo`.

The DMAC in the SG2002 SoC supports two transfer modes: Basic mode and Linked List mode. It supports data transfers between RAM and RAM, Memory and Device, Device and Memory, Device and Device, and more. See the SG2002 datasheet for more details.

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

# DMA Features

The driver currently supports:

- Basic DMA transfers
- Linked List transfers
- Polling-based transfer completion
- Interrupt-based transfer completion
- Memory-to-Memory transfers
- Memory-to-Device transfers
- Device-to-Memory transfers
- DMA channel synchronization using FreeRTOS synchronization primitives

The driver uses synchronization mechanisms to protect DMA resources and notify tasks when a transfer is completed. Each DMA channel has its own lock and completion semaphore, while a shared DMA lock protects resources shared across channels.

# Test

This project has been tested on the `Milk-V Duo 256M` with the official `duo-buildroot-sdk` version 1.1.4.

The test code covers DMA transfers in both Basic and Linked List modes, including polling and interrupt-driven transfers.

# Synchronization

The DMA driver uses FreeRTOS synchronization primitives to coordinate access to DMA resources.

Each DMA channel provides:

- A mutex to protect operations on the channel.
- A completion semaphore used to notify the waiting task when the DMA transfer has completed.

A shared DMA mutex is also used to protect resources that are shared across DMA channels.

This allows multiple tasks to use different DMA channels while preventing concurrent access to the same channel or shared DMA resources.

# Note

Linux also has its own DMA driver. Therefore, if Linux and FreeRTOS may access the same DMAC hardware at the same time, access between the two systems must still be coordinated.

The synchronization mechanisms implemented in this project protect DMA resources within the FreeRTOS environment. They do not by themselves provide synchronization between Linux and FreeRTOS.
