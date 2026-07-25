# 电子鼻 Cube.AI 离线训练

这一目录把 STM32 第五步导出的样本区间转换为 `60×5` 窗口，并训练一个
输出 16 维向量的小型 1D-CNN。三分类层只负责让特征空间在离线训练时分开，
最终交给 Cube.AI 的文件是 `enose_feature_extractor_float32.tflite`，STM32
上的类别仍由动态原型均值决定。

## 数据要求

- 标签 `0`：新鲜；`1`：不新鲜；`2`：腐坏。
- 每个区间至少 60 秒，建议 2～5 分钟。
- 每类至少 2 个相互独立的时间区间；只有一个区间时无法可靠验证。
- 尽量在关门且五个通道在线时采集。
- 不要把一个很长的连续实验切成很多相邻区间冒充独立样本。

串口保存文件允许夹杂 FAST/SLOW JSON，解析器只读取：

```text
#EXPORT_CHUNK,...
timestamp,uptime_ms,sgp40_raw,tvoc,eco2,bme688_gas,weight,valid_mask,door_closed
...
#EXPORT_END,...
```

## 使用

### Windows / PyCharm 环境

本项目固定使用 **64 位 Python 3.12**。不要选 Python 3.14：当前固定的
TensorFlow 2.18 没有 Python 3.14 的 Windows 安装包。

如果电脑没有 Python 3.12，先安装 Python 3.12.10 x64。然后在
`STM32_project\AI_Training` 目录打开 PowerShell，创建一个新的环境：

```powershell
& "C:\Users\你的用户名\AppData\Local\Programs\Python\Python312\python.exe" `
  -m venv .venv312

.\.venv312\Scripts\python.exe -m pip install --upgrade pip
.\.venv312\Scripts\python.exe -m pip install -r requirements.txt
```

在 PyCharm 中选择：

1. `文件 → 设置 → 项目: AI_Training → Python 解释器`。
2. 点击 `添加解释器 → 添加本地解释器 → 现有环境`。
3. 选择 `AI_Training\.venv312\Scripts\python.exe`。
4. 点击确定，等待右下角索引完成。

用下面命令确认解释器和三个依赖都正确：

```powershell
.\.venv312\Scripts\python.exe -c `
  "import numpy, serial, tensorflow as tf; print(numpy.__version__, serial.VERSION, tf.__version__)"
```

正常情况下最后会看到 TensorFlow `2.18.0`。首次安装 TensorFlow 下载量较大。
如果现在只做串口采样、暂时不训练，可以先只安装轻量依赖：

```powershell
.\.venv312\Scripts\python.exe -m pip install "numpy>=1.26,<2.1" pyserial==3.5
```

先检查数据，不需要 TensorFlow：

```powershell
.\.venv312\Scripts\python.exe train_feature_extractor.py `
  --input data `
  --output artifacts `
  --inspect-only
```

正式训练：

```powershell
.\.venv312\Scripts\python.exe train_feature_extractor.py `
  --input data `
  --output artifacts
```

关键输出：

- `enose_feature_extractor_float32.tflite`：下一步导入 X-CUBE-AI。
- `ai_preprocess_config.h`：以后复制到 `Core/Inc` 的预处理常量。
- `training_report.json`：参数量、验证结果和模型 SHA-256。
- `preprocess.json`：五通道顺序、均值、标准差和模型版本。

## 硬件未完成时的集成验证

FAST/SLOW 监控日志可以检查传感器是否在线、数值是否卡死，但它不是 Flash
中的 1 Hz 训练导出。分析监控日志：

```powershell
.\.venv312\Scripts\python.exe analyze_monitor_log.py `
  --input data\outdoor_monitor.log `
  --output artifacts\outdoor_monitor_report.json
```

没有真实标注样本时，可以生成一个固定随机权重的 Cube.AI 冒烟测试模型：

```powershell
.\.venv312\Scripts\python.exe make_integration_smoke_model.py `
  --output artifacts\smoke
```

生成的 `enose_integration_smoke_float32.tflite` 只用于验证 Cube.AI 转换、
RAM/Flash、60×5 输入和16维输出调用。它没有接受虚构标签训练，不能用于
宣称新鲜度分类准确率；配套清单会明确记录这一限制。

## 串口采样工具

列出设备：

```powershell
.\.venv312\Scripts\python.exe serial_tool.py ports
```

先同步设备时间，再监听 SLOW 报文中的 `ts`：

```powershell
.\.venv312\Scripts\python.exe serial_tool.py --port COM5 sync-time --tz 8
.\.venv312\Scripts\python.exe serial_tool.py --port COM5 preflight --tz 8
.\.venv312\Scripts\python.exe serial_tool.py --port COM5 monitor
```

`preflight` 会自动发送 `START`，并检查设备时间、四组传感器状态、两片
W25Q64、FreeRTOS 历史最低剩余 heap、样本库响应和当前特征模型版本。
`monitor` 也会自动发送 `START`，因此不会再一直停留在 `WAITING`。

把某个时间区间标成“不新鲜”，然后按返回的样本 ID 导出：

```powershell
.\.venv312\Scripts\python.exe serial_tool.py --port COM5 add `
  --start 1784871000 --end 1784871180 --label not_fresh

.\.venv312\Scripts\python.exe serial_tool.py --port COM5 export --id 1
```

可用标签名称为 `fresh`、`not_fresh`、`spoiled`。导出文件默认保存到
`AI_Training/data`，可以直接交给上面的数据检查和训练命令。

当前训练模型使用基础 `Conv1D/ReLU/MaxPool/GlobalAveragePool/Dense` 算子，
目的是优先保证 STM32Cube.AI 转换兼容性。不要在接入 Cube.AI 前修改输入
顺序或窗口长度，否则固件、样本库与模型会失配。

## 设计参考

- [Prototypical Networks for Few-shot Learning](https://arxiv.org/abs/1703.05175)：
  在学习得到的嵌入空间中，用每类样本均值作为原型并按距离分类。
- [ST UM2526：Getting started with X-CUBE-AI](https://www.st.com/resource/en/user_manual/dm00570145.pdf)：
  X-CUBE-AI 模型导入、分析、验证和生成代码的官方手册。
- [STMicroelectronics stm32ai-modelzoo-services](https://github.com/STMicroelectronics/stm32ai-modelzoo-services)：
  ST 官方训练、评估、量化和部署参考，其中包含 Human Activity Recognition
  等多通道时序任务。
- [Keras Timeseries classification from scratch](https://keras.io/examples/timeseries/timeseries_classification_from_scratch/)：
  官方 Conv1D 时序分类结构和标准化示例。
