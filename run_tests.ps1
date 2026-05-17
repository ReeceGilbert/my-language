$ErrorActionPreference = "Stop"

$exe = ".\cmake-build-debug\nearoh.exe"

$tests = @(
    "examples/hello.nr",
    "examples/functions.nr",
    "examples/classes.nr",
    "examples/dictionaries.nr",
    "examples/import_once_main.nr",
    "examples/import_path_normalize_main.nr",
    "examples/modules/main.nr",
    "examples/modules/main_nested.nr",
    "examples/core_stress.nr",
    "examples/arena_showcase.nr"
)

foreach ($test in $tests) {
    Write-Host ""
    Write-Host "=== RUN $test ==="

    & $exe $test

    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "FAILED: $test"
        exit $LASTEXITCODE
    }
}

Write-Host ""
Write-Host "ALL TESTS PASSED"