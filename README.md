# AniDownloader

An advanced system to automate the download, conversion, and management of your favorite anime series.

<br/>

<div align="center">

![AniDownloader Workflow](media/main_workflow.gif)

*AniDownloader in action: from planning and parallel downloading to automatic conversion.*

</div>

<br/>

AniDownloader is a complete solution, featuring both an intuitive graphical user interface (GUI) and a command-line interface (CLI), designed to simplify and automate the entire process of downloading and archiving anime episodes. It intelligently manages new releases, converts files to optimize storage space, and integrates seamlessly into your system.

---

## ✨ Key Features

*   **Intuitive Graphical Interface**: Easily manage your series, monitor progress, and configure settings with a modern GUI built in PyQt6.
*   **High-Speed Parallel Downloads**: Leverages the power of `aria2c` to download multiple episodes simultaneously, maximizing your download speed.
*   **Automatic Video Conversion**: Automatically converts downloaded videos to the H.265 (HEVC) format for significant space savings while maintaining high visual quality.
*   **Advanced Series Management**: Built-in support for series that continue across multiple seasons, with automatic file renaming to maintain a consistent and continuous episode numbering.
*   **Real-Time Monitoring**: Keep track of the status of each download and conversion directly from the main table, with color-coded status updates.
*   **Dual Mode (GUI & CLI)**: Choose between using the user-friendly graphical interface or the `AniDownloader.sh` script for server environments or automation.
*   **Process Control**: Safely stop the entire download and conversion process at any time through a confirmation dialog.
*   **Automation with Systemd**: Includes ready-to-use service files (`.service`, `.timer`) to schedule automatic runs on Linux systems.
*   **Dependency Checks**: Automatically verifies the presence of required tools (`ffmpeg`, `aria2c`) on startup to ensure proper functionality.

## 🚀 Getting Started

Follow these steps to get started with AniDownloader.

### 📋 Prerequisites

Ensure you have the following software installed on your system:

1.  **Python 3.8+**: Required to run the application.
2.  **FFmpeg**: Essential for video conversion and verification.
3.  **Aria2c**: Required for accelerated and parallel downloads.

You can install **FFmpeg** and **Aria2c** using your system's package manager:

```bash
# On Debian/Ubuntu
sudo apt update && sudo apt install ffmpeg aria2

# On Fedora
sudo dnf install ffmpeg aria2

# On macOS (with Homebrew)
brew install ffmpeg aria2

# On Windows (with Winget)
winget install "FFmpeg (Essentials Build)"
winget install aria2
```

