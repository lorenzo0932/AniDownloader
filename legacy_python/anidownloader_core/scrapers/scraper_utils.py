import os
import re
class ScraperUtils:

    # Utility function to determine the next episode number to download
    def get_next_episode_num(series_path):
        if not os.path.exists(series_path): os.makedirs(series_path)
        max_ep = 0
        for filename in os.listdir(series_path):
            if filename.endswith(('.mp4', '.mkv')):
                match = re.search(r'[._-]Ep[._-]?(\d+)', filename, re.IGNORECASE)
                if match: max_ep = max(max_ep, int(match.group(1)))
        return max_ep + 1