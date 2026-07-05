#ifndef __W25Q64_H
#define __W25Q64_H


#include "main.h"
#include "spi.h"



extern SPI_HandleTypeDef hspi2; 

// ==========================================
// ⚙️ 用户硬件配置区 (修改这里以适配你的硬件)
// ==========================================
// 1. 定义你挂载了几个 W25Q64 模块

#define W25Q64_DEVICE_COUNT  2 // 目前只接了 1 个，以后不够用直接改 2, 3...

// 2. 映射片选引脚 
// --- 第 1 个 Flash ---
#define W25Q64_1_CS_PORT    GPIOC
#define W25Q64_1_CS_PIN     GPIO_PIN_4

// --- 第 2 个 Flash (预留，即便现在没接，写在这里也没关系) ---
#define W25Q64_2_CS_PORT    GPIOC
#define W25Q64_2_CS_PIN     GPIO_PIN_5
// ==========================================

// ==========================================
// W25Q64 核心指令表
// ==========================================
#define W25X_WriteEnable        0x06 
#define W25X_ReadStatusReg      0x05 
#define W25X_ReadData           0x03 
#define W25X_PageProgram        0x02 
#define W25X_SectorErase        0x20 

// ==========================================
// 核心函数 API
// ==========================================
void W25Q64_Init(void); // 统一初始化
uint32_t W25Q64_ReadID(uint8_t dev_index); // 通过编号读取，0 = 第一个芯片，1 = 第二个芯片
int8_t W25Q64_SanityCheck(uint8_t dev_index);


// 内部使用的底层对象结构体
typedef struct {
    GPIO_TypeDef* CS_Port;
    uint16_t      CS_Pin;
} W25Q64_HandleTypeDef;

extern W25Q64_HandleTypeDef W25Q64_Devs[W25Q64_DEVICE_COUNT];// 全局声明这个 Flash 硬件阵列

// 裸机读写四个函数，外部调用时请务必传入正确的 dev_index (0 = 第一个芯片，1 = 第二个芯片)
void W25Q64_ReadData(uint8_t dev_index, uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead);
void W25Q64_WritePage(uint8_t dev_index, uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
void W25Q64_EraseSector(uint8_t dev_index, uint32_t Dst_Addr);
void W25Q64_WaitBusy(uint8_t dev_index);



#endif /* __W25Q64_H */
