"""分析 USB FAST/SLOW 监控日志，生成硬件与数据质量报告。"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
from statistics import mean
from typing import Any, Callable


NumericGetter = Callable[[dict[str, Any]], float]

SLOW_FIELDS: dict[str, NumericGetter] = {
    "hx_weight": lambda item: float(item["hx"]["w"]),
    "sgp40_voc_index": lambda item: float(item["sgp"]["v"]),
    "sgp40_raw": lambda item: float(item["sgp"]["r"]),
    "ens160_tvoc": lambda item: float(item["env"]["tv"]),
    "ens160_eco2": lambda item: float(item["env"]["co"]),
    "aht21_temperature": lambda item: float(item["env"]["at"]),
    "aht21_humidity": lambda item: float(item["env"]["ah"]),
    "bme688_temperature": lambda item: float(item["bme"]["t"]),
    "bme688_humidity": lambda item: float(item["bme"]["h"]),
    "bme688_pressure": lambda item: float(item["bme"]["p"]),
    "bme688_gas": lambda item: float(item["bme"]["g"]),
}


def parse_messages(path: Path) -> tuple[list[dict[str, Any]], int]:
    """读取逐行 JSON；返回有效消息和无法解析的行数。"""

    messages: list[dict[str, Any]] = []
    invalid_lines = 0
    for line in path.read_text(
        encoding="utf-8-sig", errors="replace"
    ).splitlines():
        try:
            value = json.loads(line)
        except json.JSONDecodeError:
            invalid_lines += 1
            continue
        if isinstance(value, dict):
            messages.append(value)
        else:
            invalid_lines += 1
    return messages, invalid_lines


def _summary(values: list[float]) -> dict[str, float]:
    return {
        "min": min(values),
        "max": max(values),
        "mean": mean(values),
        "span": max(values) - min(values),
    }


def analyze_messages(
    messages: list[dict[str, Any]], invalid_lines: int = 0
) -> dict[str, Any]:
    """汇总监控流，并主动识别“status=1 但数据为0”等假在线情况。"""

    fast = [item for item in messages if item.get("cmd") == "FAST"]
    slow = [item for item in messages if item.get("cmd") == "SLOW"]
    issues: list[dict[str, str]] = []
    report: dict[str, Any] = {
        "purpose": "hardware_and_data_path_diagnostics",
        "valid_for_freshness_training": False,
        "counts": {
            "fast": len(fast),
            "slow": len(slow),
            "invalid_lines": invalid_lines,
        },
    }

    if not slow:
        issues.append(
            {"severity": "error", "message": "日志中没有 SLOW 报文"}
        )
        report["issues"] = issues
        return report

    timestamps = [int(item["ts"]) for item in slow]
    deltas = [
        current - previous
        for previous, current in zip(timestamps, timestamps[1:])
    ]
    report["time"] = {
        "start": min(timestamps),
        "end": max(timestamps),
        "duration_seconds": max(timestamps) - min(timestamps),
        "slow_interval_seconds": sorted(set(deltas)),
    }

    channels: dict[str, dict[str, float]] = {}
    for name, getter in SLOW_FIELDS.items():
        values = [getter(item) for item in slow]
        channels[name] = _summary(values)
    if fast:
        channels["ultrasonic_distance"] = _summary(
            [float(item["dist"]) for item in fast]
        )
    report["channels"] = channels

    status_paths = {
        "hx711": "hx",
        "sgp40": "sgp",
        "ens160_aht21": "env",
        "bme688": "bme",
    }
    report["status_counts"] = {
        name: dict(
            sorted(
                Counter(
                    str(item[path].get("s"))
                    for item in slow
                ).items()
            )
        )
        for name, path in status_paths.items()
    }

    if any(delta <= 0 or delta > 15 for delta in deltas):
        issues.append(
            {
                "severity": "warning",
                "message": "SLOW 时间戳存在倒退或超过15秒的缺口",
            }
        )

    bme_false_online = [
        item
        for item in slow
        if item["bme"].get("s") == 1
        and (
            float(item["bme"].get("g", 0)) <= 0
            or float(item["bme"].get("p", 0)) <= 0
        )
    ]
    if bme_false_online:
        issues.append(
            {
                "severity": "error",
                "message": (
                    "BME688 status=1 但气阻/气压为0；"
                    f"共 {len(bme_false_online)} 条，禁止作为训练数据"
                ),
            }
        )

    if len(slow) < 7:
        issues.append(
            {
                "severity": "warning",
                "message": "不足60秒，无法观察一分钟尺度的趋势",
            }
        )

    issues.append(
        {
            "severity": "info",
            "message": (
                "FAST/SLOW 日志不是 1 Hz #EXPORT_CHUNK 数据，"
                "只能做诊断，不能直接进入训练器"
            ),
        }
    )
    report["issues"] = issues
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description="分析电子鼻监控日志")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    messages, invalid_lines = parse_messages(args.input)
    report = analyze_messages(messages, invalid_lines)
    text = json.dumps(report, ensure_ascii=False, indent=2)
    print(text)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")
    return 1 if any(
        issue["severity"] == "error" for issue in report["issues"]
    ) else 0


if __name__ == "__main__":
    raise SystemExit(main())
