# Launch Brave with remote debugging on your normal profile (PowerShell).
# Fully quit Brave first (check Task Manager) or the flag is ignored.
param([int]$Port = 9222)
$brave = "C:\Program Files\BraveSoftware\Brave-Browser\Application\brave.exe"
if (-not (Test-Path $brave)) { Write-Host "Brave not found at $brave — edit this path."; exit 1 }
Write-Host "Launching Brave with --remote-debugging-port=$Port ..."
Start-Process $brave "--remote-debugging-port=$Port"
Write-Host "Sign in at https://gemini.google.com, then run: python gemini_gen.py"
