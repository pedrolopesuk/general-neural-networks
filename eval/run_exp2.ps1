# Experiment 2 (docs/EXP2_HEBBIAN_EXPOSURE.md): novelty-gated Hebbian plastic layer, Benchmark 1c.
# v2: scale-free SLDA shrinkage (target = diag(cov)) everywhere; encoder preprocessing (enc_norm) chosen by the bar's tuning.
$py = Join-Path $PSScriptRoot "..\.venv\Scripts\python.exe"
$bench = Join-Path $PSScriptRoot "run_bench.py"
$res = Join-Path $PSScriptRoot "results"
function Run($argv) { Write-Output "===== $argv"; & $py $bench @argv 2>&1 | Where-Object { $_ -notmatch '%$' } }
function J($h) { ($h | ConvertTo-Json -Compress).Replace('"', '\"') }
$kk = @("--dataset", "kkanji", "--track", "pretrained")

# 1. Bar, re-tuned with the same shrinkage method: frozen SLDA, without and with exposure covariance.
if (-not (Test-Path "$res\tuning\slda-kkanji-diag_pretrained_s5.json")) { Run ($kk + @("--learner", "slda", "--hp", (J @{target = "diag"}), "--variant", "diag", "--tune"))
Run ($kk + @("--learner", "slda", "--exposure", "--hp", (J @{target = "diag"}), "--variant", "diag", "--tune")) }
$b1 = Get-Content "$res\tuning\slda-kkanji-diag_pretrained_s5.json" | ConvertFrom-Json
$b2 = Get-Content "$res\tuning\slda-exp-kkanji-diag_pretrained_s5.json" | ConvertFrom-Json
$norm = if ($b1.background_final_acc -ge $b2.background_final_acc) { $b1.hp.enc_norm } else { $b2.hp.enc_norm }
Write-Output "===== enc_norm (from bar tuning) = $norm"

# 2. Hebbian rows, all with target = diag and the bar's enc_norm.
$fx = @{target = "diag"; enc_norm = $norm}
function Get-HebHp($extra) { $h = $fx.Clone(); foreach ($k in $extra.Keys) { $h[$k] = $extra[$k] }; J $h }
$base = $kk + @("--exposure", "--learner", "hebbian")
Run ($base + @("--hp", (Get-HebHp @{}), "--tune"))                                                   # full gated (grid includes eta = 0)
$best = (Get-Content "$res\tuning\hebbian-exp-kkanji_pretrained_s5.json" | ConvertFrom-Json).hp
Run ($base + @("--hp", (Get-HebHp @{eta = 0.0}), "--variant", "randinit", "--tune"))                 # frozen random init, own kwta + shrinkage
Run ($base + @("--hp", (Get-HebHp @{gate = "one"}), "--variant", "g1", "--tune"))
Run ($base + @("--hp", (Get-HebHp @{rule = "kmeans"}), "--variant", "kmeans", "--tune"))
Run ($base + @("--hp", (Get-HebHp @{stream_plastic = $true}), "--variant", "streamplastic", "--tune"))
$src = "$res\omniglot\hebbian-exp-kkanji_pretrained_default_c600_s5.json"
foreach ($mode in "const", "shuffled") {  # inherit ALL of the gated row's tuned knobs, incl. cov_src
    $h = @{eta = $best.eta; kwta = $best.kwta; shrinkage = $best.shrinkage; cov_src = $best.cov_src; gate = $mode; gate_from = $src}
    Run ($base + @("--hp", (Get-HebHp $h), "--variant", $mode))
}
