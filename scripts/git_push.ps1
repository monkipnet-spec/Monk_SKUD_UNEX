# Run from local project directory in PowerShell
$ErrorActionPreference = "Stop"
git status
git add .
git commit -m "Update Monk_SKUD_UNEX C++ access control"
git push origin main
