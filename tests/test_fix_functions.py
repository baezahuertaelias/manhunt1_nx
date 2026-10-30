#!/usr/bin/env python3
"""Synthetic regression checks; no proprietary inputs required."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/fix-functions.py"


class FunctionFixes(unittest.TestCase):
    def run_fix(self, funcs, fixes, expected=0):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name, data in (("functions", funcs), ("fixes", fixes)):
                (root / name).write_text(json.dumps(data))
            output = root / "output"
            result = subprocess.run([sys.executable, "-I", str(SCRIPT),
                                     str(root / "functions"), str(root / "fixes"), str(output)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, expected, result.stderr)
            if expected:
                self.assertFalse(output.exists())
                return result.stderr
            return json.loads(output.read_text())

    def test_add_in_gap(self):
        result = self.run_fix([self.fn(0x100, 0x110), self.fn(0x130, 0x140)],
                              [self.add(0x110, 0x130)])
        self.assertEqual([int(f["start"], 16) for f in result], [0x100, 0x110, 0x130])
        added = result[1]
        self.assertEqual((added["end"], added["size"], added["name"]),
                         ("0x00000130", 0x20, "sub_00000110"))
        self.assertEqual(added["calls_to"], ["0x00000100"])
        self.assertEqual(added["num_instructions"], 3)

    def test_add_rejects_analysis_drift_and_overlap(self):
        for start, end in ((0x100, 0x110), (0x105, 0x115),
                           (0xF0, 0x105), (0xF0, 0x120)):
            with self.subTest(start=start, end=end):
                self.run_fix([self.fn(0x100, 0x110)], [self.add(start, end)], 1)

    def test_add_rejects_invalid_range_or_metadata(self):
        for start, end in ((0, 0x10), (0x110, 0x110), (0x120, 0x110),
                           (0xFFFFFFF0, 0x100000001)):
            self.run_fix([], [self.add(start, end)], 1)
        fix = self.add(0x110, 0x120)
        fix["add"]["num_instructions"] = 0
        self.run_fix([], [fix], 1)

    def test_existing_boundary_fix_and_drift_guard(self):
        fix = {"owner": "0x100", "end": "0x120", "remove": ["0x108"]}
        result = self.run_fix([self.fn(0x100, 0x108), self.fn(0x108, 0x120)], [fix])
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]["size"], 0x20)
        self.run_fix([self.fn(0x100, 0x107), self.fn(0x108, 0x120)], [fix], 1)

    @staticmethod
    def fn(start, end):
        return {"start": hex(start), "end": hex(end), "name": "synthetic"}

    @staticmethod
    def add(start, end):
        return {"add": {"start": hex(start), "end": hex(end), "section": ".text",
                        "num_instructions": 3, "calls_to": ["0x00000100"]},
                "why": "synthetic measured entry"}


if __name__ == "__main__":
    unittest.main()
