"""与 STM32 固件一致的动态原型拟合、距离分类和置信度计算。"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


CLASS_COUNT = 3
EMBEDDING_DIMENSION = 16
PROTOTYPE_SCALE_FLOOR = 1.0e-3
PROTOTYPE_REJECTION_DISTANCE = 3.0


@dataclass(frozen=True)
class PrototypeSet:
    """固件 PrototypeSnapshot_t 中与分类有关的字段。"""

    prototypes: np.ndarray
    scale: np.ndarray
    counts: np.ndarray
    valid_label_mask: int
    rejection_distance: float = PROTOTYPE_REJECTION_DISTANCE


@dataclass(frozen=True)
class PrototypeResult:
    label: int
    confidence: float
    nearest_distance: float
    valid: bool


def _validate_embeddings(embeddings: np.ndarray) -> np.ndarray:
    values = np.asarray(embeddings, dtype=np.float32)
    if values.ndim != 2 or values.shape[1] != EMBEDDING_DIMENSION:
        raise ValueError(
            "embedding 必须是 [N, 16]，"
            f"实际形状为 {list(values.shape)}"
        )
    if not np.all(np.isfinite(values)):
        raise ValueError("embedding 包含 NaN 或 Inf")
    return values


def fit_prototypes(
    embeddings: np.ndarray,
    labels: np.ndarray,
) -> PrototypeSet:
    """
    使用 double 累加、float32 保存，复现固件的 PrototypeHead_BuildSnapshot。

    scale 是全部活动类别窗口的逐维总体标准差。CNN embedding 的每一维
    没有预设物理含义，所以全部使用同一个 1e-3 方差下限。
    """

    values = _validate_embeddings(embeddings)
    label_values = np.asarray(labels, dtype=np.int64)
    if label_values.shape != (len(values),):
        raise ValueError("labels 数量必须与 embedding 数量一致")
    if len(values) == 0:
        raise ValueError("至少需要一个窗口才能建立原型")
    if np.any((label_values < 0) | (label_values >= CLASS_COUNT)):
        raise ValueError("label 必须是 0、1 或 2")

    prototypes = np.zeros(
        (CLASS_COUNT, EMBEDDING_DIMENSION),
        dtype=np.float32,
    )
    counts = np.zeros(CLASS_COUNT, dtype=np.uint32)
    valid_label_mask = 0

    # 固件重建使用 double sum/sum_sq，最后才写回 float。
    values64 = values.astype(np.float64)
    for label in range(CLASS_COUNT):
        selected = values64[label_values == label]
        counts[label] = len(selected)
        if len(selected) == 0:
            continue
        prototypes[label] = selected.mean(axis=0).astype(np.float32)
        valid_label_mask |= 1 << label

    mean64 = values64.mean(axis=0)
    variance64 = np.maximum(
        np.square(values64).mean(axis=0) - np.square(mean64),
        PROTOTYPE_SCALE_FLOOR**2,
    )
    scale = np.sqrt(variance64).astype(np.float32)

    return PrototypeSet(
        prototypes=prototypes,
        scale=scale,
        counts=counts,
        valid_label_mask=valid_label_mask,
    )


def classify_embedding(
    model: PrototypeSet,
    embedding: np.ndarray,
) -> PrototypeResult:
    """复现固件的标准化欧氏距离、拒识和展示置信度。"""

    value = np.asarray(embedding, dtype=np.float32)
    if value.shape != (EMBEDDING_DIMENSION,):
        raise ValueError("单个 embedding 必须是 16 维")
    if not np.all(np.isfinite(value)):
        raise ValueError("embedding 包含 NaN 或 Inf")
    if np.count_nonzero(
        [
            model.valid_label_mask & (1 << label)
            for label in range(CLASS_COUNT)
        ]
    ) < 2:
        return PrototypeResult(-1, 0.0, 0.0, False)

    distance_sq = np.full(CLASS_COUNT, np.inf, dtype=np.float32)
    for label in range(CLASS_COUNT):
        if model.valid_label_mask & (1 << label):
            delta = (
                (value - model.prototypes[label]) / model.scale
            ).astype(np.float32)
            distance_sq[label] = np.mean(
                np.square(delta, dtype=np.float32),
                dtype=np.float32,
            )

    order = np.argsort(distance_sq)
    best_label = int(order[0])
    best_distance_sq = float(distance_sq[best_label])
    second_distance_sq = float(distance_sq[int(order[1])])
    nearest_distance = float(np.sqrt(best_distance_sq))

    if nearest_distance > model.rejection_distance:
        confidence = min(
            1.0,
            (nearest_distance - model.rejection_distance)
            / model.rejection_distance,
        )
        return PrototypeResult(-1, confidence, nearest_distance, False)

    finite = np.isfinite(distance_sq)
    scores = np.exp(-0.5 * distance_sq[finite].astype(np.float64))
    probability = float(
        np.exp(-0.5 * best_distance_sq) / scores.sum()
    )
    distance_confidence = (
        1.0 - nearest_distance / model.rejection_distance
    )
    margin_confidence = min(
        1.0,
        (second_distance_sq - best_distance_sq)
        / (second_distance_sq + 1.0e-6),
    )
    confidence = (
        probability
        * (0.5 + 0.5 * distance_confidence)
        * (0.5 + 0.5 * margin_confidence)
    )
    return PrototypeResult(
        best_label,
        float(confidence),
        nearest_distance,
        True,
    )


def classify_embeddings(
    model: PrototypeSet,
    embeddings: np.ndarray,
) -> list[PrototypeResult]:
    values = _validate_embeddings(embeddings)
    return [classify_embedding(model, value) for value in values]
