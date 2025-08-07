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
    The graphical application requires a few Python libraries. Install them using the `requirements.txt` file.
    ```bash
    # Navigate to the GUI folder
    cd AniDownloaderGUI

    # Install the required packages
    pip install -r requirements.txt
    ```

## ⚙️ Configuration

Series configuration is handled through the `series_data.json` file. While you can edit it manually, **it is highly recommended to use the graphical interface ("Manage Series")** to avoid syntax errors.

On the first launch, the application will automatically create the necessary configuration files in `~/.config/AniDownloader/`.

![Series Management](media/series_management.gif)
*Adding, editing, and removing series is easy thanks to the integrated editor.*

The structure of a series object in the `series_data.json` file is as follows:

```json
{
    "name": "Full Series Name",
    "path": "/local/path/to/series/folder/1",
    "link_pattern": "https://server.com/anime/Series_Ep_{ep}_SUB_ITA.mp4",
    "continue": true,
    "passed_episodes": 12
}```

*   `name`: The name of the series to be displayed in the GUI.
*   `path`: The full local path to the folder where episodes will be saved.
*   `link_pattern`: The download link, where `{ep}` is the placeholder for the episode number.
*   `continue` (optional): Set to `true` if the series is a continuation of a previous season.
*   `passed_episodes` (optional): Required if `continue` is `true`. Indicates the total number of episodes from previous seasons.

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

### Automatic Mode (Systemd Service on Linux)

To run downloads automatically at regular intervals, you can use the included `systemd` service files.

1.  **Edit the paths:** Make sure the `WorkingDirectory` and `ExecStart` paths in the `systemd_services/AniDownloader.service` file match your setup.
2.  **Copy the service files:**
    ```bash
    sudo cp systemd_services/AniDownloader.service /etc/systemd/system/
    sudo cp systemd_services/AniDownloader.timer /etc/systemd/system/
    ```
3.  **Reload, enable, and start the timer:**
    ```bash
    sudo systemctl daemon-reload
    sudo systemctl enable --now AniDownloader.timer
    ```

The service will now run automatically every 15 minutes.

## 📂 Project Structure

```
.
├── AniDownloader.sh          # Main script for CLI execution
├── series_data_template.json # Template for series configuration
├── AniDownloaderGUI/           # Root folder for the GUI application
│   ├── main.py                 # GUI application entry point
│   ├── requirements.txt        # Python dependencies for the GUI
│   ├── assets/                 # Graphic assets (e.g., icons)
│   ├── config/                 # App configuration management (paths, etc.)
│   ├── core/                   # Core business logic (download, conversion)
│   ├── gui/                    # GUI components (windows, dialogs)
│   └── utils/                  # Utility functions (e.g., image loader)
└── systemd_services/           # Files for automation via systemd on Linux
    ├── AniDownloader.service
    └── AniDownloader.timer
```

## 💡 Future Developments

Here are some of the future directions for the project:

*   [ ] Integration of a desktop notification system for completed downloads.
*   [ ] Adding support for multiple download sources for the same series.
*   [ ] Creating a standalone installer package (e.g., using PyInstaller).
*   [ ] Improving network error handling with automatic retries.