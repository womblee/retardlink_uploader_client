# RetardLink - Client

<img src="https://nlog.us/ls/media/favicon.png" width="256" height="256" alt="Version 1.0">

- lightweight Windows URL shortener utility that lives in your system tray.
- Website: [https://nlog.us/ls/client.html](https://nlog.us/ls/client.html)
  
## Features

- **One-click URL shortening**: Press Ctrl+Alt+U to instantly shorten the URL in your clipboard
- **System tray integration**: Runs quietly in the background with minimal resource usage
- **Customizable ID length**: Choose between 4, 5, 6, 7, or 8 character IDs for your shortened URLs
- **Windows autostart**: Option to automatically start the application when Windows boots
- **Instant notifications**: Get notified when a URL is successfully shortened

## Usage

1. Copy any long URL to your clipboard
2. Press Ctrl+Alt+U
3. The shortened URL will automatically be copied to your clipboard
4. Paste your shortened URL wherever you need it

## System Requirements

- Windows 10 or later
- Internet connection for shortening URLs

## Building from Source

The application is built with C++ and requires:
- Visual Studio (2017 or later recommended)
- Windows SDK
- windows subsystem (project properties -> linker -> system -> subsystem)
- multi-byte encoding selected

To build:
1. Open the project in Visual Studio
2. Build for x64
3. Run the executable
