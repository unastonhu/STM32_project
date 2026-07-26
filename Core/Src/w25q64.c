#include "w25q64.h"
#include "spi.h"
#include "cmsis_os2.h"

#define W25Q64_BUSY_TIMEOUT_MS 3000U
#define W25Q64_TOTAL_BYTES     0x800000UL
#define W25Q64_SECTOR_COUNT    2048UL

// 实例化出那个硬件阵列
W25Q64_HandleTypeDef W25Q64_Devs[W25Q64_DEVICE_COUNT];

// ---------------------------------------------------------
// 函数：W25Q64_Init
// 作用：根据头文件配置，自动绑定所有芯片的引脚，并默认拉高防冲突
// ---------------------------------------------------------
void W25Q64_Init(void)
{
    // 自动装填 1 号芯片
    #if W25Q64_DEVICE_COUNT > 0
    W25Q64_Devs[0].CS_Port = W25Q64_1_CS_PORT;
    W25Q64_Devs[0].CS_Pin  = W25Q64_1_CS_PIN;
    #endif

    // 自动装填 2 号芯片
    #if W25Q64_DEVICE_COUNT > 1
    W25Q64_Devs[1].CS_Port = W25Q64_2_CS_PORT;
    W25Q64_Devs[1].CS_Pin  = W25Q64_2_CS_PIN;
    #endif

    // 遍历所有已挂载的芯片，把它们的 CS 引脚全都拉高，让总线保持安静
    for(int i = 0; i < W25Q64_DEVICE_COUNT; i++) {
        HAL_GPIO_WritePin(W25Q64_Devs[i].CS_Port, W25Q64_Devs[i].CS_Pin, GPIO_PIN_SET);
    }
}

// ---------------------------------------------------------
// 核心函数：W25Q64 健康度校验 (魔数机制)
// ---------------------------------------------------------
int8_t W25Q64_SanityCheck(uint8_t dev_index)
{
    // 计算最后一个扇区的物理地址: 第 2047 个扇区，每个扇区 4096 字节
    // 2047 * 4096 = 8,384,512 (十六进制 0x7FF000)
    uint32_t test_addr = 0x7FF000; 
    uint32_t magic_word = 0x5AA5A55A; // 效验密码
    uint32_t read_buf = 0;            // 读回来的数据容器

    // 1. 保护机制：先读一次看看，是不是以前已经写过了？
    W25Q64_ReadData(dev_index, (uint8_t*)&read_buf, test_addr, 4);
    if (read_buf == magic_word) {
        return 1; // 以前写过且数据完好无损，读写功能完美！直接放行，不消耗擦写寿命。
    }

    // 2. 如果读出来不对（说明是出厂新芯片，或者数据损坏），执行：擦除 -> 写入
    W25Q64_EraseSector(dev_index, test_addr / 4096); 
    W25Q64_WritePage(dev_index, (uint8_t*)&magic_word, test_addr, 4);

    // 3. 再次读取进行终极校验
    read_buf = 0; // 清空容器
    W25Q64_ReadData(dev_index, (uint8_t*)&read_buf, test_addr, 4);

    if (read_buf == magic_word) {
        return 1;  // 刚写完读出来严丝合缝，芯片极其健康！
    } else {
        return -1; // 完蛋，写不进去或者读出来是乱码，硬件损坏或飞线太长干扰大！
    }
}




// ---------------------------------------------------------
// 底层通信接口
// ---------------------------------------------------------
static uint8_t SPI_SwapByte(uint8_t tx_data)
{
    uint8_t rx_data = 0;
    HAL_SPI_TransmitReceive(&hspi2, &tx_data, &rx_data, 1, 100);
    return rx_data;
}

// ---------------------------------------------------------
// 函数：读取指定芯片的 ID
// 参数：dev_index (0 = 读第一块，1 = 读第二块...)
// ---------------------------------------------------------
uint32_t W25Q64_ReadID(uint8_t dev_index)
{
    // 防御编程：防止传入的编号超出了咱们设置的最大数量
    if(dev_index >= W25Q64_DEVICE_COUNT) return 0; 

    uint32_t temp = 0;
    uint32_t temp0 = 0, temp1 = 0, temp2 = 0;

    // 提取这个特定芯片的端口和引脚
    GPIO_TypeDef* port = W25Q64_Devs[dev_index].CS_Port;
    uint16_t pin       = W25Q64_Devs[dev_index].CS_Pin;

    // 1. 选中这块芯片
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET); 

    // 2. 发送指令并接收 3 字节数据
    SPI_SwapByte(0x9F); 
    temp0 = SPI_SwapByte(0xFF); 
    temp1 = SPI_SwapByte(0xFF); 
    temp2 = SPI_SwapByte(0xFF); 

    // 3. 释放这块芯片
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET); 

    temp = (temp0 << 16) | (temp1 << 8) | temp2;
    return temp;
}

