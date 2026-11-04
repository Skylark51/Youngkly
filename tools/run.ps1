param([Parameter(Mandatory=$true)][string]$Date,[int]$Week=0,[switch]$Offline,[string]$Project='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
  $env:PATH="D:/yc-tools/msys64/ucrt64/bin;$env:PATH"
  $reportDate=[datetime]::ParseExact($Date,'yyyy-MM-dd',[Globalization.CultureInfo]::InvariantCulture)
  if($Week -eq 0){$firstDay=[datetime]::new($reportDate.Year,$reportDate.Month,1);$weekday=(([int]$firstDay.DayOfWeek+6)%7);$Week=[math]::Floor(($reportDate.Day-1+$weekday)/7)+1}
  $config=Get-Content -LiteralPath 'config/youngkly.json' -Raw | ConvertFrom-Json -AsHashtable
  $config.week_of_month=$Week
  New-Item -ItemType Directory -Path '.youngkly' -Force | Out-Null
  $config | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath '.youngkly/run-config.json' -Encoding utf8NoBOM
  $arguments=@('--config','.youngkly/run-config.json','--date',$Date,'--analyze')
  if($Project){$arguments+=@('--project',$Project)}
  if(-not $Offline){$arguments+='--fetch'}
  & './build/Youngkly.exe' @arguments
  if($LASTEXITCODE){throw 'Sheet/output analysis failed'}
  $model=Get-Content -LiteralPath 'outputs/report-model.json' -Raw | ConvertFrom-Json
  if($model.sections){$count=($model.sections | ForEach-Object {$_.rows.Count} | Measure-Object -Sum).Sum}else{$count=$model.rows.Count}
  if($count -eq 0){throw "No selected rows for $Date"}
  & './build/youngkly_render.exe' 'outputs/report-model.json' '.youngkly/run-config.json' 'outputs/assets'
  if($LASTEXITCODE){throw 'Youngcraft rendering failed'}
  $author=([string]$config.report_author) -replace '[\\/:*?"<>|]', ''
  if([string]::IsNullOrWhiteSpace($author)){$author='이영섭'}
  $name='주간_연구_보고서_{0}_{1}월_{2}주차_{3}.docx' -f $author,$reportDate.Month,$Week,$reportDate.ToString('yyMMdd')
  & './build/Youngkly.exe' '--config' '.youngkly/run-config.json' '--model' 'outputs/report-model.json' '--docx' "outputs/$name"
  if($LASTEXITCODE){throw 'DOCX generation failed'}
} finally {Pop-Location}
