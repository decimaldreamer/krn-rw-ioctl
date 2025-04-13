# Kernel Mode Driver - Memory Read/Write

A Windows kernel mode driver that provides memory read/write capabilities through IOCTL communication.

## Features

- Kernel mode memory operations
- Secure IOCTL communication
- Error handling and logging
- Pattern scanning support
- Batch memory operations
- Process memory access control

## Requirements

- Windows 10/11
- Visual Studio 2019 or later
- Windows Driver Kit (WDK)
- Windows SDK
- Test signing enabled

## Installation

1. Enable test signing:
```powershell
bcdedit /set testsigning on
```

2. Build the project:
```powershell
msbuild /p:Configuration=Release /p:Platform=x64
```

3. Install the driver:
```powershell
sc create kmdriver type= kernel start= demand binPath= C:\path\to\driver.sys
sc start kmdriver
```

## Security

- Driver must be signed
- Access control for process memory
- Safe error handling
- Protected memory operations

## License

This project is licensed under the MIT License. See the LICENSE file for details.
