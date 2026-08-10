# Cardputer NetAudit

Firmware per **M5Stack Cardputer / Cardputer ADV** (ESP32-S3) per network
discovery & auditing (stile Fing) con UI cyberpunk/Matrix: digital rain,
testo verde fosforescente monospace su nero, accenti magenta/ciano.

> ⚠️ **Stato del progetto**: sviluppo incrementale in corso. Vedi
> [Roadmap](#roadmap--stato-attuale) per cosa è implementato oggi.

> ⚠️ **Uso legale**: questo firmware include (nelle fasi successive) un
> modulo di audit delle credenziali di default. Va usato **solo** su reti
> e dispositivi di tua proprietà o per cui hai autorizzazione esplicita
> alla verifica di sicurezza. Scansionare reti altrui senza permesso è
> illegale in quasi ogni giurisdizione.

## Hardware target

Cardputer ADV (Stamp-S3A, **ESP32-S3FN8: nessuna PSRAM, 8 MB flash**),
display IPS 1.14" 240x135, tastiera QWERTY 56 tasti, altoparlante +
codec ES8311, IR TX su G44, slot microSD, BMI270 6-axis. Nessun
LoRa/GPS integrato di serie (espandibili via Grove/EXT se necessario —
non previsti in questa firmware). Il codice è scritto per essere
compatibile anche con il Cardputer originale (stesso MCU/flash),
selezionabile con l'env `cardputer` in `platformio.ini`.

## Scelte tecniche

### PlatformIO + Arduino core, non ESP-IDF puro

L'ipotesi "ESP-IDF puro se serve più controllo su LWIP/raw socket" non
regge per questo caso d'uso specifico:

- **Display/tastiera/board-detect**: M5Unified + M5GFX + M5Cardputer
  sono librerie Arduino-first, senza equivalente ESP-IDF component
  mantenuto. Riscriverle da zero (driver ST7789, matrice tastiera,
  auto-detect hardware) sarebbe lavoro puro senza benefici.
- **"Raw socket" per un SYN scan vero**: non è realisticamente
  disponibile né su Arduino né su ESP-IDF senza patchare lo stack
  lwIP — lwIP su ESP32 non espone la costruzione di pacchetti TCP
  arbitrari a livello applicativo. Per questo il port scanner userà un
  **TCP connect-scan** (`connect()` con timeout breve), non un SYN
  scan: più lento ma realistico da implementare in modo affidabile e
  meno invasivo di un raw packet crafting a mano.
- **Lettura ARP cache**: usa `etharp_find_addr()` di lwIP, header
  pubblico (`lwip/etharp.h`) raggiungibile sia da Arduino-ESP32 sia da
  ESP-IDF, perché Arduino-ESP32 è comunque costruito sopra ESP-IDF/lwIP.
  Scegliere Arduino non toglie l'accesso a questo livello.
- In cambio, Arduino+PlatformIO dà accesso diretto a `WiFiClient`,
  `Preferences` (NVS), `LittleFS`, `ArduinoJson`: tutto codice che
  altrimenti andrebbe scritto a mano in ESP-IDF senza un guadagno reale
  per questo progetto.

Conclusione: **PlatformIO + framework Arduino**, board `m5stack-stamps3`
(non esiste ancora un board JSON dedicato "Cardputer ADV" in
platform-espressif32; Cardputer e Cardputer ADV condividono lo stesso
modulo Stamp-S3, quindi il file di board più vicino funziona a livello
di toolchain/flash — il resto lo fa l'auto-detect di M5Unified a
runtime). Se una futura versione di M5Unified non riconoscesse ancora
l'ID hardware della ADV, il fallback è forzare manualmente i pin via
`M5.begin()` con un `config_t` popolato a mano (vedi commento in
`src/main.cpp`).

### Struttura moduli (no monolite)

```
src/
  core/     tipi condivisi, config NVS, code/notifiche inter-task
  ui/       rendering, effetto Matrix rain, input tastiera, schermate
  net/      wifi/subnet
  scan/     ARP/ping sweep, OUI lookup, classificatore, port scan, banner grab, cred audit
  storage/  export LittleFS/SD, config persistente
data/       asset da flashare su LittleFS (DB OUI, dizionario credenziali default)
tools/      script host-side per generare gli asset in data/
test/native pure-logic unit test (girano su host gcc, non su ESP32)
```

### Architettura concorrente

Tre task FreeRTOS indipendenti, oltre al task Arduino `loop()` (lasciato
sostanzialmente a `vTaskDelay` — tutto il lavoro reale vive nei task
dedicati, così nessuno dei tre compete per CPU con gli altri):

| Task    | Core | Priorità | Ruolo |
|---------|------|----------|-------|
| `input` | 1    | 3 (alta) | poll tastiera ~50 Hz, traduce in `UiKeyEvent` astratti |
| `ui`    | 1    | 2        | render loop ~30 fps, anima il Matrix rain, disegna la schermata attiva |
| `scan`  | 0    | 1 (bassa)| ARP/ping/port-scan in background (aggiunto in fase 2) |

**Perché non passiamo `HostInfo` (con `String`/`std::vector` interni)
dentro una queue FreeRTOS**: `xQueueSend`/`xQueueReceive` copiano gli
elementi con un `memcpy` grezzo nello storage interno della coda. Se
l'elemento contenesse una `String` o uno `std::vector`, quel memcpy
duplicherebbe solo il puntatore al buffer heap: quando la variabile
locale del mittente esce di scope, il suo distruttore libera quella
memoria, e la copia nella coda resta con un puntatore penzolante — un
use-after-free reale, non ipotetico, tipico di FreeRTOS su ESP32.

La soluzione adottata (vedi `src/core/EventQueue.h`): la queue porta
solo **notifiche POD** (`ScanNotification`: tipo evento + indice host +
percentuale). I dati veri (`HostInfo`, che possiede memoria heap
legittimamente) vivono in una tabella protetta da mutex dentro
`ScanManager`; chi ha bisogno dei dati reali (il task `ui`) prende il
mutex per una copia breve e delimitata, poi lo rilascia subito.

### Memoria: niente PSRAM

L'ESP32-S3FN8 non ha PSRAM: ~512 KB SRAM totali, una parte già
riservata da stack WiFi/BT/ROM. Il canvas di compositing full-screen
(240×135×2 byte RGB565 ≈ 65 KB) è l'unico framebuffer che teniamo in
RAM, allocato esplicitamente in SRAM interna (`canvas.setPsram(false)`
prima di `createSprite()`, per rendere la scelta esplicita invece di
affidarsi al fallback automatico di LovyanGFX). L'effetto rain non
ridisegna mai l'intero schermo per animare: ogni tick tocca 2–3 celle
di glyph per colonna attiva (nuova testa, dissolvenza della vecchia
testa, cancellazione della coda), non l'intero viewport — vedi i
commenti in `src/ui/MatrixRain.cpp`.

Il partition table (`partitions.csv`) non riserva spazio per OTA
(nessun aggiornamento over-the-air è previsto: il workflow atteso è
reflash via USB-C), lasciando invece ~3.9 MB a LittleFS per il DB OUI,
il dizionario delle credenziali di default e l'export dei risultati.

### Fase 2: come funzionano discovery, ARP, OUI e hostname

- **Ping sweep = TCP connect-scan**, non ICMP: vedi il commento in
  `src/scan/PingSweep.cpp`. In breve, non esiste nell'ecosistema
  Arduino-ESP32 un'API ICMP abbastanza stabile tra versioni da volerci
  scommettere senza poterla compilare qui; un connect TCP breve su
  poche porte comuni (80/443/22/445) usa `WiFiClient`, un'API
  estremamente stabile, e come effetto collaterale fa comunque
  risolvere l'ARP dell'host — che è l'altra cosa di cui lo sweep ha
  bisogno.
- **Lettura MAC via ARP cache** (`src/scan/ArpResolver.cpp`): passa da
  `esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")` (chiave stabile con
  cui Arduino-ESP32 registra l'interfaccia STA in ESP-IDF) e poi
  `etharp_find_addr()` di lwIP. È il file più sensibile alla versione
  del framework in tutto il repo — isolato di proposito in un unico
  punto.
- **DB vendor OUI reale, non inventato**: `data/oui/oui.bin` è generato
  da `tools/gen_oui_db.py` a partire da `tools/oui.csv` (35.084 record),
  estratto con `tools/extract_ieee_oui.py` da uno snapshot reale del
  registro IEEE MA-L (bundlato nel pacchetto PyPI `netaddr`, usato come
  fonte perché `standards-oui.ieee.org` non era raggiungibile
  dall'ambiente di sviluppo sandboxato). È uno snapshot puntuale e può
  invecchiare: per aggiornarlo, `pip install --upgrade netaddr` poi
  rilancia i due script (vedi i commenti in testa a ciascuno). Il
  binario resta su LittleFS e viene interrogato con una binary search
  via `seek()` diretta sul file, mai caricato in RAM.
- **Hostname via NBNS, non mDNS** (`src/scan/HostnameResolver.cpp`):
  `ESPmDNS` è pensata per pubblicizzare il nome di *questo* dispositivo
  e fare browsing di servizi, non per interrogare l'IP di qualcun
  altro — farlo richiederebbe costruire/parsare pacchetti mDNS raw a
  mano, un pezzo di codice protocollare più grosso e rischioso di NBNS.
  NBNS (UDP/137, query "Node Status") copre comunque una parte
  concreta di una LAN casa/ufficio tipica (PC Windows, NAS/stampanti
  con Samba). I dispositivi che parlano solo mDNS (la maggior parte di
  telefoni, Mac, Chromecast) semplicemente non avranno un hostname in
  questa fase — è un limite noto e accettato, non un bug.
- **Range di scansione auto-rilevato**: `NETWORK SCAN` non chiede una
  subnet manuale — calcola network/prefix dal DHCP lease corrente
  (`WifiManager::networkAddress()`/`hostCount()`, con aritmetica IP
  fatta apposta senza usare `(uint32_t)IPAddress` direttamente, vedi il
  commento in `src/net/IpUtil.h` sul perché quel cast andrebbe
  byte-swappato rispetto a quanto ci si aspetta). Un editor manuale
  subnet/porte è rimandato alla Fase Settings futura — scelta di scope
  deliberata, non dimenticanza.
- **Credenziali WiFi**: non c'è ancora una UI di provisioning a
  tastiera; vanno in `include/secrets.h` (gitignored, copia
  `include/secrets.h.example`). Anche questo è un taglio di scope
  deliberato — vedi Roadmap.

### Fase 3: port scanner e banner grabbing

- **Un host alla volta, non l'intera subnet**: il port scan si avvia
  dalla schermata di dettaglio di un host (`Tab`), non dal menu
  principale — scansionare la porta 1-1024 su centinaia di host
  scoperti nella Fase 2 sarebbe proibitivo su un microcontrollore.
  `PortScanManager` è un singleton (una sola scansione porte alla
  volta) che riusa lo stesso pattern a pool di worker task di
  `ScanManager`, partizionando però le *porte* di un host invece degli
  *host* di una subnet.
- **Stesso problema "queue POD-only", stessa soluzione, tag in più**:
  `PortScanManager` posta sulla stessa coda condivisa di
  `ScanManager` (quella di `UiManager`). Per evitare che un discovery
  scan ancora in corso in background venga interpretato per errore
  come un risultato di port scan (o viceversa) dalla schermata in primo
  piano, `ScanNotification` porta ora anche un campo `source`
  (`Discovery`/`PortScan`) — vedi il commento in `src/core/EventQueue.h`.
- **Banner grabbing minimale, non un fingerprinting completo**:
  `BannerGrabber` riusa la stessa connessione TCP appena aperta dal
  port scan (nessun secondo connect). Per le porte HTTP-like manda un
  `HEAD / HTTP/1.0` e legge la prima riga; per FTP/SSH/Telnet/SMTP/
  POP3/IMAP semplicemente ascolta per una finestra breve (questi
  servizi tipicamente si presentano da soli alla connessione); per
  SMB (139/445) non tenta un vero handshake protocollare — la sola
  evidenza "porta aperta" è già quello che alza il rischio dell'host a
  `Warning` in `ScanManager::setHostPorts`.
- **Risk level**: l'apertura di porte legacy/di gestione note per
  essere spesso mal protette (FTP 21, Telnet 23, SMB 139/445, RDP 3389)
  alza il rischio dell'host a `Warning` (giallo). `Critical` (rosso) è
  riservato alla Fase 4, quando l'audit credenziali conferma un
  problema reale — mai una semplice porta aperta.

### Fase 4: audit credenziali di default

- **Dizionario fisso, non un brute-force**: `DefaultCredsDictionary`
  contiene 8 coppie utente/password ben note e generiche (non
  vendor-specific: niente wordlist enormi). `CredAuditManager` prova
  esattamente queste, in quest'ordine, e nient'altro — è la differenza
  tra "verifica di credenziali note" (quello che l'utente ha chiesto) e
  un brute-forcer generico (esplicitamente escluso).
- **Due protocolli, entrambi "chiari"**: HTTP Basic Auth (header
  `Authorization: Basic <base64(user:pass)>`, verificato con un test
  standalone contro vettori noti prima di essere usato — vedi git log)
  e login Telnet banner-based. Il riconoscimento del successo su
  Telnet è euristico (i prompt di login variano molto tra
  implementazioni) e **deliberatamente sbilanciato verso i falsi
  negativi**: un dispositivo vulnerabile non rilevato è un problema,
  ma un allarme falso su un dispositivo che in realtà va bene mina la
  fiducia in ogni altro risultato di questo strumento — vedi il
  commento in `CredAuditManager::tryTelnetLogin`.
- **Gate obbligatorio, non aggirabile con Enter**: `CredDisclaimerScreen`
  richiede di premere `Y` (non `Enter`, che l'utente preme di riflesso
  in ogni altra schermata) prima di abilitare il modulo per la sessione
  corrente. `AppConfig::credAuditEnabled` non viene mai persistito in
  NVS — ogni riavvio riparte da "non abilitato", anche se
  `credAuditAcknowledged` (visto almeno una volta) sì.
- **Per-host, da `HOST DETAIL` con `C`**: stessa logica di scope del
  port scanner — un audit generico su tutta la subnet non ha senso
  senza prima sapere quali porte sono aperte su ciascun host (il
  controllo richiede una porta HTTP o Telnet già scoperta da un port
  scan).
- **Risk = Critical**: è l'unico finding di tutta l'app abbastanza
  forte da giustificare il rosso — mai una singola porta aperta (quella
  resta `Warning`, Fase 3), solo credenziali di default confermate
  funzionanti.

## Compilare e flashare

```
pio run -e cardputer-adv            # build
pio run -e cardputer-adv -t upload  # flash via USB-C
pio device monitor                  # log seriale (115200 baud)
```

Usa l'env `cardputer` invece di `cardputer-adv` per il Cardputer
originale.

> **Nota sulla verifica in questo ambiente**: questa sessione di sviluppo
> gira in un sandbox la cui policy di rete blocca l'accesso al registry
> di PlatformIO (`api.registry.platformio.org`), quindi non è stato
> possibile scaricare il toolchain `espressif32` per eseguire una build
> reale né per flashare/testare su hardware. Il codice è stato scritto e
> rivisto manualmente con attenzione (bilanciamento parentesi, coerenza
> dei tipi, API M5Unified/M5GFX/M5Cardputer verificate contro gli esempi
> ufficiali), ma **va validato con `pio run` e su hardware reale prima di
> fidarsene**. Se trovi un errore di build, è quasi certamente un
> dettaglio di firma di funzione nelle librerie M5Stack che è cambiato
> tra versioni — il `README` e i commenti indicano dove guardare.

## Roadmap / stato attuale

- [x] **Fase 1 — Scaffold + UI skeleton**: struttura repo, boot screen
      con Matrix rain e boot log "in typing", menu principale navigabile
      da tastiera fisica (`;`/`.`/`,`/`/` come frecce, `Enter` conferma,
      `Del` torna indietro), schermate placeholder per i moduli futuri.
- [x] **Fase 2 — Network discovery**: subnet auto-rilevata dal DHCP
      lease, ping sweep (TCP connect-scan) + lettura ARP cache, lookup
      vendor OUI offline (DB reale IEEE, 35k record), risoluzione
      hostname via NBNS (non mDNS, vedi sopra), classificazione
      euristica del device, dashboard host list navigabile + schermata
      di dettaglio per host. Credenziali WiFi da `include/secrets.h`
      (niente UI di provisioning ancora); subnet/porte manuali rimandate
      alla Fase Settings.
- [x] **Fase 3 — Port scanner**: TCP connect-scan per singolo host
      (avviato da `HOST DETAIL` con `Tab`), banner grabbing di base
      (HTTP/FTP/SSH/Telnet/SMTP/POP3/IMAP; SMB solo come porta aperta),
      rate limiting condiviso con la Fase 2 (`maxConcurrentProbes`/
      `interProbeDelayMs`), risultati persistiti sull'host e riflessi
      nel risk level.
- [x] **Fase 4 — Credential audit**: dizionario fisso di 8 credenziali
      di default note (HTTP Basic Auth + Telnet banner-based), opt-in
      esplicito dietro disclaimer (tasto `Y`, non `Enter`), attivabile
      da `HOST DETAIL` con `C`, mai persistito tra riavvii, nessun
      brute-force generico.
- [ ] **Fase 5 — Storage/export**: risultati su LittleFS/SD in JSON/CSV.

## Test plan — Fase 1

Da verificare su hardware reale (non testabile in questo sandbox):

1. **Boot**: al power-on/flash, appare il log di boot "typing" riga per
   riga, poi il titolo `CARDPUTER` con sottotitolo, poi il prompt
   lampeggiante `[ PRESS ENTER ]`. Il rain di sfondo deve essere visibile
   e fluido (nessun tearing/flicker) durante tutta la sequenza.
2. **Navigazione menu**: da boot, `Enter` porta al menu principale con 4
   voci. `;`/`.` spostano la selezione su/giù con wraparound (dall'ultima
   torna alla prima e viceversa). `Enter` su una voce apre lo screen
   placeholder corrispondente; `Del` torna al menu.
3. **Reattività input**: la pressione dei tasti deve sembrare istantanea
   anche mentre il rain anima sullo sfondo — se non lo è, il task `ui` sta
   probabilmente bloccando il task `input` (controllare priorità/stack).
4. **Memoria**: via `pio device monitor` con `CORE_DEBUG_LEVEL=2`,
   verificare che non ci siano log di allocazione fallita
   (`UiManager: failed to allocate render canvas`) né crash/reboot per
   stack overflow nei primi minuti di uso normale del menu.
5. **Persistenza config**: spegnere/riaccendere non deve alterare il
   comportamento in questa fase (la config NVS esiste ma non ha ancora
   uno screen Settings che la modifichi).

## Test plan — Fase 2

Prerequisiti: `include/secrets.h` compilato con credenziali WiFi valide;
`pio run -t uploadfs` eseguito almeno una volta (altrimenti
`data/oui/oui.bin` non è su LittleFS e ogni lookup vendor fallisce in
silenzio, che è il comportamento atteso in quel caso — non un crash).

1. **Boot + LittleFS**: nel log seriale, verificare l'assenza di
   `OuiDatabase: could not open /oui.bin` e di `main: LittleFS mount
   failed`. Se compare il primo, mancava l'`uploadfs`.
2. **Connessione WiFi**: da `NETWORK SCAN`, deve apparire
   "connecting to wifi..." e poi, entro qualche secondo, la schermata
   con subnet/gateway rilevati. Se resta bloccato su "connecting", il
   problema più probabile è `include/secrets.h` mancante/errato — il
   `#warning` a compile-time lo segnala.
3. **Avvio scan**: `Enter` su "ENTER: start scan" deve far comparire
   la percentuale di avanzamento e host che appaiono progressivamente
   nella tabella (non tutti insieme alla fine) — è la prova che i
   worker task e le notifiche in coda funzionano, non solo il risultato
   finale.
4. **Correttezza discovery**: confrontare l'elenco IP trovati con
   quello di un tool noto sullo stesso segmento (es. `arp -a` da un PC
   sulla stessa rete, o l'app Fing). Router e almeno i dispositivi che
   rispondono su 80/443/22/445 dovrebbero comparire; è normale che
   dispositivi puramente mDNS-only compaiano come IP senza hostname.
5. **Classificazione**: il gateway deve sempre risultare `ROUTER`
   (classificazione via IP, non vendor). Aprire il dettaglio (`Enter`
   su una riga) di un paio di host noti e verificare vendor/hostname
   contro quanto sai davvero di quel dispositivo — la classificazione
   per parola chiave sul vendor è volutamente approssimativa, un
   risultato `UNKNOWN` non è un bug.
6. **Concorrenza**: mentre lo scan è in corso, l'input a tastiera deve
   restare fluido (menu/navigazione non deve bloccarsi) — se si blocca,
   il task `scan` sta probabilmente monopolizzando qualcosa che non
   dovrebbe (mutex tenuto troppo a lungo, `interProbeDelayMs` troppo
   basso).
7. **Rientro da dettaglio**: aprire il dettaglio di un host, tornare
   indietro (`Del`) mentre lo scan è ancora in corso, verificare che la
   lista host si aggiorni con eventuali nuovi host trovati nel
   frattempo (copre il path di `rebuildAliveList()` in `onEnter()`).

## Test plan — Fase 3

1. **Avvio da host detail**: su un host scoperto, `Tab` deve aprire
   `PORT SCAN <ip>`; `Enter` avvia la scansione del range configurato
   (default 1-1024) con percentuale e conteggio "open" che crescono
   progressivamente.
2. **Banner grabbing**: su un host con un webserver noto in ascolto,
   verificare che la porta 80/8080 mostri `service=http` e un banner
   (es. header `Server:` o la status line, tagliata a 20 caratteri
   nella tabella — il dettaglio completo non è ancora esposto in UI in
   questa fase). Su un servizio SSH noto, verificare che la 22 mostri
   `service=ssh` con banner tipo `SSH-2.0-...`.
3. **Nessuna interferenza tra scan**: avviare un `NETWORK SCAN` e,
   mentre è ancora in corso, entrare nel dettaglio di un host già
   trovato e avviare un port scan su di esso. Verificare che la lista
   host (se si torna a `NETWORK SCAN`) e i risultati porte non si
   corrompano a vicenda — è il caso che il campo `source` in
   `ScanNotification` esiste per prevenire.
4. **Persistenza + risk**: tornare su `HOST DETAIL` dopo un port scan
   completato: la riga `PORTS:` deve riportare il conteggio corretto
   senza dover rifare la scansione, e se una porta rischiosa
   (21/23/139/445/3389) è risultata aperta il `RISK:` deve essere
   passato a `warning` (giallo).
5. **Rescan**: da `PORT SCAN` a scansione conclusa, `Enter` deve
   rilanciarla da capo (utile se il target ha aperto/chiuso servizi nel
   frattempo).

## Test plan — Fase 4

**Da fare solo su dispositivi/reti di cui hai autorizzazione esplicita.**

1. **Gate del disclaimer**: al primo `C` su un host, deve comparire
   `AUTHORIZATION REQUIRED` con il testo del disclaimer. `Enter` non
   deve fare nulla; solo `Y`/`y` deve procedere. `Del` deve annullare
   e tornare a `HOST DETAIL` senza abilitare nulla.
2. **Persistenza della sessione**: dopo aver accettato una volta,
   aprire l'audit su un *altro* host con `C` non deve rimostrare il
   disclaimer (stesso boot). Riavviare il dispositivo e ripetere: il
   disclaimer deve ricomparire (conferma che `credAuditEnabled` non è
   persistito).
3. **Nessuna porta scansionata**: su un host senza port scan pregresso,
   l'audit deve concludersi con "no checkable service" senza tentare
   connessioni — verificare che non compaiano tentativi di login nei
   log del dispositivo target (se disponibili).
4. **Vero positivo controllato**: allestire un servizio HTTP con Basic
   Auth su credenziali presenti nel dizionario (es. `admin`/`admin`) su
   un dispositivo di test di tua proprietà, fare un port scan e poi
   l'audit: deve risultare `VULNERABLE`, `RISK:` deve passare a
   `critical` (rosso) sia in `HOST DETAIL` sia nella tabella di
   `NETWORK SCAN`.
5. **Vero negativo**: stesso servizio ma con credenziali diverse da
   quelle del dizionario: l'audit deve concludersi "clean", senza falsi
   allarmi.
6. **Rate limiting**: durante l'audit, il dispositivo target non deve
   ricevere più di un tentativo alla volta per servizio (nessuna
   parallelizzazione qui, a differenza di Fase 2/3) — verificabile
   osservando i log del target se disponibili.
