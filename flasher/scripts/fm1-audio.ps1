# Make the FM-1 behave as a sound source only:
#   - PC sound never plays through the FM-1 (its "Speakers" output is disabled in Windows)
#   - the FM-1's own sound is passed through to the PC's default output ("Listen to this device")
#   - the FM-1 is never the default microphone
# Re-run after flashing a firmware: Windows may treat it as a new sound device.
#
#   fm1-audio.ps1            apply
#   fm1-audio.ps1 status     show current state, change nothing
#   fm1-audio.ps1 mute       stop passing the FM-1 through to the PC output
param([ValidateSet('apply', 'status', 'mute')] [string]$Action = 'apply')
$ErrorActionPreference = 'Stop'

# Where PC sound goes when Windows had picked the FM-1: first connected match wins.
# Names come from fm1-audio.local.json next to this script (not in git), e.g.
#   { "outputs": ["My Headphones", "My Interface"], "mics": ["My Mic"] }
# Without it, the first working device that isn't the FM-1 is used.
$mainOutputs = @(); $mainMics = @()
$localCfg = Join-Path $PSScriptRoot 'fm1-audio.local.json'
if (Test-Path $localCfg) {
    $cfg = Get-Content $localCfg -Raw | ConvertFrom-Json
    if ($cfg.outputs) { $mainOutputs = @($cfg.outputs) }
    if ($cfg.mics) { $mainMics = @($cfg.mics) }
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace Fm1Audio2 {
    [StructLayout(LayoutKind.Sequential)] public struct PROPERTYKEY { public Guid fmtid; public int pid; }
    [StructLayout(LayoutKind.Explicit, Size = 24)] public struct PROPVARIANT {
        [FieldOffset(0)] public ushort vt;
        [FieldOffset(8)] public short boolVal;
        [FieldOffset(8)] public IntPtr ptr;
    }
    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom { }
    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator {
        [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IntPtr devices);
        [PreserveSig] int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice device);
    }
    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice {
        [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr activationParams, out IntPtr iface);
        [PreserveSig] int OpenPropertyStore(int access, out IntPtr props);
        [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
    }
    [ComImport, Guid("870AF99C-171D-4F9E-AF0D-E63DF40C2BC9")] class PolicyConfigCom { }
    [ComImport, Guid("F8679F50-850A-41CF-9C72-430F290290C8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IPolicyConfig {
        [PreserveSig] int GetMixFormat();
        [PreserveSig] int GetDeviceFormat();
        [PreserveSig] int ResetDeviceFormat();
        [PreserveSig] int SetDeviceFormat();
        [PreserveSig] int GetProcessingPeriod();
        [PreserveSig] int SetProcessingPeriod();
        [PreserveSig] int GetShareMode();
        [PreserveSig] int SetShareMode();
        [PreserveSig] int GetPropertyValue([MarshalAs(UnmanagedType.LPWStr)] string id, int fxStore, ref PROPERTYKEY key, out PROPVARIANT value);
        [PreserveSig] int SetPropertyValue([MarshalAs(UnmanagedType.LPWStr)] string id, int fxStore, ref PROPERTYKEY key, ref PROPVARIANT value);
        [PreserveSig] int SetDefaultEndpoint([MarshalAs(UnmanagedType.LPWStr)] string id, int role);
        [PreserveSig] int SetEndpointVisibility([MarshalAs(UnmanagedType.LPWStr)] string id, int visible);
    }
    public static class Audio {
        static PROPERTYKEY ListenKey = new PROPERTYKEY { fmtid = new Guid("24DBB0FC-9311-4B3D-9CF0-18FF155639D4"), pid = 1 };
        // flow: 0 = output, 1 = input. Returns null when there is no default.
        public static string GetDefault(int flow) {
            var e = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
            IMMDevice d;
            if (e.GetDefaultAudioEndpoint(flow, 1, out d) != 0 || d == null) return null;
            string id; Marshal.ThrowExceptionForHR(d.GetId(out id)); return id;
        }
        public static void SetDefault(string id) {
            var p = (IPolicyConfig)new PolicyConfigCom();
            for (int role = 0; role < 3; role++) Marshal.ThrowExceptionForHR(p.SetDefaultEndpoint(id, role));
        }
        public static void SetEnabled(string id, bool enabled) {
            var p = (IPolicyConfig)new PolicyConfigCom();
            Marshal.ThrowExceptionForHR(p.SetEndpointVisibility(id, enabled ? 1 : 0));
        }
        public static bool GetListen(string id) {
            var p = (IPolicyConfig)new PolicyConfigCom();
            PROPVARIANT v; var k = ListenKey;
            Marshal.ThrowExceptionForHR(p.GetPropertyValue(id, 0, ref k, out v));
            return v.vt == 11 && v.boolVal != 0;
        }
        public static void SetListen(string id, bool on) {
            var p = (IPolicyConfig)new PolicyConfigCom();
            var v = new PROPVARIANT { vt = 11, boolVal = (short)(on ? -1 : 0) }; var k = ListenKey;
            Marshal.ThrowExceptionForHR(p.SetPropertyValue(id, 0, ref k, ref v));
        }
    }
}
'@

$reg = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio'
function Get-ContainerIds($ids) {
    # InstanceId -> container ID, for many devices in one query.
    $map = @{}
    if ($ids) {
        Get-PnpDeviceProperty -InstanceId $ids -KeyName DEVPKEY_Device_ContainerId -ErrorAction SilentlyContinue |
            ForEach-Object { $map[$_.InstanceId] = $_.Data }
    }
    $map
}
function Get-Endpoints {
    # Every audio endpoint Windows knows, with its state (1 active, 2 disabled, 4 not present, 8 unplugged).
    # Container IDs are fetched in one call: one call per endpoint costs ~0.8 s each.
    $eps = @(Get-PnpDevice -Class AudioEndpoint)
    $cont = Get-ContainerIds $eps.InstanceId
    $eps | ForEach-Object {
        if ($_.InstanceId -match '(\{0\.0\.([01])\.00000000\})\.(\{[0-9A-Fa-f-]+\})') {
            $flow = if ($Matches[2] -eq '0') { 'Render' } else { 'Capture' }
            $key = Join-Path $reg "$flow\$($Matches[3])"
            $state = if (Test-Path $key) { (Get-ItemProperty $key).DeviceState -band 0xF } else { 4 }
            [pscustomobject]@{
                Name      = $_.FriendlyName
                PnpId     = $_.InstanceId
                PnpStatus = $_.Status
                Id        = "$($Matches[1]).$($Matches[3])".ToLower()
                Flow      = $flow
                State     = $state
                Container = $cont[$_.InstanceId]
            }
        }
    }
}
function Find-First($endpoints, $flow, $prefs) {
    foreach ($p in $prefs) {
        $hit = $endpoints | Where-Object { $_.Flow -eq $flow -and $_.State -eq 1 -and -not $_.IsFm1 -and $_.Name -like "*$p*" } | Select-Object -First 1
        if ($hit) { return $hit }
    }
    $endpoints | Where-Object { $_.Flow -eq $flow -and $_.State -eq 1 -and -not $_.IsFm1 } | Select-Object -First 1
}

# The FM-1 is whatever is plugged in with M-VAVE's USB vendor ID, whatever name the firmware gives it.
# Custom firmwares may use their own USB ID (Penumbra: 1209:0001), so also match the audio device by name.
$usb = Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -match '^USB\\VID_4C4A&PID_[0-9A-F]{4}\\' -or
    ($_.Class -eq 'MEDIA' -and $_.InstanceId -like 'USB\*' -and $_.FriendlyName -match 'FM-1')
} | Select-Object -First 1
if (-not $usb) { Write-Output 'FM-1 not found on USB.'; exit 1 }
$container = (Get-PnpDeviceProperty -InstanceId $usb.InstanceId -KeyName DEVPKEY_Device_ContainerId).Data

# Windows sometimes hides the FM-1's recording device (seen with Tessera: "Synthesizer (Tessera Out)" was hidden,
# so it was missing from the endpoint list). Find hidden recording endpoints whose interface is one of the FM-1's
# USB audio functions and show them again (no admin needed).
if ($Action -eq 'apply') {
    $media = @(Get-PnpDevice -PresentOnly -Class MEDIA)
    $mcont = Get-ContainerIds $media.InstanceId
    $fmMedia = @($media | Where-Object { $mcont[$_.InstanceId] -eq $container } | ForEach-Object FriendlyName)
    Get-ChildItem (Join-Path $reg 'Capture') | ForEach-Object {
        $st = (Get-ItemProperty $_.PSPath).DeviceState
        $iface = (Get-ItemProperty (Join-Path $_.PSPath 'Properties') -ErrorAction SilentlyContinue).'{b3f8fa53-0004-438e-9003-51a46e139bfc},6'
        if (($st -band 0x10000000) -and $iface -and $fmMedia -contains $iface) {
            [Fm1Audio2.Audio]::SetEnabled("{0.0.1.00000000}.$($_.PSChildName.ToLower())", $true)
            Write-Output "Showed the FM-1's hidden recording device ($iface)."
        }
    }
}

$all = @(Get-Endpoints) | Where-Object { $_.State -ne 4 }
$all | ForEach-Object { $_ | Add-Member IsFm1 ($_.Container -eq $container) }
$fmOut = @($all | Where-Object { $_.IsFm1 -and $_.Flow -eq 'Render' })
$fmIn = @($all | Where-Object { $_.IsFm1 -and $_.Flow -eq 'Capture' })
if (-not $fmIn -and -not $fmOut) { Write-Output 'FM-1 is on USB but exposes no audio (download mode, or a firmware without USB audio).'; exit 1 }

if ($Action -eq 'apply') {
    foreach ($o in $fmOut) {
        if ([Fm1Audio2.Audio]::GetDefault(0) -eq $o.Id) {
            $main = Find-First $all 'Render' $mainOutputs
            if ($main) { [Fm1Audio2.Audio]::SetDefault($main.Id) }
        }
        if ($o.PnpStatus -eq 'OK') {
            # Disabling a sound device needs admin; Windows shows one prompt.
            $cmd = "Disable-PnpDevice -InstanceId '$($o.PnpId)' -Confirm:`$false"
            Start-Process pwsh -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList '-NoProfile', '-Command', $cmd
        }
    }
    foreach ($i in $fmIn) {
        if ([Fm1Audio2.Audio]::GetDefault(1) -eq $i.Id) {
            $mic = Find-First $all 'Capture' $mainMics
            if ($mic) { [Fm1Audio2.Audio]::SetDefault($mic.Id) }
        }
        [Fm1Audio2.Audio]::SetListen($i.Id, $true)
    }
}
elseif ($Action -eq 'mute') {
    foreach ($i in $fmIn) { [Fm1Audio2.Audio]::SetListen($i.Id, $false) }
}

# Report what is true now.
$now = @(Get-Endpoints) | Where-Object { $_.State -ne 4 }
$byId = @{}; $now | ForEach-Object { $byId[$_.Id] = $_ }
$dOut = [Fm1Audio2.Audio]::GetDefault(0); $dIn = [Fm1Audio2.Audio]::GetDefault(1)
"PC sound output : $(if ($dOut) { $byId[$dOut.ToLower()].Name } else { 'none' })"
"PC microphone   : $(if ($dIn) { $byId[$dIn.ToLower()].Name } else { 'none' })"
foreach ($o in $fmOut) { "FM-1 as output  : $($o.Name) is $(if ($byId[$o.Id].PnpStatus -eq 'OK' -and $byId[$o.Id].State -eq 1) { 'ENABLED (PC sound could go here)' } else { 'disabled' })" }
foreach ($i in $fmIn) { "FM-1 as source  : $($i.Name), pass-through to PC output is $(if ([Fm1Audio2.Audio]::GetListen($i.Id)) { 'ON' } else { 'off' })" }
