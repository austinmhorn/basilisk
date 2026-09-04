#!/usr/bin/env python3
import json
import tempfile
import unittest
from pathlib import Path

from analyze_shadow_telemetry import analyze, gate_passes


class ShadowAnalyzerTests(unittest.TestCase):
    def test_deterministic_breakdowns_and_outcomes(self):
        records = [
            {"schemaVersion": 1, "kind": "decision", "context": "a",
             "difficulty": 2, "behavior": 0, "heuristicActionType": 0,
             "agreement": True, "actionTypeAgreement": True,
             "fallback": False, "modelError": False},
            {"schemaVersion": 1, "kind": "decision", "context": "a",
             "difficulty": 2, "behavior": 0, "heuristicActionType": 2,
             "agreement": False, "actionTypeAgreement": False,
             "fallback": True, "modelError": True},
            {"schemaVersion": 1, "kind": "outcome", "context": "a",
             "outcome": 1, "winner": 1},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "shadow.jsonl"
            path.write_text("".join(json.dumps(value) + "\n" for value in records),
                            encoding="utf-8")
            first = analyze([path])
            second = analyze([path])
        self.assertEqual(first, second)
        self.assertEqual(first["overall"]["decisions"], 2)
        self.assertEqual(first["overall"]["agreementRate"], 0.5)
        self.assertEqual(first["breakdown"]["difficulty"]["hard"]["decisions"], 2)
        self.assertEqual(first["breakdown"]["outcome"]["basilisk-killed"][
            "decisions"], 2)
        self.assertEqual(first["highestDisagreement"][0]["value"], "shoot")
        self.assertTrue(gate_passes(first, 2, 0.5, 0.5, 0.5, 0.5))
        self.assertFalse(gate_passes(first, 3, 0.5, 0.5, 0.5, 0.5))


if __name__ == "__main__":
    unittest.main()
