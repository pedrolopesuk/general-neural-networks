# Rerun of the mlp_sgd x11 baseline (PowerShell 5.1 stripped the JSON quotes in run_baselines.ps1),
# plus the diagnostic-only SLDA fixed-shrinkage check on the test stream (pretrained track).
$py = Join-Path $PSScriptRoot "..\.venv\Scripts\python.exe"
$bench = Join-Path $PSScriptRoot "run_bench.py"
function Run($argv) { Write-Output "===== $argv"; & $py $bench @argv 2>&1 | Where-Object { $_ -notmatch '%$' } }
$x11 = @("--learner", "mlp_sgd", "--hp", '{\"steps_per_example\": 11}', "--variant", "x11")
foreach ($track in "scratch", "pretrained") {
    $t = @("--track", $track)
    Run ($x11 + $t + @("--tune"))
    Run ($x11 + $t + @("--iid", "20"))
    Run ($x11 + $t + @("--protocol", "anml", "--tune"))
}
foreach ($s in "0.1", "0.3", "0.5") {
    Run (@("--learner", "slda", "--track", "pretrained", "--hp", "{\`"shrinkage\`": $s}", "--variant", "diag_shr$s"))
}
