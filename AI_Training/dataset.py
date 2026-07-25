"""电子鼻 USB 导出数据的解析、窗口化和按样本组切分工具。"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import re
from typing import Iterable

import numpy as np


WINDOW_FRAMES = 60
WINDOW_STRIDE = 15
CHANNEL_COUNT = 5
VALID_MASK_ALL = (1 << CHANNEL_COUNT) - 1
MIN_VALID_FRAMES = 54

CHANNEL_NAMES = (
    "sgp40_raw",
    "tvoc",
    "eco2",
    "bme688_gas",
    "weight",
)


@dataclass
class ExportRecord:
    """Flash2 中一条同步的 1 Hz 历史记录。"""

    timestamp: int
    uptime_ms: int
    raw: np.ndarray
    valid_mask: int
    door_closed: int


@dataclass
class ExportGroup:
    """一次用户标注的时间戳区间，可跨多个 USB 导出块。"""

    source: str
    group_id: int
    label: int
    model_version: int
    start_timestamp: int | None = None
    end_timestamp: int | None = None
    records: list[ExportRecord] = field(default_factory=list)

    @property
    def key(self) -> str:
        # 不同采集文件可能使用相同 group_id，必须把文件名纳入唯一键。
        return f"{self.source}::group_{self.group_id}"


@dataclass
class WindowDataset:
    x: np.ndarray
    y: np.ndarray
    group_keys: np.ndarray


_CHUNK_RE = re.compile(r"^#EXPORT_CHUNK,(.*)$")


def _parse_key_values(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for item in text.split(","):
        if "=" not in item:
            continue
        key, value = item.split("=", 1)
        values[key.strip()] = value.strip()
    return values


def parse_export_file(path: Path) -> list[ExportGroup]:
    """解析允许夹杂 FAST/SLOW JSON 的串口保存文件。"""

    groups: dict[int, ExportGroup] = {}
    current: ExportGroup | None = None

    with path.open("r", encoding="utf-8-sig", errors="replace") as stream:
        for line_number, raw_line in enumerate(stream, start=1):
            line = raw_line.strip()
            if not line:
                continue

            chunk_match = _CHUNK_RE.match(line)
            if chunk_match:
                metadata = _parse_key_values(chunk_match.group(1))
                try:
                    group_id = int(metadata["id"])
                    sequence = int(metadata.get("seq", "0"))
                    label = int(metadata["label"])
                    model_version = int(metadata["model"])
                    start_timestamp = (
                        int(metadata["start"])
                        if "start" in metadata
                        else None
                    )
                    end_timestamp = (
                        int(metadata["end"])
                        if "end" in metadata
                        else None
                    )
                except (KeyError, ValueError) as exc:
                    raise ValueError(
                        f"{path}:{line_number}: 导出块头字段不完整"
                    ) from exc

                if label not in (0, 1, 2):
                    raise ValueError(
                        f"{path}:{line_number}: label 必须是 0/1/2"
                    )

                current = groups.get(group_id)
                if current is None or sequence == 0:
                    # 使用独立的非 Optional 变量，让 PyCharm/Pylance 明确知道
                    # 写入字典的一定是 ExportGroup，而不是 ExportGroup | None。
                    #
                    # 同一个样本可以在没有修改元数据的情况下再次导出。新的
                    # seq=0 表示新导出会话，必须替换旧记录，不能把两次历史
                    # 合并；否则 Flash 环形覆盖后可能把已过期记录带入训练。
                    new_group = ExportGroup(
                        source=str(path.resolve()),
                        group_id=group_id,
                        label=label,
                        model_version=model_version,
                        start_timestamp=start_timestamp,
                        end_timestamp=end_timestamp,
                    )
                    groups[group_id] = new_group
                    current = new_group
                elif (
                    current.label != label
                    or current.model_version != model_version
                    or current.start_timestamp != start_timestamp
                    or current.end_timestamp != end_timestamp
                ):
                    raise ValueError(
                        f"{path}:{line_number}: 导出中途元数据发生变化"
                    )
                continue

            if line.startswith("#EXPORT_END"):
                current = None
                continue

            # 串口日志可能同时包含 FAST/SLOW/ACK JSON，训练解析器直接忽略。
            if line.startswith("{") or line.startswith("#"):
                continue
            if line.startswith("timestamp,"):
                continue
            if current is None:
                continue

            columns = [item.strip() for item in line.split(",")]
            if len(columns) != 9:
                raise ValueError(
                    f"{path}:{line_number}: CSV 应有 9 列，实际 {len(columns)}"
                )

            try:
                record = ExportRecord(
                    timestamp=int(columns[0]),
                    uptime_ms=int(columns[1]),
                    raw=np.asarray(
                        [float(value) for value in columns[2:7]],
                        dtype=np.float32,
                    ),
                    valid_mask=int(columns[7]),
                    door_closed=int(columns[8]),
                )
            except ValueError as exc:
                raise ValueError(
                    f"{path}:{line_number}: CSV 数值无法解析"
                ) from exc

            if not np.all(np.isfinite(record.raw)):
                continue
            current.records.append(record)

    # 串口重连可能重复保存某些块：按 uptime 去重后排序。
    for group in groups.values():
        unique = {record.uptime_ms: record for record in group.records}
        group.records = sorted(unique.values(), key=lambda item: item.uptime_ms)

    return list(groups.values())


def load_export_directory(input_path: Path) -> list[ExportGroup]:
    """读取单个文件或目录下所有 csv/txt/log 文件。"""

    if input_path.is_file():
        files = [input_path]
    elif input_path.is_dir():
        files = sorted(
            path
            for path in input_path.rglob("*")
            if path.suffix.lower() in {".csv", ".txt", ".log"}
        )
    else:
        raise FileNotFoundError(f"找不到数据路径：{input_path}")

    if not files:
        raise ValueError(f"{input_path} 中没有 csv/txt/log 导出文件")

    groups: list[ExportGroup] = []
    for path in files:
        groups.extend(parse_export_file(path))
    return groups


def _continuous_segments(
    records: list[ExportRecord],
) -> Iterable[list[ExportRecord]]:
    """按 uptime 切开超过 2.5 秒的采样缺口，与固件重建逻辑一致。"""

    segment: list[ExportRecord] = []
    previous_uptime: int | None = None

    for record in records:
        if previous_uptime is not None:
            delta = record.uptime_ms - previous_uptime
            if delta <= 0 or delta > 2500:
                if segment:
                    yield segment
                segment = []
        segment.append(record)
        previous_uptime = record.uptime_ms

    if segment:
        yield segment


def build_windows(
    groups: list[ExportGroup],
    window_frames: int = WINDOW_FRAMES,
    stride: int = WINDOW_STRIDE,
) -> WindowDataset:
    """生成网络输入；此处只筛数据，log1p/标准化在训练切分后进行。"""

    windows: list[np.ndarray] = []
    labels: list[int] = []
    group_keys: list[str] = []

    for group in groups:
        for segment in _continuous_segments(group.records):
            if len(segment) < window_frames:
                continue

            for start in range(0, len(segment) - window_frames + 1, stride):
                selected = segment[start : start + window_frames]
                valid_counts = np.zeros(CHANNEL_COUNT, dtype=np.int32)
                door_closed_count = 0

                for record in selected:
                    for channel in range(CHANNEL_COUNT):
                        if record.valid_mask & (1 << channel):
                            valid_counts[channel] += 1
                    door_closed_count += int(record.door_closed != 0)

                if np.any(valid_counts < MIN_VALID_FRAMES):
                    continue
                if door_closed_count < MIN_VALID_FRAMES:
                    continue

                windows.append(
                    np.stack([record.raw for record in selected]).astype(
                        np.float32
                    )
                )
                labels.append(group.label)
                group_keys.append(group.key)

    if not windows:
        return WindowDataset(
            x=np.empty(
                (0, window_frames, CHANNEL_COUNT), dtype=np.float32
            ),
            y=np.empty((0,), dtype=np.int64),
            group_keys=np.empty((0,), dtype=str),
        )

    return WindowDataset(
        x=np.stack(windows),
        y=np.asarray(labels, dtype=np.int64),
        group_keys=np.asarray(group_keys),
    )


def split_by_group(
    dataset: WindowDataset,
    validation_fraction: float,
    seed: int,
) -> tuple[np.ndarray, np.ndarray]:
    """
    以整个标注组为单位切分，禁止同一区间的重叠窗口同时进入训练和验证。
    每类只有一个组时全部留在训练集，并由调用者提示验证不足。
    """

    rng = np.random.default_rng(seed)
    validation_groups: set[str] = set()

    for label in sorted(set(dataset.y.tolist())):
        label_groups = np.unique(dataset.group_keys[dataset.y == label])
        label_groups = label_groups.copy()
        rng.shuffle(label_groups)

        if len(label_groups) < 2:
            continue

        validation_count = max(
            1, int(round(len(label_groups) * validation_fraction))
        )
        validation_count = min(validation_count, len(label_groups) - 1)
        validation_groups.update(label_groups[:validation_count].tolist())

    validation_mask = np.asarray(
        [key in validation_groups for key in dataset.group_keys],
        dtype=bool,
    )
    train_indices = np.flatnonzero(~validation_mask)
    validation_indices = np.flatnonzero(validation_mask)
    return train_indices, validation_indices


def summarize(
    groups: list[ExportGroup], dataset: WindowDataset
) -> dict[str, object]:
    group_counts = {
        str(label): sum(group.label == label for group in groups)
        for label in (0, 1, 2)
    }
    window_counts = {
        str(label): int(np.count_nonzero(dataset.y == label))
        for label in (0, 1, 2)
    }
    return {
        "groups_total": len(groups),
        "groups_per_label": group_counts,
        "windows_total": int(len(dataset.y)),
        "windows_per_label": window_counts,
        "window_shape": list(dataset.x.shape[1:]),
    }
