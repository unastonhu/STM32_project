from pathlib import Path
import sys
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from prototype_math import (
    EMBEDDING_DIMENSION,
    PROTOTYPE_SCALE_FLOOR,
    classify_embedding,
    fit_prototypes,
)


class PrototypeMathTest(unittest.TestCase):
    def test_fit_uses_uniform_scale_floor(self) -> None:
        embeddings = np.zeros((4, EMBEDDING_DIMENSION), dtype=np.float32)
        labels = np.asarray([0, 0, 1, 1], dtype=np.int64)

        model = fit_prototypes(embeddings, labels)

        np.testing.assert_allclose(
            model.scale,
            np.full(EMBEDDING_DIMENSION, PROTOTYPE_SCALE_FLOOR),
        )
        np.testing.assert_array_equal(model.counts, [2, 2, 0])
        self.assertEqual(model.valid_label_mask, 0b011)

    def test_nearest_prototype_is_selected(self) -> None:
        fresh = np.zeros((3, EMBEDDING_DIMENSION), dtype=np.float32)
        spoiled = np.full(
            (3, EMBEDDING_DIMENSION),
            4.0,
            dtype=np.float32,
        )
        model = fit_prototypes(
            np.concatenate([fresh, spoiled]),
            np.asarray([0, 0, 0, 2, 2, 2]),
        )

        result = classify_embedding(
            model,
            np.full(EMBEDDING_DIMENSION, 3.9, dtype=np.float32),
        )

        self.assertTrue(result.valid)
        self.assertEqual(result.label, 2)
        self.assertGreater(result.confidence, 0.0)

    def test_one_label_is_not_enough_to_classify(self) -> None:
        model = fit_prototypes(
            np.zeros((2, EMBEDDING_DIMENSION), dtype=np.float32),
            np.asarray([0, 0]),
        )

        result = classify_embedding(
            model,
            np.zeros(EMBEDDING_DIMENSION, dtype=np.float32),
        )

        self.assertFalse(result.valid)
        self.assertEqual(result.label, -1)


if __name__ == "__main__":
    unittest.main()
