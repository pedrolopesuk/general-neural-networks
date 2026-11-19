# Same-data oracle (stream_enc): encoder fine-tuned offline on the stream's own 3,000 shots; SLDA shrinkage picked on test (oracle).
$py = Join-Path $PSScriptRoot "..\.venv\Scripts\python.exe"
$bench = Join-Path $PSScriptRoot "run_bench.py"
function Run($argv) { Write-Output "===== $argv"; & $py $bench @argv 2>&1 | Where-Object { $_ -notmatch '%$' } }
function J($h) { ($h | ConvertTo-Json -Compress).Replace('"', '\"') }
Run @("--dataset", "kkanji", "--track", "stream_enc", "--learner", "ncm")
foreach ($e in 1e-4, 0.01, 0.1, 0.3, 0.5, 0.7, 0.9, 0.99) {
    Run @("--dataset", "kkanji", "--track", "stream_enc", "--learner", "slda", "--hp", (J @{shrinkage = $e}), "--variant", "oracle_shr$e")
}
