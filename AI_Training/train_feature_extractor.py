"""训练 60x5 -> 16 维的 STM32Cube.AI 电子鼻特征提取器。"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import random
import sys
from typing import Any

import numpy as np

from dataset import (
    CHANNEL_COUNT,
    CHANNEL_NAMES,
    WINDOW_FRAMES,
    build_windows,
    load_export_directory,
    split_by_group,
    summarize,
)


EMBEDDING_DIMENSION = 16
CLASS_COUNT = 3
MODEL_VERSION = 2


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="训练电子鼻 1D-CNN 固定特征提取器"
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("data"),
        help="USB 导出文件或目录",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("artifacts"),
        help="模型、归一化参数和报告输出目录",
    )
    parser.add_argument("--epochs", type=int, default=120)
    parser.add_argument("--batch-size", type=int, default=16)
    parser.add_argument("--validation-fraction", type=float, default=0.25)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument(
        "--inspect-only",
        action="store_true",
        help="只解析数据和检查窗口，不要求安装 TensorFlow",
    )
    return parser.parse_args()


def configure_reproducibility(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)


def preprocess_fit(x_train: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """
    先对正数传感器读数做 log1p，再按通道计算训练集均值/标准差。
    统计量只能来自训练组，防止验证数据泄漏。
    """

    transformed = np.log1p(np.maximum(x_train, 0.0))
    mean = transformed.mean(axis=(0, 1)).astype(np.float32)
    std = transformed.std(axis=(0, 1)).astype(np.float32)
    std = np.maximum(std, 1.0e-3).astype(np.float32)
    return mean, std


def preprocess_apply(
    x: np.ndarray, mean: np.ndarray, std: np.ndarray
) -> np.ndarray:
    transformed = np.log1p(np.maximum(x, 0.0))
    return ((transformed - mean) / std).astype(np.float32)


def build_models(tf: Any) -> tuple[Any, Any]:
    """
    模型保持 Cube.AI 友好的基础算子：
    Conv1D/ReLU/MaxPool/GlobalAveragePool/Dense。
    分类头只用于离线训练，真正部署的是 embedding_model。
    """

    keras = tf.keras
    inputs = keras.Input(
        shape=(WINDOW_FRAMES, CHANNEL_COUNT), name="enose_window"
    )
    x = keras.layers.Conv1D(
        16, kernel_size=5, padding="same", activation="relu", name="conv1"
    )(inputs)
    x = keras.layers.MaxPooling1D(pool_size=2, name="pool1")(x)
    x = keras.layers.Conv1D(
        24, kernel_size=3, padding="same", activation="relu", name="conv2"
    )(x)
    x = keras.layers.GlobalAveragePooling1D(name="temporal_average")(x)
    embedding = keras.layers.Dense(
        EMBEDDING_DIMENSION, activation=None, name="embedding"
    )(x)
    logits = keras.layers.Dense(
        CLASS_COUNT, activation="softmax", name="training_classifier"
    )(embedding)

    training_model = keras.Model(inputs, logits, name="enose_training_model")
    embedding_model = keras.Model(
        inputs, embedding, name="enose_feature_extractor"
    )
    return training_model, embedding_model


def inverse_frequency_class_weights(y: np.ndarray) -> dict[int, float]:
    counts = np.bincount(y, minlength=CLASS_COUNT).astype(np.float64)
    weights: dict[int, float] = {}
    nonzero = counts > 0
    normalizer = counts[nonzero].sum() / max(1, np.count_nonzero(nonzero))
    for label in range(CLASS_COUNT):
        if counts[label] > 0:
            weights[label] = float(normalizer / counts[label])
    return weights


def prototype_evaluate(
    embedding_model: Any,
    x_train: np.ndarray,
    y_train: np.ndarray,
    x_validation: np.ndarray,
    y_validation: np.ndarray,
) -> dict[str, object]:
    """用与固件相同思路的逐维标准化原型距离做离线验证。"""

    train_embedding = embedding_model.predict(x_train, verbose=0)
    validation_embedding = embedding_model.predict(
        x_validation, verbose=0
    )
    scale = np.maximum(train_embedding.std(axis=0), 1.0e-3)

    prototypes: dict[int, np.ndarray] = {}
    for label in sorted(set(y_train.tolist())):
        prototypes[label] = train_embedding[y_train == label].mean(axis=0)

    predictions: list[int] = []
    distances: list[float] = []
    for embedding in validation_embedding:
        by_label = {
            label: float(
                np.mean(np.square((embedding - prototype) / scale))
            )
            for label, prototype in prototypes.items()
        }
        label = min(by_label, key=by_label.get)
        predictions.append(label)
        distances.append(float(np.sqrt(by_label[label])))

    predictions_array = np.asarray(predictions, dtype=np.int64)
    confusion = np.zeros((CLASS_COUNT, CLASS_COUNT), dtype=np.int64)
    for expected, predicted in zip(y_validation, predictions_array):
        confusion[int(expected), int(predicted)] += 1

    return {
        "accuracy": float(np.mean(predictions_array == y_validation)),
        "mean_nearest_distance": float(np.mean(distances)),
        "confusion_matrix": confusion.tolist(),
    }


def write_preprocess_header(
    output_path: Path, mean: np.ndarray, std: np.ndarray
) -> None:
    def c_float(value: float) -> str:
        literal = f"{float(value):.9g}"
        if "." not in literal and "e" not in literal.lower():
            literal += ".0"
        return literal + "f"

    mean_values = ", ".join(c_float(value) for value in mean)
    std_values = ", ".join(c_float(value) for value in std)
    text = f"""\
