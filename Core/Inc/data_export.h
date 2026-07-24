#ifndef DATA_EXPORT_H
#define DATA_EXPORT_H

#include <stdbool.h>
#include <stdint.h>

/*
 * 单个 USB 导出块控制在 1 KB 以内，避免覆盖 usb_reporter.c 的发送缓冲。
 * 消息队列只缓存 1 块，USB 侧另有 1 块静态待发缓冲。
 * 这样仍能流水工作，同时尽量给 FreeRTOS heap 留出余量。
 */
#define DATA_EXPORT_CHUNK_DATA_SIZE 896U

typedef struct {
    uint32_t sequence;
    uint16_t length;
    uint8_t last;
    uint8_t reserved;
    char data[DATA_EXPORT_CHUNK_DATA_SIZE];
} DataExportChunk_t;

/* 在调度器启动前创建导出块队列。 */
bool DataExport_Init(void);

/*
 * 登记一个待导出的样本组。此函数不扫描 Flash，可以安全地由 USB 任务调用。
 * 登记成功后还需要调用 PrototypeWorker_RequestExport() 唤醒后台任务。
 */
bool DataExport_Begin(uint32_t group_id);
void DataExport_CancelPending(void);

/* 仅由低优先级 Task_Prototype 调用，执行真正的 Flash 扫描和 CSV 打包。 */
bool DataExport_RunPending(void);

/* USB 任务每轮最多取一个块，FAST/SLOW 报文发送完成后再发送它。 */
bool DataExport_TryGetChunk(DataExportChunk_t *out_chunk);
bool DataExport_IsBusy(void);

#endif /* DATA_EXPORT_H */
