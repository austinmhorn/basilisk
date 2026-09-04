#!/usr/bin/env python3
"""Analyze Basilisk runtime AI shadow telemetry JSONL."""

import argparse
import json
from collections import defaultdict
from pathlib import Path

DIFFICULTY = ["easy", "medium", "hard"]
BEHAVIOR = ["balanced", "explorer", "aggressive", "objective", "survivalist",
            "opportunist", "random"]
ACTION = ["move", "search", "shoot", "use-item", "contextual"]
OUTCOME = ["none", "basilisk-killed", "simultaneous-basilisk-kill",
           "escaped-with-sigil", "draw"]


def empty_bucket():
    return {"decisions": 0, "agreements": 0, "actionTypeAgreements": 0,
            "fallbacks": 0, "modelErrors": 0}


def add(bucket, record):
    bucket["decisions"] += 1
    bucket["agreements"] += bool(record["agreement"])
    bucket["actionTypeAgreements"] += bool(record["actionTypeAgreement"])
    bucket["fallbacks"] += bool(record["fallback"])
    bucket["modelErrors"] += bool(record["modelError"])


def finalize(bucket):
    count = bucket["decisions"]
    result = dict(bucket)
    result["agreementRate"] = bucket["agreements"] / count if count else 0.0
    result["actionTypeAgreementRate"] = (
        bucket["actionTypeAgreements"] / count if count else 0.0)
    result["fallbackRate"] = bucket["fallbacks"] / count if count else 0.0
    result["modelErrorRate"] = bucket["modelErrors"] / count if count else 0.0
    return result


def analyze(paths):
    overall = empty_bucket()
    groups = {name: defaultdict(empty_bucket) for name in
              ("difficulty", "behavior", "heuristicAction", "outcome")}
    contexts = defaultdict(list)
    outcome_count = 0
    for path in paths:
        with Path(path).open(encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                record = json.loads(line)
                if record.get("schemaVersion") != 1:
                    raise ValueError(f"{path}:{line_number}: incompatible schema")
                kind = record.get("kind")
                if kind == "decision":
                    for field in ("agreement", "actionTypeAgreement", "fallback",
                                  "modelError", "difficulty", "behavior",
                                  "heuristicActionType", "context"):
                        if field not in record:
                            raise ValueError(f"{path}:{line_number}: missing {field}")
                    add(overall, record)
                    add(groups["difficulty"][DIFFICULTY[record["difficulty"]]], record)
                    add(groups["behavior"][BEHAVIOR[record["behavior"]]], record)
                    add(groups["heuristicAction"][ACTION[
                        record["heuristicActionType"]]], record)
                    contexts[record["context"]].append(record)
                elif kind == "outcome":
                    outcome = OUTCOME[record["outcome"]]
                    for decision in contexts.pop(record["context"], []):
                        add(groups["outcome"][outcome], decision)
                    outcome_count += 1
                else:
                    raise ValueError(f"{path}:{line_number}: unknown record kind")
    for records in contexts.values():
        for record in records:
            add(groups["outcome"]["unfinished"], record)
    breakdown = {name: {key: finalize(value) for key, value in sorted(values.items())}
                 for name, values in groups.items()}
    disagreements = []
    for category, values in breakdown.items():
        for name, value in values.items():
            if value["decisions"]:
                disagreements.append({"category": category, "value": name,
                    "decisions": value["decisions"],
                    "disagreementRate": 1.0 - value["agreementRate"]})
    disagreements.sort(key=lambda value: (-value["disagreementRate"],
                                           -value["decisions"],
                                           value["category"], value["value"]))
    return {"schemaVersion": 1, "files": len(paths), "outcomes": outcome_count,
            "overall": finalize(overall), "breakdown": breakdown,
            "highestDisagreement": disagreements[:10]}


def print_bucket(name, bucket):
    print(f"  {name}: n={bucket['decisions']} "
          f"agreement={bucket['agreementRate']:.2%} "
          f"type={bucket['actionTypeAgreementRate']:.2%} "
          f"fallback={bucket['fallbackRate']:.2%} "
          f"error={bucket['modelErrorRate']:.2%}")


def print_report(report):
    print("Basilisk shadow telemetry v1")
    print_bucket("overall", report["overall"])
    print(f"  outcomes: {report['outcomes']}")
    for category, values in report["breakdown"].items():
        print(category)
        for name, bucket in values.items():
            print_bucket(name, bucket)
    print("highest disagreement")
    for value in report["highestDisagreement"]:
        print(f"  {value['category']}={value['value']}: "
              f"{value['disagreementRate']:.2%} (n={value['decisions']})")


def gate_passes(report, minimum_decisions, minimum_agreement,
                minimum_type_agreement, maximum_fallback, maximum_model_error):
    overall = report["overall"]
    return (overall["decisions"] >= minimum_decisions and
            overall["agreementRate"] >= minimum_agreement and
            overall["actionTypeAgreementRate"] >= minimum_type_agreement and
            overall["fallbackRate"] <= maximum_fallback and
            overall["modelErrorRate"] <= maximum_model_error)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("--json-output")
    parser.add_argument("--gate", action="store_true")
    parser.add_argument("--min-decisions", type=int, default=1000)
    parser.add_argument("--min-agreement", type=float, default=0.35)
    parser.add_argument("--min-action-type-agreement", type=float, default=0.60)
    parser.add_argument("--max-fallback-rate", type=float, default=0.001)
    parser.add_argument("--max-model-error-rate", type=float, default=0.001)
    args = parser.parse_args()
    report = analyze(args.inputs)
    print_report(report)
    if args.json_output:
        with Path(args.json_output).open("w", encoding="utf-8", newline="\n") as output:
            json.dump(report, output, sort_keys=True, separators=(",", ":"))
            output.write("\n")
    if args.gate:
        passed = gate_passes(report, args.min_decisions, args.min_agreement,
            args.min_action_type_agreement, args.max_fallback_rate,
            args.max_model_error_rate)
        print("canary gate: " + ("PASS" if passed else "FAIL"))
        if not passed:
            raise SystemExit(2)


if __name__ == "__main__":
    main()
