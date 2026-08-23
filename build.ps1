# build.ps1

# 1. Configure the project using the MSVC preset and disable LTO
Write-Host "Configuring CMake with MSVC preset..." -ForegroundColor Cyan
gci "$PSScriptRoot\build-msvc\3rdparty\libsdl-org\SDL" -fi *.cmake | del
cmake.exe --preset msvc -DUSE_LTO=OFF

# Check if the configuration step succeeded before moving to the build step
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed with exit code $LASTEXITCODE."
    Exit $LASTEXITCODE
}

# 2. Build the project and redirect both standard output and error streams to build.log
Write-Host "Building project (Logging output to build.log)..." -ForegroundColor Cyan
cmake --build --preset msvc-release --verbose > build.log 2>&1 

# Check if the build step succeeded
if ($LASTEXITCODE -eq 0) {
    Write-Host "Build completed successfully!" -ForegroundColor Green
} else {
    Write-Error "Build failed with exit code $LASTEXITCODE. Check build.log for details."
    Exit $LASTEXITCODE
}