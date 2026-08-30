# Learned AI model v1

`heuristic-imitation-v1.model` is a deterministic linear action-ranking model
trained from schema-v1 `BasiliskAiSim --transitions-output` records.

The first line is:

```
BASILISK_LINEAR_POLICY <model-version> <observation-schema> <action-schema> <feature-schema> <feature-count>
```

It is followed by exactly 128 finite, whitespace-separated weights. Inference
hashes the fixed feature names defined by `encodeLearnedPolicyFeatures` into
those 128 slots with FNV-1a, scores only the shared safety-filtered legal action
list, and deterministically chooses the first highest score.

Feature schema v1 is a 128-dimensional state-action vector. It includes action
type and position; difficulty/behavior interactions; normalized health, arrows,
and round; player-known Pit/Basilisk/Jackal/rival warnings and candidate counts;
known Sigil/extraction state; target discovery/survey/Pit/previous-cave facts;
item/contextual target type; and the previous action type. It contains no
authoritative world state or rival-private fields. Output is one scalar score
per filtered legal action, followed by deterministic argmax.

Regenerate from the repository root:

```bash
./build-game/clients/sim/BasiliskAiSim --matches 1000 --seed 123 \
  --p1-difficulty hard --p1-behavior balanced \
  --p2-difficulty medium --p2-behavior aggressive \
  --transitions-output /tmp/basilisk-heuristic-v1-transitions.jsonl
./build-game/clients/sim/BasiliskAiSim --matches 200 --seed 456 \
  --p1-difficulty hard --p1-behavior balanced \
  --p2-difficulty medium --p2-behavior aggressive \
  --transitions-output /tmp/basilisk-heuristic-v1-validation.jsonl
python3 tools/ai/train_linear_policy.py \
  --input /tmp/basilisk-heuristic-v1-transitions.jsonl \
  --validation-input /tmp/basilisk-heuristic-v1-validation.jsonl \
  --output /tmp/heuristic-imitation-v1.model --seed 123
```

Copy the verified output into this directory only when intentionally updating
the versioned model baseline.
