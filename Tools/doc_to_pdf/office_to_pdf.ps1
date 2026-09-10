# Word / PowerPoint 本体を COM で呼び出して PDF へ書き出す。convert.py から使う。
param(
    [Parameter(Mandatory = $true)][string]$ListPath,
    [Parameter(Mandatory = $true)][string]$ResultPath
)

$ErrorActionPreference = 'Stop'

$WORD_EXT = @('.doc', '.docx', '.docm', '.rtf')
$PPT_EXT  = @('.ppt', '.pptx', '.pptm')

# 変換対象は UTF-8 の TSV(入力<TAB>出力)で受け取る。日本語パスを引数で渡さずに済ませるため。
$jobs = @()
foreach ($line in (Get-Content -LiteralPath $ListPath -Encoding UTF8)) {
    if ($line.Trim() -eq '') { continue }
    $cols = $line -split "`t"
    $jobs += , @($cols[0], $cols[1])
}

$results = New-Object System.Collections.Generic.List[string]
$word = $null
$ppt = $null

# 既に起動している Office は終了させない。利用者の開きっぱなしの資料を巻き込まないため。
$wordWasRunning = [bool](Get-Process -Name WINWORD -ErrorAction SilentlyContinue)
$pptWasRunning = [bool](Get-Process -Name POWERPNT -ErrorAction SilentlyContinue)

try {
    foreach ($job in $jobs) {
        $src = $job[0]
        $dst = $job[1]
        $ext = [System.IO.Path]::GetExtension($src).ToLower()
        try {
            if ($WORD_EXT -contains $ext) {
                if ($null -eq $word) {
                    $word = New-Object -ComObject Word.Application
                    $word.Visible = $false
                    $word.DisplayAlerts = 0
                }
                # ConfirmConversions=false, ReadOnly=true。確認ダイアログで止まらないように。
                $doc = $word.Documents.Open($src, $false, $true)
                try {
                    $doc.ExportAsFixedFormat($dst, 17)   # 17 = wdExportFormatPDF
                } finally {
                    $doc.Close(0)                        # 0 = wdDoNotSaveChanges
                }
            } elseif ($PPT_EXT -contains $ext) {
                if ($null -eq $ppt) { $ppt = New-Object -ComObject PowerPoint.Application }
                # ReadOnly=-1, Untitled=0, WithWindow=0 で開く。ウィンドウを出さないため。
                $pres = $ppt.Presentations.Open($src, -1, 0, 0)
                try {
                    $pres.SaveAs($dst, 32)               # 32 = ppSaveAsPDF
                } finally {
                    $pres.Close()
                }
            } else {
                throw "未対応の拡張子です: $ext"
            }
            $results.Add("OK`t$src`t")
        } catch {
            $msg = ($_.Exception.Message) -replace "[`r`n`t]", ' '
            $results.Add("NG`t$src`t$msg")
        }
    }
} finally {
    if ($null -ne $word) {
        if (-not $wordWasRunning) { try { $word.Quit(0) } catch {} }
        try { [System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null } catch {}
    }
    if ($null -ne $ppt) {
        if (-not $pptWasRunning) { try { $ppt.Quit() } catch {} }
        try { [System.Runtime.InteropServices.Marshal]::ReleaseComObject($ppt) | Out-Null } catch {}
    }
    Set-Content -LiteralPath $ResultPath -Value $results -Encoding UTF8
}
