# Experiment 1 (docs/EXP1_NEUROMOD_PLASTICITY.md): neuromodulated encoder plasticity on Benchmark 1b.
$py = Join-Path $PSScriptRoot "..\.venv\Scripts\python.exe"
$bench = Join-Path $PSScriptRoot "run_bench.py"
$res = Join-Path $PSScriptRoot "results"
function Run($argv) { Write-Output "===== $argv"; & $py $bench @argv 2>&1 | Where-Object { $_ -notmatch '%$' } }
function J($h) { ($h | ConvertTo-Json -Compress).Replace('"', '\"') }
$base = @("--dataset", "kkanji", "--track", "pretrained", "--learner")
Run ($base + @("neuromod", "--hp", (J @{lr = 0.0}), "--variant", "lr0"))                     # sanity: must equal frozen NCM 35.1
Run ($base + @("neuromod", "--tune"))                                                         # full, v1
Run ($base + @("neuromod", "--hp", (J @{refresh = "exemplar"}), "--variant", "v2", "--tune"))  # full, v2
Run ($base + @("neuromod", "--hp", (J @{gate = "one"}), "--variant", "g1", "--tune"))          # g == 1
Run ($base + @("neuromod", "--hp", (J @{beta = 0.0}), "--variant", "surprise", "--tune"))     # surprise only
# const / shuffled: the full gated run's tuned hp + its per-seed g sequence (no separate tuning)
$best = (Get-Content "$res\tuning\neuromod-kkanji_pretrained_s5.json" | ConvertFrom-Json).hp
$src = "$res\omniglot\neuromod-kkanji_pretrained_default_c600_s5.json"
foreach ($mode in "const", "shuffled") {
    $h = @{lr = $best.lr; tau = $best.tau; k = $best.k; beta = $best.beta; gate = $mode; gate_from = $src}
    Run ($base + @("neuromod", "--hp", (J $h), "--variant", $mode))
}
Run ($base + @("replay_full", "--tune"))
# Headroom oracle: encoder trained i.i.d. on the Kanji tuning pool; SLDA shrinkage picked on the TEST stream (labelled oracle)
Run @("--dataset", "kkanji", "--track", "kkanji_enc", "--learner", "ncm")
foreach ($e in 1e-4, 0.01, 0.1, 0.3, 0.5, 0.7, 0.9, 0.99) {
    Run @("--dataset", "kkanji", "--track", "kkanji_enc", "--learner", "slda", "--hp", (J @{shrinkage = $e}), "--variant", "oracle_shr$e")
}
