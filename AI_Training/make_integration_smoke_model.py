"""生成只用于 STM32Cube.AI 接口联调的固定随机权重模型。"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

import numpy as np

from dataset import CHANNEL_COUNT, CHANNEL_NAMES, WINDOW_FRAMES
from train_feature_extractor import EMBEDDING_DIMENSION, build_models


SMOKE_SEED = 20260724


def build_smoke_model(tf: Any) -> Any:
    """
    复用正式模型的 60x5 -> 16 维结构，但不使用虚构标签训练。
    固定随机权重只验证转换、内存、推理调用和输出数据流。
    """

    tf.keras.utils.set_random_seed(SMOKE_SEED)
    _, embedding_model = build_models(tf)
    # 用一次前向传播显式建立权重和输入签名。
    embedding_model(np.zeros(
        (1, WINDOW_FRAMES, CHANNEL_COUNT), dtype=np.float32
    ))
    return embedding_model


def main() -> int:
    parser = argparse.ArgumentParser(
        description="生成 Cube.AI 集成冒烟测试模型"
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("artifacts/smoke"),
    )
    args = parser.parse_args()

    import tensorflow as tf

    args.output.mkdir(parents=True, exist_ok=True)
    model = build_smoke_model(tf)
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    tflite = converter.convert()
    model_path = args.output / "enose_integration_smoke_float32.tflite"
    model_path.write_bytes(tflite)

    # 用 TFLite 解释器验证真实文件的输入/输出契约和有限数输出。
    interpreter = tf.lite.Interpreter(model_content=tflite)
    interpreter.allocate_tensors()
    input_info = interpreter.get_input_details()[0]
    output_info = interpreter.get_output_details()[0]
    interpreter.set_tensor(
        input_info["index"],
        np.zeros(input_info["shape"], dtype=np.float32),
    )
    interpreter.invoke()
    output = interpreter.get_tensor(output_info["index"])
    if (
        tuple(input_info["shape"]) != (1, WINDOW_FRAMES, CHANNEL_COUNT)
        or tuple(output_info["shape"]) != (1, EMBEDDING_DIMENSION)
        or not np.all(np.isfinite(output))
    ):
        raise RuntimeError("生成模型没有满足 60x5 -> 16 维契约")

    manifest = {
        "artifact_type": "integration_smoke_test_model",
        "valid_for_freshness_classification": False,
        "must_be_replaced_before_claiming_accuracy": True,
        "seed": SMOKE_SEED,
        "channel_names": CHANNEL_NAMES,
        "input_shape": input_info["shape"].tolist(),
        "output_shape": output_info["shape"].tolist(),
        "input_dtype": str(input_info["dtype"]),
        "output_dtype": str(output_info["dtype"]),
        "parameter_count": int(model.count_params()),
        "tflite_bytes": len(tflite),
        "tflite_sha256": hashlib.sha256(tflite).hexdigest(),
    }
    (args.output / "SMOKE_MODEL_MANIFEST.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
