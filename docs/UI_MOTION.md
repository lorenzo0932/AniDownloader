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
- Progressi: interpolare solo il valore della barra, senza rilanciare ingressi.
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
