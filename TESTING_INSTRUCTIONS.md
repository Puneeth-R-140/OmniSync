# OmniSync v1.3 Testing Instructions

## Current Status

The v1.3 implementation is complete, but no C++ compiler is available to compile and run tests.

## Problem

The existing test executables (fuzz.exe, vle_test.exe, delta_test.exe, save_test.exe) were compiled against v1.2. The v1.3 changes to sequence.hpp added new member variables, changing the class memory layout. This makes the old executables incompatible with the new headers.

## Required Action

Install a C++ compiler to recompile all tests with v1.3 headers.

## Compiler Installation Options

### Option 1: MinGW-w64 (Recommended for Windows)

1. Download from: https://www.mingw-w64.org/downloads/
2. Or use w64devkit: https://github.com/skeeto/w64devkit/releases
3. Extract and add bin folder to PATH
4. Verify: `g++ --version`

### Option 2: MSVC (Microsoft Visual C++)

1. Download Visual Studio Build Tools
2. Install C++ build tools workload
3. Use Developer Command Prompt
4. Verify: `cl`

### Option 3: LLVM/Clang

1. Download from: https://releases.llvm.org/
2. Install and add to PATH
3. Verify: `clang++ --version`

## Compilation Commands

After installing a compiler, run these commands in E:\OmniSync:

```bash
# Compile all tests
g++ -std=c++17 -I include tests/fuzz_test.cpp -o fuzz.exe
g++ -std=c++17 -I include tests/vle_compression_test.cpp -o vle_test.exe
g++ -std=c++17 -I include tests/delta_sync_test.cpp -o delta_test.exe
g++ -std=c++17 -I include tests/save_load_test.cpp -o save_test.exe
g++ -std=c++17 -I include tests/gc_test.cpp -o gc_test.exe

# Run all tests
.\fuzz.exe
.\vle_test.exe
.\delta_test.exe
.\save_test.exe
.\gc_test.exe
```

## Expected Test Results

### Existing Tests (should still pass)
- fuzz.exe: 2,500 operations, 100% convergence
- vle_test.exe: 82.4% compression verified
- delta_test.exe: Delta sync correctness
- save_test.exe: Persistence verified

### New GC Test
- gc_test.exe: 5 tests covering GC correctness, multi-user, safety, auto-GC, and memory stats

## What to Look For

**Success criteria:**
- All existing tests pass (no regression)
- gc_test.exe: All 5 tests pass
- No memory leaks or crashes

**Potential issues:**
- Compilation errors would indicate header issues
- Test failures would indicate logic errors in GC implementation
- Crashes would indicate memory corruption

## Next Steps After Testing

1. If all tests pass, update task.md to mark testing complete
2. Run benchmarks to measure actual GC performance
3. Tag release: `git tag v1.3.0`
4. Push to repository with updated documentation