### 🛠️ Installation

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/your-username/AniDownloader.git
    cd AniDownloader
    ```

2.  **Install Python dependencies:**
    AniDownloader has two sets of Python dependencies: one for the GUI and one for the CLI.

    For the **GUI application**:
    ```bash
    # Install the required packages for GUI
    pip install -r AniDownloaderGUI/requirements.txt
    ```
    For the **CLI script**:
    ```bash
    # Install the required packages for CLI
    pip install -r anidownloader_utils/requirement_cli.txt
    ```

3.  **Make the CLI script executable:**
    ```bash
    # From the project's root folder
    chmod +x AniDownloader.sh
    ```

### 📦 Building from Source

You can create standalone executables for both the GUI and CLI applications using the provided build scripts, which leverage PyInstaller.

1.  **Install PyInstaller:**
    ```bash
    pip install pyinstaller
    ```

2.  **Run the build script:**
    *   To build the **GUI executable**:
        ```bash
        python3 anidownloader_utils/build_gui.py
        ```
    *   To build the **CLI executable**:
        ```bash
        python3 anidownloader_utils/build_cli.py
        ```

The final executables will be placed in the `dist/` folder in the project's root directory.

## ⚙️ Configuration

Series configuration is handled through the `series_data.json` file. Each series entry must include a `service` field, which specifies which scraper to use.

**It is highly recommended to use the graphical interface ("Manage Series")** to avoid syntax errors.

On the first launch, the application will automatically create the necessary configuration files in `~/.config/AniDownloader/`.

#### Example

```json
[
    {
        "name": "test",
        "path": "/home/lorenzo/Experiment/test/1",
        "series_page_url": "https://somesite.so/something",
        "service": "animeU_scraper",
        "continue": false,
        "passed_episodes": 0
    }
]
```

#### Example 2

```json
[
    {
        "name": "test",
        "path": "/home/lorenzo/Experiment/test/1",
        "series_page_url": "https://somesite.co/something",
        "service": "animeU_scraper",
        "continue": true,
        "passed_episodes": 12
    }
]
```

*   `service`: **(Required)** The identifier of the scraper to use (e.g., `"animeW_scraper"`, `"animeU_scraper"`).
*   `name`: The name of the series to be displayed.
*   `path`: The full local path where episodes will be saved.
*   `series_page_url`: The URL of the main series page.
*   `continue` (optional): Set to `true` if the series is a continuation of a previous season.
*   `passed_episodes` (optional): Required if `continue` is `true`.

## ▶️ Usage

AniDownloader can be run in three different modes.

### GUI Mode (Recommended)

The graphical interface is the easiest way to use the application.

```bash
# From the project's root folder
cd AniDownloaderGUI/
python3 main.py
```

For better integration on Linux desktops, you can use the `AniDownloaderGUI.desktop` file.

![Safe Stop Feature](media/stop_feature.gif)
*You can safely stop the process at any time.*

### CLI Mode (Command-Line)

For terminal use or for integration into custom scripts, you can run the `AniDownloader.sh` script directly.

```bash
# From the project's root folder
./AniDownloader.sh
```

The script will read the configuration, download, and convert the episodes, displaying the progress directly in the terminal.

### Automatic Mode (Systemd User Service on Linux)

To run downloads automatically at regular intervals without requiring root privileges, you can use the included `systemd` user service files.

1.  **Edit the paths:** Make sure the `WorkingDirectory` and `ExecStart` paths in the `systemd_services/AniDownloader.service` file match your setup.
2.  **Create the user service directory (if it doesn't exist):**
    ```bash
    mkdir -p ~/.config/systemd/user/
    ```
3.  **Copy the service files:**
    ```bash
    cp systemd_services/AniDownloader.service ~/.config/systemd/user/
    cp systemd_services/AniDownloader.timer ~/.config/systemd/user/
    ```
4.  **Reload, enable, and start the timer:**
    ```bash
    systemctl --user daemon-reload
    systemctl --user enable --now AniDownloader.timer
    ```
5.  **Enable lingering (optional, for running services after logout):**
    If you want the service to continue running after you log out, enable lingering:
    ```bash
    loginctl enable-linger $(whoami)
    ```

The service will now run automatically every 15 minutes for your user.

## 📂 Project Structure

```
.
├── AniDownloader.py          # Main script for CLI execution
├── AniDownloader.sh          # Shell wrapper for the CLI script
├── AniDownloader.desktop     # Desktop entry for Linux GUI launcher
├── logo.png                  # Application logo
├── README.md                 # This documentation file
├── anidownloader_config/       # Global configuration management
│   ├── app_config_manager.py # Manages application settings
│   └── defaults.py         # Default configuration values
├── anidownloader_core/         # Core business logic shared between GUI and CLI
│   ├── media_processor.py  # Processes media (conversion, verification)
│   ├── planning_service.py   # Plans download and conversion tasks
│   ├── series_repository.py # Manages series data (CRUD)
│   └── scrapers/             # Website scrapers
├── anidownloader_utils/        # Utility scripts, build tools, and CLI dependencies
│   ├── build_cli.py          # Build script for the CLI executable
│   ├── build_gui.py          # Build script for the GUI executable
│   ├── check_cli_deps.sh     # Script to check CLI dependencies
│   └── requirement_cli.txt   # Python dependencies for CLI
├── AniDownloaderGUI/           # Root folder for the GUI application
│   ├── main.py                 # GUI application entry point
│   ├── requirements.txt        # Python dependencies for the GUI
│   ├── assets/                 # Graphic assets (e.g., icons)
│   ├── core/                   # GUI-specific logic
│   │   └── download_worker.py  # Handles download/conversion tasks in a separate thread
│   ├── gui/                    # GUI components (windows, widgets)
│   └── utils/                  # Utility functions for GUI
├── media/                    # Contains GIFs and images for README
└── systemd_services/           # Files for automation via systemd on Linux
    ├── AniDownloader.service   # Systemd service unit file
    └── AniDownloader.timer     # Systemd timer unit file
```

## 💡 Future Developments

Here are some of the future directions for the project:

*   Integration of a desktop notification system for completed downloads.
*   Adding support for multiple download sources for the same series.
*   Improving network error handling with automatic retries.
*   Rework .desktops files
*   Implement an user friedly installation process
*   Need to update gifs (h265 check missing)
