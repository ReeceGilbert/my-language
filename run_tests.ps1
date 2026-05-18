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
    "examples/arena_showcase.nr",
    "examples/fancy_builtins.nr",
    "examples/string_builtins.nr"
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
    },
    @{
        Path = "examples/modules/missing_main.nr"
        Expected = @(
            "Could not read imported file:",
            "examples/modules/missing_utils.nr"
        )
    },
    @{
        Path = "examples/errors/bad_abs.nr"
        Expected = @(
            "abs() expects a number."
        )
    },
    @{
        Path = "examples/errors/bad_floor.nr"
        Expected = @(
            "floor() expects a number."
        )
    },
    @{
        Path = "examples/errors/bad_min_empty.nr"
        Expected = @(
            "min() expects at least 1 argument."
        )
    },
    @{
        Path = "examples/errors/bad_min_type.nr"
        Expected = @(
            "min() expects only numbers."
        )
    },
    @{
        Path = "examples/errors/bad_randint_zero.nr"
        Expected = @(
            "randint() max must be greater than 0."
        )
    },
    @{
        Path = "examples/errors/bad_values.nr"
        Expected = @(
            "values() expects a dictionary."
        )
    },
    @{
        Path = "examples/errors/bad_clock_ms.nr"
        Expected = @(
            "clock_ms() expects exactly 0 arguments but got 1."
        )
    },
    @{
        Path = "examples/errors/bad_lower.nr"
        Expected = @(
            "lower() expects a string."
        )
    },
    @{
        Path = "examples/errors/bad_contains.nr"
        Expected = @(
            "contains() expects the second argument to be a string."
        )
    },
    @{
        Path = "examples/errors/bad_substr_start.nr"
        Expected = @(
            "substr() start index out of bounds."
        )
    },
    @{
        Path = "examples/errors/bad_substr_length.nr"
        Expected = @(
            "substr() length cannot be negative."
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