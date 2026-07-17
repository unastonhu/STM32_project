# Smart Elevator Energy Efficiency System (2026 Embedded Systems Competition Entry)

[![Platform](https://img.shields.io/badge/Platform-STM32F407-blue.svg)](https://www.st.com/)
[![Framework](https://img.shields.io/badge/Framework-FreeRTOS-green.svg)](https://www.freertos.org/)
[![Build](https://img.shields.io/badge/Build-CMake-orange.svg)](https://cmake.org/)

This repository contains the firmware and project configuration for our entry in the **2026 Embedded Systems Competition (嵌入式芯片与系统设计竞赛)**. It features an AI-driven smart elevator traffic analysis and energy efficiency model.

本项目为 **2026年全国大学生嵌入式芯片与系统设计竞赛** 参赛作品。系统基于 STM32F407 开发，通过 AI 算法对电梯流量进行分析，实现智能调度与节能控制。

---

##  Features | 功能特性

- **Smart Traffic Analysis**: AI-driven models to analyze and predict elevator traffic. (基于 AI 的电梯流量分析与预测)
- **Multi-tasking RTOS**: Integrated with **FreeRTOS** for robust task scheduling and resource management. (集成 FreeRTOS 实现多任务调度)
- **Mass Storage**: **FATFS** file system integration via SD Card for offline data logging. (集成 FATFS 文件系统，支持 SD 卡离线数据日志保存)
- **Virtual COM Port**: **USB CDC (Virtual COM Port)** implementation for high-speed data transmission and debugging. (支持 USB CDC 虚拟串口，用于高速数据传输与调试)
- **Modern Build System**: Configured with **CMake** and **VS Code + PlatformIO/ESP-IDF** alignment. (基于 CMake 构建系统，无缝对接现代 IDE 开发流程)

---

## Hardware & Tools | 硬件与开发工具

### Hardware
- **MCU**: STM32F407VET6/ZGT6
- **Peripherals Used**: USB OTG (CDC), SDIO (FATFS), I2C (Sensors), GPIO, USART

### Software Toolchain
- **IDE**: VS Code (with CMake Tools / Cortex-Debug)
- **OS**: FreeRTOS
- **FATFS Version**: R0.12c (or your current version)

---

##  Repository Structure | 目录结构

- `Core/`: Core application logic and main loop.
- `Drivers/`: Hardware abstraction layer and board support package.
- `FATFS/`: File system configuration and SD card driver.
- `USB_DEVICE/`: USB CDC stack and virtual serial port implementation.
- `CMakeLists.txt`: Global build configuration.
- `project.ioc`: STM32CubeMX configuration file.
