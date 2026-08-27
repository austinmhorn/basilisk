# Basilisk Heuristic AI Baseline v1

This directory preserves the first reproducible aggregate benchmark baseline
for Basilisk's player-safe heuristic AI. It exists to calibrate the current
heuristics, identify seat/configuration bias, and provide a stable comparison
point for future learned-policy and self-play work. Raw episode JSONL and build
artifacts are intentionally not stored here.

## Provenance

- Benchmark schema version: `1`
- Simulation seed: `123`
- Matches per orientation: `2,000`
- Difficulty suite: `18,000` total matches
- Hard behavior suite: `98,000` total matches
- Approximate observed throughput:
  - Difficulty suite: `~539 matches/sec` average across matchup rows
  - Hard behavior suite: `~636 matches/sec` average across matchup rows

Throughput is informational and hardware-dependent. Episode results are
deterministic for the recorded seed and configurations; asymmetric matchups use
the same episode seeds in both seat orientations.

## Baseline findings

Difficulty sanity passed for all three comparisons:

- Medium outperformed Easy.
- Hard outperformed Medium.
- Hard outperformed Easy.

Identical-configuration seat deltas were small for the Balanced difficulty
calibration: Easy `-0.25pp`, Medium `-1.40pp`, and Hard `-0.85pp` (P1 minus P2
win rate). Across mirrored asymmetric Hard behavior matchups, the largest
observed seat delta was `2.75pp` for configuration A and `3.20pp` for
configuration B, below the benchmark's `5pp` meaningful-dependence reporting
threshold. Hard Random vs Hard Random had the largest identical-configuration
delta at `-4.30pp`.

## Regeneration

From the repository root after configuring the native game build:

```bash
./build-game/clients/sim/BasiliskAiSim \
  --benchmark difficulty \
  --matches-per-orientation 2000 \
  --seed 123 \
  --benchmark-output benchmarks/ai/heuristic-v1/difficulty.csv

./build-game/clients/sim/BasiliskAiSim \
  --benchmark hard-behaviors \
  --matches-per-orientation 2000 \
  --seed 123 \
  --benchmark-output benchmarks/ai/heuristic-v1/hard-behaviors.csv
```

These commands overwrite only the aggregate CSVs. Use the simulator's separate
`--output` option when temporary per-episode JSONL is needed; do not add those
raw files to this baseline directory.
