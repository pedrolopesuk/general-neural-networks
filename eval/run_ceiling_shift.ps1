# v1.1: proper i.i.d. ceilings (tuned i.i.d.) on frozen features + rotated-Omniglot domain shift.
$py = Join-Path $PSScriptRoot "..\.venv\Scripts\python.exe"
$bench = Join-Path $PSScriptRoot "run_bench.py"
function Run($argv) { Write-Output "===== $argv"; & $py $bench @argv 2>&1 | Where-Object { $_ -notmatch '%$' } }
foreach ($l in "linear", "mlp_sgd", "mlp_replay") { Run @("--learner", $l, "--track", "pretrained", "--iid", "20", "--tune") }
foreach ($shift in "rot90", "rot180") {
    foreach ($l in "ncm", "slda", "mlp_replay") { Run @("--learner", $l, "--track", "pretrained", "--shift", $shift, "--tune") }
    Run @("--learner", "linear", "--track", "pretrained", "--shift", $shift, "--iid", "20", "--tune")
}
