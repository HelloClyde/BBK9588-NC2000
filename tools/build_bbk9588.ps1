[CmdletBinding()]
param(
    [ValidateSet('Os', 'O2')]
    [string]$Optimization = 'O2',
    [switch]$SkipToolchainSetup,
    [switch]$DspSelfTest
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sdkRoot = Join-Path $repoRoot 'sdk'

if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot 'scripts\setup_toolchain.ps1'))) {
    throw 'BBK9588 SDK submodule is missing. Run: git submodule update --init sdk'
}
if (-not $SkipToolchainSetup) {
    & (Join-Path $sdkRoot 'scripts\setup_toolchain.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Toolchain setup failed' }
}

function Find-CrossTool([string]$Name) {
    $roots = @(
        (Join-Path $sdkRoot '.toolchain\bin'),
        (Join-Path $sdkRoot '.toolchain\g++-mipsel-none-elf-15.2.0\bin')
    )
    foreach ($root in $roots) {
        $candidate = Join-Path $root "mipsel-none-elf-$Name.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    throw "Cross tool not found: mipsel-none-elf-$Name.exe"
}

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $Executable $($Arguments -join ' ')"
    }
}

$gcc = Find-CrossTool 'gcc'
$gxx = Find-CrossTool 'g++'
$objcopy = Find-CrossTool 'objcopy'
$objdump = Find-CrossTool 'objdump'
$libgcc = (& $gcc -EL -march=mips32 -msoft-float -print-libgcc-file-name).Trim()
if (-not (Test-Path -LiteralPath $libgcc -PathType Leaf)) {
    throw "libgcc not found: $libgcc"
}
$python = (Get-Command python -ErrorAction Stop).Source
$buildRoot = Join-Path $repoRoot 'build\nc2000_bbk9588'
$objectRoot = Join-Path $buildRoot 'obj'
New-Item -ItemType Directory -Force -Path $objectRoot | Out-Null
$localLibgcc = Join-Path $objectRoot 'libgcc.a'
# The bundled GNU linker is not Unicode-path-safe when GCC expands its own
# library search directory.  A build-local copy is passed like the other
# object paths, which also keeps builds working from Chinese-named folders.
Copy-Item -LiteralPath $libgcc -Destination $localLibgcc -Force

$sources = @(
    'platform\bbk9588\entry.S',
    'platform\bbk9588\startup.c',
    'platform\bbk9588\runtime.c',
    'platform\bbk9588\cxx_runtime.cpp',
    'platform\bbk9588\core_stubs.cpp',
    'platform\bbk9588\diagnostic_log.cpp',
    'platform\bbk9588\cpu_loop_bbk.cpp',
    'platform\bbk9588\jit_mips32.cpp',
    'platform\bbk9588\sound_bbk.cpp',
    'platform\bbk9588\frontend.cpp',
    'dsp\dsp.cpp',
    'comm.cpp',
    'nc2000.cpp',
    'mem.cpp',
    'io_new.cpp',
    'NekoDriverIO.cpp',
    'nand.cpp',
    'nor.cpp',
    'ram.cpp',
    'cpu.cpp',
    'ansi\w65c02cpu.cpp',
    'ansi\w65c02op.cpp',
    'iv_uart.cpp'
)

$common = @(
    '-EL', '-march=mips32', '-msoft-float', '-mno-abicalls', '-G0', '-fno-pic',
    "-$Optimization", '-ffreestanding', '-fno-builtin',
    '-flto',
    '-ffunction-sections', '-fdata-sections',
    '-DBBK9588', '-DHANDYPSP', '-DNDEBUG',
    '-Wno-unused-variable', '-Wno-unused-function', '-Wno-unused-parameter',
    '-I', $repoRoot,
    '-I', (Join-Path $repoRoot 'platform\bbk9588\libc\include'),
    '-I', (Join-Path $sdkRoot 'sdk\include')
)
if ($DspSelfTest) {
    $common += '-DNC2000_DSP_SELF_TEST'
}

