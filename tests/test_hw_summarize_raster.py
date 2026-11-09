#!/usr/bin/env python3
"""Synthetic regression checks of the [MH-DEKO-N] parsing; no proprietary inputs required."""
import importlib.util
from pathlib import Path
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/hw-summarize.py"
spec = importlib.util.spec_from_file_location("hw_summarize", SCRIPT)
hw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hw)


def report(flip, raster, unattributed, transform, other, classes):
    lines = ["[MH-DEKO-N] flip=%d raster_ns=%d unattributed=%d transform_refused=%d more_than_16_combinations=%d combinations=%d"
             % (flip, raster, unattributed, transform, other, len(classes))]
    lines += ["[MH-DEKO-N] flip=%d ns=%d class=%s" % (flip, ns, name) for name, ns in classes]
    return lines


class RasterReports(unittest.TestCase):
    def test_complete_report(self):
        log = "\n".join(report(5, 100, 10, 20, 30, [("shading", 15), ("cull+shading", 25)]))
        complete, rejected = hw.raster_reports(log)
        self.assertEqual(rejected, [])
        self.assertEqual(complete[0]["buckets"]["cull+shading"], 25)

    def test_lost_class_line_is_rejected(self):
        lines = report(5, 100, 10, 20, 30, [("shading", 15), ("cull+shading", 25)])
        del lines[2]
        self.assertEqual(hw.raster_reports("\n".join(lines)), ([], [5]))

    def test_sum_mismatch_is_rejected(self):
        log = "\n".join(report(5, 101, 10, 20, 30, [("shading", 15), ("cull+shading", 25)]))
        self.assertEqual(hw.raster_reports(log), ([], [5]))

    def test_line_with_foreign_prefix_is_rejected(self):
        lines = report(5, 100, 10, 20, 30, [("shading", 40)])
        lines[1] = "[MH-INPUT] " + lines[1]
        self.assertEqual(hw.raster_reports("\n".join(lines)), ([], [5]))

    def test_class_lines_without_header_are_rejected(self):
        self.assertEqual(hw.raster_reports("[MH-DEKO-N] flip=5 ns=7 class=shading"), ([], [5]))

    def test_window_subtracts_the_earlier_report(self):
        log = "\n".join(report(5, 100, 100, 0, 0, [])
                        + report(10, 150, 100, 0, 0, [("shading", 50)])
                        + report(15, 450, 150, 0, 0, [("shading", 200), ("cull", 100)]))
        complete, rejected = hw.raster_reports(log)
        self.assertEqual(rejected, [])
        window = hw.raster_window(complete)
        self.assertEqual((window["from"], window["to"], window["raster_ns"]), (5, 15, 350))
        self.assertEqual(window["buckets"]["shading"], 200)
        self.assertEqual(window["buckets"]["(unattributed)"], 50)
        self.assertEqual(sum(window["buckets"].values()), window["raster_ns"])

    def test_same_log_read_twice_is_harmless(self):
        log = "\n".join(report(5, 100, 60, 0, 0, [("shading", 40)]) * 2)
        complete, rejected = hw.raster_reports(log)
        self.assertEqual(rejected, [])
        self.assertEqual(len(complete), 1)

    def test_two_runs_in_one_log_give_no_window(self):
        run_a = [report(f, f * 10, f * 10, 0, 0, []) for f in range(5, 101, 5)]
        run_b = [report(f, f * 3, f * 3, 0, 0, []) for f in range(5, 96, 5)]
        log = "\n".join(line for r in run_a + run_b for line in r)
        complete, rejected = hw.raster_reports(log)
        self.assertEqual(rejected, list(range(5, 96, 5)))
        self.assertIsNone(hw.raster_window(complete))

    def window_of(self, first, second):
        complete, rejected = hw.raster_reports("\n".join(first + second))
        self.assertEqual(rejected, [])
        return hw.raster_window(complete)

    def test_class_going_backwards_gives_no_window(self):
        self.assertIsNone(self.window_of(report(5, 500, 0, 0, 0, [("shading", 400), ("cull", 100)]),
                                         report(15, 1000, 0, 0, 0, [("shading", 100), ("cull", 900)])))

    def test_class_missing_from_the_last_report_gives_no_window(self):
        self.assertIsNone(self.window_of(report(5, 500, 0, 0, 0, [("shading", 400), ("cull", 100)]),
                                         report(15, 1000, 0, 0, 0, [("shading", 1000)])))

    def test_total_going_backwards_gives_no_window(self):
        self.assertIsNone(self.window_of(report(5, 500, 500, 0, 0, []), report(15, 100, 100, 0, 0, [])))

    def test_window_needs_ten_flips(self):
        complete, _ = hw.raster_reports("\n".join(report(5, 1, 1, 0, 0, []) + report(10, 2, 2, 0, 0, [])))
        self.assertIsNone(hw.raster_window(complete))


