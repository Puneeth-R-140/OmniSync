param(
    [int]$OpsPerUser = 10000,
    [int]$NumUsers = 5,
    [int]$Seed = 42,
    [double]$StabilityDurationHours = 1.0,
    [string]$BuildType = "Release",
    [string]$BuildDir = "build",
    [int]$Iterations = 1
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "=========================================="
Write-Host "OmniSync STRESS TEST"
Write-Host "=========================================="
Write-Host "Fuzz Stress: OpsPerUser=$OpsPerUser, NumUsers=$NumUsers, Seed=$Seed"
Write-Host "Stability Duration: $StabilityDurationHours hours"
Write-Host "Build Type: $BuildType"
Write-Host "Iterations: $Iterations"
Write-Host ""

if (-not (Test-Path $BuildDir)) { mkdir $BuildDir | Out-Null }

Push-Location $PSScriptRoot/.. | Out-Null

# Build once
Write-Host "[1/3] Building in $BuildType mode..."
cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=$BuildType -DOMNISYNC_BUILD_TESTS=ON -DOMNISYNC_BUILD_EXAMPLES=ON
cmake --build $BuildDir --config $BuildType -- -m

$ts = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$outFile = "stress_test_results_$((Get-Date).ToString('yyyyMMdd_HHmmss')).txt"
"OmniSync Stress Test Run`nGenerated: $ts`nFuzz Params: OpsPerUser=$OpsPerUser, NumUsers=$NumUsers, Seed=$Seed`nStability Duration: $StabilityDurationHours hours`nIterations: $Iterations`n" | Out-File $outFile -Encoding utf8

Write-Host ""
Write-Host "[2/3] Running heavy fuzz iterations..."

for ($i = 1; $i -le $Iterations; $i++) {
    Write-Host "  Iteration $i/$Iterations..."
    $fuzzExe = Join-Path $BuildDir "$BuildType\fuzz_test.exe"
    
    if (Test-Path $fuzzExe) {
        $iterationSeed = $Seed + $i - 1
        "--- FUZZ TEST ITERATION $i (Seed: $iterationSeed) ---`n" | Out-File $outFile -Append -Encoding utf8
        & $fuzzExe $OpsPerUser $NumUsers $iterationSeed 2>&1 | Out-File $outFile -Append -Encoding utf8
        "" | Out-File $outFile -Append -Encoding utf8
    }
}

Write-Host ""
Write-Host "[3/3] Running extended stability test ($StabilityDurationHours hour(s))..."
$stabExe = Join-Path $BuildDir "$BuildType\stability_test.exe"
if (Test-Path $stabExe) {
    "--- STABILITY TEST ($StabilityDurationHours hour(s)) ---`n" | Out-File $outFile -Append -Encoding utf8
    & $stabExe --duration-hours $StabilityDurationHours 2>&1 | Out-File $outFile -Append -Encoding utf8
}

Write-Host ""
Write-Host "=========================================="
Write-Host "Stress test complete!"
Write-Host "Results: $outFile"
Write-Host "=========================================="

Pop-Location | Out-Null

Exit 0
