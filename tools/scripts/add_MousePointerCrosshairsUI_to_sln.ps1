param(
  [string]$SolutionPath = "C:\Users\mikehall\Documents\GitHub\PowerToys\PowerToys.sln"
)

$projRel = 'src\modules\MouseUtils\MousePointerCrosshairsUI\MousePointerCrosshairsUI.vcxproj'
$projGuid = '{D0B8D2C9-7F3E-4B5D-BF5F-9CF7E7D0F1A1}'
$projName = 'MousePointerCrosshairsUI'
$cppTypeGuid = '{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}'

if (!(Test-Path -LiteralPath $SolutionPath)) {
  Write-Error "Solution not found: $SolutionPath"
  exit 1
}

$lines = Get-Content -LiteralPath $SolutionPath
if (($lines -join "`n") -match [regex]::Escape($projRel)) {
  Write-Output 'EXISTS'
  exit 0
}

# Find the 'Global' line to insert before it
$globalIdx = (Select-String -InputObject $lines -Pattern '^Global$' -SimpleMatch | Select-Object -First 1).LineNumber
if (-not $globalIdx) { Write-Error 'Global section not found'; exit 1 }
$insertIdx = $globalIdx - 1

$projTemplate = @'
Project("{0}") = "{1}", "{2}", "{3}"
EndProject
'@
$projText = [string]::Format($projTemplate, $cppTypeGuid, $projName, $projRel, $projGuid)
$projLines = $projText -split "`r?`n"

# Insert project block
$before = $lines[0..($insertIdx-1)]
$after = $lines[$insertIdx..($lines.Length-1)]
$lines = @()
$lines += $before
$lines += $projLines
$lines += $after

# Insert configuration mappings
$cfgStart = (Select-String -InputObject $lines -Pattern '^\s*GlobalSection\(ProjectConfigurationPlatforms\) = postSolution$' | Select-Object -First 1).LineNumber
if (-not $cfgStart) { Write-Error 'ProjectConfigurationPlatforms section not found'; exit 1 }
$endCandidates = (Select-String -InputObject $lines -Pattern '^\s*EndGlobalSection$' | ForEach-Object { $_.LineNumber })
$cfgEnd = ($endCandidates | Where-Object { $_ -gt $cfgStart } | Select-Object -First 1)
if (-not $cfgEnd) { Write-Error 'EndGlobalSection for ProjectConfigurationPlatforms not found'; exit 1 }

$cfgFmt = "`t`t{0}.{1}|{2}.{3} = {4}|{5}"
$cfgLines = @(
  [string]::Format($cfgFmt, $projGuid, 'Debug', 'Any CPU', 'ActiveCfg', 'Debug', 'x64'),
  [string]::Format($cfgFmt, $projGuid, 'Debug', 'Any CPU', 'Build.0',  'Debug', 'x64'),
  [string]::Format($cfgFmt, $projGuid, 'Debug', 'ARM64',   'ActiveCfg', 'Debug', 'ARM64'),
  [string]::Format($cfgFmt, $projGuid, 'Debug', 'ARM64',   'Build.0',  'Debug', 'ARM64'),
  [string]::Format($cfgFmt, $projGuid, 'Debug', 'x64',     'ActiveCfg', 'Debug', 'x64'),
  [string]::Format($cfgFmt, $projGuid, 'Debug', 'x64',     'Build.0',  'Debug', 'x64'),
  [string]::Format($cfgFmt, $projGuid, 'Debug', 'x86',     'ActiveCfg', 'Debug', 'x64'),
  [string]::Format($cfgFmt, $projGuid, 'Release', 'Any CPU','ActiveCfg','Release','x64'),
  [string]::Format($cfgFmt, $projGuid, 'Release', 'Any CPU','Build.0', 'Release','x64'),
  [string]::Format($cfgFmt, $projGuid, 'Release', 'ARM64',  'ActiveCfg','Release','ARM64'),
  [string]::Format($cfgFmt, $projGuid, 'Release', 'ARM64',  'Build.0', 'Release','ARM64'),
  [string]::Format($cfgFmt, $projGuid, 'Release', 'x64',    'ActiveCfg','Release','x64'),
  [string]::Format($cfgFmt, $projGuid, 'Release', 'x64',    'Build.0', 'Release','x64'),
  [string]::Format($cfgFmt, $projGuid, 'Release', 'x86',    'ActiveCfg','Release','x64')
)

$beforeCfg = $lines[0..($cfgEnd-2)]
$afterCfg = $lines[($cfgEnd-1)..($lines.Length-1)]
$lines = @()
$lines += $beforeCfg
$lines += $cfgLines
$lines += $afterCfg

$lines | Set-Content -LiteralPath $SolutionPath -NoNewline
Write-Output 'OK'
