param([Parameter(Mandatory=$true)][string]$ProjectRoot,
      [Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference = 'Stop'
$requests = [System.Collections.Generic.SortedSet[string]]::new([StringComparer]::Ordinal)
$roots = @((Join-Path $ProjectRoot 'RePlayEngine'), (Join-Path $ProjectRoot 'Source'))
$pattern = '\.CompileFile\(\s*shader_directory\s*/\s*(?:L?"(?<file>[^"\r\n]+)"|(?<table>\w+)\[index\])\s*,\s*L"(?<entry>\w+)"\s*,\s*L"(?<target>\w+)"\s*(?:,\s*debug_layer_enabled_\s*)?\)'
foreach ($file in Get-ChildItem -LiteralPath $roots -Recurse -File) {
    if ($file.Extension -notin @('.cpp', '.h', '.hpp', '.inl')) { continue }
    if ($file.Name -like '*Validation*' -or $file.Name -in @('D3D12ShaderCompiler.cpp', 'ShaderCompiler.cpp', 'ShaderPack.cpp')) { continue }
    $source = [IO.File]::ReadAllText($file.FullName)
    $calls = [regex]::Matches($source, '\.CompileFile\s*\(')
    $resolvedCalls = [regex]::Matches($source, $pattern)
    if ($calls.Count -ne $resolvedCalls.Count) {
        throw "Shader pack inventory cannot resolve CompileFile in $($file.FullName). Extend the inventory generator for the new compile options or path expression."
    }
    foreach ($call in $resolvedCalls) {
        $names = @($call.Groups['file'].Value)
        if ($call.Groups['table'].Success) {
            $name = [regex]::Escape($call.Groups['table'].Value)
            $table = [regex]::Match($source, "\b$name\[\]\s*\{(?<body>[^}]+)\}")
            if (!$table.Success) { throw "Unresolved shader array in $($file.FullName)" }
            $names = @([regex]::Matches($table.Groups['body'].Value, 'L"([^"\r\n]+)"') | ForEach-Object { $_.Groups[1].Value })
            $remainder = [regex]::Replace($table.Groups['body'].Value, 'L"[^"\r\n]+"|[\s,]', '')
            if ($names.Count -eq 0 -or $remainder.Length -ne 0) { throw "Unsupported shader array in $($file.FullName)" }
        }
        foreach ($name in $names) {
            if (!(Test-Path -LiteralPath (Join-Path $ProjectRoot "Shader/$name") -PathType Leaf)) { throw "Missing shader: $name" }
            [void]$requests.Add(('    {{ L"{0}", L"{1}", L"{2}" }},' -f $name.Replace('\','/'), $call.Groups['entry'].Value, $call.Groups['target'].Value))
        }
    }
    $sourceCalls = [regex]::Matches($source, '\.CompileSource\s*\(')
    $supportedSources = [regex]::Matches($source, '(?m)^\s*// replay-pack-source: (?:surface|ui-effect)\r?\n\s*const (?:auto|D3D12ShaderCompileResult) \w+ = compiler\.CompileSource\(combined,\s*source\.source_path,\s*L"main",\s*L"ps_6_0",\s*(?:options|ShaderPack::UIOptions\(debug_layer_enabled_\))\);')
    if ($sourceCalls.Count -ne $supportedSources.Count) {
        throw "Unregistered generated shader in $($file.FullName). Add an export recipe and its replay-pack-source annotation."
    }
}
if ($requests.Count -eq 0) { throw 'Shader pack inventory is empty.' }
$content = "// Generated from DX12 compile requests; do not edit.`r`n" + ($requests -join "`r`n") + "`r`n"
$absoluteOutput = [IO.Path]::GetFullPath($Output)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($absoluteOutput))
if (!(Test-Path -LiteralPath $absoluteOutput) -or [IO.File]::ReadAllText($absoluteOutput) -ne $content) {
    [IO.File]::WriteAllText($absoluteOutput, $content, [Text.UTF8Encoding]::new($true))
}
