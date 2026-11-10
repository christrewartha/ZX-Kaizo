# Serves build/web/ on this machine, to try the page before deploying it.
# The emulator can't run from a file:// URL, as browsers won't let it fetch
# its WebAssembly core or the tape.
#
#   tools/serve_web.ps1 [-Port 8080]
#
# Then open http://localhost:8080/. Ctrl+C stops it.

param([int]$Port = 8080)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$site = [IO.Path]::GetFullPath((Join-Path $root 'build/web'))
if (-not (Test-Path $site)) { throw "build/web not found: run tools/build_web.ps1 first" }

$types = @{
    '.html' = 'text/html; charset=utf-8'; '.js' = 'text/javascript'; '.wasm' = 'application/wasm'
    '.png' = 'image/png'; '.txt' = 'text/plain'; '' = 'text/plain'
}

$listener = New-Object Net.HttpListener
$listener.Prefixes.Add("http://localhost:$Port/")
$listener.Start()
Write-Host "Serving $site on http://localhost:$Port/"
try {
    while ($listener.IsListening) {
        # Poll, so Ctrl+C gets a chance to stop the script between requests
        $wait = $listener.GetContextAsync()
        while (-not $wait.AsyncWaitHandle.WaitOne(200)) { }
        $ctx = $wait.Result
        $path = [Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath)
        if ($path.EndsWith('/')) { $path += 'index.html' }
        $file = [IO.Path]::GetFullPath((Join-Path $site $path.TrimStart('/')))
        if ($file.StartsWith($site) -and (Test-Path $file -PathType Leaf)) {
            $ext = [IO.Path]::GetExtension($file).ToLower()
            $ctx.Response.ContentType = if ($types.ContainsKey($ext)) { $types[$ext] } else { 'application/octet-stream' }
            $bytes = [IO.File]::ReadAllBytes($file)
            $ctx.Response.ContentLength64 = $bytes.Length
            $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
        } else {
            $ctx.Response.StatusCode = 404
        }
        $ctx.Response.Close()
        Write-Host "$($ctx.Response.StatusCode) $path"
    }
} finally {
    $listener.Stop()
}
