# All Benchmark-1 baselines, both tracks, one protocol (v1):
#   tuning = 100 background classes held out from encoder pretraining (eval/omniglot.py: tuning_classes)
#   default 5-shot stream, its i.i.d. ceiling (20 passes, order-sensitive learners only), ANML protocol.
$py = Join-Path $PSScriptRoot "..\.venv\Scripts\python.exe"
$bench = Join-Path $PSScriptRoot "run_bench.py"
function Run($argv) { Write-Output "===== $argv"; & $py $bench @argv 2>&1 | Where-Object { $_ -notmatch '%$' } }
$x11 = @("--hp", '{\"steps_per_example\": 11}', "--variant", "x11")
$learners = @(@("--learner", "ncm"), @("--learner", "slda"), @("--learner", "mlp_sgd"),
              (@("--learner", "mlp_sgd") + $x11), @("--learner", "mlp_ewc"), @("--learner", "mlp_replay"),
              @("--learner", "vit_online"), @("--learner", "linear"))
foreach ($track in "scratch", "pretrained") {
    foreach ($cfg in $learners) {
        if ($track -eq "pretrained" -and ($cfg -contains "vit_online")) { continue }
        $t = @("--track", $track)
        Run (@($cfg) + $t + @("--tune"))
        if (-not ($cfg -contains "ncm" -or $cfg -contains "slda")) { Run (@($cfg) + $t + @("--iid", "20", "--tune")) }
        Run (@($cfg) + $t + @("--protocol", "anml", "--tune"))
    }
}
