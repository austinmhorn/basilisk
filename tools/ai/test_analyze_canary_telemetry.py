#!/usr/bin/env python3
import json
import tempfile
import unittest
from pathlib import Path

from analyze_canary_telemetry import analyze, health


class CanaryAnalysisTest(unittest.TestCase):
    def test_aggregation_and_health(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "canary.jsonl"
            records = []
            for index in range(1000):
                learned = index % 2 == 0
                records.append({"schemaVersion": 1, "kind": "canary-decision",
                    "context": f"match-{index // 2}", "round": 1,
                    "player": 1 if learned else 2, "difficulty": 1,
                    "behavior": 0, "assigned": learned,
                    "authoritativePolicy": "learned" if learned else "heuristic",
                    "actionType": 0, "fallback": False, "modelError": False,
                    "safetyViolation": False})
            for index in range(500):
                records.append({"schemaVersion": 1, "kind": "canary-outcome",
                    "context": f"match-{index}", "outcome": 1,
                    "winner": 1 if index % 2 == 0 else 2,
                    "completed": True, "stalled": False})
            path.write_text("".join(json.dumps(value) + "\n" for value in records),
                            encoding="utf-8")
            report = analyze([path])
            self.assertEqual(report["cohorts"]["canary"]["decisions"], 500)
            self.assertEqual(report["cohorts"]["heuristic"]["decisions"], 500)
            self.assertIn("difficulty:medium", report["breakdown"])
            self.assertFalse(health(report)["passed"])
            self.assertTrue(health(report, minimum_decisions=500)["passed"])

    def test_health_recommends_zero_on_failure(self):
        report = {"cohorts": {"canary": rates(1000, 2, 0, 0, 10, 9, 1),
                              "heuristic": rates(1000, 0, 0, 0, 10, 10, 0)}}
        result = health(report)
        self.assertFalse(result["passed"])
        self.assertIn("--ai-canary-percent 0", result["recommendation"])


def rates(decisions, fallbacks, errors, safety, matches, complete, wins):
    return {"decisions": decisions, "fallbacks": fallbacks,
            "modelErrors": errors, "safetyViolations": safety,
            "matches": matches, "completed": complete,
            "fallbackRate": fallbacks / decisions,
            "modelErrorRate": errors / decisions,
            "completionRate": complete / matches, "winRate": wins / matches}


if __name__ == "__main__":
    unittest.main()
