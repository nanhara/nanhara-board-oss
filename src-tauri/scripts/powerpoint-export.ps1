param(
  [Parameter(Mandatory = $true)]
  [string]$InputPath,
  [Parameter(Mandatory = $true)]
  [string]$OutputPath
)

$ErrorActionPreference = "Stop"
$powerPoint = $null
$presentation = $null

try {
  $powerPoint = New-Object -ComObject PowerPoint.Application
  # msoAutomationSecurityForceDisable: never run document macros during import.
  $powerPoint.AutomationSecurity = 3
  # ReadOnly=true, Untitled=false, WithWindow=false.
  $presentation = $powerPoint.Presentations.Open($InputPath, -1, 0, 0)
  # ppFixedFormatTypePDF = 2.
  $presentation.ExportAsFixedFormat($OutputPath, 2)

  if (-not (Test-Path -LiteralPath $OutputPath)) {
    throw "PowerPoint did not create the PDF output."
  }
}
finally {
  if ($null -ne $presentation) {
    try { $presentation.Close() } catch {}
    try { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($presentation) } catch {}
  }
  if ($null -ne $powerPoint) {
    try { $powerPoint.Quit() } catch {}
    try { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($powerPoint) } catch {}
  }
  [GC]::Collect()
  [GC]::WaitForPendingFinalizers()
}
