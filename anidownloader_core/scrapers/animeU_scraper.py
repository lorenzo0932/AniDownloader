import os
import re
import time
from urllib.parse import urljoin
import traceback

from selenium import webdriver
from selenium.webdriver.chrome.service import Service
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC

from .base_scraper import BaseScraper
from bs4 import BeautifulSoup
from .scraper_utils import ScraperUtils

class animeUScraper(BaseScraper):
    EPISODE_LIST_SELECTOR = "div.episode-wrapper div.episode-item a"

    def _setup_driver(self):
        print("[DEBUG] Configurando il driver di Selenium...")
        chrome_options = Options()
        chrome_options.add_argument("--headless")
        chrome_options.add_argument("--no-sandbox")
        chrome_options.add_argument("--disable-dev-shm-usage")
        chrome_options.add_argument("--log-level=3")
        chrome_options.add_argument('--ignore-certificate-errors')
        chrome_options.add_argument('--autoplay-policy=no-user-gesture-required')
        chrome_options.add_experimental_option('excludeSwitches', ['enable-logging'])
        driver = webdriver.Chrome(options=chrome_options)
        print("[DEBUG] Driver di Selenium configurato.")
        return driver

    def plan_series_task(self, series: dict) -> dict:
        print(f"\n--- [DEBUG] Inizio pianificazione per: {series['name']} (AnimeU Scraper) ---")
        task = { "series": series, "action": "skip", "reason": "Nessun nuovo episodio trovato." }
        driver = None

        try:
            driver = self._setup_driver()
            
            series_page_url = series.get("series_page_url")
            print(f"[DEBUG] Navigazione alla pagina principale: {series_page_url}")
            driver.get(series_page_url)
            
            WebDriverWait(driver, 15).until(EC.presence_of_element_located((By.CSS_SELECTOR, self.EPISODE_LIST_SELECTOR)))
            episode_elements = driver.find_elements(By.CSS_SELECTOR, self.EPISODE_LIST_SELECTOR)
            
            found_episodes = []
            for element in episode_elements:
                match = re.search(r'(\d+)', element.text)
                if match: found_episodes.append({"number": int(match.group(1)), "element": element})

            if not found_episodes: return {**task, "reason": "Nessun episodio trovato."}
            
            is_continuation = series.get("continue", False); passed_episodes = series.get("passed_episodes", 0)
            next_episode_on_disk = ScraperUtils.get_next_episode_num(series.get("path"))
            found_episodes.sort(key=lambda x: x['number'])
            
            episode_to_process, final_ep_num = None, 0
            for ep in found_episodes:
                local_equivalent = ep['number'] + passed_episodes if is_continuation else ep['number']
                if local_equivalent >= next_episode_on_disk:
                    episode_to_process, final_ep_num = ep, local_equivalent
                    break
            
            if not episode_to_process: return task

            print(f"[DEBUG] Nuovo episodio trovato: N.{final_ep_num}. Tento di cliccare sul pulsante.")

            initial_iframe_src = driver.find_element(By.ID, "embed").get_attribute("src")
            
            # --- MODIFICA CHIAVE: Esegui il click tramite JavaScript ---
            element_to_click = episode_to_process["element"]
            driver.execute_script("arguments[0].click();", element_to_click)
            # --- FINE MODIFICA ---
            
            print("[DEBUG] Pulsante cliccato via JavaScript. In attesa che l'iframe del player si aggiorni...")

            WebDriverWait(driver, 15).until(
                lambda d: d.find_element(By.ID, "embed").get_attribute("src") != initial_iframe_src
            )
            print("[DEBUG] Iframe aggiornato con successo.")
            
            iframe_element = driver.find_element(By.ID, "embed")
            driver.switch_to.frame(iframe_element)
            
            time.sleep(3)

            print("[DEBUG] Tento di leggere la variabile 'window.downloadUrl'...")
            download_url = driver.execute_script("return window.downloadUrl;")
            print(f"[DEBUG] Valore estratto: {download_url}")

            if download_url and isinstance(download_url, str) and download_url.startswith('http'):
                print("[DEBUG] URL valido trovato! Preparazione del task.")
                task.update({
                    "action": "process", "reason": f"Pronto per scaricare Ep. {final_ep_num}",
                    "download_url": download_url, "final_ep_number": final_ep_num
                })
            else:
                task["reason"] = "La variabile 'window.downloadUrl' non è stata trovata o è invalida."

        except Exception as e:
            print(f"\n[DEBUG] ERRORE CRITICO DURANTE LO SCRAPING DI '{series['name']}':\n{traceback.format_exc()}")
            task["reason"] = f"Errore Selenium: {e}"

        finally:
            if driver:
                print("[DEBUG] Chiusura del driver.")
                driver.quit()
            print(f"--- [DEBUG] Fine pianificazione per: {series['name']}. Risultato: {task['reason']} ---\n")
        
        return task