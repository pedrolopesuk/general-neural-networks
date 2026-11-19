# Benchmark 1b: Kuzushiji-Kanji, 600 classes x 5 shots, pretrained (Omniglot encoder) + scratch,
# hyper-parameters tuned on a length-matched (600-class) stream from the disjoint tuning pool.
$py = Join-Path $PSScriptRoot "..\.venv\Scripts\python.exe"
$bench = Join-Path $PSScriptRoot "run_bench.py"
function Run($argv) { Write-Output "===== $argv"; & $py $bench @argv 2>&1 | Where-Object { $_ -notmatch '%$' } }
foreach ($track in "pretrained", "scratch") {
    foreach ($l in "ncm", "slda", "mlp_replay", "mlp_sgd") { Run @("--dataset", "kkanji", "--learner", $l, "--track", $track, "--tune") }
    Run @("--dataset", "kkanji", "--learner", "linear", "--track", $track, "--iid", "20", "--tune")
}
