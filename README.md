# STM32-Based Electronic Nose & Air Quality Gateway (2026 Embedded Systems Competition Entry)

[![Platform](https://img.shields.io/badge/MCU-STM32F407-blue.svg)](https://www.st.com/)
[![Co-Processor](https://img.shields.io/badge/Gateway-ESP32-red.svg)](https://www.espressif.com/)
[![Framework](https://img.shields.io/badge/Framework-FreeRTOS-green.svg)](https://www.freertos.org/)
[![Build](https://img.shields.io/badge/Build-CMake-orange.svg)](https://cmake.org/)

This repository contains the firmware and project configuration for our entry in the **2026 Embedded Systems Competition (嵌入式芯片与系统设计竞赛)**. The project implements a multi-sensor "Electronic Nose" system for localized environmental monitoring and smart actuation, with cloud connectivity routed through an ESP32 co-processor.

本项目为 **2026年全国大学生嵌入式芯片与系统设计竞赛** 参赛作品。系统基于 STM32F407 核心板开发，构建了一套多传感器融合的“电子鼻”空气质量监测与控制网关系统，并通过 ESP32 实现间接物联网云接入。

---

##  Features | 功能特性

- **Multi-Sensor Electronic Nose (电子鼻气体检测)**
  - Real-time monitoring of **VOCs**, **eCO2**, and **AQI**. (实时监测 VOC、等效二氧化碳、空气质量指数等)
- **Local Smart Actuation (本地执行控制)**
  - Hard real-time GPIO control for driving external execution modules (e.g., ventilation fans, filtration pumps, or alarms). (通过 GPIO 直接控制外设执行模块，如排风扇、过滤阀、警报器等)
- **Hybrid Gateway Topology (混合网关通信架构)**
  - High-speed data exchange with an **ESP32** co-processor via **USB CDC (Virtual COM Port)**. (STM32 与 ESP32 之间通过 USB 虚拟串口进行高速数据交互，由 ESP32 间接代理联网)
- **Multi-tasking RTOS (多任务实时操作系统)**
  - Driven by **FreeRTOS** to ensure strict timing for sensor sampling and communication. (使用 FreeRTOS 管理多传感器并发采集、数据队列与安全控制任务)
- **Offline Data Logging (离线数据存储)**
  - Integrated with **FATFS** over SDIO to log critical telemetry locally. (集成 FATFS 文件系统，支持将关键数据本地记录在 SD 卡中)

---

##  Directory Structure | 目录结构

- `Core/`: Main application flow, multi-tasking scheduler, and sensor calibration logic.
- `Drivers/`: BSP (Board Support Package) for sensors and actuator GPIO mappings.
- `USB_DEVICE/`: USB Device stack configured as Virtual COM Port (VCP) for STM32-to-ESP32 bridge.
- `FATFS/`: File system configuration and low-level SD card SPI/SDIO drivers.
- `CMakeLists.txt`: Project compilation rules.
- `project.ioc`: STM32CubeMX configuration file.