$objects = @()
foreach ($relativeSource in $sources) {
    $source = Join-Path $repoRoot $relativeSource
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Source is missing: $relativeSource"
    }
    $objectName = ($relativeSource -replace '[\\/:]', '_') -replace '\.(cpp|c|S)$', '.o'
    $object = Join-Path $objectRoot $objectName
    $extension = [IO.Path]::GetExtension($source)
    if ($extension -ceq '.cpp') {
        $compiler = $gxx
        $language = @('-std=c++11', '-fno-exceptions', '-fno-rtti',
            '-fno-threadsafe-statics', '-fno-use-cxa-atexit')
    } elseif ($extension -ceq '.c') {
        $compiler = $gcc
        $language = @('-std=c11')
    } else {
        $compiler = $gcc
        $language = @('-x', 'assembler-with-cpp')
    }
    Invoke-Checked $compiler ($common + $language + @('-c', $source, '-o', $object))
    $objects += $object
}

$elf = Join-Path $buildRoot 'nc2000_bbk9588.elf'
$raw = Join-Path $buildRoot 'nc2000_bbk9588.bin'
$map = Join-Path $buildRoot 'nc2000_bbk9588.map'
$dump = Join-Path $buildRoot 'nc2000_bbk9588.dump.txt'
$bda = Join-Path $buildRoot 'NC2000.bda'
$linker = Join-Path $repoRoot 'platform\bbk9588\bda.ld'
$icon = Join-Path $repoRoot 'platform\bbk9588\assets\nc2000_icon.png'
$linkRoot = Join-Path ([IO.Path]::GetTempPath()) 'nc2000_bbk9588_lto'
$linkObjectRoot = Join-Path $linkRoot 'obj'
New-Item -ItemType Directory -Force -Path $linkObjectRoot | Out-Null

# GCC's LTO wrapper is not Unicode-path-safe. Stage every linker input and
# output under the ASCII system temp path, then copy the finished artifacts
# back to the normal project build directory.
$linkObjects = @()
foreach ($object in $objects) {
    $stagedObject = Join-Path $linkObjectRoot ([IO.Path]::GetFileName($object))
    Copy-Item -LiteralPath $object -Destination $stagedObject -Force
    $linkObjects += $stagedObject
}
$stagedLibgcc = Join-Path $linkObjectRoot 'libgcc.a'
$stagedLinker = Join-Path $linkRoot 'bda.ld'
$stagedElf = Join-Path $linkRoot 'nc2000_bbk9588.elf'
$stagedRaw = Join-Path $linkRoot 'nc2000_bbk9588.bin'
$stagedMap = Join-Path $linkRoot 'nc2000_bbk9588.map'
Copy-Item -LiteralPath $localLibgcc -Destination $stagedLibgcc -Force
Copy-Item -LiteralPath $linker -Destination $stagedLinker -Force

if (-not (Test-Path -LiteralPath $icon -PathType Leaf)) {
    throw "BDA icon is missing: $icon"
}

$linkArguments = @(
    '-EL', '-march=mips32', '-msoft-float', '-mno-abicalls', '-G0', '-fno-pic',
    '-flto',
    '-nostdlib', '-Wl,--build-id=none', '-Wl,--gc-sections',
    "-Wl,-T,$stagedLinker", "-Wl,-Map,$stagedMap", '-o', $stagedElf
)
Invoke-Checked $gcc ($linkArguments + $linkObjects + @($stagedLibgcc))
Invoke-Checked $objcopy @('-O', 'binary', $stagedElf, $stagedRaw)
Copy-Item -LiteralPath $stagedElf -Destination $elf -Force
Copy-Item -LiteralPath $stagedRaw -Destination $raw -Force
Copy-Item -LiteralPath $stagedMap -Destination $map -Force
& $objdump -d -h $stagedElf | Out-File -LiteralPath $dump -Encoding ascii
if ($LASTEXITCODE -ne 0) { throw 'objdump failed' }
Invoke-Checked $python @(
    (Join-Path $repoRoot 'tools\pack_bbk9588_bda.py'),
    $raw, '--sdk', $sdkRoot, '--title', 'NC2000', '--category', '4',
    '--icon', $icon,
    '--output', $bda
)

Write-Host "ELF: $elf"
Write-Host "BDA: $bda"
