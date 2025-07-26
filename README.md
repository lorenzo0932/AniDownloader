# AniDownloader: Anime Download and Conversion System

This repository contains a Python script and its related configuration files and utilities to automate the download and conversion of anime episodes.

## 1. `downloadAnime.sh` (Main Script)

This is the main Python script that manages the entire process of downloading and converting anime episodes.

**Main features:**
- **Configuration loading**: Reads series data from the `series_data.json` file to know which anime to download and where to save them.
- **Next episode determination**: Identifies the last downloaded episode in a specific folder and calculates the number of the next episode to download.
- **Parallel download**: Uses `aria2c` to download episodes in parallel, optimizing download speed.
- **Series continuation management**: Supports series that span multiple seasons, renaming files based on the overall episode numbering.
- **Video Conversion**: Uses ffmpeg for H.265 video conversion and post-conversion integrity verification directly within the script.
- **Final report**: At the end of execution, it provides a detailed summary of downloaded episodes, conversion status, and time taken for each operation.

**Dependencies:**
- `python3`: Required for script execution.
- `aria2c`: Command-line tool for accelerated downloads.
- `ffmpeg`: Used for video file conversion and verification after conversion.

## 2. `series_data.json` (Configuration File)

This JSON file is essential for configuring the series that `downloadAnime.sh` should download. **Do not directly modify `series_data.json` unless you know what you are doing.**

To add new series or modify existing ones, refer to the `series_data_template.json` template.

### Series object structure:

Each object within the JSON array represents a series and must contain the following properties:

- **`name`**: The full name of the series.
- **`path`**: The full local path where the series episodes will be saved, followed by the season number (e.g., `/home/lorenzo/Video/Simulcast/SeriesName/1`).
- **`link_pattern`**: The download link pattern for the series. It is crucial to replace the episode number with `{ep}`. Example: `https://srv16-suisen.sweetpixel.org/DDL/ANIME/SentaiDaishikkaku2/SentaiDaishikkaku2_Ep_{ep}_SUB_ITA.mp4`.
- **`continue`**: (Optional) A boolean value (`true` or `false`). Set to `true` if the series is a continuation of a previous season and episode numbering should account for already passed episodes.
- **`passed_episodes`**: (Required if `continue` is `true`) An integer defining the total number of episodes from previous seasons already downloaded.

**Example structure:**

```json
[
    {
        "name": "Series Name",
        "path": "local_series_path/season_number",
        "link_pattern": "series_download_link",
        "continue": true,
        "passed_episodes": 12
    },
    {
        "name": "Series Name2",
        "path": "local_series_path/season_number",
        "link_pattern": "series_download_link"
    }
]
```

## 3. `series_data_template.json` (Configuration Template)

This file serves as a template for creating or modifying the `series_data.json` file.

**Instructions for use:**
1. Open `series_data_template.json`.
2. **Delete all comments** present in the file (lines starting with `//`).
3. Modify the content by adding information related to your series, following the "Series object structure" described above.
4. Save the final file with the name `series_data.json` (without `_template`) in the same directory.

## 4. `AniDownloader.desktop` (Desktop Launcher)

This file is a desktop application for Linux systems (compatible with desktop environments like GNOME, KDE, etc.) that provides a simple way to launch the `downloadAnime.sh` script with a double click.

**Details:**
- **`Name=AniDownloader`**: The displayed name of the application in the menu or on the desktop.
- **`Comment=Downloads all configured simulcast anime`**: A brief description of its function.
- **`Path=/home/lorenzo/.local/share/myScript/downloadAnime/`**: Specifies the working directory from which the `downloadAnime.sh` script will be executed.
- **`Exec=sh -c './downloadAnime.sh'`**: The actual command that is executed when the application is launched.
- **`Icon=/home/lorenzo/.local/share/myScript/downloadAnime/logo.png`**: The path to the icon displayed for the application.
- **`Terminal=true`**: Indicates that the application should be run within a terminal window, allowing the script's output to be viewed.

## 5. `AniDownloaderGUI/` (Graphical User Interface)

This directory contains a new Python-based Graphical User Interface (GUI) application built with PyQt6, offering a user-friendly way to manage and automate anime downloads.

**Main Features:**
- **Centralized Management**: Provides a visual interface to view the status of ongoing downloads and conversions.
- **Series Configuration**: Allows users to easily add, edit, and remove series entries, including their names, download paths, link patterns, and continuation settings, through dedicated management and editing dialogs.
- **Real-time Progress**: Displays real-time progress updates for downloads and conversions, along with overall system status.
- **Dependency Checks**: Automatically verifies the presence of necessary dependencies (`aria2c`, `ffmpeg`, `psutil`) and provides guidance if any are missing.
- **Customizable Paths**: Users can configure the `series_data.json` file path and the output directory for converted videos directly from the GUI.
- **Cross-platform**: Built with PyQt6, it is designed to run on various operating systems.
- **Desktop Integration**: Includes a `.desktop` file for easy launching from Linux desktop environments.

**Dependencies:**
- `PyQt6`: Python bindings for Qt, used for creating the graphical interface.
- `psutil`: A cross-platform library for system and process monitoring, used for managing download/conversion processes.
- `requests`: Used for making HTTP requests, primarily for checking the availability of download links.

**How to Run:**
To run the GUI, navigate to the `AniDownloaderGUI/` directory and execute `main.py`:
```bash
cd AniDownloaderGUI/
python3 main.py
```
Ensure all dependencies listed in `AniDownloaderGUI/requirements.txt` are installed (e.g., `pip install -r AniDownloaderGUI/requirements.txt`).


