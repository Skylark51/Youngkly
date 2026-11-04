param([Parameter(Mandatory=$true)][string]$InputDocx,[Parameter(Mandatory=$true)][string]$OutputPdf)
$ErrorActionPreference='Stop'
$inputPath=(Resolve-Path -LiteralPath $InputDocx).Path
$outputPath=[System.IO.Path]::GetFullPath($OutputPdf)
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($outputPath)) | Out-Null
$word=$null
$document=$null
try {
  $word=New-Object -ComObject Word.Application
  $word.Visible=$false
  $word.DisplayAlerts=0
  $word.AutomationSecurity=3
  $document=$word.Documents.Open($inputPath,$false,$true,$false)
  $document.ExportAsFixedFormat($outputPath,17)
} finally {
  if($null -ne $document){$document.Close(0);[void][Runtime.InteropServices.Marshal]::ReleaseComObject($document)}
  if($null -ne $word){$word.Quit(0);[void][Runtime.InteropServices.Marshal]::ReleaseComObject($word)}
}
