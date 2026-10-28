import { describe, it, expect } from 'vitest';
import { STATO_GESTITE, indiceDaEntrySerie, creaStatoGestite } from './managedSeriesState.js';

describe('managedSeriesState', () => {
  it('indice: solo identity non vuote, confronto esatto', () => {
    const indice = indiceDaEntrySerie([
      { identity: 'animeW_scraper:/play/a.1' },
      { identity: '' },
      { name: 'senza identity' },
      null,
    ]);
    expect(indice.size).toBe(1);
    expect(indice.has('animeW_scraper:/play/a.1')).toBe(true);
  });

  it('stato iniziale: niente badge e niente aggiunte (non "vuoto")', () => {
    const s = creaStatoGestite();
    expect(s.stato).toBe(STATO_GESTITE.CARICAMENTO);
    expect(s.gestita('animeW_scraper:/play/a.1')).toBe(false);
    expect(s.aggiuntaConsentita('animeW_scraper:/play/a.1')).toBe(false);
  });

  it('errore caricamento: tutto bloccato, niente falso "non gestita"', () => {
    const s = creaStatoGestite();
    s.fallimento('rete giù');
    expect(s.stato).toBe(STATO_GESTITE.ERRORE);
    expect(s.gestita('x')).toBe(false);
    expect(s.aggiuntaConsentita('x')).toBe(false);
  });

  it('pronto: badge su match esatto, mai su titolo o case diversa', () => {
    const s = creaStatoGestite();
    s.carica([{ identity: 'animeW_scraper:/play/Ghost.0H0eO' }]);
    expect(s.gestita('animeW_scraper:/play/Ghost.0H0eO')).toBe(true);
    expect(s.gestita('animeW_scraper:/play/ghost.0H0eO')).toBe(false);
    expect(s.gestita('Ghost')).toBe(false);
    expect(s.gestita('')).toBe(false);
    expect(s.aggiuntaConsentita('animeW_scraper:/play/Ghost.0H0eO')).toBe(false);
    expect(s.aggiuntaConsentita('animeW_scraper:/play/Altra.9')).toBe(true);
  });

  it('salvataggio: doppia aggiunta bloccata, poi riuscita con identity risposta', () => {
    const s = creaStatoGestite();
    s.carica([]);
    const id = 'animeW_scraper:/play/Nuova.1';
    expect(s.aggiuntaConsentita(id)).toBe(true);
    s.iniziaSalvataggio(id);
    expect(s.aggiuntaConsentita(id)).toBe(false);
    s.fineSalvataggio(id);
    s.aggiuntaRiuscita('animeW_scraper:/play/NuovaUrl.2');
    expect(s.gestita('animeW_scraper:/play/NuovaUrl.2')).toBe(true);
  });

  it('conflitto 409: badge noto anche senza refetch', () => {
    const s = creaStatoGestite();
    s.carica([]);
    s.conflitto('animeW_scraper:/play/Altra.3');
    expect(s.gestita('animeW_scraper:/play/Altra.3')).toBe(true);
    expect(s.aggiuntaConsentita('animeW_scraper:/play/Altra.3')).toBe(false);
  });
});