//-------------------------------------------------------------
// ---------------------------------------------------------
// 内部函数：读取状态寄存器，等待芯片空闲 (写操作必备)
// ---------------------------------------------------------
bool W25Q64_WaitBusy(uint8_t dev_index)
{
    uint8_t status = 0;
    uint32_t start_tick;

    if (dev_index >= W25Q64_DEVICE_COUNT) {
        return false;
    }

    GPIO_TypeDef* port = W25Q64_Devs[dev_index].CS_Port;
    uint16_t pin       = W25Q64_Devs[dev_index].CS_Pin;
    start_tick = HAL_GetTick();

    do {
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
        SPI_SwapByte(W25X_ReadStatusReg);
        status = SPI_SwapByte(0xFF);
        HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);

        if ((status & 0x01U) == 0U) {
            return true;
        }
        if ((uint32_t)(HAL_GetTick() - start_tick) >=
            W25Q64_BUSY_TIMEOUT_MS) {
            return false;
        }

        /* 调度器运行后主动让出 CPU；启动阶段则用 HAL tick 短等。 */
        if (osKernelGetState() == osKernelRunning) {
            osDelay(1U);
        } else {
            HAL_Delay(1U);
        }
    } while (true);
}

// ---------------------------------------------------------
// 内部函数：写使能 (每次写或擦除前，必须调一次)
// ---------------------------------------------------------
static void W25Q64_WriteEnable(uint8_t dev_index)
{
    GPIO_TypeDef* port = W25Q64_Devs[dev_index].CS_Port;
    uint16_t pin       = W25Q64_Devs[dev_index].CS_Pin;

    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    SPI_SwapByte(W25X_WriteEnable);
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
}

// ---------------------------------------------------------
// 核心函数：读取任意长度数据
// ---------------------------------------------------------
void W25Q64_ReadData(uint8_t dev_index, uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead)
{
    if (dev_index >= W25Q64_DEVICE_COUNT ||
        pBuffer == NULL ||
        ReadAddr >= W25Q64_TOTAL_BYTES ||
        NumByteToRead > W25Q64_TOTAL_BYTES - ReadAddr) {
        return;
    }

    GPIO_TypeDef* port = W25Q64_Devs[dev_index].CS_Port;
    uint16_t pin       = W25Q64_Devs[dev_index].CS_Pin;

    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    SPI_SwapByte(W25X_ReadData); // 发送读指令
    SPI_SwapByte((uint8_t)((ReadAddr) >> 16)); // 发送 24 位地址
    SPI_SwapByte((uint8_t)((ReadAddr) >> 8));
    SPI_SwapByte((uint8_t)ReadAddr);
    
    for (uint16_t i = 0; i < NumByteToRead; i++) {
        pBuffer[i] = SPI_SwapByte(0xFF); // 循环读数据
    }
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
}

// ---------------------------------------------------------
// 核心函数：擦除一个扇区 (4KB) - ⚠️ 写数据前必须擦除！
// ---------------------------------------------------------
void W25Q64_EraseSector(uint8_t dev_index, uint32_t Dst_Addr)
{
    if (dev_index >= W25Q64_DEVICE_COUNT ||
        Dst_Addr >= W25Q64_SECTOR_COUNT ||
        !W25Q64_WaitBusy(dev_index)) {
        return;
    }

    Dst_Addr *= 4096; // 把扇区号换算成实际物理地址
    W25Q64_WriteEnable(dev_index); // 空闲后再写使能，避免 WREN 被忙状态忽略

    GPIO_TypeDef* port = W25Q64_Devs[dev_index].CS_Port;
    uint16_t pin       = W25Q64_Devs[dev_index].CS_Pin;

    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    SPI_SwapByte(W25X_SectorErase); // 发送擦除指令
    SPI_SwapByte((uint8_t)((Dst_Addr) >> 16));
    SPI_SwapByte((uint8_t)((Dst_Addr) >> 8));
    SPI_SwapByte((uint8_t)Dst_Addr);
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    
    (void)W25Q64_WaitBusy(dev_index);
}

// ---------------------------------------------------------
// 核心函数：页编程 (最大写入 256 字节)
// ---------------------------------------------------------
void W25Q64_WritePage(uint8_t dev_index, uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
    if (dev_index >= W25Q64_DEVICE_COUNT ||
        pBuffer == NULL ||
        NumByteToWrite == 0U ||
        NumByteToWrite > 256U ||
        WriteAddr >= W25Q64_TOTAL_BYTES ||
        NumByteToWrite > W25Q64_TOTAL_BYTES - WriteAddr ||
        ((WriteAddr & 0xFFU) + NumByteToWrite) > 256U ||
        !W25Q64_WaitBusy(dev_index)) {
        return;
    }

    W25Q64_WriteEnable(dev_index); // 空闲后再写使能
    
    GPIO_TypeDef* port = W25Q64_Devs[dev_index].CS_Port;
    uint16_t pin       = W25Q64_Devs[dev_index].CS_Pin;

    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    SPI_SwapByte(W25X_PageProgram); // 发送写页指令
    SPI_SwapByte((uint8_t)((WriteAddr) >> 16));
    SPI_SwapByte((uint8_t)((WriteAddr) >> 8));
    SPI_SwapByte((uint8_t)WriteAddr);
    
    for (uint16_t i = 0; i < NumByteToWrite; i++) {
        SPI_SwapByte(pBuffer[i]); // 循环写数据
    }
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    
    (void)W25Q64_WaitBusy(dev_index);
}
