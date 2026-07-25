"""生成可重复的电子鼻动态原型软件演示数据。

这些数值是人为构造的三类趋势，只用于检查“导出解析 -> Cube.AI ->
原型重建 -> 增删/改标签”链路。它们不是传感器实测，禁止用于报告识别准确率。
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np


LABEL_COUNT = 3
DEFAULT_GROUPS_PER_LABEL = 2
DEFAULT_SECONDS_PER_GROUP = 180
DEFAULT_SEED = 20260725
DEFAULT_MODEL_VERSION = 2
ROWS_PER_CHUNK = 20


def _group_values(
    label: int,
    group_index: int,
    seconds: int,
    rng: np.random.Generator,
) -> np.ndarray:
    """构造正值、缓慢漂移且组间略有差异的五通道序列。"""

    # 仅模拟大致趋势，不声称这些变化能代表某一种真实果蔬。
    centers = np.asarray(
        [
            [30000.0, 20.0, 420.0, 90000.0, 500.0],
            [34500.0, 95.0, 620.0, 57000.0, 493.0],
            [41000.0, 280.0, 1050.0, 26000.0, 478.0],
        ],
        dtype=np.float64,
    )
    noise_scale = np.asarray(
        [180.0, 3.0, 9.0, 900.0, 0.35],
        dtype=np.float64,
    )
    time_axis = np.linspace(0.0, 1.0, seconds, dtype=np.float64)
    phase = 0.7 * group_index + 0.3 * label

    values = np.repeat(centers[label][np.newaxis, :], seconds, axis=0)
    # 每组保留轻微偏移，避免演示数据变成完全相同的复制品。
    values += (group_index - 0.5) * np.asarray(
        [220.0, 4.0, 12.0, 1200.0, 0.8],
        dtype=np.float64,
    )
    values += np.sin(time_axis[:, None] * 4.0 * np.pi + phase) * (
        noise_scale[np.newaxis, :] * 0.8
    )
    values += rng.normal(
        0.0,
        noise_scale,
        size=(seconds, len(noise_scale)),
    )

    # 不新鲜/腐坏组增加随时间变化的气体趋势，称重只做很小的缓慢下降。
    severity = float(label)
    values[:, 0] += time_axis * 1000.0 * severity
    values[:, 1] += time_axis * 30.0 * severity
    values[:, 2] += time_axis * 60.0 * severity
    values[:, 3] -= time_axis * 7000.0 * severity
    values[:, 4] -= time_axis * 2.0 * severity
    return np.maximum(values, 0.0).astype(np.float32)


def generate_demo_text(
    groups_per_label: int = DEFAULT_GROUPS_PER_LABEL,
    seconds_per_group: int = DEFAULT_SECONDS_PER_GROUP,
    seed: int = DEFAULT_SEED,
    model_version: int = DEFAULT_MODEL_VERSION,
) -> str:
    """返回与固件 `#EXPORT_CHUNK` 完全同结构的合成导出文本。"""

    if groups_per_label < 1:
        raise ValueError("每类样本组数必须至少为 1")
    if seconds_per_group < 60:
        raise ValueError("每个样本组至少需要 60 秒")
    if model_version < 1:
        raise ValueError("模型版本必须为正整数")

    rng = np.random.default_rng(seed)
    lines = [
        "#SYNTHETIC_DEMO,not_real_sensor_data=1,"
        f"seed={seed},groups_per_label={groups_per_label}\n"
    ]
    group_id = 1
    base_timestamp = 1800000000

    for label in range(LABEL_COUNT):
        for group_index in range(groups_per_label):
            values = _group_values(
                label,
                group_index,
                seconds_per_group,
                rng,
            )
            start_timestamp = base_timestamp + (group_id - 1) * 600
            end_timestamp = start_timestamp + seconds_per_group - 1
            uptime_base = (group_id - 1) * 300000

            for start_row in range(0, seconds_per_group, ROWS_PER_CHUNK):
                sequence = start_row // ROWS_PER_CHUNK
                lines.append(
                    "#EXPORT_CHUNK,"
                    f"id={group_id},seq={sequence},label={label},"
                    f"model={model_version},start={start_timestamp},"
                    f"end={end_timestamp}\n"
                )
                if sequence == 0:
                    lines.append(
                        "timestamp,uptime_ms,sgp40_raw,tvoc,eco2,"
                        "bme688_gas,weight,valid_mask,door_closed\n"
                    )

                stop_row = min(
                    start_row + ROWS_PER_CHUNK,
                    seconds_per_group,
                )
                for second in range(start_row, stop_row):
                    row = values[second]
                    lines.append(
                        f"{start_timestamp + second},"
                        f"{uptime_base + second * 1000},"
                        f"{row[0]:.7g},{row[1]:.7g},{row[2]:.7g},"
                        f"{row[3]:.7g},{row[4]:.7g},31,1\n"
                    )

            lines.append(
                f"#EXPORT_END,id={group_id},"
                f"records={seconds_per_group},invalid=0\n"
            )
            group_id += 1

    return "".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="生成不可用于准确率声明的动态原型合成演示数据"
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/demo_dynamic_library.txt"),
    )
    parser.add_argument(
        "--groups-per-label",
        type=int,
        default=DEFAULT_GROUPS_PER_LABEL,
    )
    parser.add_argument(
        "--seconds",
        type=int,
        default=DEFAULT_SECONDS_PER_GROUP,
    )
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument(
        "--model-version",
        type=int,
        default=DEFAULT_MODEL_VERSION,
    )
    args = parser.parse_args()

    text = generate_demo_text(
        groups_per_label=args.groups_per_label,
        seconds_per_group=args.seconds,
        seed=args.seed,
        model_version=args.model_version,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8")
    print(
        f"已生成合成演示数据：{args.output}；"
        "该文件只能验证软件链路，不能用于准确率声明。"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
