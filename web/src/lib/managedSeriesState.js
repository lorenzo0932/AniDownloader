// Stato delle serie in gestione per la pagina Scopri (F3): un'unica fonte
// per badge "In gestione" e guard pre-POST. Confronta SOLO identity opache
// fornite dal server: nessun parser URL e nessun match per titolo nel browser.
// Modulo puro senza Svelte: testabile in isolamento, nessuna dipendenza.
export const STATO_GESTITE = {
  CARICAMENTO: 'loading',
  PRONTO: 'ready',
  ERRORE: 'error',
};

// Indice identity -> entry dalle entry di GET /api/series. Le entry senza
// identity (backend vecchio) vengono ignorate: mai dedurre equivalenze qui.
export function indiceDaEntrySerie(entries) {
  const indice = new Map();
  for (const e of entries || []) {
    const id = e && typeof e.identity === 'string' ? e.identity : '';
    if (id) indice.set(id, e);
  }
  return indice;
}

// Store minimale con stati espliciti. Aggiungi bloccato finché non è PRONTO:
// un errore di caricamento non è mai "nessuna serie gestita".
export function creaStatoGestite() {
  return {
    stato: STATO_GESTITE.CARICAMENTO,
    indice: new Map(),
    conflittoNote: new Set(),
    inSalvataggio: new Set(),
    errore: '',

    carica(entries) {
      this.indice = indiceDaEntrySerie(entries);
      this.stato = STATO_GESTITE.PRONTO;
      this.errore = '';
    },
    fallimento(messaggio) {
      this.stato = STATO_GESTITE.ERRORE;
      this.errore = messaggio || 'Elenco serie non disponibile.';
    },
    // true solo a PRONTO con identity nota: badge "In gestione".
    gestita(identity) {
      if (this.stato !== STATO_GESTITE.PRONTO || !identity) return false;
      return this.indice.has(identity) || this.conflittoNote.has(identity);
    },
    // Aggiunta consentita solo a PRONTO, non gestita, non già in salvataggio.
    aggiuntaConsentita(identity) {
      if (this.stato !== STATO_GESTITE.PRONTO || !identity) return false;
      return !this.gestita(identity) && !this.inSalvataggio.has(identity);
    },
    iniziaSalvataggio(identity) {
      this.inSalvataggio.add(identity);
    },
    fineSalvataggio(identity) {
      this.inSalvataggio.delete(identity);
    },
    // Successo: identity di RISPOSTA (il form può aver cambiato l'URL).
    aggiuntaRiuscita(identity) {
      if (identity) this.indice.set(identity, { identity });
      this.conflittoNote.delete(identity);
    },
    // 409 series_already_managed: memorizza il conflitto noto anche se il
    // refetch fallisce; nessun falso successo di aggiunta.
    conflitto(identity) {
      if (identity) this.conflittoNote.add(identity);
    },
  };
}
