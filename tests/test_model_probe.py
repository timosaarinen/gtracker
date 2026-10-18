"""Independent generated signals for the known-score measurement helper."""
import math
import random
import unittest
from tools import model_probe


class ModelProbeMeasurements(unittest.TestCase):
    def test_conditioned_pitch_measures_detuning_and_rejects_noise_silence(self):
        rate = 16000
        samples = [0.3*math.sin(2*math.pi*110.3*i/rate) for i in range(4800)]
        result = model_probe.known_pitch(samples, rate, 110)
        self.assertEqual(result["status"], "estimated")
        self.assertAlmostEqual(result["hz"], 110.3, delta=0.03)
        self.assertGreater(result["error_cents"], 4)
        self.assertEqual(model_probe.known_pitch([0.0]*4800, rate, 110)["status"], "no_signal")
        noise = random.Random(42)
        result = model_probe.known_pitch([noise.uniform(-0.3, 0.3) for _ in range(4800)], rate, 110)
        self.assertEqual(result["status"], "unreliable")
        self.assertIsNone(result["hz"])

    def test_decay_matches_a_known_exponential_with_window_resolution(self):
        rate = 8000
        samples = [0.7*math.exp(-i/rate/0.5)*math.sin(2*math.pi*100*i/rate) for i in range(rate*3)]
        result = model_probe.decay_20db(samples, rate)
        self.assertEqual(result["status"], "measured")
        self.assertAlmostEqual(result["seconds_from_reference"], math.log(10)*0.5, delta=0.05)
        self.assertEqual(model_probe.decay_20db([0.0]*rate, rate)["status"], "no_reference_signal")


if __name__ == "__main__": unittest.main()
