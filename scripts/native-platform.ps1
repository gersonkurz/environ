# Detect the true native Windows CPU architecture even when running under x64 emulation on ARM64.
# Outputs "ARM64" or "x64".
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class NativeArchHelper
{
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool IsWow64Process2(IntPtr hProcess, out ushort pProcessMachine, out ushort pNativeMachine);

    [DllImport("kernel32.dll")]
    private static extern IntPtr GetCurrentProcess();

    private const ushort IMAGE_FILE_MACHINE_ARM64 = 0xAA64;

    public static string GetNativePlatform()
    {
        ushort procMachine = 0;
        ushort nativeMachine = 0;
        if (IsWow64Process2(GetCurrentProcess(), out procMachine, out nativeMachine))
        {
            if (nativeMachine == IMAGE_FILE_MACHINE_ARM64)
                return "ARM64";
            return "x64";
        }
        return Environment.GetEnvironmentVariable("PROCESSOR_ARCHITECTURE") == "ARM64" ? "ARM64" : "x64";
    }
}
"@

[NativeArchHelper]::GetNativePlatform()
