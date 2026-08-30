#!/usr/bin/env python3
"""Train Basilisk's deterministic linear imitation policy from transition JSONL."""

import argparse
import json
import math
import random
from pathlib import Path

MODEL_VERSION = 1
OBSERVATION_SCHEMA = 1
ACTION_SCHEMA = 1
FEATURE_SCHEMA = 1
FEATURE_COUNT = 128

DIFFICULTY = {"EASY": 0, "MEDIUM": 1, "HARD": 2}
BEHAVIOR = {
    "BALANCED": 0,
    "EXPLORER": 1,
    "AGGRESSIVE": 2,
    "OBJECTIVE-FOCUSED": 3,
    "SURVIVALIST": 4,
    "OPPORTUNIST": 5,
}


def feature_index(name: str) -> int:
    value = 14695981039346656037
    for byte in name.encode("utf-8"):
        value ^= byte
        value = (value * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value % FEATURE_COUNT


def add(values, name, amount=1.0):
    values[feature_index(name)] += amount


def add_state_action(values, state, action_type, amount=1.0):
    add(values, f"{state}|type={action_type}", amount)


def ratio(value, maximum):
    return max(0.0, min(1.0, value / maximum)) if maximum > 0 else 0.0


def encode(observation, action, difficulty, behavior, position):
    values = [0.0] * FEATURE_COUNT
    action_type = action["type"]
    add(values, "bias")
    add(values, f"type={action_type}")
    add(values, f"difficulty={difficulty}|type={action_type}")
    add(values, f"behavior={behavior}|type={action_type}")
    add_state_action(values, "health_ratio", action_type,
                     ratio(observation["health"], observation["maxHealth"]))
    add_state_action(values, "arrows_ratio", action_type,
                     ratio(observation["arrows"], observation["maxArrows"]))
    add_state_action(values, "round", action_type,
                     min(1.0, observation["round"] / 100.0))
    actions = observation["legalActions"]
    add_state_action(values, "legal_position", action_type,
                     position / (len(actions) - 1) if len(actions) > 1 else 0.0)

    knowledge = observation["knowledge"]
    for json_name, feature_name in (
        ("pitWarning", "pit_warning"),
        ("basiliskAdjacentWarning", "basilisk_adjacent"),
        ("basiliskDistantWarning", "basilisk_distant"),
        ("jackalWarning", "jackal_warning"),
        ("rivalWarning", "rival_warning"),
    ):
        if knowledge[json_name]:
            add_state_action(values, feature_name, action_type)
    add_state_action(values, "basilisk_candidates", action_type,
                     min(1.0, knowledge["basiliskCandidateCount"] / 6.0))
    add_state_action(values, "pit_candidates", action_type,
                     min(1.0, knowledge["unresolvedPitCandidates"] / 6.0))
    add_state_action(values, "repeated_searches", action_type,
                     min(1.0, knowledge["repeatedSearches"] / 5.0))

    objective = observation["objective"]
    if objective["recoverableSigil"]:
        add_state_action(values, "recoverable_sigil", action_type)
    if objective["hasSigil"]:
        add_state_action(values, "has_sigil", action_type)
    if objective["extractionCave"] is not None:
        add_state_action(values, "known_extraction", action_type)

    target_cave = action["targetCave"]
    if target_cave is not None:
        add(values, f"target_cave|type={action_type}")
        cave = next((candidate for candidate in observation["map"]
                     if candidate["cave"] == target_cave), None)
        if cave is not None:
            add(values, f"target_known|type={action_type}")
            if cave["surveyed"]:
                add(values, f"target_surveyed|type={action_type}")
            if cave["confirmedPit"]:
                add(values, f"target_confirmed_pit|type={action_type}")
            if cave["pitCandidate"]:
                add(values, f"target_pit_candidate|type={action_type}")
        if knowledge["previousCave"] == target_cave:
            add(values, f"target_previous_cave|type={action_type}")
    if action["targetTunnel"] is not None:
        add(values, f"target_tunnel|type={action_type}")
    if action["targetItem"] is not None:
        add(values, f"item={action['targetItem']}|type={action_type}")
    if action["contextualAction"] is not None:
        add(values, f"contextual={action['contextualAction']}|type={action_type}")
    previous = observation["previousAction"]
    if previous is not None:
        add(values, f"previous={previous['type']}|type={action_type}")
    return values


def read_examples(path):
    examples = []
    with Path(path).open(encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            record = json.loads(line)
            if record.get("schemaVersion") != 1:
                raise ValueError(f"line {line_number}: incompatible transition schema")
            observation = record["observation"]
            if observation.get("schemaVersion") != OBSERVATION_SCHEMA:
                raise ValueError(f"line {line_number}: incompatible observation schema")
            actions = observation["legalActions"]
            if not actions:
                continue
            if any(action.get("schemaVersion") != ACTION_SCHEMA for action in actions):
                raise ValueError(f"line {line_number}: incompatible action schema")
            chosen = record["decision"]["legalActionIndex"]
            if chosen < 0 or chosen >= len(actions):
                raise ValueError(f"line {line_number}: chosen action is not legal")
            difficulty = DIFFICULTY[record["difficulty"]]
            behavior = BEHAVIOR[record["resolvedBehavior"]]
            features = [encode(observation, action, difficulty, behavior, index)
                        for index, action in enumerate(actions)]
            examples.append((features, chosen))
    if not examples:
        raise ValueError("transition dataset contained no decisions")
    return examples


def score(weights, features):
    return sum(weight * value for weight, value in zip(weights, features))


def accuracy(weights, examples):
    correct = 0
    for candidates, chosen in examples:
        predicted = max(range(len(candidates)), key=lambda index: score(weights, candidates[index]))
        correct += predicted == chosen
    return correct / len(examples)


def train(examples, epochs, learning_rate, regularization, seed):
    weights = [0.0] * FEATURE_COUNT
    order = list(range(len(examples)))
    rng = random.Random(seed)
    for epoch in range(epochs):
        rng.shuffle(order)
        rate = learning_rate / (1.0 + epoch * 0.15)
        shrink = max(0.0, 1.0 - rate * regularization)
        for example_index in order:
            candidates, chosen = examples[example_index]
            scores = [score(weights, candidate) for candidate in candidates]
            peak = max(scores)
            probabilities = [math.exp(value - peak) for value in scores]
            total = sum(probabilities)
            probabilities = [value / total for value in probabilities]
            weights = [weight * shrink for weight in weights]
            for feature in range(FEATURE_COUNT):
                expected = sum(probabilities[index] * candidates[index][feature]
                               for index in range(len(candidates)))
                weights[feature] += rate * (candidates[chosen][feature] - expected)
    return weights


def write_model(path, weights):
    with Path(path).open("w", encoding="utf-8", newline="\n") as output:
        output.write(
            f"BASILISK_LINEAR_POLICY {MODEL_VERSION} {OBSERVATION_SCHEMA} "
            f"{ACTION_SCHEMA} {FEATURE_SCHEMA} {FEATURE_COUNT}\n")
        output.write(" ".join(format(weight, ".17g") for weight in weights))
        output.write("\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--validation-input")
    parser.add_argument("--seed", type=int, default=123)
    parser.add_argument("--epochs", type=int, default=8)
    parser.add_argument("--learning-rate", type=float, default=0.03)
    parser.add_argument("--regularization", type=float, default=0.0001)
    args = parser.parse_args()
    examples = read_examples(args.input)
    validation = read_examples(args.validation_input) if args.validation_input else None
    weights = train(examples, args.epochs, args.learning_rate,
                    args.regularization, args.seed)
    write_model(args.output, weights)
    message = f"examples={len(examples)} imitation_accuracy={accuracy(weights, examples):.6f}"
    if validation is not None:
        message += (f" validation_examples={len(validation)}"
                    f" validation_accuracy={accuracy(weights, validation):.6f}")
    print(message)


if __name__ == "__main__":
    main()