class TextureReports(unittest.TestCase):
    H = "[MH-DEKO-X] flip=%d raster_ns=%d other=%d evicted=%d not_printed=%d textures=%d of=9 target_draws_lost=0"
    E = "[MH-DEKO-X] flip=%d ns=%d draws=2 fmt=0x07 size=512x512 pitch=0 levels=1 filter=0x0 addr=1,3 blend=0x302/0x303 stages=0x1 shading=2 combiner=1 offset=0x%08X target=1 rendered_flip=7 bound_flip=8 reasons=tex_format"

    F = "[MH-DEKO-F] flip=%d fmt=0x07 ns=%d alone_ns=%d draws=4"

    def test_latest_complete_report_sorted_by_time(self):
        log = "\n".join([self.H % (5, 100, 100, 0, 0, 0), self.H % (10, 300, 50, 25, 25, 2), self.F % (10, 250, 200), self.E % (10, 50, 1), self.E % (10, 150, 2)])
        report, rejected = hw.texture_report(log)
        self.assertEqual(rejected, [])
        self.assertEqual(report["flip"], 10)
        self.assertEqual(report["not_printed_ns"], 25)
        self.assertEqual(report["evicted_ns"], 25)
        self.assertEqual(report["formats"][0], {"fmt": "0x07", "ns": 250, "alone_ns": 200, "draws": 4})
        self.assertEqual([e["ns"] for e in report["textures"]], [150, 50])
        self.assertEqual((report["textures"][0]["rendered_flip"], report["textures"][0]["bound_flip"]), (7, 8))
        self.assertEqual(report["textures"][0]["reasons"], "tex_format")
        self.assertEqual((report["textures"][0]["addr"], report["textures"][0]["blend"]), ("1,3", "0x302/0x303"))

    def test_lost_line_or_bad_sum_is_rejected(self):
        self.assertEqual(hw.texture_report("\n".join([self.H % (5, 100, 50, 0, 0, 2), self.F % (5, 50, 0), self.E % (5, 50, 1)])), (None, [5]))
        self.assertEqual(hw.texture_report("\n".join([self.H % (5, 101, 50, 0, 0, 1), self.F % (5, 51, 0), self.E % (5, 50, 1)])), (None, [5]))

    def test_lost_format_line_is_rejected(self):
        self.assertEqual(hw.texture_report("\n".join([self.H % (5, 100, 50, 0, 0, 1), self.E % (5, 50, 1)])), (None, [5]))

    def test_two_runs_at_one_flip_are_rejected(self):
        log = "\n".join([self.H % (5, 50, 50, 0, 0, 0), self.H % (5, 60, 60, 0, 0, 0)])
        self.assertEqual(hw.texture_report(log), (None, [5, 5]))

    def test_row_flags(self):
        def flags(stages, combiner=1, reasons="tex_format"):
            return hw.texture_row_flags({"stages": stages, "combiner": combiner, "reasons": reasons})
        self.assertEqual(flags("0x1"), "")
        self.assertIn("stage 0 not sampled", flags("0x0"))
        self.assertIn("other stages active", flags("0x21"))
        self.assertIn("no combiner seen", flags("0x0", combiner=0))
        self.assertIn("screen path", flags("0x0", reasons="path+tex_format"))
        self.assertNotIn("not sampled", flags("0x0", reasons="path+tex_format"))
        self.assertIn("not sampled", flags("0x0", reasons="tex_filter"))

    def test_two_runs_sharing_flips_give_no_report(self):
        old = [self.H % (f, f * 10, f * 10, 0, 0, 0) for f in range(5, 301, 5)]
        new = [self.H % (f, f * 3, f * 3, 0, 0, 0) for f in range(5, 151, 5)]
        report, rejected = hw.texture_report("\n".join(old + new))
        self.assertIsNone(report)
        self.assertTrue(rejected)

    def test_last_report_in_the_log_wins_over_the_highest_flip(self):
        log = "\n".join([self.H % (300, 50, 50, 0, 0, 0), self.H % (150, 60, 60, 0, 0, 0)])
        self.assertEqual(hw.texture_report(log)[0]["flip"], 150)


if __name__ == "__main__":
    unittest.main()
