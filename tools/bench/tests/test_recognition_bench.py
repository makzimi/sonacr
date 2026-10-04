import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from recognition_bench import Outcome, Trial, excerpt_starts, parse_probe_output, probe_completed, summarize  # noqa: E402


def outcome(trigger_id, recognized_id, first_ms=None, callbacks=0, noise=0.0):
    trial = Trial(trigger_id, Path("a.wav"), 0.0, 5.0, noise)
    return Outcome(trial, recognized_id, first_ms, callbacks)


class RecognitionBenchTest(unittest.TestCase):
    def test_excerpt_starts_are_centered_in_equal_slices(self):
        self.assertEqual(excerpt_starts(25.0, 5.0, 4), [2.5, 7.5, 12.5, 17.5])

    def test_excerpt_starts_clamp_short_audio_to_zero(self):
        self.assertEqual(excerpt_starts(3.0, 5.0, 2), [0.0, 0.0])

    def test_parse_probe_output_keeps_first_hit_and_counts_callbacks(self):
        text = "\n".join([
            '{"event":"recognized","triggerId":"a","confidence":0.9,"alignedCount":12,"atMs":3100}',
            '{"event":"recognized","triggerId":"b","confidence":0.8,"alignedCount":9,"atMs":3400}',
            '{"event":"end","durationMs":5000}',
        ])
        self.assertEqual(parse_probe_output(text), ("a", 3100, 2))

    def test_parse_probe_output_without_hits(self):
        self.assertEqual(parse_probe_output('{"event":"end","durationMs":5000}'), (None, None, 0))

    def test_probe_completed_with_recognized_and_end_lines(self):
        text = "\n".join([
            '{"event":"recognized","triggerId":"a","confidence":0.9,"alignedCount":12,"atMs":3100}',
            '{"event":"end","durationMs":5000}',
        ])
        self.assertTrue(probe_completed(text))

    def test_probe_completed_with_empty_text(self):
        self.assertFalse(probe_completed(""))

    def test_probe_completed_with_only_recognized_line(self):
        text = '{"event":"recognized","triggerId":"a","confidence":0.9,"alignedCount":12,"atMs":3100}'
        self.assertFalse(probe_completed(text))

    def test_summarize_scores_correct_wrong_missed_and_false_callbacks(self):
        summary = summarize([
            outcome("a", "a", first_ms=3000),
            outcome("a", "b", first_ms=2000),
            outcome("b", None),
            outcome("b", "b", first_ms=4000, noise=0.1),
            outcome(None, "a", callbacks=2),
        ])
        self.assertEqual(summary["positiveTrials"], 4)
        self.assertEqual(summary["correct"], 2)
        self.assertEqual(summary["wrong"], 1)
        self.assertEqual(summary["hitRate"], 0.5)
        self.assertEqual(summary["byNoise"]["0.0"], {"trials": 3, "correct": 1, "hitRate": 0.333})
        self.assertEqual(summary["medianLatencyMs"], 3500)
        self.assertEqual(summary["falseCallbacks"], 2)


if __name__ == "__main__":
    unittest.main()
