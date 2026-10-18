# Animazioni dell'interfaccia

Le animazioni devono accompagnare un'azione o rendere leggibile un cambio di
stato, non rallentare l'accesso ai contenuti. Nessuna libreria aggiuntiva.

## Fonte comune

`web/src/lib/motion.js` contiene i token e le primitive Svelte. `initMotion()`
genera le variabili CSS `--motion-*` prima del mount dell'app;
`web/src/lib/motion.css` contiene i keyframe e le microinterazioni condivise.
Non aggiungere durate numeriche o copie dei keyframe nei componenti.

| Token | Uso |
|---|---|
| `fast` | Feedback dei controlli (120ms) |
| `enter` | Comparsa di pagine, righe e contenuti (200ms) |
| `layout` | Relayout FLIP e apertura pannelli (260ms) |
| `exit` | Chiusura overlay/pannelli (150ms) |
| `stagger`, `staggerMax` | Sequenza delle liste, massimo 144ms di ritardo |

## Regole

- Pagine: `in:pageEnter|global`, per attivare l'ingresso anche dal router,
  senza animare l'uscita dell'intera schermata.
- Liste: `motion-rise` e ritardo limitato; mantenere chiavi stabili. Non
  rimontare le card al cambio densità o per aggiornamenti di progresso.
- Relayout: FLIP sul wrapper, separato dall'animazione della card. La griglia
  deve mantenere una larghezza vincolata allo spazio disponibile.
- Modali: `motion-fade` sull'overlay, `motion-panel` sul pannello; chiusura
  tramite `out:overlayExit` / `out:panelExit`. Nessun ridimensionamento
  decorativo all'arrivo dei dati asincroni.
- Lightbox: `motion-scale` e `out:lightboxExit`.
- Pulsanti: feedback cromatico e pressione lieve; niente pressione applicata
  a pulsanti che occupano card/righe/poster interi, né a controlli disabilitati.
  Su touch/pen `installTouchFeedback()` completa un impulso breve anche dopo
  il rilascio: luminosità per card/righe/poster, scala per gli altri pulsanti.
  Non dipende dall'hover ed è disabilitato con movimento ridotto.
- Azioni serie: blu per dettagli, verde per aggiunta, ambra per modifica,
  rosso per eliminazione. Token comuni in `web/src/lib/actions.css`; mantenere
  anche icone e label, mai affidare il significato al solo colore.
- Overlay delle card: hover reale o `:focus-visible`, non `:focus-within`.
  Restituire il focus dopo un modale non deve simulare un hover da mouse.
- Metadati asincroni: skeleton e contenuto condividono un riquadro stabile;
  `in:contentEnter` / `out:overlayExit` per la dissolvenza, senza reflow del modale.
- Dettaglio mobile: altezza vincolata a `100dvh`, catena flex con `min-height:0`,
  testo scorrevole e chiusura/azioni non comprimibili. Safe-area al posto di
  padding inferiori fissi. Verificare anche il resize con il dialogo già aperto.
- Progressi: interpolare solo il valore della barra, senza rilanciare ingressi.
- Home in elaborazione: pannelli Conversione/Download affiancati, una colonna
  fino a 768px; Scaricati in attesa sotto. Percentuali separate; nessuna barra
  nell'attesa. Completati, Saltati e Da elaborare chiusi di default; Errori aperti.
- Card episodio: `taskMotion.js` applica FLIP ai wrapper identificati da `key`,
  anche fra contenitori. Movimento solo con origine e destinazione interamente
  nel viewport; altrimenti dissolvenza locale. Nessun replay su snapshot/reload,
  nessuno scroll automatico per inseguire una card, nessuna durata fuori dai token.
- Cronologia Home: orari e soli passaggi significativi; percentuali nei pannelli,
  non nel log. Autoscroll solo finché l'utente resta in fondo alla cronologia.
- Evitare `transition: all`, ritardi proporzionali illimitati, rimbalzi e
  animazioni continue diverse dagli indicatori di caricamento.

## Accessibilità e verifica

`prefers-reduced-motion` disabilita le animazioni CSS e azzera la durata delle
primitive Svelte. La preferenza viene riletta quando parte una transizione.
Focus visibile, tastiera e azioni touch non dipendono dall'hover.

Verificare nel browser anche i frame intermedi: cambi rapidi di vista,
nessun overflow orizzontale, stessi nodi delle card, modali apribili/chiudibili
con Escape e nessuna sovrapposizione persistente durante la navigazione.
I test DOM non sostituiscono la verifica del layout nel browser.
