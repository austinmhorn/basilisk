#!/usr/bin/env python3
"""Compare Basilisk linear imitation models on transition JSONL."""

import argparse
import json
from collections import defaultdict
from pathlib import Path

from train_linear_policy import BEHAVIOR, DIFFICULTY, encode, score


def load_model(path):
    tokens = Path(path).read_text(encoding="utf-8").split()
    if len(tokens) < 6 or tokens[0] != "BASILISK_LINEAR_POLICY":
        raise ValueError(f"{path}: invalid model header")
    model_version, observation, action, feature_schema, count = map(int, tokens[1:6])
    weights = [float(value) for value in tokens[6:]]
    if observation != 1 or action != 1 or len(weights) != count:
        raise ValueError(f"{path}: incompatible or incomplete model")
    return model_version, feature_schema, count, weights


def evaluate(inputs, feature_schema, feature_count, weights):
    totals = defaultdict(lambda: [0, 0, 0])
    for path in inputs:
        with Path(path).open(encoding="utf-8") as source:
            for line in source:
                record = json.loads(line)
                observation = record["observation"]
                actions = observation["legalActions"]
                if not actions:
                    continue
                chosen = record["decision"]["legalActionIndex"]
                difficulty = DIFFICULTY[record["difficulty"]]
                behavior = BEHAVIOR[record["resolvedBehavior"]]
                candidates = [encode(observation, action, difficulty, behavior,
                                     index, feature_schema, feature_count)
                              for index, action in enumerate(actions)]
                predicted = max(range(len(candidates)),
                    key=lambda index: score(weights, candidates[index]))
                exact = predicted == chosen
                type_match = actions[predicted]["type"] == actions[chosen]["type"]
                for key in ("overall", record["difficulty"].lower()):
                    totals[key][0] += 1
                    totals[key][1] += exact
                    totals[key][2] += type_match
    return totals


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, action="append")
    parser.add_argument("--model", required=True, action="append")
    args = parser.parse_args()
    for path in args.model:
        version, schema, count, weights = load_model(path)
        totals = evaluate(args.input, schema, count, weights)
        examples, exact, action_type = totals["overall"]
        print(f"model={Path(path).name} version={version} feature_schema={schema} "
              f"features={count} examples={examples} "
              f"imitation_accuracy={exact / examples:.6f} "
              f"action_type_agreement={action_type / examples:.6f}")
        for difficulty in ("easy", "medium", "hard"):
            count_value, exact_value, type_value = totals[difficulty]
            print(f"  {difficulty}: examples={count_value} "
                  f"imitation_accuracy={exact_value / count_value:.6f} "
                  f"action_type_agreement={type_value / count_value:.6f}")


if __name__ == "__main__":
    main()
