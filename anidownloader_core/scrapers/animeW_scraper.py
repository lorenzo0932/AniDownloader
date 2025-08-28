import os
import re
import requests
from bs4 import BeautifulSoup
from urllib.parse import urljoin
from .base_scraper import BaseScraper

def get_next_episode_num(series_path):
    if not os.path.exists(series_path): os.makedirs(series_path)
    max_ep = 0
    for filename in os.listdir(series_path):
        if filename.endswith(('.mp4', '.mkv')):
            match = re.search(r'[._-]Ep[._-]?(\d+)', filename, re.IGNORECASE)
            if match: max_ep = max(max_ep, int(match.group(1)))
    return max_ep + 1

class animeWScraper(BaseScraper):
    """Scraper specializzato per il sito 'AnimeW'."""

    # I selettori sono ora hardcoded qui, specifici per questo scraper.
    EPISODE_LIST_SELECTOR = "div.server.active ul.episodes.active li.episode a"
    DOWNLOAD_LINK_SELECTOR = "#alternativeDownloadLink"

    def plan_series_task(self, series: dict) -> dict:
        name = series["name"]
        path = series["path"]
        series_page_url = series.get("series_page_url")

        task = { "series": series, "action": "skip", "reason": "Nessun nuovo episodio trovato." }

        try:
            
            headers = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36'}
            response_main = requests.get(series_page_url, timeout=15, headers=headers, verify=False)
            response_main.raise_for_status()
            soup_main = BeautifulSoup(response_main.text, 'lxml')
            
            episode_page_links = soup_main.select(self.EPISODE_LIST_SELECTOR)
            if not episode_page_links:
                task["reason"] = "Selettore lista episodi non ha trovato link."
                return task

            found_episodes = []
            for link in episode_page_links:
                ep_num_str = link.get('data-episode-num') or link.get_text(strip=True)
                match = re.search(r'(\d+)', ep_num_str)
                if match:
                    ep_num = int(match.group(1))
                    ep_page_url = urljoin(series_page_url, link.get('href'))
                    found_episodes.append({"number": ep_num, "page_url": ep_page_url})
            
            if not found_episodes:
                task["reason"] = "Impossibile estrarre i numeri degli episodi dai link."
                return task

            is_continuation = series.get("continue", False)
            passed_episodes = series.get("passed_episodes", 0)
            next_episode_on_disk = get_next_episode_num(path)
            
            found_episodes.sort(key=lambda x: x['number'])
            
            episode_to_process, final_ep_num = None, 0
            for ep in found_episodes:
                local_equivalent = ep['number'] + passed_episodes if is_continuation else ep['number']
                if local_equivalent >= next_episode_on_disk:
                    episode_to_process, final_ep_num = ep, local_equivalent
                    break
            
            if not episode_to_process:
                return task

            # FASE 2: Trova il link di download finale
            response_ep = requests.get(episode_to_process['page_url'], timeout=15, headers=headers, verify=False)
            response_ep.raise_for_status()
            soup_ep = BeautifulSoup(response_ep.text, 'lxml')
            
            final_link_element = soup_ep.select_one(self.DOWNLOAD_LINK_SELECTOR)
            if not final_link_element:
                for link in soup_ep.find_all('a', href=True):
                    if "download alternativo" in link.get_text(strip=True).lower():
                        final_link_element = link
                        break
            
            if final_link_element:
                task.update({
                    "action": "process",
                    "reason": f"Pronto per scaricare Ep. {final_ep_num}",
                    "download_url": urljoin(episode_to_process['page_url'], final_link_element.get('href')),
                    "final_ep_number": final_ep_num
                })
            else:
                task["reason"] = f"Trovato Ep. {final_ep_num}, ma non il link di download finale."

        except requests.RequestException as e: task["reason"] = f"Errore di rete: {e}"
        except Exception as e: task["reason"] = f"Errore imprevisto: {e}"
        
        return task