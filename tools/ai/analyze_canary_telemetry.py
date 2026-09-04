#!/usr/bin/env python3
"""Analyze monitored Basilisk learned-policy canary JSONL."""

import argparse
import json
from collections import defaultdict
from pathlib import Path

DIFFICULTY = ["easy", "medium", "hard"]
BEHAVIOR = ["balanced", "explorer", "aggressive", "objective", "survivalist",
            "opportunist", "random"]
ACTION = ["move", "search", "shoot", "use-item", "contextual"]


def bucket():
    return {"decisions": 0, "fallbacks": 0, "modelErrors": 0,
            "safetyViolations": 0, "matches": 0, "completed": 0,
            "stalled": 0, "wins": 0, "losses": 0, "draws": 0}


def rates(value):
    result = dict(value)
    decisions = value["decisions"]
    matches = value["matches"]
    for name in ("fallbacks", "modelErrors", "safetyViolations"):
        result[name[:-1] + "Rate"] = value[name] / decisions if decisions else 0.0
    result["completionRate"] = value["completed"] / matches if matches else 0.0
    result["stallRate"] = value["stalled"] / matches if matches else 0.0
    result["winRate"] = value["wins"] / matches if matches else 0.0
    return result


def analyze(paths):
    cohorts = {"canary": bucket(), "heuristic": bucket()}
    breakdown = defaultdict(lambda: {"canary": bucket(), "heuristic": bucket()})
    players = defaultdict(dict)
    outcomes = {}
    for path in paths:
        with Path(path).open(encoding="utf-8") as source:
            for number, line in enumerate(source, 1):
                record = json.loads(line)
                if record.get("schemaVersion") != 1:
                    raise ValueError(f"{path}:{number}: incompatible schema")
                if record.get("kind") == "canary-decision":
                    required = ("context", "player", "difficulty", "behavior",
                                "assigned", "authoritativePolicy", "actionType",
                                "fallback", "modelError", "safetyViolation")
                    if any(field not in record for field in required):
                        raise ValueError(f"{path}:{number}: incomplete decision")
                    # Assignment defines the cohort even when a model error makes
                    # the authoritative decision fall back to the heuristic.
                    cohort = "canary" if record["assigned"] else "heuristic"
                    target = cohorts[cohort]
                    target["decisions"] += 1
                    target["fallbacks"] += bool(record["fallback"])
                    target["modelErrors"] += bool(record["modelError"])
                    target["safetyViolations"] += bool(record["safetyViolation"])
                    for label in (f"difficulty:{DIFFICULTY[record['difficulty']]}",
                                  f"behavior:{BEHAVIOR[record['behavior']]}",
                                  f"action:{ACTION[record['actionType']]}"):
                        grouped = breakdown[label][cohort]
                        grouped["decisions"] += 1
                        grouped["fallbacks"] += bool(record["fallback"])
                        grouped["modelErrors"] += bool(record["modelError"])
                        grouped["safetyViolations"] += bool(record["safetyViolation"])
                    key = (record["context"], record["player"])
                    players[key] = {"cohort": cohort,
                                    "difficulty": DIFFICULTY[record["difficulty"]],
                                    "behavior": BEHAVIOR[record["behavior"]]}
                elif record.get("kind") == "canary-outcome":
                    outcomes[record["context"]] = record
                elif record.get("kind") not in ("decision", "outcome"):
                    raise ValueError(f"{path}:{number}: unknown record kind")

    for (context, player), identity in players.items():
        outcome = outcomes.get(context)
        if outcome is None:
            continue
        cohort = identity["cohort"]
        keys = (f"difficulty:{identity['difficulty']}",
                f"behavior:{identity['behavior']}")
        targets = [cohorts[cohort]] + [breakdown[key][cohort] for key in keys]
        for target in targets:
            target["matches"] += 1
            target["completed"] += bool(outcome["completed"])
            target["stalled"] += bool(outcome["stalled"])
            winner = outcome.get("winner")
            if winner is None:
                target["draws"] += 1
            elif winner == player:
                target["wins"] += 1
            else:
                target["losses"] += 1
    return {"schemaVersion": 1,
            "cohorts": {name: rates(value) for name, value in cohorts.items()},
            "breakdown": {key: {name: rates(value) for name, value in groups.items()}
                          for key, groups in sorted(breakdown.items())}}


def health(report, minimum_decisions=1000, maximum_fallback=0.001,
           maximum_model_error=0.001, maximum_completion_regression=0.05,
           maximum_outcome_regression=0.05):
    canary = report["cohorts"]["canary"]
    heuristic = report["cohorts"]["heuristic"]
    reasons = []
    if canary["decisions"] < minimum_decisions:
        reasons.append("insufficient canary decisions")
    if canary["safetyViolations"] != 0:
        reasons.append("safety violations observed")
    if canary["fallbackRate"] >= maximum_fallback:
        reasons.append("fallback rate exceeds threshold")
    if canary["modelErrorRate"] >= maximum_model_error:
        reasons.append("model error rate exceeds threshold")
    if not canary["matches"] or not heuristic["matches"]:
        reasons.append("comparable completed cohorts unavailable")
    else:
        if heuristic["completionRate"] - canary["completionRate"] > maximum_completion_regression:
            reasons.append("material completion regression")
        if heuristic["winRate"] - canary["winRate"] > maximum_outcome_regression:
            reasons.append("material outcome regression")
    return {"passed": not reasons, "reasons": reasons,
            "recommendation": ("hold current canary" if not reasons else
                               "set --ai-canary-percent 0")}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("--json-output")
    parser.add_argument("--gate", action="store_true")
    parser.add_argument("--min-canary-decisions", type=int, default=1000)
    parser.add_argument("--max-fallback-rate", type=float, default=0.001)
    parser.add_argument("--max-model-error-rate", type=float, default=0.001)
    parser.add_argument("--max-completion-regression", type=float, default=0.05)
    parser.add_argument("--max-outcome-regression", type=float, default=0.05)
    args = parser.parse_args()
    report = analyze(args.inputs)
    report["health"] = health(report, args.min_canary_decisions,
        args.max_fallback_rate, args.max_model_error_rate,
        args.max_completion_regression, args.max_outcome_regression)
    print(json.dumps(report, sort_keys=True, indent=2))
    if args.json_output:
        Path(args.json_output).write_text(
            json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n",
            encoding="utf-8")
    if args.gate and not report["health"]["passed"]:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