#ifndef AI_PREPROCESS_CONFIG_H
#define AI_PREPROCESS_CONFIG_H

/* 由离线训练脚本生成；Cube.AI 接入时复制到 Core/Inc。 */
#define AI_TRAINED_MODEL_VERSION {MODEL_VERSION}U
#define AI_TRAINED_INPUT_FRAMES {WINDOW_FRAMES}U
#define AI_TRAINED_INPUT_CHANNELS {CHANNEL_COUNT}U
#define AI_TRAINED_EMBEDDING_DIM {EMBEDDING_DIMENSION}U

static const float g_ai_preprocess_mean[{CHANNEL_COUNT}] = {{
    {mean_values}
}};
static const float g_ai_preprocess_std[{CHANNEL_COUNT}] = {{
    {std_values}
}};

#endif /* AI_PREPROCESS_CONFIG_H */
"""
    output_path.write_text(text, encoding="utf-8")


def main() -> int:
    args = parse_arguments()
    configure_reproducibility(args.seed)

    groups = load_export_directory(args.input)
    dataset = build_windows(groups)
    summary = summarize(groups, dataset)
    print(json.dumps(summary, ensure_ascii=False, indent=2))

    if dataset.x.shape[0] == 0:
        print("错误：没有可用的 60 秒窗口。", file=sys.stderr)
        return 2

    train_indices, validation_indices = split_by_group(
        dataset, args.validation_fraction, args.seed
    )
    summary["train_windows"] = int(len(train_indices))
    summary["validation_windows"] = int(len(validation_indices))

    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "dataset_summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    if args.inspect_only:
        print("数据检查完成；--inspect-only 未执行 TensorFlow 训练。")
        return 0

    present_labels = set(dataset.y[train_indices].tolist())
    if present_labels != {0, 1, 2}:
        print(
            "错误：正式训练要求训练集中同时存在标签 0、1、2。",
            file=sys.stderr,
        )
        return 3

    try:
        import tensorflow as tf
    except ImportError:
        print(
            "错误：未安装 TensorFlow。请在独立虚拟环境安装 "
            "AI_Training/requirements.txt。",
            file=sys.stderr,
        )
        return 4

    tf.random.set_seed(args.seed)
    x_train_raw = dataset.x[train_indices]
    y_train = dataset.y[train_indices]
    mean, std = preprocess_fit(x_train_raw)
    x_train = preprocess_apply(x_train_raw, mean, std)

    if len(validation_indices) > 0:
        x_validation = preprocess_apply(
            dataset.x[validation_indices], mean, std
        )
        y_validation = dataset.y[validation_indices]
        validation_data = (x_validation, y_validation)
        monitor = "val_loss"
    else:
        x_validation = np.empty((0, WINDOW_FRAMES, CHANNEL_COUNT))
        y_validation = np.empty((0,), dtype=np.int64)
        validation_data = None
        monitor = "loss"
        print(
            "警告：每类最好至少采集两个独立时间区间；当前无法做组级验证。"
        )

    training_model, embedding_model = build_models(tf)
    training_model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=1.0e-3),
        loss="sparse_categorical_crossentropy",
        metrics=["accuracy"],
    )

    callbacks = [
        tf.keras.callbacks.EarlyStopping(
            monitor=monitor, patience=18, restore_best_weights=True
        ),
        tf.keras.callbacks.ReduceLROnPlateau(
            monitor=monitor,
            factor=0.5,
            patience=7,
            min_lr=1.0e-5,
        ),
    ]

    history = training_model.fit(
        x_train,
        y_train,
        validation_data=validation_data,
        epochs=args.epochs,
        batch_size=min(args.batch_size, len(x_train)),
        class_weight=inverse_frequency_class_weights(y_train),
        callbacks=callbacks,
        verbose=2,
        shuffle=True,
    )

    training_model.save(args.output / "enose_training_model.keras")
    embedding_model.save(args.output / "enose_feature_extractor.keras")

    converter = tf.lite.TFLiteConverter.from_keras_model(embedding_model)
    tflite_model = converter.convert()
    tflite_path = args.output / "enose_feature_extractor_float32.tflite"
    tflite_path.write_bytes(tflite_model)

    preprocess = {
        "operation": "log1p(max(raw, 0)) then channel standardization",
        "channel_names": CHANNEL_NAMES,
        "mean": mean.tolist(),
        "std": std.tolist(),
        "input_shape": [1, WINDOW_FRAMES, CHANNEL_COUNT],
        "output_shape": [1, EMBEDDING_DIMENSION],
        "model_version": MODEL_VERSION,
    }
    (args.output / "preprocess.json").write_text(
        json.dumps(preprocess, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    write_preprocess_header(
        args.output / "ai_preprocess_config.h", mean, std
    )

    report: dict[str, object] = {
        "dataset": summary,
        "model_version": MODEL_VERSION,
        "parameter_count_training": int(training_model.count_params()),
        "parameter_count_embedding": int(embedding_model.count_params()),
        "epochs_completed": len(history.history["loss"]),
        "final_metrics": {
            key: float(values[-1])
            for key, values in history.history.items()
        },
        "tflite_bytes": len(tflite_model),
        "tflite_sha256": hashlib.sha256(tflite_model).hexdigest(),
    }
    if len(validation_indices) > 0:
        report["prototype_validation"] = prototype_evaluate(
            embedding_model,
            x_train,
            y_train,
            x_validation,
            y_validation,
        )

    (args.output / "training_report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    print(f"Cube.AI 输入模型：{tflite_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
