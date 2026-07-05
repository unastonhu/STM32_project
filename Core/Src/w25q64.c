#include "w25q64.h"
#include "spi.h"

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
