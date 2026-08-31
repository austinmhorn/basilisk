# Learned AI models

`heuristic-imitation-v2.model` is the current deterministic linear action-ranking model
trained from schema-v1 `BasiliskAiSim --transitions-output` records.
`heuristic-imitation-v1.model` remains checked in for offline comparison, but the
runtime intentionally rejects it as incompatible and falls back to the heuristic.

The first line is:

```
BASILISK_LINEAR_POLICY <model-version> <observation-schema> <action-schema> <feature-schema> <feature-count>
```

The v2 artifact uses feature schema 2 and is followed by exactly 128 finite,
whitespace-separated weights. Inference
hashes the fixed feature names defined by `encodeLearnedPolicyFeatures` into
those 128 slots with FNV-1a, scores only the shared safety-filtered legal action
list, and deterministically chooses the first highest score.

Feature schema v2 is a 128-dimensional state-action vector. It includes action
type and position; difficulty/behavior interactions; normalized health, arrows,
and round; player-known Pit/Basilisk/Jackal/rival warnings and candidate counts;
known Sigil/extraction state; target discovery/survey/Pit/previous-cave facts;
item/contextual target type; the previous action type; legal-action count;
health/ammo/candidate bands; and target connectivity. It contains no
authoritative world state or rival-private fields. Output is one scalar score
per filtered legal action, followed by deterministic argmax.

Regenerate from the repository root:

```bash
./build-game/clients/sim/BasiliskAiSim --matches 300 --seed 101 \
  --p1-difficulty easy --p1-behavior balanced \
  --p2-difficulty medium --p2-behavior explorer \
  --transitions-output /tmp/basilisk-v2-train-1.jsonl
# Repeat with the documented Phase 4 seed/config matrix for train-2..6 and validation-1..6.
python3 tools/ai/train_linear_policy.py \
  --input /tmp/basilisk-v2-train-1.jsonl \
  --validation-input /tmp/basilisk-v2-validation-1.jsonl \
  --initial-model clients/shared/models/heuristic-imitation-v1.model \
  --output /tmp/heuristic-imitation-v2.model --seed 20260830 --epochs 2 \
  --learning-rate 0.005 --type-target-weight 0.1
```

The complete deterministic matrix uses 300 matches for each training row and
75 for each validation row:

| Set | Seed | P1 | P2 |
| --- | ---: | --- | --- |
| Train | 101 | Easy/Balanced | Medium/Explorer |
| Train | 202 | Medium/Aggressive | Hard/Survivalist |
| Train | 303 | Hard/Objective | Easy/Opportunist |
| Train | 404 | Easy/Survivalist | Hard/Aggressive |
| Train | 505 | Medium/Objective | Medium/Balanced |
| Train | 606 | Hard/Explorer | Hard/Opportunist |
| Validation | 1101 | Easy/Aggressive | Easy/Objective |
| Validation | 1202 | Medium/Survivalist | Medium/Opportunist |
| Validation | 1303 | Hard/Balanced | Hard/Objective |
| Validation | 1404 | Easy/Explorer | Hard/Balanced |
| Validation | 1505 | Medium/Objective | Hard/Aggressive |
| Validation | 1606 | Hard/Survivalist | Medium/Explorer |

Pass all six corresponding `--input` and `--validation-input` arguments to the
trainer. `evaluate_linear_policy.py` compares v1 and v2 against the same held-out
transition files.

## Shadow analysis and canary rollout

Analyze one or more Phase 3 shadow streams with:

```bash
python3 tools/ai/analyze_shadow_telemetry.py shadow.jsonl --gate
```

The initial gate requires at least 1,000 decisions, 35% exact agreement, 60%
action-type agreement, and at most 0.1% fallback or model-error rates. Every
threshold is an explicit command-line option so later rollout decisions can be
recorded without changing gameplay code.

Runtime canaries remain disabled by default. A local or server process opts in
with `--ai-policy canary --ai-model <path> --ai-canary-percent <0-100>`.
Canary assignment defaults to Medium and Hard AI only. Override the eligible
set with `--ai-canary-difficulties medium,hard` (or another comma-separated
subset). The default percentage remains zero.

Canary JSONL can be evaluated offline with
`tools/ai/analyze_canary_telemetry.py --gate <telemetry.jsonl>`. The gate
requires 1,000 learned-canary decisions, zero safety violations, fallback and
model-error rates below 0.1%, and no material completion or outcome regression
against the comparable heuristic cohort. A failure recommends returning the
configured canary percentage to zero; it never mutates runtime configuration.
Assignment is a stable hash of match context and `PlayerId`; it never consumes
gameplay RNG. `heuristic`, `learned`, and `shadow` retain their Phase 3 behavior.

Copy the verified output into this directory only when intentionally updating
the versioned model baseline.
