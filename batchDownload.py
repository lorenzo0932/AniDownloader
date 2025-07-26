
import os
import subprocess
import argparse
import re
import threading
import queue
import time
import shutil


def get_filename_from_url(url):
    """Extracts the filename from a URL.

    Args:
        url: The URL to extract the filename from.

    Returns:
        The filename, or None if it cannot be extracted.
    """
    try:
        # Extract the filename from the URL using regex
        filename = re.search(r"\/([^\/+\.[a-zA-Z0-9]+)$", url)
        if filename:
            return filename.group(1)
        else:
            # Try extracting the filename from the URL path
            return os.path.basename(url)
    except:
        return None


def download_file(link, temp_download_path, final_download_path, episode_str):
    """Downloads a single file using aria2c to a temporary directory and then moves it to the final destination."""
    filename = get_filename_from_url(link)

    if not filename:
        filename = f"episode_{episode_str}.mp4"  # Default filename if extraction fails
        print(f"Warning: Could not extract filename from {link}. Using default filename: {filename}")

    temp_output_file = os.path.join(temp_download_path, filename)
    final_output_file = os.path.join(final_download_path, filename)

    command = [
        "aria2c",
        "-d", temp_download_path,
        "-x", "16",
        "-s", "16",
        "-o", filename,
        link,
    ]

    try:
        subprocess.run(command, check=True)
        print(f"Downloaded: {link} to {temp_output_file}")

        # Move the file to the final destination
        shutil.move(temp_output_file, final_output_file)
        print(f"Moved: {temp_output_file} to {final_output_file}")

    except subprocess.CalledProcessError as e:
        print(f"Error downloading {link}: {e}")
    except Exception as e:
        print(f"Error moving file: {e}")


def worker(queue, temp_download_path, final_download_path):
    """Worker thread to download files from the queue."""
    while True:
        item = queue.get()
        if item is None:
            break
        link, episode_str = item
        download_file(link, temp_download_path, final_download_path, episode_str)
        queue.task_done()


def download_with_aria2c(link_template, start_episode, end_episode, final_download_path, max_concurrent_downloads=12):
    """Downloads a series of files using aria2c with a maximum number of concurrent downloads."""

    start_time = time.time()

    temp_download_path = "/tmp"  # Use /tmp as the temporary download directory
    os.makedirs(temp_download_path, exist_ok=True)  # Ensure /tmp exists

    episode_queue = queue.Queue()

    # Populate the queue with download tasks
    for episode in range(start_episode, end_episode + 1):
        episode_str = str(episode).zfill(2)  # Pad with leading zeros if needed
        final_link = link_template.replace("*", episode_str)
        episode_queue.put((final_link, episode_str))

    # Create worker threads
    threads = []
    for _ in range(max_concurrent_downloads):
        t = threading.Thread(target=worker, args=(episode_queue, temp_download_path, final_download_path))
        threads.append(t)
        t.start()

    # Block until all tasks are done
    episode_queue.join()

    # Stop workers
    for _ in range(max_concurrent_downloads):
        episode_queue.put(None)
    for t in threads:
        t.join()

    end_time = time.time()
    total_time = end_time - start_time

    print(f"Total download time: {total_time:.2f} seconds")


def main():
    parser = argparse.ArgumentParser(description="Download files in batch using aria2c.")
    parser.add_argument("--link", help="Link template with '*' as episode placeholder.")
    parser.add_argument("--max-concurrent", type=int, default=12, help="Maximum concurrent downloads (default: 12).")
    parser.add_argument("--path", help="Destination download path. If specified, the script will not ask for it.")
    args = parser.parse_args()

    if args.link:
        link_template = args.link
    else:
        link_template = input("Enter the link template (e.g., link_ep_*_resto_link): ")

    try:
        start_episode = int(input("Enter the starting episode number: "))
        end_episode = int(input("Enter the ending episode number: "))
    except ValueError:
        print("Invalid episode number. Please enter an integer.")
        return

    if args.path:
        download_path = args.path
    else:
        default_download_path = os.path.join(os.path.expanduser("~"), "Video")
        download_path = input(f"Enter the download path (default: {default_download_path}): ")
        if not download_path:
            download_path = default_download_path

    if not os.path.exists(download_path):
        try:
            os.makedirs(download_path)
            print(f"Created directory: {download_path}")
        except OSError as e:
            print(f"Error creating directory {download_path}: {e}")
            return

    download_with_aria2c(link_template, start_episode, end_episode, download_path, args.max_concurrent)


if __name__ == "__main__":
    main()
