param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
    [switch]$SkipGitUpdate
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
foreach ($tool in @('git', 'gh')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool 명령이 없습니다. Git 및 GitHub CLI 설치 후 다시 실행하세요."
    }
}
if (-not $SkipGitUpdate) {
    & git -C $ProjectRoot fetch origin main
    if ($LASTEXITCODE -ne 0) { throw 'Git fetch failed.' }
    & git -C $ProjectRoot merge --ff-only origin/main
    if ($LASTEXITCODE -ne 0) { throw 'Git fast-forward failed. 로컬 변경을 확인하세요.' }
}
$sha = (& git -C $ProjectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $sha) { throw 'Cannot determine current commit.' }
$runsJson = & gh run list -R Skylark51/Youngkly --workflow windows-build.yml --branch main --status success --limit 50 --json databaseId,headSha
if ($LASTEXITCODE -ne 0) { throw 'GitHub Actions 조회 실패. gh auth login으로 로그인하세요.' }
$runs = @($runsJson | ConvertFrom-Json)
$run = $runs | Where-Object { $_.headSha -eq $sha } | Select-Object -First 1
if (-not $run) { throw "현재 커밋 $sha 에 대한 성공한 Windows 빌드를 찾지 못했습니다." }
$temp = Join-Path ([IO.Path]::GetTempPath()) ('youngkly-update-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temp -Force | Out-Null
try {
    & gh run download $run.databaseId -R Skylark51/Youngkly -n Youngkly-Windows-portable -D $temp
    if ($LASTEXITCODE -ne 0) { throw 'GitHub Actions 실행 파일 다운로드 실패.' }
    $source = Join-Path $temp 'Youngkly.exe'
    if (-not (Test-Path -LiteralPath $source)) { throw '다운로드한 Youngkly.exe가 없습니다.' }
    $target = Join-Path $ProjectRoot 'Youngkly.exe'
    if (Test-Path -LiteralPath $target) {
        $backupDir = Join-Path $ProjectRoot 'backups'
        New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        Copy-Item -LiteralPath $target -Destination (Join-Path $backupDir "Youngkly-$stamp.exe") -Force
    }
    Copy-Item -LiteralPath $source -Destination $target -Force
    Write-Host "Youngkly 로컬 업데이트 완료: $target"
    Write-Host "Git commit: $sha"
    Write-Host "GitHub Actions run: $($run.databaseId)"
} finally {
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
