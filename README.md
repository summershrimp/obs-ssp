# OBS SSP Plugin

This plugin adds support for SSP (Simple Stream Protocol) cameras in OBS Studio.

## Features

- Adds SSP camera sources to OBS
- Provides a toolbar for quick access to SSP camera web interfaces
- Automatic camera discovery via mDNS
- Low-latency video streaming from SSP cameras

## Installation

### Using the Installer (Recommended)

1. Download the installer for your platform from the [Releases](https://github.com/your-username/obs-ssp/releases) page
2. Run the installer and follow the on-screen instructions
3. Launch OBS Studio

### Manual Installation

#### Windows
1. Extract the ZIP file to your OBS Studio installation directory
2. The plugin files should be in the correct locations:
   - Plugin DLL: `obs-plugins/64bit/obs-ssp.dll`
   - Dependencies: `obs-plugins/64bit/*.dll`
   - Data files (if any): `data/obs-plugins/obs-ssp/*`

#### macOS
1. Extract the package
2. Copy the `obs-ssp.plugin` directory to `~/Library/Application Support/obs-studio/plugins/`

#### Linux
1. Install using your package manager (DEB/RPM) or extract the TGZ archive
2. For manual installation, copy the files to:
   - Plugin: `/usr/lib/obs-plugins/obs-ssp.so`
   - Data: `/usr/share/obs/obs-plugins/obs-ssp/*`

## Usage

1. Launch OBS Studio
2. Add a new source by clicking the + button in the Sources panel
3. Select "SSP Source" from the list
4. Configure the source settings:
   - Enter IP address manually or select from discovered devices
   - Adjust video format and other settings as needed
5. Click OK to add the source

## Toolbar

The plugin adds a toolbar to OBS that lets you quickly access the web interface of any connected SSP camera:

1. The toolbar appears automatically when SSP sources are added
2. Click the camera name in the toolbar to open its web interface
3. Close the web interface by clicking the X in the dock window

## License

This project is licensed under the GPL v2 License - see the LICENSE file for details.

## Acknowledgments

- OBS Studio team for their excellent software
- IMVT for their SSP protocol specification
