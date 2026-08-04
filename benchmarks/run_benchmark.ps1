# OmniSync vs Yjs Benchmark Runner
# Run from repo root: .\benchmarks\run_benchmark.ps1

$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent

# ── 1. Build OmniSync benchmark ───────────────────────────────────────────────
Write-Host "`n[1/4] Building OmniSync benchmark (Release)..." -ForegroundColor Cyan
cmake --build "$Root\build" --config Release --target omnisync_bench | Out-Null
Write-Host "      Built." -ForegroundColor Green

# ── 2. Install Yjs ────────────────────────────────────────────────────────────
Write-Host "[2/4] Installing Yjs..." -ForegroundColor Cyan
if (!(Test-Path "$PSScriptRoot\node_modules\yjs")) {
    Push-Location $PSScriptRoot
    npm install yjs --save-dev --silent
    Pop-Location
}
Write-Host "      Ready." -ForegroundColor Green

# ── 3. Run OmniSync ───────────────────────────────────────────────────────────
Write-Host "[3/4] Running OmniSync benchmark..." -ForegroundColor Cyan
$omniOutput = & "$Root\build\Release\omnisync_bench.exe" 2>&1
$omniResults = @{}
foreach ($line in $omniOutput) {
    if ($line -match "^RESULT:(.+):(.+)$") {
        $omniResults[$Matches[1]] = [double]$Matches[2]
    }
}
Write-Host "      Done." -ForegroundColor Green

# ── 4. Run Yjs ────────────────────────────────────────────────────────────────
Write-Host "[4/4] Running Yjs benchmark..." -ForegroundColor Cyan
$yjsOutput = node --expose-gc "$PSScriptRoot\yjs_bench.mjs" 2>&1
$yjsResults = @{}
foreach ($line in $yjsOutput) {
    if ($line -match "^RESULT:(.+):(.+)$") {
        $yjsResults[$Matches[1]] = [double]$Matches[2]
    }
}
Write-Host "      Done.`n" -ForegroundColor Green

# ── 5. Print comparison table ─────────────────────────────────────────────────
$workloads = @(
    @{ key = "sequential_insert";  label = "Sequential Insert (10K)" },
    @{ key = "random_insert";      label = "Random Insert (10K)"     },
    @{ key = "sequential_delete";  label = "Sequential Delete (10K)" },
    @{ key = "concurrent_merge";   label = "Concurrent Merge (5K+5K)"},
    @{ key = "tostring_snapshot";  label = "toString x1000 snapshot" }
)

$divider = "=" * 74
Write-Host $divider -ForegroundColor Yellow
Write-Host ("  {0,-30} {1,16} {2,16} {3,8}" -f "Workload", "OmniSync (C++)", "Yjs (Node.js)", "Winner") -ForegroundColor Yellow
Write-Host $divider -ForegroundColor Yellow

foreach ($w in $workloads) {
    $o = $omniResults[$w.key]
    $y = $yjsResults[$w.key]

    if ($o -lt $y) {
        $winner = "OmniSync"
        $winColor = "Green"
    } elseif ($y -lt $o) {
        $winner = "Yjs"
        $winColor = "Red"
    } else {
        $winner = "Tie"
        $winColor = "Gray"
    }

    $speedup = if ($o -gt 0) { [math]::Round($y / $o, 2) } else { "N/A" }
    $label = $w.label
    $oStr  = "{0:F3} ms" -f $o
    $yStr  = "{0:F3} ms" -f $y

    $row = "  {0,-30} {1,16} {2,16}" -f $label, $oStr, $yStr
    Write-Host $row -NoNewline
    Write-Host ("  {0} ({1}x)" -f $winner, $speedup) -ForegroundColor $winColor
}

Write-Host $divider -ForegroundColor Yellow
Write-Host "`n  N=10,000 ops | 5 runs | median reported | OmniSync built in Release mode`n" -ForegroundColor DarkGray
