"""在 PC 上回放固件的 60x5 -> Cube.AI -> 动态原型完整数据流。"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, replace
import json
from pathlib import Path
from typing import Any

import numpy as np

from dataset import (
    CHANNEL_COUNT,
    CHANNEL_NAMES,
    WINDOW_FRAMES,
    ExportGroup,
    WindowDataset,
    build_windows,
    load_export_directory,
    summarize,
)
from prototype_math import (
    EMBEDDING_DIMENSION,
    PrototypeSet,
    classify_embeddings,
    fit_prototypes,
)
from train_feature_extractor import preprocess_apply


@dataclass(frozen=True)
class PreprocessConfig:
    mean: np.ndarray
    std: np.ndarray
    model_version: int
    smoke_test: bool


def parse_relabel(value: str) -> tuple[int, int]:
    """解析 --relabel GROUP_ID:LABEL。"""

    try:
        group_text, label_text = value.split(":", 1)
        group_id = int(group_text)
        label = int(label_text)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "重标记格式应为 GROUP_ID:LABEL，例如 3:2"
        ) from exc
    if group_id < 0 or label not in (0, 1, 2):
        raise argparse.ArgumentTypeError("GROUP_ID>=0，LABEL 只能是 0/1/2")
    return group_id, label


def apply_sample_edits(
    groups: list[ExportGroup],
    deleted_ids: set[int],
    relabels: dict[int, int],
) -> list[ExportGroup]:
    """
    在内存中模拟用户删除样本组或修改标签，不改原始导出文件。

    多个导出文件若含相同 group_id，会同时应用操作；报告里会保留各自 source，
    避免把它们误认为同一个独立采集组。
    """

    known_ids = {group.group_id for group in groups}
    unknown = (deleted_ids | set(relabels)) - known_ids
    if unknown:
        raise ValueError(f"找不到样本组 ID：{sorted(unknown)}")

    edited: list[ExportGroup] = []
    for group in groups:
        if group.group_id in deleted_ids:
            continue
        label = relabels.get(group.group_id, group.label)
        edited.append(replace(group, label=label))
    return edited


def load_preprocess_config(
    path: Path | None,
    model_path: Path,
) -> PreprocessConfig:
    if path is None:
        if "smoke" not in model_path.name.lower():
            raise ValueError("正式模型必须同时提供 --preprocess preprocess.json")
        return PreprocessConfig(
            mean=np.zeros(CHANNEL_COUNT, dtype=np.float32),
            std=np.ones(CHANNEL_COUNT, dtype=np.float32),
            model_version=2,
            smoke_test=True,
        )

    value = json.loads(path.read_text(encoding="utf-8"))
    mean = np.asarray(value.get("mean"), dtype=np.float32)
    std = np.asarray(value.get("std"), dtype=np.float32)
    if mean.shape != (CHANNEL_COUNT,) or std.shape != (CHANNEL_COUNT,):
        raise ValueError("preprocess.json 的 mean/std 必须各有 5 项")
    if not np.all(np.isfinite(mean)) or not np.all(np.isfinite(std)):
        raise ValueError("preprocess.json 包含 NaN 或 Inf")
    if np.any(std < 1.0e-3):
        raise ValueError("preprocess.json 的 std 不得小于 1e-3")
    if tuple(value.get("channel_names", ())) != CHANNEL_NAMES:
        raise ValueError("preprocess.json 的五通道顺序与固件不一致")
    if value.get("input_shape") != [1, WINDOW_FRAMES, CHANNEL_COUNT]:
        raise ValueError("preprocess.json 输入形状不是 [1, 60, 5]")
    if value.get("output_shape") != [1, EMBEDDING_DIMENSION]:
        raise ValueError("preprocess.json 输出形状不是 [1, 16]")

    return PreprocessConfig(
        mean=mean,
        std=std,
        model_version=int(value["model_version"]),
        smoke_test=False,
    )


def run_tflite(
    model_path: Path,
    windows: np.ndarray,
    config: PreprocessConfig,
) -> np.ndarray:
    """逐窗口推理，保持和 MCU 固定 batch=1 的调用方式一致。"""

    import tensorflow as tf

    transformed = preprocess_apply(windows, config.mean, config.std)
    interpreter = tf.lite.Interpreter(model_path=str(model_path))
    interpreter.allocate_tensors()
    input_info = interpreter.get_input_details()[0]
    output_info = interpreter.get_output_details()[0]

    if input_info["shape"].tolist() != [1, WINDOW_FRAMES, CHANNEL_COUNT]:
        raise ValueError(
            f"TFLite 输入不是 [1,60,5]：{input_info['shape'].tolist()}"
        )
    if output_info["shape"].tolist() != [1, EMBEDDING_DIMENSION]:
        raise ValueError(
            f"TFLite 输出不是 [1,16]：{output_info['shape'].tolist()}"
        )
    if input_info["dtype"] != np.float32 or output_info["dtype"] != np.float32:
        raise ValueError("当前固件接入只支持 float32 输入和输出")

    embeddings = np.empty(
        (len(transformed), EMBEDDING_DIMENSION),
        dtype=np.float32,
    )
    for index, window in enumerate(transformed):
        interpreter.set_tensor(
            input_info["index"],
            window[np.newaxis, ...],
        )
        interpreter.invoke()
        embeddings[index] = interpreter.get_tensor(
            output_info["index"]
        )[0]

    if not np.all(np.isfinite(embeddings)):
        raise RuntimeError("TFLite 输出包含 NaN 或 Inf")
    return embeddings


def _prototype_summary(model: PrototypeSet) -> dict[str, Any]:
    return {
        "valid_label_mask": model.valid_label_mask,
        "window_counts": model.counts.tolist(),
        "scale": {
            "min": float(model.scale.min()),
            "max": float(model.scale.max()),
            "mean": float(model.scale.mean()),
        },
        "rejection_distance": model.rejection_distance,
    }


def _pipeline_self_check(
    model: PrototypeSet,
    embeddings: np.ndarray,
    expected: np.ndarray,
) -> dict[str, Any]:
    """
    使用同一批窗口建库并回放，只验证计算链一致性，不把结果称为准确率。
    """

    results = classify_embeddings(model, embeddings)
    predicted = np.asarray(
        [result.label for result in results],
        dtype=np.int64,
    )
    return {
        "purpose": "pipeline_consistency_not_accuracy",
        "matched_windows": int(np.count_nonzero(predicted == expected)),
        "rejected_windows": int(np.count_nonzero(predicted < 0)),
        "total_windows": int(len(predicted)),
        "mean_nearest_distance": float(
            np.mean([result.nearest_distance for result in results])
        ),
    }


def _group_holdout(
    dataset: WindowDataset,
    embeddings: np.ndarray,
) -> dict[str, Any]:
    """整组留出，禁止同一时间段的重叠窗口同时建库和验证。"""

    folds: list[dict[str, Any]] = []
    all_predictions: list[int] = []
    all_expected: list[int] = []
    skipped = 0

    for group_key in np.unique(dataset.group_keys):
        test_mask = dataset.group_keys == group_key
        train_mask = ~test_mask
        train_labels = dataset.y[train_mask]
        if (
            np.count_nonzero(train_mask) == 0
            or len(np.unique(train_labels)) < 2
        ):
            skipped += 1
            continue

        model = fit_prototypes(
            embeddings[train_mask],
            train_labels,
        )
        results = classify_embeddings(model, embeddings[test_mask])
        predicted = [result.label for result in results]
        expected = dataset.y[test_mask].astype(int).tolist()
        all_predictions.extend(predicted)
        all_expected.extend(expected)
        folds.append(
            {
                "group": str(group_key),
                "windows": len(predicted),
                "matched": sum(
                    left == right
                    for left, right in zip(predicted, expected)
                ),
                "rejected": sum(value < 0 for value in predicted),
            }
        )

    if not all_expected:
        return {
            "available": False,
            "reason": "至少需要两个类别及多个独立样本组",
            "skipped_folds": skipped,
            "folds": folds,
        }

    return {
        "available": True,
        "accuracy": float(
            np.mean(
                np.asarray(all_predictions)
                == np.asarray(all_expected)
            )
        ),
        "rejected_windows": sum(
            value < 0 for value in all_predictions
        ),
        "evaluated_windows": len(all_expected),
        "skipped_folds": skipped,
        "folds": folds,
    }


def replay_scenario(
    groups: list[ExportGroup],
    model_path: Path,
    config: PreprocessConfig,
) -> dict[str, Any]:
    dataset = build_windows(groups)
    summary = summarize(groups, dataset)
    report: dict[str, Any] = {"dataset": summary}

    if len(dataset.y) == 0:
        report.update(
            {
                "replay_available": False,
                "reason": (
                    "没有合格的 1 Hz #EXPORT_CHUNK 窗口；"
                    "FAST/SLOW 监控日志不能插值成训练数据"
                ),
            }
        )
        return report

    embeddings = run_tflite(model_path, dataset.x, config)
    model = fit_prototypes(embeddings, dataset.y)
    report.update(
        {
            "replay_available": True,
            "embedding_shape": list(embeddings.shape),
            "embedding_range": {
                "min": float(embeddings.min()),
                "max": float(embeddings.max()),
            },
            "prototype": _prototype_summary(model),
            "self_check": _pipeline_self_check(
                model,
                embeddings,
                dataset.y,
            ),
            "group_holdout": _group_holdout(dataset, embeddings),
        }
    )
    return report


def compare_scenarios(
    baseline: dict[str, Any],
    edited: dict[str, Any],
) -> dict[str, Any]:
    """把样本编辑造成的结构变化整理成演示时可直接读取的摘要。"""

    baseline_dataset = baseline["dataset"]
    edited_dataset = edited["dataset"]
    baseline_windows = baseline_dataset["windows_per_label"]
    edited_windows = edited_dataset["windows_per_label"]

    comparison: dict[str, Any] = {
        "groups_total_delta": (
            int(edited_dataset["groups_total"])
            - int(baseline_dataset["groups_total"])
        ),
        "windows_total_delta": (
            int(edited_dataset["windows_total"])
            - int(baseline_dataset["windows_total"])
        ),
        "windows_per_label_delta": {
            str(label): (
                int(edited_windows[str(label)])
                - int(baseline_windows[str(label)])
            )
            for label in range(3)
        },
        "prototype_changed": False,
    }

    if (
        baseline.get("replay_available")
        and edited.get("replay_available")
    ):
        before = baseline["prototype"]
        after = edited["prototype"]
        comparison["prototype_changed"] = (
            before["valid_label_mask"] != after["valid_label_mask"]
            or before["window_counts"] != after["window_counts"]
            or before["scale"] != after["scale"]
        )
        comparison["valid_label_mask"] = {
            "before": before["valid_label_mask"],
            "after": after["valid_label_mask"],
        }

    return comparison


def main() -> int:
    parser = argparse.ArgumentParser(
        description="PC 端回放 STM32 Cube.AI 动态原型数据流"
    )
    parser.add_argument("--input", type=Path, default=Path("data"))
    parser.add_argument(
        "--model",
        type=Path,
        default=Path(
            "artifacts/smoke/"
            "enose_integration_smoke_float32.tflite"
        ),
    )
    parser.add_argument("--preprocess", type=Path)
    parser.add_argument(
        "--delete-id",
        type=int,
        action="append",
        default=[],
        help="在内存中删除一个样本组，可重复",
    )
    parser.add_argument(
        "--relabel",
        type=parse_relabel,
        action="append",
        default=[],
        metavar="GROUP_ID:LABEL",
        help="在内存中修改样本标签，可重复",
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    groups = load_export_directory(args.input)
    relabels = dict(args.relabel)
    edited_groups = apply_sample_edits(
        groups,
        set(args.delete_id),
        relabels,
    )
    config = load_preprocess_config(args.preprocess, args.model)
    baseline = replay_scenario(groups, args.model, config)
    has_edits = bool(args.delete_id or relabels)
    # 没有编辑操作时不重复执行整套 TFLite 推理。
    edited = (
        replay_scenario(edited_groups, args.model, config)
        if has_edits
        else baseline
    )

    report = {
        "purpose": "firmware_equivalent_offline_replay",
        "valid_for_freshness_accuracy": False
        if config.smoke_test
        else None,
        "model": str(args.model.resolve()),
        "model_version": config.model_version,
        "smoke_test_model": config.smoke_test,
        "edits": {
            "deleted_ids": sorted(set(args.delete_id)),
            "relabels": {
                str(group_id): label
                for group_id, label in sorted(relabels.items())
            },
        },
        "baseline": baseline,
        "edited": edited,
        "comparison": compare_scenarios(baseline, edited),
    }

    if not config.smoke_test:
        counts = report["edited"]["dataset"]["groups_per_label"]
        report["valid_for_freshness_accuracy"] = all(
            int(counts[str(label)]) >= 2 for label in range(3)
        ) and bool(
            report["edited"].get("group_holdout", {}).get("available")
        )

    text = json.dumps(report, ensure_ascii=False, indent=2)
    print(text)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text + "\n", encoding="utf-8")

    return 0 if report["edited"]["replay_available"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
