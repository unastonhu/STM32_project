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

建议创建独立 Python 虚拟环境，不要往 STM32CubeIDE 自带环境安装包：

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r AI_Training\requirements.txt
```

先检查数据，不需要 TensorFlow：

```powershell
python AI_Training\train_feature_extractor.py `
  --input AI_Training\data `
  --output AI_Training\artifacts `
  --inspect-only
```

正式训练：

```powershell
python AI_Training\train_feature_extractor.py `
  --input AI_Training\data `
  --output AI_Training\artifacts
```

关键输出：

- `enose_feature_extractor_float32.tflite`：下一步导入 X-CUBE-AI。
- `ai_preprocess_config.h`：以后复制到 `Core/Inc` 的预处理常量。
- `training_report.json`：参数量、验证结果和模型 SHA-256。
- `preprocess.json`：五通道顺序、均值、标准差和模型版本。

## 串口采样工具

列出设备：

```powershell
python AI_Training\serial_tool.py ports
```

先同步设备时间，再监听 SLOW 报文中的 `ts`：

```powershell
python AI_Training\serial_tool.py --port COM5 sync-time --tz 8
python AI_Training\serial_tool.py --port COM5 monitor
```

把某个时间区间标成“不新鲜”，然后按返回的样本 ID 导出：

```powershell
python AI_Training\serial_tool.py --port COM5 add `
  --start 1784871000 --end 1784871180 --label not_fresh

python AI_Training\serial_tool.py --port COM5 export --id 1
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
