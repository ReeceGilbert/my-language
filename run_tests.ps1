$ErrorActionPreference = "Continue"

$exe = ".\cmake-build-debug\nearoh.exe"

$passTests = @(
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

$failTests = @(
    @{
        Path = "examples/import_cycle_a.nr"
        Expected = @(
            "Circular import detected."
        )
    },
    @{
        Path = "examples/modules/bad_main.nr"
        Expected = @(
            "examples/modules/bad_utils.nr",
            "Undefined variable."
        )
    }
)

foreach ($test in $passTests) {
    Write-Host ""
    Write-Host "=== PASS TEST $test ==="

    & $exe $test
    $exitCode = $LASTEXITCODE

    if ($exitCode -ne 0) {
        Write-Host ""
        Write-Host "FAILED: expected success but got exit code $exitCode"
        Write-Host "TEST: $test"
        exit $exitCode
    }
}

foreach ($test in $failTests) {
    Write-Host ""
    Write-Host "=== EXPECTED FAILURE TEST $($test.Path) ==="

    $output = & $exe $test.Path 2>&1
    $exitCode = $LASTEXITCODE
    $text = $output -join "`n"

    Write-Host $text

    if ($exitCode -eq 0) {
        Write-Host ""
        Write-Host "FAILED: expected failure but got success"
        Write-Host "TEST: $($test.Path)"
        exit 1
    }

    foreach ($expectedText in $test.Expected) {
        if ($text -notlike "*$expectedText*") {
            Write-Host ""
            Write-Host "FAILED: expected error output to contain:"
            Write-Host $expectedText
            Write-Host ""
            Write-Host "Actual output:"
            Write-Host $text
            exit 1
        }
    }
}

Write-Host ""
Write-Host "ALL TESTS PASSED"