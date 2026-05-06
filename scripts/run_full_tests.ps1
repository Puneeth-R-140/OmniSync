param(
    [int]$OpsPerUser = 100,
    [int]$NumUsers = 5,
    [int]$Seed = 1337,
    [string]$BuildType = "Debug",
    [string]$BuildDir = "build"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "Run Full Tests: OpsPerUser=$OpsPerUser NumUsers=$NumUsers Seed=$Seed BuildType=$BuildType"

if (-not (Test-Path $BuildDir)) { mkdir $BuildDir | Out-Null }

Push-Location $PSScriptRoot/.. | Out-Null

Write-Host "Configuring (cmake)..."
cmake -S . -B $BuildDir -DCMAKE_BUILD_TYPE=$BuildType -DOMNISYNC_BUILD_TESTS=ON -DOMNISYNC_BUILD_EXAMPLES=ON

Write-Host "Building..."
cmake --build $BuildDir --config $BuildType -- -m

$ts = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$outFile = "test_results_full_$((Get-Date).ToString('yyyyMMdd_HHmmss')).txt"
"OmniSync Full Test Run`nGenerated: $ts`n" | Out-File $outFile -Encoding utf8

Write-Host "Running CTest..."
ctest --test-dir $BuildDir --build-config $BuildType --output-on-failure 2>&1 | Out-File $outFile -Append -Encoding utf8

Write-Host "Running fuzz_test (OpsPerUser=$OpsPerUser, NumUsers=$NumUsers, Seed=$Seed)..."
$fuzzExe = Join-Path $BuildDir "$BuildType\fuzz_test.exe"
if (Test-Path $fuzzExe) {
    "--- FUZZ TEST ---`nGenerated: $ts`n" | Out-File test_results_fuzz.txt -Encoding utf8
    & $fuzzExe $OpsPerUser $NumUsers $Seed 2>&1 | Out-File test_results_fuzz.txt -Append -Encoding utf8
    Get-Content test_results_fuzz.txt | Out-File $outFile -Append -Encoding utf8
} else { Write-Warning "fuzz_test.exe not found: $fuzzExe" }

Write-Host "Running network malformed tests..."
$netExe = Join-Path $BuildDir "$BuildType\network_malformed_test.exe"
if (Test-Path $netExe) {
    "--- NETWORK MALFORMED TESTS ---`nGenerated: $ts`n" | Out-File test_results_vle.txt -Encoding utf8
    & $netExe 2>&1 | Out-File test_results_vle.txt -Append -Encoding utf8
    Get-Content test_results_vle.txt | Out-File $outFile -Append -Encoding utf8
} else { Write-Warning "network_malformed_test.exe not found: $netExe" }

Write-Host "Collecting other test outputs..."
$tests = @("delta_test.exe","gc_test.exe","gc_coord_test.exe","vle_test.exe","save_load_test.exe")
foreach ($t in $tests) {
    $exe = Join-Path $BuildDir "$BuildType\$t"
    if (Test-Path $exe) {
        "--- $t ---`nGenerated: $ts`n" | Out-File $outFile -Append -Encoding utf8
        & $exe 2>&1 | Out-File $outFile -Append -Encoding utf8
    }
}

Write-Host "Running stability_test (1 hour duration)..."
$stabExe = Join-Path $BuildDir "$BuildType\stability_test.exe"
if (Test-Path $stabExe) {
    "--- stability_test.exe ---`nGenerated: $ts`n" | Out-File $outFile -Append -Encoding utf8
    & $stabExe --duration-hours 1 2>&1 | Out-File $outFile -Append -Encoding utf8
}

Write-Host "Full results written to: $outFile"

Pop-Location | Out-Null

Exit 0
