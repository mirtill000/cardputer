# Cardputer NetAudit

Firmware per **M5Stack Cardputer / Cardputer ADV** (ESP32-S3) per network
discovery & auditing (stile Fing) con UI cyberpunk/Matrix: testo verde
fosforescente monospace su nero, accenti magenta/ciano. L'effetto
"digital rain" di sfondo è stato rimosso per ora — vedi
["UI: digital rain rimosso"](#ui-digital-rain-rimosso-per-ora).

> ⚠️ **Stato del progetto**: sviluppo incrementale in corso. Vedi
> [Roadmap](#roadmap--stato-attuale) per cosa è implementato oggi.

> ⚠️ **Uso legale**: questo firmware include un modulo di **attacco a
> credenziali** (brute-force con wordlist su HTTP/Telnet/FTP, non più
> solo una verifica di default noti — vedi Fase 4), dietro un gate
> opt-in con disclaimer esplicito. Va usato **solo** su reti e
> dispositivi di tua proprietà o per cui hai autorizzazione esplicita e
> documentabile alla verifica di sicurezza. Attaccare dispositivi senza
> permesso è illegale in quasi ogni giurisdizione. Il resto del
> firmware (discovery, port scan) resta un tool di audit passivo/attivo
> ma non distruttivo.

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
  ui/       rendering, input tastiera, schermate
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
| `ui`    | 1    | 2        | render loop ~30 fps, disegna la schermata attiva |
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
affidarsi al fallback automatico di LovyanGFX).

Il partition table (`partitions.csv`) non riserva spazio per OTA
(nessun aggiornamento over-the-air è previsto: il workflow atteso è
reflash via USB-C), lasciando invece ~3.9 MB a LittleFS per il DB OUI,
il dizionario delle credenziali di default e l'export dei risultati.

### UI: digital rain rimosso (per ora)

Le prime versioni di questo firmware includevano un effetto "digital
rain" di sfondo (colonne di caratteri che scorrono, stile Matrix) su
boot screen e menu principale, con ridisegno incrementale per non
pesare sulla RAM/SPI (vedi la cronologia git, `src/ui/MatrixRain.*`,
per l'implementazione se serve reintrodurlo). È stato rimosso su
richiesta esplicita per tenere la UI più semplice mentre si stabilizza
il resto del firmware — non è mai stato verificato su hardware reale
prima di essere tolto, quindi non è chiaro se fosse correlato ai
problemi di avvio riscontrati; toglierlo comunque riduce la superficie
di codice non ancora testata su hardware. Le schermate ora hanno
semplicemente sfondo nero pieno. Reintrodurlo (o qualunque altro
effetto di sfondo) è possibile in futuro senza toccare l'architettura:
`Screen::draw()` riceve comunque l'intero `M5Canvas` e può disegnarci
sopra quello che vuole prima del proprio contenuto.

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
- **Credenziali WiFi**: mai hardcoded. Scansione reti + selezione +
  password da tastiera fisica, con persistenza in NVS — vedi
  "WIFI SETUP" più sotto.

### WIFI SETUP: provisioning da tastiera, nessuna credenziale hardcoded

- **Nessun `secrets.h`**: le versioni precedenti di questo firmware
  leggevano SSID/password da un header C++ compilato nel binario. Ora
  `WifiSetupScreen` fa scan (`WiFi.scanNetworks(async=true)`, non
  bloccante — il task `ui` continua a disegnare/rispondere alla
  tastiera mentre lo scan gira), l'utente seleziona una rete dalla
  lista (deduplicata per SSID, ordinata per potenza del segnale) e
  digita la password sulla tastiera fisica. Alla prima connessione
  riuscita, `WifiManager::saveCredentials()` scrive SSID+password in
  NVS (`Preferences`, namespace `wifi`) — **solo dopo** che la
  connessione ha funzionato, mai prima, così una password sbagliata
  digitata per errore non finisce mai salvata. Ad ogni boot,
  `WifiManager::autoConnect()` (chiamato una volta in `main.cpp`)
  riprova con le credenziali salvate, se presenti.
- **NVS non è cifrata di default**: stesso modello di fiducia di tutto
  ciò che questa firmware scrive in flash — chi ha accesso fisico al
  dispositivo ha accesso a quello che contiene. Non diverso, in
  pratica, da un client WiFi salvato su qualsiasi laptop/telefono.
- **Conflitto risolto tra navigazione e digitazione testo**: i tasti
  `;`/`.`/`,`/`/` fanno normalmente da frecce direzionali (vedi Fase 1),
  ma sono anche caratteri comuni in password reali. `InputManager`
  espone `setTextEntryMode(bool)`: quando un campo di testo è attivo
  (l'inserimento password), quei quattro tasti smettono di essere
  rimappati e arrivano come caratteri letterali. `UiManager` lo
  reimposta forzatamente a `false` ad ogni cambio di schermata (push/
  pop/replace), così uno screen che dimentica di ripulirlo all'uscita
  non può bloccare la navigazione a frecce ovunque nel resto dell'app —
  vedi i commenti in `src/ui/InputManager.h`.
- **Accessibile da ovunque**: il tasto `W` apre `WIFI SETUP` sia dal
  menu principale sia da dentro `NETWORK SCAN` (utile se ti accorgi di
  non essere sulla rete giusta mentre stai già scansionando).
- **`F` per dimenticare la rete salvata**: dalla schermata iniziale di
  `WIFI SETUP`, se una rete è salvata. Cancella SSID/password da NVS e
  disconnette (`WiFi.disconnect(false, true)`, la seconda `true`
  cancella anche la config AP cache a livello di driver, non solo la
  nostra copia in `Preferences`).

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

### Fase 4: audit credenziali → vero brute-force con wordlist

> ⚠️ Questo modulo è cambiato natura durante lo sviluppo: nella sua
> prima versione era "solo" una verifica di 8 credenziali di default
> note, esplicitamente non un brute-force. **Ora, su richiesta esplicita
> dell'utente, è un vero strumento di attacco a credenziali** con
> wordlist personalizzabili. Resta dietro lo stesso gate opt-in/
> disclaimer, rafforzato di conseguenza (vedi sotto).

- **Dizionario rapido + wordlist completa**: `CredAuditManager` prova
  prima le 8 coppie di `DefaultCredsDictionary` (hit rapidi sui casi
  più comuni), poi — se non trova nulla — l'intero prodotto cartesiano
  di `data/creds/users.txt` × `data/creds/passwords.txt` (caricati da
  LittleFS via `WordlistLoader`, plain text, una voce per riga, righe
  `#...` ignorate). Nessuna UI di upload file su dispositivo (niente
  SD cablata, niente web server): per personalizzare le liste si edita
  il file nel repo e si rilancia `pio run -t uploadfs`, stesso workflow
  già usato per il DB OUI.
- **Tre protocolli**: HTTP Basic Auth (header `Authorization: Basic
  <base64(user:pass)>`, encoder verificato con un test standalone
  contro vettori noti prima di essere usato — vedi git log), FTP
  (`USER`/`PASS`, controllo sui codici di risposta RFC 959 — `230` =
  login riuscito, `331` = utente ok serve password — protocollo
  testuale con codici numerici ben definiti, quindi più affidabile
  della euristica Telnet) e login Telnet banner-based, la cui
  rilevazione del successo resta euristica (i prompt variano tra
  implementazioni) e **deliberatamente sbilanciata verso i falsi
  negativi**: un dispositivo vulnerabile non rilevato è un problema, ma
  un allarme falso su un dispositivo che in realtà va bene mina la
  fiducia in ogni altro risultato di questo strumento — vedi il
  commento in `CredAuditManager::tryTelnetLogin`. **SSH non è
  supportato**: implementare un client SSH (handshake, key exchange,
  cifratura) da zero e senza poterlo testare su hardware reale non era
  un rischio accettabile — vedi "Cosa non è stato implementato" sotto.
- **Un tentativo alla volta, mai in parallelo**: a differenza di
  discovery/port scan (worker pool), qui la concorrenza è
  deliberatamente assente — un vero attacco a un login non va
  parallelizzato, non è più veloce (il rate limit si applica per
  tentativo comunque) ed è più facile da notare/bloccare per il target.
  Rate limiting condiviso con le altre fasi (`interProbeDelayMs`).
- **Log dei tentativi live**: ogni tentativo posta una notifica
  (`user:pass OK/FAIL`, troncata per stare nel campo fisso a 40 byte di
  `ScanNotification`) che `CredAuditScreen` mostra in una finestra
  scorrevole stile terminale, insieme ai contatori tentativi/successi.
- **Gate rafforzato, non aggirabile con Enter**: `CredDisclaimerScreen`
  richiede di premere `Y` (non `Enter`, che l'utente preme di riflesso
  in ogni altra schermata) prima di abilitare il modulo per la sessione
  corrente. Il testo ora dichiara esplicitamente "this IS a real attack
  tool now, not just a defaults check". `AppConfig::credAuditEnabled`
  non viene mai persistito in NVS — ogni riavvio riparte da "non
  abilitato", anche se `credAuditAcknowledged` (visto almeno una volta)
  sì.
- **Per-host, da `HOST DETAIL` con `C`**: stessa logica di scope del
  port scanner — un attacco generico su tutta la subnet non ha senso
  senza prima sapere quali porte sono aperte su ciascun host (serve una
  porta HTTP/Telnet/FTP già scoperta da un port scan).
- **Risk = Critical**: è l'unico finding di tutta l'app abbastanza
  forte da giustificare il rosso — mai una singola porta aperta (quella
  resta `Warning`, Fase 3), solo credenziali confermate funzionanti.

### Fase 5: export risultati

- **JSON/CSV scritti in streaming, non in RAM**: `ResultStore` scrive
  riga per riga/campo per campo direttamente sul file, senza costruire
  prima un documento in memoria — con centinaia di host possibili e
  zero PSRAM, bufferizzare l'intero export in RAM prima di scriverlo
  non è un rischio che valga la pena correre per quella che è
  fondamentalmente una serializzazione lineare.
- **Escaping vero, non cosmetico**: sia il JSON (`\"`, `\\`, caratteri
  di controllo come `\u00XX`) sia il CSV (RFC 4180: virgolette
  raddoppiate, campo tra virgolette se contiene virgola/virgolette/
  a-capo) gestiscono correttamente stringhe che arrivano dalla rete
  (hostname NBNS, vendor OUI, banner) e che quindi non sono sotto il
  nostro controllo — senza escaping corretto, un hostname o banner con
  un carattere `"` al suo interno romperebbe la struttura del file
  esportato.
- **Solo LittleFS di default, SD volutamente non cablata**: `ResultStore`
  accetta un `fs::FS&` generico (funziona sia con `LittleFS` sia con
  `SD`), ma questa firmware non chiama `SD.begin(pin)` con un pin di
  default — non avendo la scheda fisica in mano, non è stato possibile
  verificare quale GPIO sia collegato al chip-select dello slot
  microSD del Cardputer ADV, e sbagliarlo avrebbe potuto significare un
  fallimento silenzioso o peggio. Se conosci il pin CS corretto (dallo
  schematico ufficiale M5Stack), `ResultStore::exportJson(SD, path)`
  funziona identico a LittleFS una volta fatto `SD.begin(pin)` — è
  scritto per questo, semplicemente non abbiamo cablato un default.
- **Attivazione**: da `NETWORK SCAN`, tasto `E` (funziona anche mentre
  uno scan è in corso — esporta lo stato attuale, non serve aspettare
  il completamento). Scrive `/export.json` e `/export.csv` sulla radice
  di LittleFS.

### Fase 6: restyle UI verso il mockup "NETRUNNER"

Su richiesta dell'utente, che ha condiviso un mockup grafico in stile
dashboard desktop (sidebar a icone, grafici a ciambella, radar
animato). Prima di implementare, due chiarimenti necessari (vedi la
conversazione): il mockup era pensato per lo schermo fisico 240×135 —
non una dashboard web — e l'utente ha confermato esplicitamente di
volere anche le funzionalità di attacco mostrate nel mockup
(brute-force con wordlist, non solo verifica credenziali note).

**Cosa è stato adattato:**

- **Header condiviso con status bar**: `chrome::drawHeader()`
  sostituisce l'header disegnato a mano in ogni singola schermata (~10
  file), aggiungendo un indicatore WiFi + batteria (`M5.Power.
  getBatteryLevel()`) sulla stessa riga del titolo — niente riga
  verticale in più su uno schermo già stretto.
- **Palette spostata verso ciano/magenta** come accento dominante
  (bordi, header, selezioni), mantenendo il verde per gli stati "ok" —
  la semantica rischio (verde/ambra/rosso) già costruita non è stata
  toccata, solo l'uso cromatico per elementi non legati al rischio.
- **SETTINGS reale**: prima un placeholder, ora un editor funzionante
  (timeout, concorrenza, ritardo tra probe, range porte, auto-export)
  che scrive `AppConfig` in RAM ad ogni modifica e lo persiste in NVS
  una sola volta all'uscita (`</>` per regolare, `ENTER`/`DEL` per
  salvare e uscire) — chiudendo un gap lasciato aperto fin dalla Fase 1.
- **NETWORK SCAN / PORT MAPPING**: aggiunto tempo di scansione
  (`mm:ss`) e riepilogo porte aperte in fondo schermo, sempre dati
  reali già disponibili — mai inventati per assomigliare di più al
  mockup.
- **CREDENTIAL GUESS**: vedi Fase 4 sopra — upgrade reale, non solo
  estetico.

**Cosa NON è stato implementato, e perché:**

- **Moduli exploit/RCE** (es. "Synology DSM RCE" nel mockup): scrivere
  codice di exploitation funzionante per vulnerabilità specifiche è un
  lavoro tecnico sostanzialmente diverso — e più rischioso — di uno
  scanner/audit generico, soprattutto senza poterlo testare contro il
  bersaglio reale in questo ambiente di sviluppo. Non implementato senza
  un contesto di autorizzazione molto più specifico di quello disponibile
  qui (un incarico di pentest concreto, con target e autorizzazione
  documentata).
- **Brute-force SSH**: implementare un client SSH da zero (handshake,
  scambio chiavi, cifratura) è un pezzo di codice crittografico/
  protocollare enormemente più grande e rischioso di quanto ragionevole
  scrivere senza possibilità di test — un bug qui non fallisce in modo
  ovvio, fallisce in modo silenziosamente insicuro. HTTP/Telnet/FTP
  restano i tre servizi supportati.
- **Rilevamento SMBv1 / enumerazione UPnP**: entrambi richiedono
  implementare un parsing di protocollo aggiuntivo (negotiate-protocol
  SMB, SSDP) non ancora scritto — funzionalità reale rimandabile, non
  scartata per principio, semplicemente fuori dal perimetro di questa
  passata.
- **Radar animato, grafici a ciambella, dropdown**: elementi grafici
  pensati per un display a colori ad alta risoluzione con mouse/touch;
  su 240×135 con font 6×8 e sola tastiera non sono realisticamente
  realizzabili in una forma che valga la spesa in complessità/rischio,
  specialmente dopo aver appena rimosso l'effetto Matrix rain per
  stabilizzare il boot (vedi sopra) — non sembrava il momento di
  reintrodurre complessità di rendering non necessaria.

## Compilare e flashare

```
pio run -e cardputer-adv            # build
pio run -e cardputer-adv -t upload  # flash via USB-C
pio device monitor                  # log seriale (115200 baud)
```

Usa l'env `cardputer` invece di `cardputer-adv` per il Cardputer
originale.

> **Nota sulla verifica in questo ambiente**: lo sviluppo è avvenuto in
> un sandbox la cui policy di rete blocca l'accesso al registry di
> PlatformIO, quindi il codice non è mai stato compilato né testato qui
> — solo scritto e rivisto manualmente con attenzione. La build e il
> flash reali sono stati fatti dall'utente su Mac, che ha riscontrato e
> permesso di correggere tre bug reali non individuabili da revisione
> del codice sola: due errori di build (`esp_netif_get_netif_impl()`
> inesistente nella versione IDF del toolchain; SubType `littlefs` non
> valido per `gen_esp32part.py`) e un crash al boot (`pushSprite()`
> chiamato senza un display "genitore" impostato, panic
> `LoadProhibited` al primo frame). **Al momento (confermato
> dall'utente su Cardputer ADV reale): il dispositivo compila, flasha e
> fa boot correttamente, con boot screen e navigazione del menu
> principale funzionanti.** Le fasi successive (WiFi setup, network
> scan, port scanner, credential audit, export) hanno ciascuna un test
> plan dedicato più sotto ma non sono ancora state verificate su
> hardware — se trovi un problema in una di queste, è il prossimo passo
> naturale da testare e riportare.

## Roadmap / stato attuale

- [x] **Fase 1 — Scaffold + UI skeleton**: struttura repo, boot screen
      con boot log "in typing", menu principale navigabile da tastiera
      fisica (`;`/`.`/`,`/`/` come frecce, `Enter` conferma, `Del` torna
      indietro), schermate placeholder per i moduli futuri. Effetto
      Matrix rain rimosso successivamente — vedi "UI: digital rain
      rimosso" sopra. **Boot + navigazione menu confermati funzionanti
      su Cardputer ADV reale.**
- [x] **Fase 2 — Network discovery**: subnet auto-rilevata dal DHCP
      lease, ping sweep (TCP connect-scan) + lettura ARP cache, lookup
      vendor OUI offline (DB reale IEEE, 35k record), risoluzione
      hostname via NBNS (non mDNS, vedi sopra), classificazione
      euristica del device, dashboard host list navigabile + schermata
      di dettaglio per host; subnet/porte manuali rimandate alla Fase
      Settings.
- [x] **WIFI SETUP**: scan reti + selezione + password da tastiera,
      nessuna credenziale hardcoded, persistenza in NVS, auto-reconnect
      al boot. Vedi sezione dedicata sopra.
- [x] **Fase 3 — Port scanner**: TCP connect-scan per singolo host
      (avviato da `HOST DETAIL` con `Tab`), banner grabbing di base
      (HTTP/FTP/SSH/Telnet/SMTP/POP3/IMAP; SMB solo come porta aperta),
      rate limiting condiviso con la Fase 2 (`maxConcurrentProbes`/
      `interProbeDelayMs`), risultati persistiti sull'host e riflessi
      nel risk level.
- [x] **Fase 4 — Credential audit → brute-force reale**: dizionario
      rapido (8 coppie) + wordlist completa da LittleFS
      (`data/creds/{users,passwords}.txt`), HTTP Basic Auth + FTP + Telnet
      banner-based, log tentativi live, opt-in esplicito dietro
      disclaimer rafforzato (tasto `Y`, non `Enter`), attivabile da
      `HOST DETAIL` con `C`, mai persistito tra riavvii. Vedi sopra per
      il perché del cambio di scope.
- [x] **Fase 5 — Storage/export**: risultati su LittleFS in JSON/CSV
      (streaming, escaping RFC4180/JSON corretto), attivabile da
      `NETWORK SCAN` con `E` (e ora anche in automatico a fine scan se
      abilitato da `SETTINGS`). Export su SD supportato dal codice
      (`ResultStore` è agnostico al filesystem) ma non cablato con un
      pin CS di default — vedi sopra.
- [x] **Fase 6 — Restyle UI**: header condiviso con status bar wifi/
      batteria, palette spostata verso ciano/magenta, `SETTINGS` reale
      (prima placeholder), stat footer su `NETWORK SCAN`/`PORT MAPPING`.
      Moduli exploit/RCE, brute-force SSH, rilevamento SMBv1/UPnP e
      grafica avanzata (radar, grafici a ciambella) deliberatamente non
      implementati — vedi sopra.

## Test plan — Fase 1

Boot base e navigazione menu confermati su hardware reale (Cardputer
ADV). Checklist di dettaglio, utile per una verifica più fine o dopo
modifiche future:

1. **Boot**: al power-on/flash, appare il log di boot "typing" riga per
   riga, poi il titolo `CARDPUTER` con sottotitolo, poi il prompt
   lampeggiante `[ PRESS ENTER ]`, su sfondo nero pieno (niente più
   digital rain — vedi sopra).
2. **Navigazione menu**: da boot, `Enter` porta al menu principale con
   5 voci (WIFI SETUP, NETWORK SCAN, PORT SCANNER, CREDENTIAL AUDIT,
   SETTINGS). `;`/`.` spostano la selezione su/giù con wraparound
   (dall'ultima torna alla prima e viceversa). `Enter` su una voce apre
   lo screen corrispondente; `Del` torna al menu.
3. **Reattività input**: la pressione dei tasti deve sembrare istantanea
   — se non lo è, il task `ui` sta probabilmente bloccando il task
   `input` (controllare priorità/stack).
4. **Memoria**: via `pio device monitor` con `CORE_DEBUG_LEVEL=2`,
   verificare che non ci siano log di allocazione fallita
   (`UiManager: failed to allocate render canvas`) né crash/reboot per
   stack overflow nei primi minuti di uso normale del menu.
5. **Persistenza config**: spegnere/riaccendere non deve alterare il
   comportamento in questa fase (la config NVS esiste ma non ha ancora
   uno screen Settings che la modifichi).

## Test plan — WIFI SETUP

1. **Nessuna credenziale precaricata**: su un dispositivo appena
   flashato (o dopo un `pio run -t erase`), `WIFI SETUP` deve mostrare
   "no network configured" — non deve tentare di connettersi a nulla.
2. **Scan**: `ENTER` da `WIFI SETUP` deve mostrare "scanning..." e poi
   la lista delle reti visibili, ordinate per segnale (le più vicine/
   forti in cima), senza duplicati per reti con più access point sullo
   stesso SSID (mesh). Le reti aperte devono essere marcate `o`
   (colore ambra), quelle protette `*` (colore verde).
3. **Rete aperta**: selezionare una rete senza password (se
   disponibile, es. un hotspot di test) e premere `ENTER` — deve
   connettersi direttamente, senza chiedere una password.
4. **Rete protetta — digitazione password**: selezionare una rete
   WPA2, verificare che compaia il campo password e che digitando sulla
   tastiera fisica compaiano i caratteri attesi — **incluse le lettere
   normalmente rimappate a frecce** (`;` `.` `,` `/`): se una di queste
   viene interpretata come navigazione invece che come carattere, è il
   bug che `setTextEntryMode()` dovrebbe prevenire (vedi sezione
   dedicata sopra). `DEL` su un campo non vuoto deve cancellare l'ultimo
   carattere; `DEL` su campo vuoto deve tornare alla lista reti.
5. **Connessione riuscita**: con la password corretta, `ENTER` deve
   mostrare "connecting to..." poi "connected!" con l'IP ottenuto.
   Uscire (`ENTER`/`DEL`) e rientrare in `WIFI SETUP`: deve mostrare
   "connected: <ssid>" con lo stesso IP, non richiedere di rifare lo
   scan.
6. **Password sbagliata**: ripetere con una password errata — deve
   mostrare "connection failed" entro ~15s (o prima, se il device
   risponde con un fallimento esplicito), tornare alla lista reti su
   `ENTER`/`DEL`, e **non** deve aver salvato nulla in NVS (verificabile
   riavviando: `WIFI SETUP` non deve mostrare quella rete come "saved"
   se il precedente tentativo corretto non è mai andato a buon fine).
7. **Persistenza tra riavvii**: dopo una connessione riuscita,
   riavviare il dispositivo (power cycle, non solo reset software) e
   verificare che si riconnetta da solo entro pochi secondi dal boot,
   senza dover rientrare in `WIFI SETUP`.
8. **Navigazione altrove intatta**: dopo essere stati in `WIFI SETUP`
   (in particolare dopo aver digitato una password), tornare al menu
   principale e verificare che `;`/`.`/`,`/`/` funzionino di nuovo come
   frecce normalmente — copre la rete di sicurezza in
   `UiManager::activate()`.
9. **Scorciatoia `W`**: da `NETWORK SCAN`, sia connessi sia no, `W`
   deve aprire `WIFI SETUP` immediatamente.
10. **Dimenticare la rete**: da `WIFI SETUP` con una rete salvata, `F`
    deve disconnettere e tornare a "no network configured". Riavviare:
    non deve più tentare di riconnettersi da solo.

## Test plan — Fase 2

Prerequisiti: `pio run -t uploadfs` eseguito almeno una volta
(altrimenti `data/oui/oui.bin` non è su LittleFS e ogni lookup vendor
fallisce in silenzio, che è il comportamento atteso in quel caso — non
un crash); una rete WiFi configurata da `WIFI SETUP` (vedi il suo test
plan più sotto) — senza credenziali salvate, `NETWORK SCAN` mostra
"no network configured" invece di connettersi.

1. **Boot + LittleFS**: nel log seriale, verificare l'assenza di
   `OuiDatabase: could not open /oui.bin` e di `main: LittleFS mount
   failed`. Se compare il primo, mancava l'`uploadfs`.
2. **Connessione WiFi**: con credenziali già salvate da `WIFI SETUP`,
   da `NETWORK SCAN` deve apparire "connecting to <ssid>..." e poi,
   entro qualche secondo, la schermata con subnet/gateway rilevati.
   Senza credenziali salvate, deve apparire "no network configured" con
   l'indicazione "W: wifi setup" — non un blocco silenzioso.
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

## Test plan — Fase 5

1. **Export base**: da `NETWORK SCAN` con almeno un host trovato,
   premere `E`: deve comparire "exported /export.json + .csv" sopra la
   riga dei tasti. Scaricare i due file (via `pio run -t downloadfs` o
   estraendo la LittleFS image) e verificare che siano JSON/CSV validi
   — un validatore JSON online o `python3 -m json.tool export.json`
   bastano.
2. **Contenuto coerente**: il numero di righe/oggetti esportati deve
   corrispondere al numero di host "found" mostrato in `NETWORK SCAN`.
   Un host con port scan e cred audit già eseguiti deve riportare le
   stesse porte/esito visti in `HOST DETAIL`.
3. **Escaping**: se possibile, forzare un banner con un carattere `"`
   o `,` al suo interno (es. un webserver di test con un header
   `Server` fatto apposta) e verificare che il JSON/CSV risultante
   resti parsabile e non "rompa" la riga/struttura.
4. **Export durante uno scan**: premere `E` mentre `NETWORK SCAN` è
   ancora in corso — non deve bloccare l'avanzamento della scansione
   né corrompere lo stato, e il file deve contenere gli host trovati
   fino a quel momento.
5. **Filesystem pieno**: scenario edge-case, difficile da testare
   deliberatamente — se `LittleFS` è quasi piena (dopo il DB OUI da
   ~1.2 MB restano ~2.6 MB), verificare che un export fallito riporti
   "export FAILED" invece di un crash o un file troncato silenzioso.

## Test plan — Fase 6 (restyle + credential audit reale)

**Solo su dispositivi/reti di tua proprietà o con autorizzazione
esplicita per le voci 4-6 (ora un vero strumento di attacco).**

1. **Header condiviso**: su ogni schermata (menu, network scan, host
   detail, port scan, cred audit, wifi setup, settings, placeholder),
   verificare che compaia l'indicatore `W <batteria>%` in alto a destra
   sulla stessa riga del titolo, colorato ciano se connesso WiFi,
   grigio se no. Verificare che non si sovrapponga mai al titolo anche
   con nomi di schermata lunghi.
2. **Settings**: aprire `SETTINGS` dal menu, navigare i 6 campi con
   `;`/`.`, modificare un valore con `,`/`/` (es. THREADS da 4 a 6),
   uscire con `ENTER`. Rientrare in `SETTINGS`: il valore modificato
   deve essere ancora 6. Riavviare il dispositivo: deve restare 6
   (persistito in NVS). Verificare che `PORT START` non possa superare
   `PORT END` e viceversa (clamp reciproco).
3. **Auto-export**: abilitare `AUTO-EXPORT` (ON) in Settings, avviare
   un `NETWORK SCAN`: al completamento deve comparire "auto-exported
   /export.json + .csv" senza dover premere `E` manualmente.
4. **Wordlist personalizzate**: modificare `data/creds/users.txt` o
   `passwords.txt` (aggiungere una voce nota di un dispositivo di
   test), `pio run -t uploadfs`, poi lanciare `CREDENTIAL AUDIT` su
   quell'host — la nuova voce deve comparire nel log dei tentativi.
5. **Log tentativi live**: durante un audit, verificare che la
   finestra scorrevole mostri gli ultimi 5 tentativi (`user:pass
   OK`/`FAIL`), che il contatore `attempts:`/`success:` cresca in
   tempo reale, e che un `OK` sia evidenziato in rosso mentre i `FAIL`
   restano grigi.
6. **FTP**: su un servizio FTP di test con credenziali note presenti
   nel dizionario/wordlist, verificare che l'audit lo rilevi come
   `VULNERABLE` — copre il path RFC 959 (`USER`/`PASS`/codici `230`/
   `331`) mai testato su un server reale finora.
7. **Durata**: con le wordlist di default (14 utenti × 25 password =
   350 combinazioni per servizio, dopo le 8 del dizionario rapido), un
   audit senza esito su tre servizi può richiedere diversi minuti — è
   atteso, non un blocco. `DEL` durante l'esecuzione torna a `HOST
   DETAIL` senza fermare l'audit in background.

## Limiti noti e tagli di scope deliberati

Riepilogo di quanto già menzionato nelle sezioni sopra, in un unico
posto:

- **Nessun editor manuale di subnet**: `NETWORK SCAN` usa sempre la
  subnet DHCP-rilevata (non ha senso poterla cambiare finché non c'è
  un modo di specificare un range arbitrario in modo sicuro). Il range
  porte invece **è** ora editabile da `SETTINGS` (Fase 6).
- **mDNS non implementato**: hostname via NBNS soltanto (vedi Fase 2)
  — dispositivi Apple/Android/Chromecast tipicamente non avranno un
  hostname risolto.
- **Euristica Telnet non affidabile al 100%**: il rilevamento di login
  riuscito è basato su pattern-matching testuale, non su un parser di
  protocollo — vedi Fase 4. FTP invece usa i codici di risposta
  numerici del protocollo (RFC 959), più affidabile.
- **Moduli exploit/RCE, brute-force SSH, rilevamento SMBv1/UPnP,
  grafica avanzata (radar/grafici a ciambella) non implementati** —
  scelte deliberate motivate in dettaglio nella sezione "Fase 6"
  sopra, non dimenticanze.
- **SD card non cablata di default**: supportata dal codice
  (`ResultStore` è filesystem-agnostico) ma senza un pin CS verificato
  per il Cardputer ADV — vedi Fase 5.
- **Copertura caratteri per la password WiFi non garantita al 100%**:
  `WifiSetupScreen` accetta qualunque carattere che `M5Cardputer.Keyboard`
  consegni in `status.word` (lettere, cifre, simboli comuni raggiunti
  con `Fn`/`Opt`). Non è stato possibile verificare su hardware reale
  se **ogni** combinazione di simboli usabile in una passphrase WPA2 sia
  effettivamente raggiungibile dalla tastiera fisica del Cardputer — se
  la tua password contiene un carattere che non riesci a digitare, è un
  limite della mappatura tastiera di M5Cardputer, non di questo codice
  (che si limita a inoltrare quello che la libreria gli consegna).
- **Sound design non implementato**: la spec lo marcava "opzionale";
  non è stato aggiunto in nessuna fase. `M5Cardputer.Speaker` è
  disponibile per chi voglia aggiungerlo.
- **LoRa/GPS**: non presenti di serie sul Cardputer ADV (confermato
  nella ricerca hardware iniziale di questo progetto), quindi non
  affrontati.
- **Nessuna build reale eseguita**: vale per ogni fase di questo
  progetto — il sandbox di sviluppo non ha accesso al registry
  PlatformIO. Tutto il codice è stato scritto con attenzione e, dove
  possibile, la logica non hardware-dipendente è stata verificata con
  test standalone su host (aritmetica IP, formato DB OUI, encoder
  Base64) — ma **una build (`pio run`) e un test su hardware reale
  restano il passo successivo prima di fidarsi di questo firmware.**
