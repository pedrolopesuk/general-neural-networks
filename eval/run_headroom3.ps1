# Same-data oracle at shorter training lengths (100, 250 steps) to rule out overfitting; best length is reported.
$py = Join-Path $PSScriptRoot "..\.venv\Scripts\python.exe"
$bench = Join-Path $PSScriptRoot "run_bench.py"
function Run($argv) { Write-Output "===== $argv"; & $py $bench @argv 2>&1 | Where-Object { $_ -notmatch '%$' } }
function J($h) { ($h | ConvertTo-Json -Compress).Replace('"', '\"') }
while (Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'run_headroom2' }) { Start-Sleep 30 }
foreach ($n in 100, 250) {
    Run @("--dataset", "kkanji", "--track", "stream_enc", "--learner", "ncm", "--hp", (J @{enc_steps = $n}), "--variant", "steps$n")
    foreach ($e in 1e-4, 0.01, 0.1, 0.3, 0.5, 0.7, 0.9, 0.99) {
        Run @("--dataset", "kkanji", "--track", "stream_enc", "--learner", "slda", "--hp", (J @{shrinkage = $e; enc_steps = $n}), "--variant", "oracle_shr$e-steps$n")
    }
}
