# Haltech NSP ARM64 bridge

Experimental compatibility layer for connecting Haltech NSP to an Elite ECU
from Windows 11 ARM64. It is intended for Apple Silicon Macs running Windows
in Parallels, where NSP is an x86 application and FTDI provides no compatible
x86-to-ARM64 D2XX user-mode path.

The replacement `ftd2xx.dll` implements the subset of the D2XX API used by
NSP's Elite transport and maps it directly to the native Windows COM API:

```text
NSP (x86) -> compatibility DLL (x86) -> Win32 COM API
          -> Microsoft/FTDI ARM64 VCP driver -> Elite ECU
```


## Status

- Target: Windows 11 ARM64 under Parallels on Apple Silicon
- ECU used during development: Haltech Elite 2500, firmware 3.x
- USB device: FTDI FT232H, VID `0403`, PID `6014`
- Software used during development: Haltech NSP 1.48.4
- This project is unofficial and is not affiliated with Haltech or FTDI.

## Safety

### !! It is unclear if using this driver will void your warranty, caution is advised !!

ECU communication is safety-critical. Begin with connection and map-read tests.
Keep ECU power and USB stable during transfers. I do not recommend using this driver to do a firmware update. Keep as upported x64 system available.

## Requirements

- Windows 11 ARM64
- Haltech NSP installed in its default location
- ARM64 FTDI VCP driver exposing the Elite as a COM port
- Visual Studio 2022 Build Tools with the x86 C++ toolchain
- Administrator access for installation and FTDI latency configuration

## Build

Run from a normal Command Prompt or PowerShell:

```powershell
.\build.cmd
```

The resulting binary is `dist\ftd2xx.dll`. It is an x86 DLL because NSP is an
x86 process. The build links only against Windows system libraries.

## Install

Close NSP, then run PowerShell as Administrator:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1
```

The installer:

1. Backs up any existing application-local `ftd2xx.dll`.
2. Copies the compatibility DLL beside `NSP.exe`.
3. Sets the Elite FTDI VCP latency timer to 2 ms.
4. Sets the minimum read/write timeout floors to zero.

Restart Windows after installation so the FTDI driver reloads its parameters.

## Remove or roll back

Close NSP and run as Administrator:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
```

The script removes this DLL and restores the application-local backup, if one
was created.

## Implemented FTDI D2XX modules

- Device enumeration and identity
- Open by index or serial number
- Close, read and write
- Receive/transmit queue status
- Purge and reset
- Baud rate, framing and RTS/CTS flow control
- Read/write timeouts
- Latency and USB-buffer compatibility calls
- Driver/library version reporting

The shim selects the FTDI VCP entry from
`HKLM\HARDWARE\DEVICEMAP\SERIALCOMM`. Systems with multiple FTDI VCP devices
may require more specific device selection in a future release.

## Contributing

Hardware reports should include Windows architecture, NSP version, ECU model
and firmware, FTDI VID/PID, connection time, map-read time, and whether writes
were attempted. Do not attach proprietary firmware or map files to issues.

