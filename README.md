# Cardputer NetAudit

Firmware per **M5Stack Cardputer / Cardputer ADV** (ESP32-S3) per network
discovery & auditing (stile Fing) con UI cyberpunk/Matrix: testo verde
fosforescente monospace su nero, accenti magenta/ciano. L'effetto
"digital rain" di sfondo è stato rimosso per ora — vedi
["UI: digital rain rimosso"](#ui-digital-rain-rimosso-per-ora).

> ⚠️ **Stato del progetto**: sviluppo incrementale in corso. Vedi
> [Roadmap](#roadmap--stato-attuale) per cosa è implementato oggi.

> ⚠️ **Uso legale**: questo firmware include moduli di **attacco a
> credenziali** (brute-force con wordlist su HTTP/Telnet/FTP/POP3/IMAP/
> SMTP, non più solo una verifica di default noti — vedi Fase 4 e Fase
> 16) e, dalla Fase 16, strumenti che **agiscono attivamente su
> dispositivi di terze parti**: ARP spoofing/MITM, deauthenticazione
> WiFi, un access point che imita una rete reale. Tutti dietro un gate
> opt-in con disclaimer esplicito — quelli della Fase 16 dietro un gate
> rafforzato (va scritta per intero la parola AUTHORIZED). Va usato
> **solo** su reti e dispositivi di tua proprietà o per cui hai
> autorizzazione esplicita e documentabile alla verifica di sicurezza.
> Attaccare dispositivi senza permesso è illegale in quasi ogni
> giurisdizione, e intercettare il traffico di altri utenti su una rete
> — anche una di cui sei amministratore — può implicare leggi sulle
> intercettazioni separate dalla sola autorizzazione del proprietario
> della rete. Il resto del firmware (discovery, port scan) resta un
> tool di audit passivo/attivo ma non distruttivo.

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
- **Dropdown**: non applicabile senza mouse/touch — le voci `SETTINGS`
  restano un editor "valore ± con `,`/`/`", non un vero dropdown.

Radar animato e grafici a ciambella sono stati **poi implementati** su
richiesta esplicita dell'utente, con adattamenti — vedi Fase 7.

### Fase 7: radar animato + donut chart (versione adattata al 240×135)

Richiesti di nuovo dall'utente dopo aver visto il resto della UI
funzionare su hardware reale, con il permesso esplicito di renderli
puramente decorativi dove non c'è un dato reale da mostrare.

- **Radar** (`HostDetailScreen`): pannello sovrapposto in alto a destra
  (disegnato per ultimo, così il suo riempimento di sfondo copre anche
  eventuali testi troppo lunghi finiti sotto — stesso trucco usato per
  il pannello "top ports" del port scan). Linea di scansione rotante
  (un giro completo ogni ~3s, calcolata da `millis()`), 3 cerchi
  concentrici, un mirino, e 4 "blip" magenta la cui posizione è
  derivata da un hash dei byte IP dell'host — stabile tra un frame e
  l'altro (niente jitter), diversa da host a host, **ma senza alcun
  significato spaziale reale**: i dispositivi non hanno coordinate,
  esattamente come nel mockup originale.
- **Donut chart** (`PortScanScreen`): a differenza di quanto concesso
  dall'utente ("anche se non legati a dati reali"), qui i due segmenti
  **sono dati reali**: porte aperte trovate vs. resto del range
  configurato in `SETTINGS` (`count / (portRangeEnd-portRangeStart+1)`).
  Non distingue filtered/closed (richiederebbe raw socket che non
  abbiamo, vedi Fase 3) ma open-vs-non-open è informazione vera, non
  serviva inventare nulla.
- **Homepage**: `MAIN MENU` è diventato una vera dashboard —
  intestazione "NETRUNNER", skyline stilizzata (dati statici, non
  animata: essendo ridisegnata ad ogni frame, un pattern casuale
  avrebbe fatto tremolare invece che animare), barra di stato in fondo
  con uptime (non orario reale — niente RTC/NTP configurato, per
  evitare la complessità/ambiguità dei fusi orari), "READY." e IP
  corrente. L'hint tasti è stato tolto da questa sola schermata (spazio
  verticale insufficiente per skyline + menu + status bar + hint) —
  frecce/Enter è già la convenzione appresa da tutte le altre
  schermate.
- **Nuove API grafiche usate per la prima volta**: `drawCircle`,
  `fillCircle`, `drawLine`, `fillArc` — a differenza di `fillRect`/
  `drawRect`/`drawFastHLine` (già confermati funzionanti su hardware
  reale nelle fasi precedenti), queste non sono ancora state
  verificate fisicamente. Sono API LovyanGFX standard e ben
  documentate, quindi il rischio è considerato basso, ma se dopo il
  flash una di queste tre schermate (menu principale, host detail, port
  scan) desse un errore di compilazione o un comportamento grafico
  inatteso, è il primo posto da guardare.

### Fase 8: database porte/servizi completo (+ un bug corretto nell'OUI)

- **Bug reale trovato e corretto**: `OuiDatabase::begin()` usava come
  path di default `/oui.bin`, ma l'asset è in `data/oui/oui.bin` —
  `uploadfs` di PlatformIO preserva le sottocartelle, quindi a runtime
  il file è `/oui/oui.bin`, non `/oui.bin`. Il fallimento di
  `LittleFS.open()` non causa un crash (viene loggato e la funzione
  ritorna `false`), quindi il sintomo sarebbe stato **tutti i lookup
  vendor silenziosamente falliti** ("unknown" ovunque) senza che
  nessuno se ne accorgesse necessariamente subito. Trovato per
  confronto con `WordlistLoader` (che invece usa correttamente
  `/creds/users.txt`) mentre si implementava questo stesso modulo.
  Corretto il default a `/oui/oui.bin`.
- **~12.000 porte, non ~15**: `BannerGrabber` aveva uno switch
  hardcoded con una quindicina di porte comuni. `PortServiceDb`
  (stesso design a binary-search-su-flash di `OuiDatabase`, ~300 KB)
  copre l'intero set di assegnazioni IANA note tramite nmap, incluse
  sia TCP sia UDP (quest'ultime non ancora usate — lo scanner fa solo
  TCP connect-scan, vedi Fase 3 — ma il database è pronto).
- **Perché non `nmap-services` direttamente**: quel file specifico è
  (C) Insecure.Com LLC sotto Nmap Public Source License, la cui licenza
  dichiara esplicitamente che un'applicazione che "legge o include file
  di dati protetti da copyright come nmap-os-db o nmap-service-probes"
  costituisce opera derivata ai fini della licenza — includerlo
  as-is avrebbe trascinato l'intero firmware sotto termini GPL-2 con le
  clausole aggiuntive di Nmap, una scelta di licenza che non spettava a
  questo assistente prendere per conto dell'utente. `tools/
  extract_port_services.py` estrae invece solo i **fatti** nudi
  (porta/protocollo → nome servizio — gli stessi fatti pubblici che IANA
  mantiene nel proprio registro), scartando tutto ciò che è
  specificamente farina del sacco di Nmap (percentuali di frequenza
  reale, commenti, riferimenti RFC, struttura del file) e riscrivendo
  il risultato nel nostro formato. Il file sorgente `nmap-services` **non
  è incluso nel repo** (richiede `apt install nmap` in locale per
  rigenerare).
- **Un regressione trovata e corretta durante l'integrazione**: con il
  nuovo DB attivo, le porte HTTP alternative (8080, 8000, 8888) hanno
  un nome più specifico e reale (`http-proxy`, `http-alt`,
  `sun-answerbook`) invece del generico `"http"` che lo switch
  hardcoded restituiva sempre. `CredAuditManager` però decide quali
  porte attaccare come HTTP controllando `p.service == "http"` — con il
  nuovo nome quella condizione non sarebbe più stata vera per quelle
  porte, disattivando silenziosamente l'audit su di esse. Corretto
  forzando `result.service = "http"` in modo incondizionato per le
  porte che `BannerGrabber` già tratta come HTTP-shaped
  (`looksLikeHttp()`), non solo quando il campo era vuoto — la
  ricerca di questa fragilità (grep di ogni `.service ==` nel
  codebase) fa ora parte della checklist quando si tocca questo file.

### Fase 9: restyle splash screen + NETWORK SCAN da mockup

- **Skyline condivisa**: l'array `Building`/il loop di disegno usato
  finora solo da `MainMenuScreen` è stato estratto in
  `chrome::drawSkyline(gfx, baselineY)` (`src/ui/Chrome.h/.cpp`), cosicché
  `BootScreen` possa riusare la stessa silhouette invece di duplicarne i
  dati — un solo posto da aggiornare se lo skyline cambia.
- **Boot screen a due fasi**: la sequenza di boot log "in typing" resta
  identica (init a basso livello), ma alla fine non mostra più solo
  titolo/sottotitolo — passa a una vista da dashboard che ricalca
  `MAIN MENU`: header condiviso (`CARDPUTER ADV`), titolo `NETRUNNER` in
  magenta, sottotitolo `ADVANCED NETWORK TOOLKIT`, versione
  `v1.0.0-ADV`, banda skyline, prompt `[ PRESS ENTER ]` lampeggiante e la
  stessa status bar in fondo (uptime / `SYSTEM READY` / IP) — lo splash
  ora è leggibile come il primo frame della stessa dashboard, non una
  schermata a parte con convenzioni proprie.
- **`NETWORK SCAN` con tabella incorniciata**: la vecchia riga di stato
  combinata (`scanning XX% found:N time:mm:ss`) è diventata una vera
  stat strip a due colonne — `HOSTS FOUND: N` a sinistra, percentuale
  live (in scansione) o `SCAN TIME mm:ss` (a scansione finita) a destra
  — e la tabella host ora vive dentro un riquadro con bordo e una riga di
  intestazione colonne (`IP` / `TYPE` / `VENDOR`), sullo stile già usato
  da `PORT MAPPING` per la sua tabella porte. I dati mostrati sono
  invariati (IP, classe dispositivo, vendor OUI, colore per risk level) —
  è cambiata solo la cornice.
- **`PORT MAPPING` non toccata**: il mockup allegato mostra anche quello
  schermo, ma la richiesta esplicita riguardava solo lo splash e
  `NETWORK SCAN` — `PortScanScreen` aveva già un trattamento coerente
  dalla Fase 7 (tabella + donut + footer) e non è stata modificata qui.
- **Addendum post-Fase 9**: su richiesta dell'utente, la skyline
  decorativa è stata rimossa da `MAIN MENU` (restava solo sullo splash,
  vedi sopra) — le voci di menu sono salite subito sotto l'header, righe
  leggermente più alte (16px invece di 14px) per usare lo spazio
  liberato. `chrome::drawSkyline()` resta comunque condivisa, perché la
  usa ancora `BootScreen`.

### Fase 10: cronologia scan, SD, WiFi multi-rete, UDP, mDNS, firme vuln, OTA

Otto migliorie tecniche/funzionali richieste dall'utente dopo una lista
di 10 proposte fatte da questo assistente (vedi cronologia) — numerate
qui come nell'elenco originale (1, 2, 3, 4, 5, 6, 7, 10; la 8 e la 9
della lista originale — dashboard web e OTA erano scambiate, l'OTA era
la 10 — non sono state richieste dall'utente e non sono state fatte).

- **#3 — SD card cablata**: nuovo modulo `storage/SdCard.h/.cpp`. Pin
  SPI dedicato (SCK=40, MISO=39, MOSI=14, CS=12, bus `HSPI` separato dal
  display) preso dallo sketch di esempio SD ufficiale di M5Stack per
  Cardputer. **Questo è l'unico numero "magico" in tutto il firmware che
  non è stato possibile incrociare con nient'altro già funzionante nel
  codebase** (a differenza, per dire, dei pin tastiera/display che
  M5Unified auto-rileva) — se `sdcard::begin()` fallisce sempre anche
  con una scheda inserita (log seriale: `sdcard: no SD card detected`),
  è il primo posto da correggere. Quando una SD è presente, `NETWORK
  SCAN` (export manuale `E` e auto-export) e la cronologia scan (sotto)
  scrivono lì invece che su LittleFS — `sdcard::exportFs()`/
  `exportFsLabel()` centralizzano la scelta, così ogni chiamante mostra
  "(SD)" o "(flash)" nel proprio messaggio di stato senza doverlo sapere
  in anticipo.
- **#1 — Cronologia scan persistita**: nuovo modulo
  `storage/ScanHistory.h/.cpp` + nuova schermata `SCAN HISTORY` (in
  `MAIN MENU`, sesta voce). Ogni `NETWORK SCAN` completato salva uno
  snapshot JSON (`/history/scan_NNNNN.json`, host vivi con IP/MAC/
  hostname/vendor/classe/risk) via ArduinoJson — libreria già dichiarata
  come dipendenza dalla Fase 1 ma mai usata finora, qui finalmente
  sfruttata per la lettura strutturata che il formato di export di
  `ResultStore` (scrittura streaming one-way) non offre. Il numero di
  sequenza (non un timestamp: **niente RTC/NTP su questa scheda**, vedi
  sotto) vive in NVS e viene incrementato a ogni salvataggio; solo gli
  ultimi `kMaxEntries` (20) snapshot vengono tenuti, i più vecchi
  vengono cancellati automaticamente. `SCAN HISTORY` mostra l'elenco
  (più recente in cima, marcato "latest") e il dettaglio di ogni scan in
  una tabella incorniciata nello stile della Fase 9.
- **#2 — Diff e alert sui cambiamenti**: usa la Fase 1 sopra. Al termine
  di un `NETWORK SCAN`, lo snapshot appena salvato viene confrontato con
  quello immediatamente precedente: gli host presenti ora ma non prima
  sono "nuovi" — evidenziati in magenta nella tabella di `NETWORK SCAN`
  e conteggiati nella stat strip (`HOSTS FOUND: N (+M new)`). Per le
  porte, `PortScanManager` salva/confronta un piccolo snapshot per-host
  (`/history/ports_<ip>.json`, solo numeri di porta) a ogni port scan:
  una porta aperta ora ma non l'ultima volta è "nuova" — evidenziata in
  magenta su `PORT MAPPING` (a meno che non abbia anche una firma
  vulnerabile nota, vedi #6 sotto, nel qual caso vince il rosso) e
  conteggiata (`open:N (+M new)`). Limite accettato: il diff porte
  confronta solo per numero di porta, non (porta, protocollo) — una
  porta 53 TCP e una 53 UDP aperte nello stesso host sono
  indistinguibili per questo confronto (vedi commento in
  `ScanHistory.h`).
- **#4 — Probe UDP di base**: nuovo modulo `scan/UdpProbe.h/.cpp`,
  eseguito una volta per host subito dopo lo sweep TCP di
  `PortScanManager`. Tre probe fissi — DNS/53 (query A valida per un
  nome che non risolverà mai a nulla di reale, sufficiente a far
  rispondere qualunque server DNS reale), NTP/123 (richiesta SNTP
  standard, primo byte `0x1B`), SNMP/161 (GetRequest SNMPv1 per
  `sysDescr.0`, community `public` — pacchetto BER/ASN.1 costruito e
  verificato a mano byte-per-byte con uno script Python usa-e-getta
  prima di essere hardcoded, stessa disciplina già usata per il formato
  binario del DB OUI). **Onestà del risultato**: a differenza del TCP
  connect-scan, l'UDP senza ICMP raw non permette di distinguere "porta
  chiusa" da "pacchetto silenziosamente scartato" — questi probe quindi
  **non riportano mai "chiuso/filtrato"**, solo "ha risposto" (nel qual
  caso è sicuramente aperto) o "nessuna riga in tabella" (nessuna
  affermazione). Le porte UDP trovate appaiono in `PORT MAPPING` con
  suffisso `/u`.
- **#5 — Risoluzione hostname via mDNS**: nuovo modulo
  `scan/MdnsReverseResolver.h/.cpp` + `net/DnsWire.h/.cpp` (parsing/
  building DNS condiviso, con supporto alla compressione dei nomi RFC
  1035 §4.1.4 — verificato con uno script Python di riferimento,
  incluso un caso con puntatore di compressione, prima di essere
  scritto in C++). A differenza di `HostnameResolver` (NBNS, Fase 2),
  **non** usa la libreria `ESPmDNS` — quella è pensata per pubblicizzare
  il nome di *questo* dispositivo e sfogliare servizi, non per
  interrogare l'IP di qualcun altro. Questo modulo costruisce a mano una
  query PTR reverse (`10.1.168.192.in-addr.arpa`, RFC 6762 §3 elenca
  esplicitamente le zone reverse private come valide su mDNS senza
  suffisso `.local`) e la spedisce in multicast su `224.0.0.251:5353`.
  `ScanManager::probeHost` la usa come fallback **solo** se NBNS non ha
  già trovato un nome — copre un'ampia fetta di dispositivi (telefoni,
  Mac, Chromecast, molto IoT) che NBNS da solo non vedeva. **Rischio più
  alto del resto del codice di rete**: il multicast UDP (bind sulla
  porta 5353, join al gruppo) non è mai stato esercitato altrove in
  questo codebase per fare un confronto, e la firma esatta di
  `WiFiUdp::beginMulticast()` usata (2 argomenti) potrebbe non essere
  quella giusta per la versione del core Arduino-ESP32 in uso — se non
  compila, è il primo posto da guardare (vedi commento nel sorgente).
- **#6 — Firme di vulnerabilità note**: nuovo modulo
  `scan/VulnSignatures.h/.cpp` — una tabella piccola e scelta a mano
  (9 voci: backdoor vsftpd 2.3.4/ProFTPD 1.3.3c, OpenSSH 1.x-3.x, IIS
  5/6, Apache 1.3/2.0.x), **non** un database CVE generico e **non** un
  parser euristico di range di versione — solo substring match esatte
  su stringhe di banner reali, con la provenienza di ogni voce
  documentata nel sorgente. Un match forza il risk level dell'host a
  Critical (stessa forza di un default credential confermato) e mostra
  una riga `VULN:` su `HOST DETAIL`; su `PORT MAPPING` la porta
  interessata appare in rosso (priorità massima tra i colori,
  sopra "nuova porta" e "porta legacy generica").
- **#7 — Reti WiFi multiple salvate**: `WifiManager` ora persiste fino a
  `kMaxSavedNetworks` (3) reti invece di una sola, come lista MRU
  (most-recently-used) in NVS — un nuovo salvataggio va sempre in testa,
  un duplicato per SSID viene deduplicato/aggiornato in posizione, oltre
  la soglia la voce meno recente viene scartata. `WIFI SETUP` ha un
  nuovo stato "reti salvate" (tasto `S` da Idle): selezionarne una
  riconnette senza dover ridigitare la password. **Bug evitato in fase
  di progettazione**: riconnettersi a una rete salvata non richiama
  `saveCredentials()` (che avrebbe sovrascritto la password reale con
  quella vuota di questo percorso, mai digitata) — usa invece
  `touchSavedNetwork()`, che rilegge la password già salvata e la
  riscrive invariata solo per aggiornare l'ordine MRU.
- **#10 — Aggiornamento OTA**: `partitions.csv` riscritta per due slot
  app da 1.625 MB (`ota_0`/`ota_1`) + `otadata`, al posto dell'unico
  slot `factory` da 4 MB — vedi i commenti nel file per l'analisi
  completa degli offset (somma esatta a 8 MB, verificata con uno script
  Python). Nuovo modulo `net/OtaUpdater.h/.cpp` (scarica un
  `firmware.bin` via HTTP — solo `http://`, non `https://`, deliberato:
  niente bundle CA da gestire per un dispositivo pensato per aggiornarsi
  da un host sulla stessa LAN — e lo flasha nello slot OTA inattivo
  tramite la libreria `Update` di Arduino-ESP32) + nuova schermata `OTA
  UPDATE` (raggiungibile da `SETTINGS` con `O`). L'operazione è
  volutamente sincrona sul task UI (blocca il rendering per la durata
  del download+flash, con schermata "DO NOT power off" fissa) — non è
  una svista: un aggiornamento OTA ha comunque bisogno di una schermata
  da cui l'utente non può navigare via, quindi bloccare è la UX
  corretta, non un bug. **Rischio dimensione slot**: alla build reale di
  questa fase, il firmware occupava 1.251.041 byte su 1.638.400
  disponibili per slot (76.4%) — margine che si è poi rivelato
  insufficiente: la Fase 13 (BLE scanning soprattutto) ha fatto
  sforare quel tetto, `pio run -t upload` ha fallito esattamente come
  previsto qui sotto, ed è stato corretto allargando gli slot — vedi il
  bullet dedicato nella sezione "Fase 13" più avanti. Il vero problema
  trovato al primo flash reale di *questa* fase non era la dimensione,
  ma un bug di offset in `otadata` — vedi il bullet dedicato più sotto.
- **Bug reale di link trovato durante la prima build della Fase 10**:
  `src/scan/Base64.cpp` (dalla Fase 4, encoder minimale per
  `Authorization: Basic`) definiva `namespace base64 { String
  encode(...); }` — nome identico a quello del `base64::encode`
  incorporato nel core Arduino-ESP32 stesso
  (`cores/esp32/base64.cpp`). I due non si erano mai scontrati prima
  perché quel file del core è linkato "pigro" (un `.o` dentro un
  archivio `.a` viene incluso solo se qualcos'altro lo referenzia
  davvero) — finché nessun modulo di questo firmware usava
  `HTTPClient`, quel simbolo del core restava inutilizzato e quindi mai
  linkato. `net/OtaUpdater.cpp` (Fase 10, #10) è stato il primo a
  includere `<HTTPClient.h>`, che internamente referenzia il
  `base64::encode` del core — da lì in poi l'archivio lo tira dentro,
  e il linker trova due definizioni dello stesso simbolo ("multiple
  definition of `base64::encode`"). Non rilevabile da una revisione
  del codice sola (i due file non si vedono a vicenda, il conflitto
  esiste solo a livello di symbol table del binario finale) — trovato
  dal primo tentativo di link reale dell'utente dopo l'aggiunta
  dell'OTA. Corretto rinominando il namespace del nostro encoder da
  `base64` a `b64` (nessun cambio di comportamento, solo di nome).
- **Bug reale trovato al primo flash reale della tabella OTA — boot
  loop**: dopo `erase` + `upload` + `uploadfs` andati tutti a buon
  fine (nessun errore in nessuno dei tre), il device restava bloccato
  in un loop di reset, ripetendo solo il banner della ROM
  (`ESP-ROM:esp32s3-20210327`...) senza mai stampare una riga di
  bootloader di secondo stadio o dell'app — stesso Program Counter
  salvato a ogni giro, segno di un crash deterministico prestissimo nel
  boot. La diagnosi è arrivata dal log completo di `esptool.py` durante
  `upload`: tra la scrittura della partition table (`0x8000`) e quella
  dell'app (`0x10000`) compariva una scrittura da 8192 byte a
  `0x0000e000` mai richiesta esplicitamente da questo progetto —
  PlatformIO/Arduino-ESP32, quando rileva una tabella con OTA
  (`ota_0`/`ota_1`/`otadata`), flasha automaticamente un piccolo blob
  (`boot_app0.bin`, inizializza `otadata` per puntare a `ota_0`) **a un
  offset fisso hardcoded, `0xE000`**, indipendentemente da cosa dice
  `partitions.csv`. La tabella di questa fase metteva `otadata` a
  `0xF000` (con `phy_init` a `0xE000`, ricalcando il layout della
  vecchia tabella non-OTA) — quella scrittura automatica cancellava
  quindi tutta `phy_init` e la prima metà di `otadata`, lasciando
  `otadata` con contenuto incoerente e il bootloader di secondo stadio
  (che legge proprio `otadata` per decidere quale slot avviare) in
  crash immediato. Non individuabile da una revisione del codice sola —
  serviva il log esatto di `esptool.py` durante un flash reale, che
  l'utente ha fornito. Corretto spostando `otadata` esattamente a
  `0xE000` e togliendo `phy_init` come voce separata (le tabelle OTA di
  riferimento di Arduino-ESP32 non ne hanno una: la calibrazione PHY
  funziona comunque, senza una partizione dedicata) — vedi i commenti
  in `partitions.csv` per il dettaglio completo. **Richiede un nuovo
  `erase` + `upload` + `uploadfs`**: l'`otadata` corrotto dal tentativo
  precedente va ripulito, non basta riflashare sopra.

### Fase 11: war driving, allowlist per la scoperta attiva, orario NTP

- **Perché non "collegati a ogni rete aperta trovata", come richiesto
  alla lettera**: la richiesta originale era "fai una scansione
  costante delle reti wifi e, per ogni rete aperta, collegati e fai una
  discovery" — implementarla così com'è scritta avrebbe significato
  collegarsi automaticamente e scansionare porte/host su reti di
  sconosciuti incontrate per caso girando, senza alcuna autorizzazione
  da parte di chi le gestisce. È una categoria diversa da tutto il
  resto di questo firmware, che finora ha sempre e solo toccato la rete
  che l'utente stesso ha configurato come propria (via `WIFI SETUP`,
  con credenziali mai hardcoded). Questo assistente non ha implementato
  quella parte alla lettera. Al suo posto: **war driving passivo
  sempre attivo** (scansiona e logga ogni AP visto — SSID, BSSID,
  RSSI, canale, cifratura, vendor — senza mai collegarsi a nulla, come
  Kismet/Wigle) più **una allowlist esplicita** di SSID che l'utente
  aggiunge a mano (reti proprie o di un cliente autorizzato): solo per
  quelle, se aperte, scatta la connessione automatica + discovery +
  port scan + salvataggio. Stessa logica di scope già usata per
  `CREDENTIAL AUDIT` (disclaimer esplicito, opt-in), applicata qui alla
  singola rete invece che all'intero modulo.
- **`scan/WardrivingManager`**: un task FreeRTOS permanente (creato una
  volta in `begin()`, mai distrutto — internamente resta inerte finché
  non viene avviato da `WAR DRIVING` con `ENTER`) che ogni 15s rifà una
  `WiFi.scanNetworks()` (stesso wrapper `WifiManager::beginScan()` già
  usato da `WIFI SETUP`), deduplica per BSSID (non per SSID: reti
  diverse con lo stesso nome — "Free WiFi", reti mesh — sono AP fisici
  distinti), fa il lookup vendor sull'OUI del BSSID riusando
  `OuiDatabase` già esistente, e appende ogni AP mai visto prima come
  riga CSV su `/wardrive/wardrive.csv` (SD se presente, altrimenti
  LittleFS — riusa `sdcard::exportFs()` della Fase 10; **path spostato in
  `/netrunner/wardrive.csv` dalla Fase 29** — vedi quella sezione). Per
  un AP aperto
  E in allowlist E non ancora scoperto in questa sessione: si
  disconnette da dove si trovava, si collega alla rete aperta (mai
  salvata tra le reti WiFi — non deve mai spodestare le reti vere
  dell'utente dai 3 slot MRU della Fase 10), lancia
  `ScanManager::startDiscoveryScan()` e, per i primi 5 host vivi
  trovati, `PortScanManager::startScan()` — **sono gli stessi identici
  moduli usati da `NETWORK SCAN`/`PORT SCANNER`**, nessuna logica di
  scansione duplicata — poi esporta i risultati con `ResultStore` sotto
  `/wardrive/scans/<ssid>_<bssid>.json|csv` (namespace separato dalla
  cronologia scan dell'utente, per non mischiare le due cose; **path
  spostato in `/netrunner/<timestamp>_<ssid>_<bssid>.json|csv` dalla
  Fase 29**) e infine richiama `WifiManager::autoConnect()` per tornare
  alla propria rete.
- **Allowlist NVS-backed**: `scan/WardrivingManager` gestisce un proprio
  namespace NVS (`wardrive_al`, fino a 10 SSID, stesso pattern di
  storage MRU-semplice già usato da `WifiManager` per le reti salvate).
  Aggiungere un SSID (da `WAR DRIVING` → `A`) richiede di digitarlo e
  poi confermare esplicitamente con `Y` davanti a un avviso ("solo reti
  TUE o per cui sei autorizzato") — stessa meccanica di
  `CredDisclaimerScreen`, applicata qui all'azione di aggiungere una
  rete invece che all'intero modulo credenziali.
- **`WardrivingScreen`**: nuova voce di menu ("WAR DRIVING", settima —
  la spaziatura verticale di `MAIN MENU` è stata ristretta da 16 a 14px
  per riga per farcela stare senza toccare la status bar in fondo).
  Stato Idle mostra contatori (visti/aperti/scoperti) e una tabella
  degli ultimi AP visti (rosso→magenta se già scoperti, ambra se
  aperti); stato Running mostra un log live delle attività (nuovo AP
  trovato, connessione a un AP in allowlist, host scoperti,
  riconnessione) — si può uscire con `DEL` e il war driving continua in
  background, esattamente come `CREDENTIAL GUESS` della Fase 4.
- **Limite noto, non risolto per scope**: `WardrivingManager` e
  `ScanManager`/`PortScanManager` condividono lo stesso singolo radio
  WiFi e gli stessi flag `_running` di guardia — se l'utente avvia
  manualmente `NETWORK SCAN`/`PORT SCANNER`/`CREDENTIAL AUDIT` esattamente
  mentre il war driving sta facendo un'incursione su un AP in allowlist,
  le due richieste si accavallano silenziosamente (una delle due vince,
  l'altra diventa un no-op) — stesso genere di limite già accettato per
  `PortScanManager` ("un solo scan alla volta", vedi Fase 3). Evitare di
  usare altri moduli di scansione mentre il war driving è attivo.
- **`net/TimeSync`**: orario reale via NTP pubblico (`pool.ntp.org` di
  default), usando il client SNTP già incorporato in ESP-IDF
  (`configTime()`), non un client UDP scritto a mano — a differenza del
  resto del codice di rete di questa fase e della Fase 10, qui non c'è
  un protocollo non familiare da interpretare, quindi non aveva senso
  reinventarlo. Solo UTC, niente timezone/DST (ogni altro timestamp già
  in questo firmware — cronologia scan, log war driving — è già UTC).
  Armato una volta al boot e di nuovo dopo ogni connessione WiFi riuscita
  (`main.cpp`/`WifiSetupScreen`), si sincronizza da solo appena c'è
  rete. **Armonizzazione con quanto già costruito**: gli snapshot di
  `SCAN HISTORY` (Fase 10) ora includono un campo `time` reale quando
  disponibile (mostrato al posto di "latest" nella lista); la status bar
  di `MAIN MENU`/boot screen mostra l'ora reale invece dell'uptime non
  appena sincronizzata (uptime resta il fallback, come prima, se non c'è
  ancora sincronizzazione).

### Fase 12: splash da mockup, audio, rinomina, scroll, screen timeout, detection

Sette migliorie richieste in blocco dall'utente, indipendenti tra loro:

- **Splash screen ridisegnata sul mockup allegato**: header decorativo
  centrato `-( CARDPUTER ADV )-` (non più il `chrome::drawHeader`
  condiviso — una splash non ha bisogno di icone wifi/batteria, e il
  mockup non ha una linea divisoria lì), skyline arricchita (17 edifici
  invece di 14, alcuni con una piccola antenna, colore graduato
  ciano→magenta in base all'altezza — verificato con uno script Python
  che i due colori estremi dell'interpolazione coincidano esattamente
  con `theme::CYAN`/`theme::MAGENTA`), e una nuova griglia prospettica
  stile synthwave sotto lo skyline (`chrome::drawPerspectiveGrid` —
  linee orizzontali che si diradano scendendo + linee convergenti su un
  punto di fuga centrale, pura geometria con `drawLine`/`drawFastHLine`,
  nessuna proiezione 3D reale). Tolta la status bar (uptime/READY/IP) da
  questa schermata per fare spazio alla griglia — quell'informazione
  resta comunque visibile un attimo dopo, su `MAIN MENU`.
- **Melodia di avvio**: nuovo modulo `ui/Sound.h/.cpp`
  (`M5Cardputer.Speaker.tone()`), un breve arpeggio ascendente di 4 note
  (sotto il secondo). Suona esattamente nel momento in cui il boot log
  "in typing" lascia il posto alla vista brandizzata — "dopo il
  loading" preso alla lettera. Gated da `AppConfig::uiSoundEnabled`,
  campo dichiarato dalla Fase 1 ma mai letto da nessuno finora — ora
  `SETTINGS` ha una riga `SOUND` reale per attivarlo/disattivarlo.
- **Rinominata "WIFI SETUP" in "WIFI SCAN"**: solo l'etichetta nel menu
  e l'header della schermata — nessuna rinomina di classi/file
  (`WifiSetupScreen` resta tale internamente, per non innescare un
  refactor a cascata su un cambio che è puramente di presentazione).
- **Scroll su/giù nella lista reti di `WAR DRIVING`**: la tabella degli
  AP visti (stato Idle) prima mostrava solo le prime 8 righe senza modo
  di vedere oltre; ora Su/Giù scorrono la lista con lo stesso pattern
  già usato altrove (`NETWORK SCAN`, `SCAN HISTORY` — finestra scorrevole
  ancorata alla riga selezionata).
- **Timeout schermo di 30 secondi, lavoro in background invariato**:
  `UiManager` traccia l'ultimo evento tastiera; dopo 30s di inattività
  abbassa la retroilluminazione (`M5Cardputer.Display.setBrightness()`,
  12/255) invece di spegnere/oscurare il canvas — lo schermo resta
  leggibile da vicino, solo più fioco, e nessuna schermata deve sapere
  che sta succedendo. I manager in background (`ScanManager`,
  `PortScanManager`, `CredAuditManager`, `WardrivingManager`) sono già
  task FreeRTOS indipendenti dal task UI: continuano esattamente come
  prima, il timeout non li tocca in alcun modo. Qualunque tasto
  ripristina la luminosità piena.
- **Rilevamento reti WiFi migliorato**: `WifiManager::beginScan()` ora
  chiama `WiFi.scanNetworks()` con `show_hidden=true` (prima le reti che
  non trasmettono l'SSID erano invisibili — silenziosamente scartate sia
  dal picker di `WIFI SCAN` sia da `WardrivingManager`) e
  `max_ms_per_chan=400` invece del default 300 (permanenza più lunga per
  canale, scansione un po' più lenta ma più affidabile su AP con beacon
  deboli/lenti). `WardrivingManager` ora logga anche le reti nascoste
  (etichettate `<hidden>`, identificate dal BSSID) invece di scartarle —
  non entrano mai in considerazione per la scoperta attiva anche se il
  loro nome placeholder combaciasse per caso con una voce
  dell'allowlist, dato che nessun utente può aver autorizzato
  consapevolmente "la rete senza nome all'indirizzo X" scrivendone il
  nome.

### Fase 13: scan BLE passivo, evil-twin, audio audit, STATS, backup config, baseline dispositivi, signal finder

Sette migliorie scelte dall'utente da una lista di dieci proposte di
questo assistente ("Implementa 1, 2, 4, 5, 7, 8 e 10"):

- **Scan BLE passivo — implementato, poi rimosso in Fase 14** (vedi la
  sezione "Fase 14" più avanti per il perché). Per la cronaca: era un
  modulo `scan/BleScanManager.h/.cpp` gemello passivo di
  `WardrivingManager` (mai pairing/connessione, solo ascolto degli
  advertisement), che ha prodotto la sequenza di debug reale più densa
  di tutto il progetto — due build consecutive su hardware reale hanno
  trovato tre bug API veri nella libreria BLE "classica" di
  arduino-esp32, tutti diversi da quanto verificabile in anticipo dai
  soli sorgenti pubblici (`BLEScan::start()` che ritornava
  `BLEScanResults` per valore invece che per puntatore;
  `BLEAddress::toString()`/`BLEAdvertisedDevice::getName()` che
  ritornavano `std::string` invece di Arduino `String`; un `#include
  <Arduino.h>` mancante) — e poi, una volta risolti tutti e tre, un
  quarto problema non di compilazione ma di dimensione: la libreria BLE
  da sola ha fatto sforare gli slot OTA da 1,6 MB (vedi il bullet
  dedicato più sotto). Storia completa nel git log di questa fase.
- **Rilevamento evil-twin / AP sospetti** (`WardrivingManager`): durante
  la scansione passiva, se lo stesso SSID compare con un livello di
  cifratura diverso su due BSSID distinti, entrambe le voci vengono
  marcate `suspicious` (con nota) — euristica volutamente conservativa
  (niente fuzzy-matching sul vendor, scartato come troppo soggetto a
  falsi positivi) che **non** prova a stabilire quale dei due sia
  quello legittimo, perché non è deducibile dal solo ordine di
  scoperta. In `WAR DRIVING` le voci sospette diventano rosse (priorità
  massima nello stacking colore, sopra `discovered`/`open`) con
  etichetta `!EVIL`, il contatore Idle/Running mostra `evil:N` in
  rosso quando >0, e ogni nuova rilevazione suona lo stesso allarme a
  due toni discendenti dell'audit credenziali (sotto) — stessa fascia
  di urgenza, un possibile evil-twin è un finding paragonabile a una
  credenziale di default funzionante.
- **Audio su audit credenziali riuscito** (`ui/Sound::playCredAlert`,
  chiamato da `CredAuditManager::run()` appena una coppia
  utente/password risulta valida): due toni discendenti (1400Hz poi
  600Hz, con una breve pausa nel mezzo) — deliberatamente più duro del
  singolo beep già esistente per un AP aperto trovato in war driving,
  perché una credenziale confermata funzionante è un finding più forte
  e azionabile. Come tutto l'audio, gated da
  `AppConfig::uiSoundEnabled`.
- **Schermata STATS** (`ui/screens/StatsScreen`, raggiungibile con `S`
  da `SCAN HISTORY`): grafico a barre dell'andamento host-trovati nel
  tempo su tutti gli snapshot in `ScanHistory`, barre rosse per gli
  scan in cui era presente almeno un host a rischio Critical, contorno
  magenta sulla barra più recente; sopra il grafico, conteggio scan/
  media host/andamento (↑/↓/= rispetto allo scan precedente) in testo.
  Nessuna legenda testuale separata per il colore rosso — si appoggia
  alla stessa convenzione cromatica già stabilita altrove nell'app
  (rosso = critical), per stare nel budget di spazio del footer.
- **Backup/restore impostazioni su SD** (`storage/ConfigBackup.h/.cpp`,
  tasti `B`/`R` in `SETTINGS`): esporta/reimporta `AppConfig`, le reti
  WiFi salvate (**incluse le password**, in chiaro nel JSON — scelta
  deliberata: un backup che richiedesse di reinserire le password a
  mano non servirebbe al suo scopo, che è sopravvivere a un
  `pio run -t erase` a chip pieno) e l'allowlist di war driving.
  Richiede esplicitamente una SD (mai LittleFS, che l'erase cancella
  insieme all'NVS che si vorrebbe recuperare) — vedi il commento in
  cima a `ConfigBackup.h`. Il restore ripristina l'ordine MRU delle
  reti salvate reinserendole in ordine inverso attraverso la stessa
  `WifiManager::saveCredentials()` già esistente, senza bisogno di
  nessuna nuova API a basso livello.
- **Baseline "dispositivi noti" per rete** (`storage/ScanHistory` +
  `HostListScreen`): ogni snapshot di cronologia ora porta anche il
  nome della rete WiFi corrente (`g_wifi.currentSsid()`), cosa che
  permette a `ScanHistory::loadKnownMacs()` di raccogliere i MAC visti
  in passato **sulla stessa rete** (non una baseline globale — un host
  mai visto sulla rete di casa ma noto su quella dell'ufficio deve
  comunque poter essere segnalato come nuovo). A fine scan,
  `HostListScreen` confronta gli host trovati contro questa baseline
  *prima* di salvare il nuovo snapshot e marca in rosso (priorità
  massima, sopra il magenta di "nuovo in questo scan") ogni host il
  cui MAC non risulta mai visto prima su questa rete; lo stat-strip
  compatto mostra `+N` (nuovi in questo scan, magenta) e `!N` (mai
  visti prima su questa rete, rosso).
- **Indicatore di potenza segnale RSSI** (`ui/screens/SignalFinderScreen`,
  raggiungibile con `Tab` da una riga selezionata in `WAR DRIVING`):
  barra RSSI live per localizzare fisicamente un AP camminando e
  osservandola muoversi, con testo `GETTING CLOSER`/`GETTING FARTHER`
  quando il segnale cambia tra una lettura e la successiva. Guida un
  proprio ciclo di scan continuo riarmando `WifiManager::beginScan()`
  ogni volta che uno scan finisce, invece di leggere le sighting di
  `WardrivingManager` (la cui cadenza di ~15s è troppo lenta per "cammina
  e guarda cambiare in tempo reale") — stessa API di basso livello già
  usata da `WifiSetupScreen`. Come ogni altra schermata che guida un suo
  scan, usarla insieme a `WAR DRIVING`/`WIFI SCAN` fa competere le due
  per l'unica radio disponibile — stessa limitazione già accettata
  altrove, vedi sotto.
- **Bug reale trovato al primo `pio run -t upload` di questa fase — slot
  OTA troppo piccoli**: dopo aver corretto i tre bug di compilazione di
  `BleScanManager.cpp` sopra, il link è andato a buon fine ma
  `checkprogsize` ha fallito: l'immagine compilata occupava 1.857.937
  byte contro un tetto di 1.638.400 per slot (113,4%, 219.537 byte oltre
  il limite) — un fallimento sicuro e visibile ("app image is too big"),
  non un device brickato, ma comunque bloccante. Il margine dell'88,6%
  osservato alla Fase 10 (vedi sopra) non è bastato: BLE da solo tira
  dentro l'intera libreria "classica" arduino-esp32 (`liba1d/BLE`, ~25
  file `.cpp`), di gran lunga la libreria più pesante linkata finora,
  sommata alle altre sei migliorie di questa fase. Corretto allargando
  `ota_0`/`ota_1` da 1,6 MB a 2,25 MB ciascuno (e restringendo `spiffs`
  da 4,69 MB a 3,375 MB di pari passo — i soli asset statici su
  LittleFS, DB OUI/porte + wordlist, pesano circa 1,5 MB, quindi c'è
  ancora ampio margine lì) — vedi i commenti aggiornati in
  `partitions.csv` per la tabella offset completa. Non ancora
  riconfermato da una build/flash completa dopo la correzione.

### Fase 14: rimozione dello scan BLE (risparmio spazio flash)

Richiesta esplicita dell'utente: **"Elimina la funzionalità Scan BLE
passivo per risparmiare un po' di spazio"**, dopo che la Fase 13 aveva
già dovuto allargare gli slot OTA proprio a causa del peso di quella
libreria (vedi il bullet sopra). Rimossi `scan/BleScanManager.h/.cpp`,
`ui/screens/BleScanScreen.h/.cpp`, il tasto `B` (e la relativa riga nel
footer) da `WAR DRIVING`, la voce `Ble` da `ScanSource` in
`core/EventQueue.h`, e il wiring in `main.cpp`
(`#include`/`g_bleScanManager.begin(...)`) — nessun residuo di codice
o riferimento rimasto, verificato con una ricerca a tappeto della
stringa "BLE"/"Ble" nell'intero albero `src/`. Tutte le altre sei
migliorie della Fase 13 (evil-twin, audio audit, STATS, backup
config, baseline dispositivi, signal finder) restano intatte — erano
indipendenti dal modulo BLE, non ne condividevano codice.

**`partitions.csv` lasciata invariata per ora, deliberatamente**: gli
slot OTA restano a 2,25 MB/`spiffs` a 3,375 MB. Rimuovere BLE (di gran
lunga la libreria più pesante mai linkata in questo firmware, vedi
sopra) libera sicuramente spazio nel binario, ma di quanto esattamente
non è verificabile senza una build reale — e restringere di nuovo gli
slot a un valore indovinato rischierebbe una terza iterazione di
"guess-and-fail" sulla dimensione, esattamente il tipo di rischio che
la Fase 13 ha appena dimostrato essere concreto (non ipotetico) su
questo progetto. Se dopo la prossima build la dimensione del binario
risulta comodamente sotto l'1,6 MB originale, gli slot possono essere
ristretti di nuovo per restituire spazio a `spiffs` — ma quella è una
decisione da prendere con il numero reale in mano, non ora.

### Fase 15: melodia di boot cyberpunk

Richiesta esplicita dell'utente: sostituire il jingle di boot esistente
(un arpeggio ascendente di 4 note in maggiore, circa mezzo secondo) con
qualcosa di più marcatamente "cyberpunk". `sound::playBootJingle()`
resta la stessa funzione/stesso punto di innesco (suona esattamente
quando il boot log lascia il posto alla vista NETRUNNER, vedi
`BootScreen.cpp`) — è cambiato solo il contenuto di `kBootJingle` in
`ui/Sound.cpp`: un riff synthwave di 13 note, ~1,5s totali, in tre
parti — un pulse ripetuto tonica/quinta (A2/E3, l'"idle da motore"
tipico dei synth intro darksynth/Outrun), un arpeggio ascendente in *la
minore* con settima minore (G naturale, non G# come sarebbe in
maggiore, per un colore più cupo), e una risoluzione discendente con
una nota di passaggio in stile frigio (F) prima di tornare e tenere la
tonica grave. `M5Cardputer.Speaker` è monofonico (un solo `tone()` per
volta, niente accordi reali), quindi il riff resta una linea singola
sequenziale come il jingle precedente — stesso vincolo tecnico, solo
più elaborato.

Effetto collaterale gestito: essendo la chiamata bloccante (stessa
scelta di design del jingle originale — non c'è nulla che animi sullo
schermo in quell'istante preciso, il prompt lampeggiante non è ancora
mostrato), il vecchio ritardo fisso di 600ms tra titolo e prompt
lampeggiante (`kPromptDelayMs` in `BootScreen.cpp`) era pensato per un
jingle di mezzo secondo e sarebbe stato quasi interamente "mangiato"
dal nuovo riff da 1,5s, facendo comparire il prompt nell'istante esatto
in cui finisce l'ultima nota. Portato a 1700ms per lasciare un piccolo
respiro di silenzio dopo la musica prima che il prompt inizi a
lampeggiare.

> **Aggiornamento (Fase 17)**: `sound::playBootJingle()` descritta qui
> sotto è stata rimossa e sostituita da un loop continuo — vedi la
> sezione "Fase 17" più avanti. Il riff a 13 note resta comunque un
> pezzo di storia utile: la Fase 17 lo riusa concettualmente come base
> ritmica del nuovo loop.

### Fase 16: strumenti offensive per reti locali

> ⚠️ **Cambio di categoria, non solo di scope**: tutto quello che questo
> firmware aveva fatto fino alla Fase 15 — anche l'audit credenziali
> "vero" della Fase 4 — agiva solo su servizi/host già scoperti dallo
> stesso device che li scopre. Gli otto strumenti di questa fase sono
> diversi nella sostanza: **agiscono su dispositivi di terze parti**
> (avvelenamento ARP, deauth, un access point che imita una rete reale),
> non solo su "quello che questo device stesso trova". Restano dietro lo
> stesso principio guida di tutto il progetto — solo su reti/dispositivi
> di tua proprietà o per cui hai autorizzazione esplicita — ma dietro un
> gate ancora più severo (`OffensiveDisclaimerScreen`: bisogna scrivere
> per intero la parola AUTHORIZED, non basta premere un tasto), condiviso
> dai tre strumenti realmente attivi (MITM/ARP spoof, deauth, evil twin).
> Vedi "Limiti noti" per il riepilogo dei tagli di scope deliberati che
> tengono ciascuno di questi il più contenuto possibile.

- **ARP spoofing / MITM controllato** (`scan/ArpSpoofManager`,
  schermata `MITM AUDIT` da `HOST DETAIL` con `M`): avvelena la cache
  ARP di **un solo host esplicito** (mai l'intera subnet), spacciandosi
  per il gateway, per un tempo limitato (max 10 minuti) con ripristino
  automatico garantito all'uscita. **Deliberatamente monodirezionale e
  senza alcun relay/forwarding**: non avvelena mai la cache del gateway
  sul target, e non inoltra da nessuna parte il traffico che intercetta
  — relayare correttamente pacchetti IP arbitrari (riscrivere la
  destinazione L2 di ogni frame catturato e reiniettarlo senza mai
  duplicarlo, perderlo o corromperlo) è un problema di sistema enorme e
  facile da sbagliare, e sbagliarlo non vuol dire "non compila": vuol
  dire rompere silenziosamente la connettività reale del target per
  tutta la sessione, trasformando un audit in un DoS non voluto. Senza
  relay: su una rete **cifrata (WPA2/3)** questo strumento prova se
  l'avvelenamento ha successo (la rete ha Dynamic ARP Inspection o
  equivalente?) ma non può leggere il contenuto del traffico; su una
  rete **aperta** lo sniffing passivo (vedi sotto) vede già tutto in
  chiaro senza bisogno di alcuno spoofing.
- **Session/cookie sniffing passivo durante il MITM**: parte della
  stessa classe, attivabile/disattivabile all'avvio della sessione.
  Cattura in modalità promiscua i frame 802.11 rilevanti e cerca al
  loro interno (con una ricerca a sottostringa sull'intero frame
  catturato, non un parsing preciso del payload TCP — vedi sotto per il
  perché) pattern come `Authorization: Basic`, `Cookie:`, `USER `/
  `PASS ` FTP/Telnet. I frame cifrati (bit Protected della 802.11
  header) vengono riconosciuti e **mai** passati a questa ricerca — il
  contenuto sarebbe comunque solo rumore illeggibile.
- **DNS spoofing locale**: piccola lista hostname→IP forgiata (fino a 5
  voci, gestibile dalla schermata MITM con `D`) — mentre una query DNS
  del target passa per lo sniffing, se il nome corrisponde viene
  costruita e inviata una risposta DNS forgiata invece di lasciarla
  proseguire verso il vero server.
- **Evil twin attivo** (`scan/EvilTwinManager`, schermata `EVIL TWIN`
  raggiungibile con `E` da una sighting di `WAR DRIVING`): access point
  con lo stesso SSID di una rete bersaglio (modalità AP+STA
  concorrente, quindi la connessione WiFi propria del device sopravvive
  — con il limite hardware che il canale finisce comunque per essere
  quello della propria connessione STA, se ce n'è una attiva, non
  necessariamente quello richiesto: un solo radio non può stare su due
  canali insieme). Logga solo indirizzo MAC + timestamp di ogni
  dispositivo che si associa, **sempre aperto** (nessuna password, a
  prescindere dalla cifratura reale della rete clonata — non potendo
  conoscere una passphrase WPA2 vera, il test interessante è comunque
  lo stesso: un client si riconnette a una rete con lo stesso nome ma
  sicurezza più debole, senza che l'utente se ne accorga?). **Nessun
  captive portal, nessuna richiesta o elaborazione di credenziali.**
- **Deauth mirato + cattura handshake WPA** (`scan/DeauthManager`,
  schermata `DEAUTH + CAPTURE` da una sighting con `X`): la tecnica più
  dirompente di tutto il firmware — a differenza di ogni altra cosa qui
  dentro, un frame di deauth interrompe la connessione di un client
  reale nell'istante stesso in cui arriva, senza che quel client possa
  in alcun modo evitarlo. Per questo la finestra di manovra è tagliata
  al minimo indispensabile: **un solo client MAC esplicito** (mai
  broadcast/"tutti i client"), **una raffica fissa piccola** (4 frame,
  mai un loop o una modalità ripeti-ogni-N-secondi — rilanciare richiede
  di riavviare la sessione a mano), poi una finestra di cattura limitata
  (10s) che scrive ogni frame 802.11 rilevante in un file `.pcap`
  standard su SD (formato pcap classico, `LINKTYPE_IEEE802_11`) **senza
  mai tentare di interpretare o craccare l'handshake su questo
  device** — l'analisi (Wireshark/aircrack-ng/hashcat) resta
  deliberatamente offline, su un PC.
- **Mini path brute-forcer HTTP stile dirb** (`scan/HttpPathBruteforcer`,
  schermata `HTTP PATH BRUTE` da `HOST DETAIL` con `H`, solo se una
  porta HTTP è già stata scoperta): dizionario fisso di path comuni
  (`/admin`, `/.git/config`, `/.env`, `/backup`, `/phpmyadmin`, ecc.),
  una GET alla volta, rate-limited come ogni altro probe di questo
  firmware. Pura enumerazione — nessun invio di dati, nessun tentativo
  di sfruttare quello che trova, solo "questo path esiste?".
- **Credential guessing esteso a POP3/IMAP/SMTP**
  (`CredAuditManager::tryPop3Login`/`tryImapLogin`/`trySmtpLogin`):
  stesso motore/wordlist della Fase 4, tre protocolli testuali in più
  con codici di risposta deterministici (RFC 1939/3501/4954), stesso
  stile di `tryFtpLogin` più che di `tryTelnetLogin` euristico. **SSH
  deliberatamente non aggiunto** — un vero login SSH richiede un
  handshake client completo (key exchange, gestione della host key,
  negoziazione della cifratura simmetrica): implementarlo da zero senza
  possibilità di testarlo su hardware reale è un rischio di correttezza
  E di sicurezza che questo progetto non si prende. La via giusta, se
  servisse in futuro, è una libreria client SSH di terze parti
  verificata come vera dipendenza (`lib_deps`), non primitive scritte
  da zero qui.
- **RISK — di gran lunga il codice meno verificato di tutto il
  progetto, oltre il livello già visto con BLE**: `ArpSpoofManager` e
  `DeauthManager` fanno parsing manuale di frame 802.11 grezzi (frame
  control, campi indirizzo dipendenti da ToDS/FromDS, QoS control
  opzionale, LLC/SNAP, IPv4, TCP/UDP) byte per byte, senza alcuna
  possibilità di testarlo contro traffico reale catturato prima di una
  build vera. Ogni passaggio di parsing è scritto per fallire in modo
  sicuro (controlli sui limiti, salta silenziosamente qualunque cosa
  non corrisponda alla forma attesa) invece di assumere il successo —
  così un bug di parsing degrada a "questo frame viene ignorato", mai a
  dati corrotti agiti o ritrasmessi. Usano solo API WiFi esp-idf
  pubbliche e documentate (`esp_wifi_set_promiscuous`/
  `esp_wifi_set_promiscuous_rx_cb`, `wifi_promiscuous_pkt_t`,
  `esp_wifi_ap_get_sta_list`) tranne `esp_wifi_80211_tx` (iniezione di
  frame management per il deauth), dichiarata in `esp_wifi.h` su alcune
  versioni del core e nell'header "privato" `esp_private/wifi.h` su
  altre — gestito con una guardia `__has_include` invece di assumere
  quale dei due sia quello giusto per la build reale di questo
  progetto. Vedi i commenti in cima a `ArpSpoofManager.h`/
  `DeauthManager.h` per il dettaglio completo.
- **Aggiornamento — due bug reali trovati dalla prima build vera**:
  `esp_wifi_set_promiscuous_rx_cb()` richiede una callback con la firma
  ESATTA `void(*)(void*, wifi_promiscuous_pkt_type_t)` — `int` al posto
  del tipo enum reale (quello scritto inizialmente, per non introdurre
  `esp_wifi.h` negli header) non converte implicitamente, in entrambi
  `ArpSpoofManager.cpp` e `DeauthManager.cpp`. Risolto includendo
  `esp_wifi.h` anche negli header (serve il tipo dell'enum nella firma
  dichiarata) e usando `wifi_promiscuous_pkt_type_t` per davvero. Questo
  ha anche fatto emergere un secondo bug, silente, sotto il primo: il
  filtro sul tipo di pacchetto usava un numero magico indovinato
  (`WIFI_PKT_DATA == 3`), ma l'ordine reale nell'enum di esp-idf mette
  `WIFI_PKT_DATA` a 2 — sostituito con le costanti nominali dell'enum
  invece di un altro numero indovinato, per non ripetere lo stesso
  errore. `EvilTwinManager.h` aveva anche lui il bug ormai familiare di
  `String` non risolvibile (mancava `#include <Arduino.h>` — a
  differenza di `ArpSpoofManager.h`/`DeauthManager.h`/
  `HttpPathBruteforcer.h`, che lo ottengono gratis da `<IPAddress.h>`,
  questo header non aveva bisogno di indirizzi IP e quindi non lo
  includeva).

### Fase 17: musica di boot in loop + nebbia digitale

Due richieste sullo splash screen: musica continua invece di un singolo
sting, e un effetto di nebbia/statica leggera sullo sfondo.

- **Musica in loop stile "Nightcall" di Kavinsky, non una trascrizione
  del brano vero**: `sound::playBootJingle()` della Fase 15 (~1,5s, una
  volta sola, bloccante) è stata **rimossa interamente** — nessun
  residuo, non aveva senso tenerla come funzione morta una volta che
  BootScreen non la chiama più — e sostituita da
  `sound::startBootLoop()`/`stopBootLoop()`: un loop di circa 4s (pulse
  di basso arpeggiato D2/A2/D3, poi un hook melodico più lento in re
  minore naturale, poi una risoluzione grave tenuta) su un proprio task
  FreeRTOS in background, che si ripete finché non si preme ENTER per
  entrare in `MAIN MENU` (fermato da `BootScreen::onExit()`, che ora
  esiste apposta). **Non è una trascrizione del brano reale**: sia per
  motivi tecnici (`M5Cardputer.Speaker` è monofonico — un solo `tone()`
  alla volta, non può suonare basso+melodia+voce contemporaneamente
  come fa il brano vero) sia deliberatamente (riprodurre nota per nota
  una melodia di qualcun altro non è quello che fa questo progetto) —
  è una composizione originale che ne evoca l'atmosfera (synthwave
  cupo, tonalità minore, pulse di basso arpeggiato). Essendo
  `startBootLoop()` non bloccante (avvia il task e ritorna subito), il
  ritardo fisso prima del prompt lampeggiante (`kPromptDelayMs` in
  `BootScreen.cpp`) è tornato al valore originale di 600ms della Fase
  12 — il valore allargato a 1700ms nella Fase 15 esisteva solo per
  compensare il vecchio sting bloccante, che non c'è più.
- **Nebbia digitale puntinata** (`chrome::drawDigitalFog`, nuova
  funzione condivisa in `ui/Chrome.h/.cpp`, chiamata solo sulla vista
  brandizzata dello splash): punti singoli, sparsi e fiochi
  (`theme::GREEN_DIM`) sopra l'intero schermo, ridisegnati su un
  intervallo di ~150ms (non ad ogni frame — un reshuffle completo ad
  ogni frame leggerebbe come statica, non come nebbia che deriva) così
  da dare un effetto di deriva/shimmer leggero. Disegnata per prima,
  prima di titolo/sottotitolo/versione: ogni cella di carattere che
  questa UI stampa riempie il proprio sfondo, quindi il testo sopra
  cancella pulitamente qualunque puntino ci finisca sotto, senza dover
  calcolare a mano una zona "sicura" da evitare.

**Ritocco successivo, su feedback dell'utente dopo il primo test su
hardware reale** ("riduci la velocità della nebbia digitale, alza un
po' il volume, fai la musica più cyberpunk e rallenta il tempo"):

- **Nebbia più lenta**: l'intervallo di reseed di `drawDigitalFog` è
  passato da ~150ms a ~400ms — stessa densità di puntini, deriva
  visibilmente più lenta/leggera.
- **Volume più alto**: `M5Cardputer.Speaker.setVolume(180)` (alzato dal
  default di M5Unified, intorno a 128/255 — un primo tentativo a 220 è
  risultato troppo alto su hardware reale, assestato a 180) chiamato una
  volta dentro `startBootLoop()`. È un'impostazione globale del device,
  non per singolo `tone()` — quindi alza anche il volume di
  `playAlert()`/
  `playCredAlert()`, non solo del loop di boot; non c'è un reset al
  volume precedente quando il loop si ferma, dato che un allarme
  leggermente più udibile è un effetto collaterale ragionevole, non un
  problema da evitare.
- **Musica più cupa/"cyberpunk" e più lenta**: durata di ogni nota
  allungata su tutto il loop (da ~4s a ~6,5s per giro), e l'hook
  melodico ora passa per un **tritono** (Ab3, la classica "quinta
  diminuita" contro la tonica di re minore — il colore dissonante e
  minaccioso tipico del synthwave più cupo) prima di risolvere verso
  l'alto, più un breve stab di lead un'ottava sopra (D5/A4) per
  contrasto rispetto al resto, deliberatamente basso e lento.

**Secondo ritocco: melodia sostituita con uno spartito fornito
dall'utente**. A differenza della composizione originale "in stile
Nightcall" descritta sopra, questa volta l'utente ha allegato uno
spartito pianistico reale e ha confermato esplicitamente che è una
composizione propria (o comunque di cui ha i diritti) — solo a quel
punto, coerentemente con quanto scritto sopra sul non riprodurre
melodie di terzi senza autorizzazione, è stata trascritta. `kBootLoop`
in `ui/Sound.cpp` ora è un **arrangiamento a voce sola** delle prime
quattro battute dello spartito (progressione La minore-Fa-Do-Sol, la
classica "i-VI-III-VII"): per ogni battuta, la nota di basso della mano
sinistra (tenuta), poi l'arpeggio della mano destra letto dallo
spartito dal basso all'acuto, poi la sua nota più alta tenuta un po' di
più come picco melodico — dato che `M5Cardputer.Speaker` suona una sola
nota alla volta, le due mani del pianoforte sono state condensate in
un'unica linea, non una trascrizione letterale di una mano sola. Tempo
allineato a quello indicato sullo spartito (♩ = 89). Loop di ~13s,
letto da una foto dello spartito e non passato per OCR — la fedeltà
esatta nota per nota non è garantita, e correzioni su note specifiche
che suonano sbagliate rispetto all'originale sono benvenute, stesso
principio del "la build reale è l'unica verifica" usato per tutto il
resto del firmware.

**Terzo ritocco: di nuovo una composizione originale, ma con "sentori
di Nightcall"**. Richiesta esplicita dell'utente dopo aver sentito la
trascrizione: tornare a una composizione originale (non una
trascrizione di niente) che però evochi l'atmosfera di Nightcall —
esattamente il criterio della primissima versione della Fase 17, non
un'eccezione ad esso. `kBootLoop` riparte dalla progressione La minore-
Fa-Do-Sol dello spartito dell'utente (armonia libera da riutilizzare
sia perché autorizzata sia perché è una progressione talmente comune —
la base armonica di innumerevoli canzoni pop — che non appartiene a
nessuno), ma tutto il resto è nuovo: un ostinato di basso pulsante
tonica/quinta sotto ogni accordo (la caratteristica più riconoscibile
del sound di quel brano), un hook melodico lento e malinconico in
tonalità minore sopra, e un outro discendente semicromatico e nostalgico
che torna alla tonica grave prima che il loop ricominci — l'atmosfera
evocata attraverso la tecnica compositiva, non la melodia vera e propria
citata. Loop di ~10s.

### Fase 18: discovery passiva, negotiate SMB e report kill-chain

Sei funzionalità nuove orientate a **discovery** e a esporre la superficie
d'attacco, tenute deliberatamente sul lato *difensivo/benigno*: su
richiesta esplicita dell'utente per questo batch — *"non implementare
funzionalità rischiose"* — nessuna di queste agisce su dispositivi di
terze parti come fanno gli strumenti della Fase 16. Ascoltano, interrogano
in modo standard, o rielaborano dati già raccolti. L'unica funzione
offensiva richiesta in origine per questo giro (la cattura PMKID) è stata
mantenuta ma resta dietro lo stesso gate `OffensiveDisclaimerScreen` degli
strumenti Fase 16, non tra le voci qui sotto.

- **Cattura PMKID senza deauth** (`scan/PmkidManager`, schermata
  `PMKID CAPTURE` da una sighting di `WAR DRIVING` con `P`, dietro il gate
  offensive): l'alternativa **più gentile** al deauth della Fase 16. Si
  associa all'AP bersaglio con una password volutamente sbagliata; molti
  AP WPA2-PSK mandano il PMKID nel primo messaggio EAPOL *prima* di
  validare la password. Quel traffico grezzo viene catturato in modalità
  promiscua in un `.pcap` (stesso formato/`storage/PcapWriter` condiviso
  con il deauth) da craccare offline (hashcat mode 22000). **Non manda mai
  un frame di deauth**: nessun client di terze parti viene disconnesso —
  è proprio per questo che l'abbiamo scelta, essendo la dirompenza del
  deauth già la cosa più invasiva del firmware. È gated perché l'*intento*
  (raccogliere materiale WPA per il cracking offline) è lo stesso, a
  prescindere dalla gentilezza della tecnica.
- **Sniffing passivo CDP/LLDP** (`scan/CdpLldpSniffer`, schermata
  `LAN TOPOLOGY` dal menu principale): ascolta in modalità promiscua gli
  annunci che switch e router mandano periodicamente — CDP (Cisco,
  incapsulamento SNAP con OUI `00:00:0C`) e LLDP (standard, ethertype
  `0x88CC`) — e ricostruisce i vicini di rete (device ID + port ID) senza
  inviare **nulla**. Puramente passivo. **Limite reale**: si vede qualcosa
  solo se l'AP fa da bridge dei MAC multicast di CDP/LLDP sul segmento
  wireless (molti non lo fanno) e solo su reti **aperte** (i frame WPA
  cifrati non sono leggibili) — documentato in cima a `CdpLldpSniffer.h`.
- **Discovery attiva UPnP/SSDP** (`scan/SsdpDiscovery`, schermata
  `UPNP DISCOVERY` dal menu): manda una M-SEARCH standard al gruppo
  multicast `239.255.255.250:1900` e ascolta le risposte unicast — smart
  TV, NAS, router, IoT si annunciano da soli. È **UDP normale** su un
  `WiFiUDP`, niente promiscua, niente parsing di frame grezzi: rischio
  basso, stessa categoria non-invasiva del `NETWORK SCAN`. Le risposte
  sono testo HTTP-like (header `SERVER:`/`LOCATION:`/`USN:`), niente
  formato binario da sbagliare.
- **Rilevamento passivo di rogue DHCP** (`scan/RogueDhcpDetector`,
  schermata `ROGUE DHCP` dal menu): ascolta i `DHCPOFFER`/`DHCPACK` sul
  filo e segnala **in rosso** ogni server DHCP il cui IP è diverso dal
  gateway che questo device sta effettivamente usando — il classico segno
  di un secondo server DHCP che corre contro quello legittimo (spesso per
  spacciarsi come gateway/DNS in un MITM). È **difensivo**: non risponde
  mai al DHCP, non fa niente oltre a segnalare. Riusa il parsing 802.11
  condiviso (`net/Ieee80211Frame`) e un piccolo parser IPv4/UDP/BOOTP
  self-contained (RFC 2131, `op`/`yiaddr`/magic cookie/opzione 53), tutto
  bounds-checked. Stesso limite "solo reti aperte" della famiglia
  promiscua.
- **Check SMB1 Negotiate** (`scan/SmbNegotiateCheck`, schermata
  `SMB NEGOTIATE` da `HOST DETAIL` con `S`, mostrata solo se una porta
  smb/netbios è aperta): manda **un solo** `SMB_COM_NEGOTIATE` — lo stesso
  primo messaggio che manda qualunque client Windows/Samba — e legge il
  *Security Mode* annunciato dal server: sicurezza a livello utente vs
  l'antico share-level, password in chiaro vs challenge/response, SMB
  signing. **Scope volutamente ridotto**: NON è enumerazione di share o
  utenti — niente Session Setup, niente login a credenziali nulle, niente
  Tree Connect o NetShareEnum/DCE-RPC (lavoro di protocollo giudicato di
  rischio comparabile al login SSH che avevamo declinato in Fase 16). È
  l'equivalente SMB di un banner grab, niente di più intrusivo.
- **Report HTML "kill chain"** (`storage/ReportGenerator`, tasto `R` su
  `NETWORK SCAN` accanto all'export `E`): genera su SD un singolo file
  `report.html` autocontenuto (CSS inline, nessuna risorsa esterna, si
  apre offline) con stile cyberpunk coerente con l'UI del device. Riporta
  un riepilogo, una sezione **ATTACK SURFACE** che ordina i finding più
  interessanti (credenziali deboli → servizi in chiaro telnet/ftp → banner
  con firma vulnerabile nota → SMB esposto), e l'inventario completo degli
  host. Attinge dalla tabella host live di `ScanManager` più il contesto
  WiFi corrente, e **referenzia** (senza ri-aggregarli) gli artifact che
  gli altri moduli scrivono già sulla card (`export.json/.csv`,
  `wardrive.csv`, `eviltwin/associations.csv`, `handshakes/`), elencando
  solo quelli effettivamente presenti.

- **Refactor condiviso** (a supporto di questo batch): la parte di parsing
  più a rischio, già isolata in Fase 16, è ora fattorizzata in due moduli
  condivisi — `net/Ieee80211Frame` (`parseDataFrame`/`parseSnap`, estratti
  verbatim da `ArpSpoofManager`) e `storage/PcapWriter`
  (`writeGlobalHeader`/`writeRecord`, estratti da `DeauthManager`) — così
  i cinque consumatori promiscui (ArpSpoof, Deauth, PMKID, CDP/LLDP, rogue
  DHCP) verificano quel codice difficile una volta sola, in un posto solo,
  invece di riderivarlo ciascuno (era esattamente lì che la prima build
  vera aveva trovato il bug del numero magico `WIFI_PKT_DATA`). Il parsing
  IPv4/UDP di basso rischio è invece rimasto duplicato di proposito tra
  `ArpSpoofManager` (verificato su hardware) e `RogueDhcpDetector`: ~20
  righe ben capite, duplicarle è più sicuro che rischiare una regressione
  nel percorso già testato per amore del DRY.

> **Radio condivisa — il limite si allarga a cinque**: `esp_wifi` accetta
> **una sola** callback promiscua alla volta. Far girare
> contemporaneamente due delle funzioni che la usano — ARP/MITM, deauth,
> PMKID, CDP/LLDP, rogue DHCP — fa sì che l'ultima avviata "rubi" i frame
> alle altre, silenziosamente. È lo stesso limite di radio condivisa già
> documentato per WAR DRIVING vs NETWORK SCAN: accettato, non un bug. Usa
> una funzione promiscua alla volta.

### Fase 19: UX/discovery — status bar radio, dashboard THREATS, auto-assess, host passivi, servizi mDNS, SNMP

Sei migliorie scelte dall'utente (1, 2, 5, 6, 7, 8 di una lista di dieci),
tutte sul lato difensivo/benigno. Il punto 10 della lista (Responder-lite
/ cattura NetNTLM), segnalato come ad alto rischio, è stato **escluso su
istruzione esplicita dell'utente**.

- **Barra di stato radio globale** (`ui/ActivityStatus`, agganciata a
  `chrome::drawHeader` → compare su ogni schermata): rende visibile l'unico
  pezzo di stato globale che l'UI prima nascondeva — **quale funzione
  promiscua possiede la radio**. `esp_wifi` accetta una sola callback
  promiscua alla volta, quindi l'header ora mostra `RF:<nome>` (ambra)
  quando una è attiva e `RF:N!` (rosso) quando due o più lo sono insieme
  (il conflitto). Trasforma il footgun documentato in un avviso a colpo
  d'occhio. Tenuto fuori da `Chrome.cpp` (che altrimenti dovrebbe includere
  tutti i manager) in un modulo dedicato.
- **Dashboard THREATS** (`ui/screens/ThreatsScreen`, voce di menu
  `THREATS`): una vista unica che **aggrega live** i finding sparsi tra i
  moduli — credenziali deboli e banner con firma vulnerabile (dalla tabella
  host di `ScanManager`), servizi in chiaro (telnet/ftp), e server rogue
  DHCP sospetti (da `RogueDhcpDetector`). Read-only, ri-deriva la lista dai
  dati vivi a ogni frame: è il corrispettivo on-device della sezione
  ATTACK SURFACE del report HTML, senza dover passare da SD.
- **Auto-assess a un tasto** (`scan/AssessmentRunner` +
  `ui/screens/AssessmentScreen`, voce `AUTO ASSESS`): concatena i passi
  *non-gated* di un assessment di base — discovery → port scan di ogni host
  vivo a turno → report HTML su SD — con barra di avanzamento a fasi.
  **Guida** soltanto i manager esistenti via le loro API pubbliche
  (aspetta i loro `isRunning()`), non re-implementa niente. L'**audit
  credenziali è deliberatamente escluso** dalla catena: sta dietro un gate
  di autorizzazione per-sessione e automatizzarlo su ogni host aggirerebbe
  il modello di consenso esplicito del resto del firmware — l'utente lo
  lancia a mano per host.
- **Scoperta host passiva** (`scan/PassiveHostDiscovery` +
  `ui/screens/PassiveHostScreen`, voce `PASSIVE HOSTS`): impara quali host
  esistono **solo ascoltando** il traffico in modalità promiscua — ogni
  frame dati IPv4 dà una coppia (MAC, IP sorgente). Un host che non
  risponde mai a un probe attivo (firewallato, o addormentato durante lo
  sweep) compare comunque appena trasmette qualcosa. Lista propria con
  schermata dedicata, **non** iniettata nella tabella di `ScanManager`
  (la cui logica di generazione è tarata sullo sweep attivo — mescolarci
  righe da un listener in background sarebbe rischioso). Aggiunge un sesto
  consumatore promiscuo, riflesso nella nuova barra di stato radio.
- **Enumerazione servizi mDNS/DNS-SD** (`scan/ServiceEnumerator` +
  `ui/screens/ServiceScreen`, voce `SERVICE SCAN`): la meta-query DNS-SD
  standard a due livelli — prima PTR di `_services._dns-sd._udp.local` per
  elencare i **tipi** di servizio annunciati (`_airplay._tcp`, `_ipp._tcp`,
  `_googlecast._tcp`, ...), poi PTR di ogni tipo per le **istanze** con nome
  (e la porta dal record SRV quando presente). Ben più ricca del fallback
  reverse-PTR di `MdnsReverseResolver`. Riusa gli helper verificati di
  `net/DnsWire` e lo stesso pattern multicast. *Scope*: tipo + nome istanza
  (+ porta), senza inseguire i target SRV fino ai record A per l'IP.
- **Sweep SNMP community "public"** (`scan/SnmpSweep` +
  `ui/screens/SnmpScreen`, voce `SNMP SWEEP`): per ogni host vivo manda una
  GET SNMPv2c di `sysDescr.0` con community `public` e, alla risposta,
  registra la descrizione di sistema. Un agent che risponde a `public` è
  una misconfig classica e diffusissima (la community di lettura di default
  espone modello, OS/firmware, interfacce). **Read-only**: solo GET (mai
  SET), solo lo scalare `sysDescr` — l'equivalente SNMP di un banner grab.
  Richiesta e risposta BER/ASN.1 costruite/parsate a mano, tutto
  bounds-checked.

> **La radio condivisa ora vale per sei funzioni**: alla famiglia
> promiscua (ARP/MITM, deauth, PMKID, CDP/LLDP, rogue DHCP) si aggiunge la
> scoperta host passiva. La nuova barra di stato in header serve proprio a
> non farsi sorprendere: se vedi `RF:2!` in rosso, due funzioni si stanno
> rubando i frame a vicenda.

### Fase 20: connessione alle reti aperte dal war driving + rilevamento captive portal

Due richieste, di cui una accolta e una **deliberatamente declinata**.

- **Connettersi a una rete aperta con `c`** (`ui/screens/OpenConnectScreen`,
  raggiungibile con `c` su una sighting **aperta** in `WAR DRIVING`):
  mostra prima una conferma di autorizzazione (stesso spirito
  dell'allowlist e del disclaimer offensive — "solo reti che possiedi o sei
  autorizzato a usare"), poi si unisce alla rete aperta senza password
  (`WifiManager::beginConnectWithCredentials(ssid, "")`), e infine lancia
  il rilevamento captive portal. Funziona solo per gli AP aperti: uno
  protetto non è comunque unibile da qui senza password.
- **Rilevamento captive portal** (`net/CaptivePortalDetector`): lo stesso
  identico controllo che fa ogni sistema operativo dopo essersi unito a una
  rete — una GET HTTP a un endpoint noto che dovrebbe rispondere `204 No
  Content`. Se arriva davvero un 204 → internet aperta, nessun portale; se
  arriva un redirect o un 200 con HTML → **il portale sta intercettando**,
  e viene mostrata la sua presenza (e l'URL, quando il portale ne
  restituisce uno) così che l'utente sappia di dover aprire il browser per
  autenticarsi. Non invasivo, plain HTTP di proposito (è ciò che un portale
  intercetta).

- **Il *bypass* del captive portal NON è implementato — scelta
  deliberata.** Un captive portal è un meccanismo di controllo d'accesso;
  aggirarlo in modo generico ("qualsiasi portale") in un contesto di war
  driving significa ottenere accesso a reti di terzi che non sono tue —
  cioè accesso non autorizzato / furto di servizio. È una categoria diversa
  dagli strumenti offensivi già presenti (deauth, evil twin, PMKID): quelli
  sono gated e inquadrati come test della *tua* rete, mentre "bypassa
  qualsiasi portale trovi" non ha un equivalente "sulla tua rete". Questo
  firmware *identifica* il portale (utile per un audit), ma non tenta di
  eluderlo. Un tester di robustezza del captive portal a **singolo target
  esplicito e autorizzato**, dietro il gate `OFFENSIVE`, sarebbe l'unica
  forma legittima e resta implementabile solo su richiesta esplicita per
  quel caso.

### Fase 21: data-store esposti + audit servizi per-host (DB/VNC/FTP/SMB/HTTP)

Cinque sviluppi offensive scelti dall'utente (1, 2, 4, 5, 8 di una lista
di dieci), per un assessment autorizzato. La crypto necessaria usa
**`mbedtls`** (già su ESP32: SHA1/MD5/DES) — libreria, non primitive
scritte a mano, coerente col principio che aveva escluso SSH.

- **DataStore sweep (item 1)** (`scan/DataStoreProbe`, voce di menu
  `DATASTORE SWEEP`): sweep read-only sugli host vivi che cerca i data
  store più spesso lasciati esposti senza autenticazione — **Redis**
  (6379, `PING`/`INFO`), **Memcached** (11211, `version`),
  **Elasticsearch** (9200, `GET /`), **MongoDB** (27017, `isMaster` +
  `listDatabases`). Segnala in rosso quelli raggiungibili **senza auth**.
  Non gated: è detection read-only, stessa fascia di SNMP `public`.
- **Service audit per-host (item 2/4/5/8)** (`scan/ServiceAuditManager`,
  tasto `V` da `HOST DETAIL`, dietro lo stesso gate del CREDENTIAL AUDIT):
  dato un host già scansionato, guarda le sue porte aperte e lancia i
  check pertinenti, raccogliendo i finding:
  - **FTP (21)** — login **anonimo** + test di scrivibilità (item 5).
  - **SMB (445)** — negotiate + **null session** SMB1 nel path
    non-extended-security (item 5).
  - **Redis (6379)** — no-auth + `AUTH` con password di default (item 2).
  - **MySQL (3306)** — `mysql_native_password` con credenziali di default
    (mbedtls SHA1) (item 2).
  - **PostgreSQL (5432)** — trust/cleartext/MD5 di default (mbedtls MD5);
    SCRAM rilevato e segnalato, non brute-forzato (item 2).
  - **VNC (5900)** — rilevamento **no-auth** (server che offre security
    type "None") (item 4). Il brute della challenge DES è stato tolto: la
    mbedtls di ESP-IDF ha `MBEDTLS_DES_C` disabilitato (DES deprecato),
    quindi `mbedtls_des_*` non linka, e scrivere DES a mano è escluso dal
    principio "niente crypto artigianale".
  - **HTTP (80/8080/8000/8888)** — brute **basic-auth** con credenziali di
    default (item 8).
  Ogni brute usa un **set compatto di credenziali di default** (non l'intera
  wordlist) per tenere bounded gli handshake sui protocolli binari.

- **Scoping deliberato di questo batch** (vedi anche "Limiti noti"):
  **MSSQL** (TDS), **NFS `showmount`** (RPC) e il **`NetShareEnum` SMB
  completo** (DCE-RPC, già declinato in Fase 18) restano fuori.
  **MySQL 8 `caching_sha2_password`** non è gestito (funziona contro
  MySQL 5.7/MariaDB native). **PostgreSQL SCRAM** viene rilevato ma non
  brute-forzato. Il **form-login brute HTTP** è rimandato (basic-auth è
  pieno). Nulla di distruttivo: solo tentativi di login/anon, rate-limited.

### Fase 22: restyle GUI di WIFI SCAN e WAR DRIVING (da mockup)

Restyle delle due schermate su mockup forniti dall'utente, restando dentro
il tema esistente (stessi colori/font, `chrome::drawHeader` invariato per
non divergere dalle altre ~20 schermate).

- **Widget condivisi** (`ui/Chrome`): `drawSignalBars` (meter a 4 barre
  colorato verde/ambra/rosso per RSSI), `drawWifiIcon`, `securityLabel`/
  `securityColor` (OPEN/WEP/WPA/WPA2/WPA2-ENT/WPA3) — così WIFI SCAN e
  WAR DRIVING rendono RSSI e sicurezza in modo identico.
- **WIFI SCAN**: la lista reti è ora una tabella `SSID · CH · RSSI · SEC`
  con intestazioni magenta, icona wifi, chevron di selezione, barre di
  segnale e riga "... N more networks" — tutti dati reali dallo scan.
- **WAR DRIVING**: box di stato `STATUS: RECORDING/STANDBY` + `TIME`
  (timer di sessione lato UI), strip di 4 riquadri e lista AP con le
  stesse barre/sicurezza.
- **Niente GPS inventato**: il mockup War Driving mostrava `GPS 3D FIX`,
  `SPEED` e una mappa stradale con percorso. Il Cardputer ADV **non ha
  GPS** (vedi "Limiti noti"), quindi quei riquadri sono stati sostituiti
  con metriche **reali** (SEEN/OPEN/DISC/EVIL) e al posto della mappa
  geografica c'è la lista reale degli AP. Come per il radar dell'host
  detail, questo firmware non mostra dati sensore fabbricati.

### Fase 23: tutti gli strumenti di discovery raggruppati sotto NETWORK SCAN

Su richiesta ("avere tutte le informazioni di discovery in un unico
punto"): i sette strumenti di discovery che erano voci separate del menu
principale — **LAN TOPOLOGY, UPNP DISCOVERY, SERVICE SCAN, PASSIVE HOSTS,
ROGUE DHCP, SNMP SWEEP, DATASTORE SWEEP** — sono ora dietro un unico
sotto-menu **DISCOVERY** (`ui/screens/DiscoveryMenuScreen`), raggiungibile
con **`D` da NETWORK SCAN**. Il menu principale scende a **7 voci**
(WIFI SCAN, NETWORK SCAN, AUTO ASSESS, THREATS, SCAN HISTORY, WAR DRIVING,
SETTINGS): `PORT SCANNER` e `CREDENTIAL AUDIT` sono stati tolti dal menu
perché erano solo schermate informative (`PlaceholderScreen`) che non
avviavano nulla — sono azioni per-host da HOST DETAIL (`TAB` per il port
scan, `C` per l'audit credenziali). I manager di background sono
sempre avviati in `setup()` come prima — è cambiata solo la navigazione,
non la logica. Ogni strumento gestisce da sé i propri prerequisiti (es.
SNMP/DATASTORE ricordano di lanciare prima un NETWORK SCAN).

### Fase 24: passata UX/UI trasversale (10 aree)

Dieci migliorie di usabilità, fatte in modo **additivo** (nessuna firma
esistente di `chrome::drawHeader`/`Screen` cambiata, così nessuna schermata
si rompe):

1. **Help overlay `?`** — `Screen::helpText()` (virtuale, default vuoto);
   `UiManager` intercetta `?` (fuori dai campi di testo) e mostra un
   pannello con la legenda tasti della schermata; un tasto qualsiasi lo
   chiude. Help scritto per menu, NETWORK SCAN, DISCOVERY, HOST DETAIL.
2. **Header coerente** — le tre schermate di autorizzazione ora usano un
   unico `chrome::drawAlertHeader` (mantiene il rosso "pericolo", ma
   centralizzato) invece di disegnarlo a mano.
3. **Breadcrumb** — `chrome::drawHeader` mostra il genitore di navigazione
   (`Screen::title()`) come prefisso: es. `>> NET/DISCOVERY`,
   `>> DISC/SNMP SWEEP`.
4. **Filtro host list** — `F` cicla il filtro (tutti / risky / con porte);
   l'indicatore compare accanto al conteggio.
5. **Progress standard** — `chrome::drawProgressBar`/`drawSpinner`
   condivisi; applicati a SNMP SWEEP (barra al posto del `%` testuale).
6. **Legenda colori** — legenda del significato dei colori riga sullo
   stato idle di NETWORK SCAN (rosso=never-seen, magenta=new, ecc.).
7. **Empty-state coerente** — `chrome::drawEmptyState` (titolo + hint
   "prossima azione"); applicato a DATASTORE/SNMP SWEEP.
8. **HOST DETAIL: spazio ai dati reali** — il radar decorativo è stato
   rimpicciolito a badge in alto a destra e lo spazio liberato mostra la
   **lista porte aperte** reali.
9. **Text-entry password** — caret lampeggiante, indicatore `[TYPING]`, e
   toggle mostra/nascondi con `TAB`.
10. **Feedback di completamento** — `UiManager` suona un breve blip
    (`sound::playDone`, non bloccante) a ogni evento `ScanFinished`,
    rispettando l'impostazione SOUND.

### Fase 25: "RUN ALL DISCOVERY" nel sotto-menu DISCOVERY

Nuova prima voce del menu DISCOVERY che esegue **tutti** gli strumenti di
discovery con un tasto (`scan/DiscoveryRunner` + `ui/screens/
DiscoveryAllScreen`). **Non è realmente simultaneo, e non può esserlo**:
tre tool (LAN topology, passive hosts, rogue DHCP) condividono l'unica
callback promiscua di `esp_wifi`, quindi avviarli insieme farebbe ricevere
i frame solo all'ultimo. Il runner quindi **sequenzia**:

1. le query one-shot UDP/TCP una dopo l'altra — UPnP/SSDP, servizi mDNS,
   sweep SNMP, sweep data-store;
2. poi ogni listener promiscuo a turno, con una finestra di cattura
   dedicata (~12s l'uno), fermato prima di passare al successivo.

I risultati restano nelle schermate dei singoli strumenti (la lista vicini
di LAN TOPOLOGY, i device UPnP, ecc.); `RUN ALL` mostra solo fase +
avanzamento + log, con `ENTER` start/stop. È lo stesso pattern-orchestratore
di `AUTO ASSESS`: guida i manager esistenti via le loro API pubbliche, non
reimplementa nulla, e rispetta il vincolo della radio condivisa che tutto
il firmware tiene documentato.

### Fase 26: QR handoff, file manager, ricerca, range custom, diagnostica, energia

Sei funzionalità scelte dall'utente (1, 3, 4, 7, 9, 10), additive:

- **#1 Share via QR** (`ui/screens/QrShareScreen`, `Q` da NETWORK SCAN):
  QR con un riepilogo compatto dell'assessment (rete, conteggi host, IP
  critici) renderizzato con `M5GFX::qrcode()` — si scansiona col telefono
  per portarsi via il risultato senza SD.
- **#3 File manager** (`FileManagerScreen`, `F` da SETTINGS): sfoglia
  file/cartelle del filesystem di export (SD o LittleFS), entra nelle
  directory ed elimina file (con conferma).
- **#4 Ricerca** (`SearchScreen`, `S` da NETWORK SCAN): ricerca testuale
  live sugli host correnti per IP/MAC/vendor/hostname; `ENTER` passa a
  navigare i match e aprirne il dettaglio.
- **#7 Range/target custom** (`TargetRangeScreen`, `T` da NETWORK SCAN +
  `ScanManager::setScanRange`): scansiona un `/24` diverso da quello DHCP
  digitando l'IP di base; `C` per tornare alla subnet connessa. (MAC/vendor
  solo sulla subnet locale; altre subnet via ping L3 instradato.)
- **#9 Diagnostica** (`DiagnosticsScreen`, `D` da SETTINGS): self-test di
  SD, WiFi, batteria, **IMU** (BMI270), tastiera, speaker (`S`), uptime,
  RAM libera.
- **#10 Gestione energia**: la `%` batteria in header ora è colorata
  (rosso <15%, ambra <30%), un **alert una-tantum** suona sotto il 15%, e
  un toggle **LOW-POWER** in SETTINGS accorcia il dimming del backlight per
  le sessioni lunghe non presidiate.

Nessuna di queste è una nuova *tipologia di scansione*, quindi niente da
agganciare al `RUN ALL` (la #7 modifica il NETWORK SCAN esistente).

### Fase 27: beacon/probe intelligence + PNL harvesting, mDNS/DNS-SD correlato agli host, RTC a batteria

Tre evoluzioni scelte dall'utente da una lista di dieci proposte in fase
di analisi del progetto:

- **Beacon/Probe intelligence + PNL harvesting client** (`scan/
  BeaconProbeSniffer`, `BEACON/PROBE INTEL` nel sotto-menu DISCOVERY, ora
  anche ultima fase di `RUN ALL DISCOVERY`): sniffer passivo di frame
  MANAGEMENT 802.11 (mai un frame trasmesso da questo dispositivo) che
  fa channel-hopping su tutti i 13 canali 2.4GHz mentre è attivo. Due
  raccolte separate:
  - **AP intel** da Beacon/Probe-Response: SSID, BSSID, canale (dal DS
    Parameter Set IE, o il canale in ascolto se assente), cifratura
    ricostruita dai bit reali (capability info + RSN/vendor-WPA IE, non
    dal riassunto che dà `WiFi.scanNetworks()`), vendor OUI, e un
    **reveal di SSID nascosti**: se un BSSID già visto con SSID vuoto
    (rete "hidden") compare più tardi in un frame con l'SSID valorizzato
    (tipicamente una probe-response a un client che lo chiede per nome),
    viene segnalato — la controprova pratica del perché nascondere
    l'SSID non è mai stata una vera misura di sicurezza.
  - **Client PNL harvesting** da Probe-Request: ogni client non associato
    trasmette periodicamente probe request; quelle *dirette* (con un SSID
    non vuoto) rivelano una rete che quel dispositivo ha già conosciuto —
    la sua "Preferred Network List". Il MAC sorgente viene controllato
    per il bit locally-administered (randomizzazione MAC, ormai lo
    standard su iOS/Android moderni quando non associati): se impostato,
    niente lookup vendor (l'OUI di un MAC randomizzato non significa
    nulla) e il dispositivo non è tracciabile a lungo tramite quello
    stesso MAC.
  - **Nessun gate/disclaimer**: è ricezione pura, mai un frame trasmesso
    — stesso principio già applicato al war-driving passivo (Fase 11).
    A differenza del war-driving però tocca anche l'identità dei
    *client* (MAC + reti probate), non solo le AP — vedi il commento
    esteso in `BeaconProbeSniffer.h` sul perché questo resta comunque
    legittimo (stesso dato che Kismet/Wireshark già mostrano gratis in
    monitor mode) ma va trattato con cura in ogni export. Dati solo in
    RAM per la sessione, nessun CSV automatico (a differenza del log
    sempre-attivo del war-driving) — stesso stile "session-only" di
    `CdpLldpSniffer`/`PassiveHostDiscovery`.
  - **Effetto collaterale accettato**: il channel-hopping rompe
    necessariamente la connessione WiFi propria di questo dispositivo
    per tutta la durata dell'ascolto (un solo radio non può restare
    associato su un canale e saltare su tutti gli altri insieme) — stesso
    compromesso già accettato da `DeauthManager`/`PmkidManager`, con la
    stessa riconnessione automatica (`WifiManager::autoConnect()`) allo
    stop. Essendo un effetto solo su *questo* dispositivo (non su terzi),
    non richiede il gate "offensive" — vedi il commento in `core/
    Config.h` sul criterio "third-party-affecting" usato per quel gate.
  - Condivide con `ArpSpoofManager`/`DeauthManager`/`PmkidManager`/
    `CdpLldpSniffer`/`RogueDhcpDetector`/`PassiveHostDiscovery` l'unico
    callback promiscuo di `esp_wifi` — vedi `ActivityStatus` per
    l'indicatore `RF:BCN` in header.
- **mDNS/DNS-SD correlato agli host + hostname migliorato**
  (`ServiceEnumerator`, `ScanManager::mergeMdnsService`): il browser
  DNS-SD completo esisteva già dalla Fase 19 (tipo+istanza+porta), ma
  restava una lista slegata da qualunque host — non c'era modo di sapere
  *quale* dispositivo offriva un dato servizio. Ora ogni `Service`
  registra anche l'IP sorgente della risposta mDNS che l'ha annunciato
  (`fromIp`, letto gratis da `udp.remoteIP()` — nessuna query aggiuntiva,
  stesso trucco già usato da `SsdpDiscovery::Device::fromIp`), e sia
  `DiscoveryRunner` (fine fase Services) sia `ServiceScreen` (uso standalone
  da SERVICE SCAN, fuori da RUN ALL) richiamano
  `ScanManager::mergeMdnsService` per ogni servizio trovato: se `fromIp`
  combacia con un host già nella tabella di NETWORK SCAN, gli viene
  appesa una riga di riepilogo (`HostInfo::mdnsServices`, mostrata in
  HOST DETAIL) e — solo se l'host non ha ancora un hostname da
  NBNS/reverse-PTR — il nome istanza DNS-SD viene adottato come hostname
  (spesso è il nome assegnato dall'utente al dispositivo, "Living Room
  speaker", una fonte migliore della reverse-PTR generica che molti
  dispositivi ignorano). Esportato anche in JSON/CSV (`ResultStore`).
- **RTC a batteria** (`net/TimeSync`, unità Grove tipo M5Stack RTC Unit/
  Mini RTC, chip BM8563): il Cardputer/Cardputer ADV non ha un RTC a
  bordo (a differenza di Core2/CoreS3), quindi `main.cpp` ora imposta
  `cfg.external_rtc = true` prima di `M5Cardputer.begin()` — dice a
  M5Unified di sondare anche il bus I2C della porta Grove per un chip
  RTC noto, non solo quello interno (di default `false`, quindi senza
  questa riga un'unità fisicamente collegata resterebbe comunque "non
  rilevata"). Se rilevata (`TimeSync::rtcAvailable()`), `TimeSync::begin()`
  semina l'orologio di sistema dall'RTC **prima** di armare NTP — ora
  reale disponibile da subito al boot, senza aspettare il WiFi — e ogni
  sync NTP viene poi riscritto sull'RTC (`TimeSync::syncRtcIfNeeded()`,
  richiamato ogni ~5s dal loop di `UiManager` insieme al check batteria:
  un'attesa di grazia di 90s dopo il primo sync per lasciare che sia
  quello NTP, non l'eco del seed appena letto, poi ogni 30 minuti) così
  l'RTC resta accurato tra una sessione e l'altra anche senza WiFi al
  boot successivo. `DIAGNOSTICS` mostra presenza RTC e batteria
  scarica (`getVoltLow()`). **RISK**: l'API M5Unified usata
  (`M5.Rtc.isEnabled()`/`setSystemTimeFromRtc()`/`getDateTime()`/
  `setDateTime()`/`getVoltLow()`) è stata verificata leggendo i sorgenti
  reali di M5Unified (non solo la documentazione), ma senza un'unità RTC
  fisica in mano per un build reale — su ogni scheda senza RTC collegato
  (il caso comune) tutto questo blocco è no-op per costruzione
  (`isEnabled()` false), quindi il comportamento esistente non cambia.

Nessuna di queste tre è gated da un disclaimer — nessuna attacca
attivamente terzi (il beacon/probe sniffer non trasmette mai nulla) né
tratta credenziali. Aggiornati i "Limiti noti" più sotto per riflettere
sia questa fase sia il fatto che il browser mDNS/DNS-SD completo era già
descritto correttamente solo dalla Fase 19 in poi (la vecchia voce
"solo fallback a NBNS" era ormai obsoleta anche prima di questa fase).

### Fase 28: ogni report di scansione salvato su SD sotto /netrunner, un file per run

Prima di questa fase, l'export JSON/CSV di `NETWORK SCAN` (`E`) e il
report HTML (`R` da `NETWORK SCAN`, o quello generato in automatico da
`AUTO ASSESS`) scrivevano sempre sugli stessi tre nomi fissi
(`/export.json`, `/export.csv`, `/report.html`), sovrascritti a ogni
run — niente storico, e impossibile distinguere due assessment fatti su
reti diverse (o sulla stessa rete in momenti diversi) senza rinominare
i file a mano prima del run successivo.

- **`storage/NetrunnerPaths`** (nuovo, piccolo modulo condiviso): crea
  `/netrunner` se non esiste e restituisce una base
  `/netrunner/<TIMESTAMP>_<SSID corrente>` — senza estensione, stesso
  pattern già usato da `WardrivingManager::handleOpenAllowlistedAp` per
  i propri export per-AP (`base + ".json"`, `base + ".csv"`, ...): un
  'unico timestamp condiviso per l'intero trio JSON/CSV/HTML di un run,
  non tre timestamp leggermente diversi se calcolati separatamente.
  `TIMESTAMP` usa il nuovo `TimeSync::nowFilenameString()`
  (`YYYYMMDD-HHMMSS`, filesystem-safe, niente `:`/spazi) quando l'ora è
  sincronizzata — spesso vera fin dal boot ora che la Fase 27 ha
  aggiunto il supporto RTC — altrimenti un fallback `uptime-<secondi>`,
  stessa convenzione già usata dal log CSV del war driving. Il nome
  rete viene sanificato (`/`, `\`, `:`, spazio → `_`).
- **Tre punti di scrittura aggiornati**: l'export manuale e quello
  automatico a fine scan in `HostListScreen` (entrambi in `NETWORK
  SCAN`), il report HTML manuale (`R`) e quello di `AssessmentRunner`
  (`AUTO ASSESS`). `ReportGenerator`'s **COMPANION ARTIFACTS** (l'indice
  degli artefatti trovati sulla card, in fondo a ogni report HTML) non
  cerca più i vecchi nomi fissi ma elenca `/netrunner/` come cartella
  (stesso trattamento già riservato a `/handshakes/`, dato che il nome
  file esatto ora varia per ogni run).
- **Deliberatamente NON toccato in questa fase (poi spostato in Fase
  29 su richiesta esplicita)**: l'export per-AP del war driving restava
  inizialmente nel proprio namespace separato (`/wardrive/scans/`) — vedi
  la sezione "Fase 29" sotto per il ripensamento. `ScanHistory` (i suoi
  snapshot interni per il diff "nuovo host mai visto") resta invece un
  meccanismo separato, non un report pensato per l'utente — quello non è
  cambiato.

### Fase 29: anche i file del war driving sotto /netrunner

Su richiesta esplicita dell'utente, la separazione decisa in Fase 28 tra
i report di `NETWORK SCAN`/`AUTO ASSESS` e quelli di `WardrivingManager`
è stata rimossa: ora tutto vive sotto `/netrunner`.

- **`storage/NetrunnerPaths::reportBase` ora prende un `label` esplicito**
  invece di leggere sempre `g_wifi.currentSsid()` al suo interno: i
  chiamanti di `NETWORK SCAN`/`AUTO ASSESS` passano `g_wifi.currentSsid()`
  loro stessi (comportamento identico a prima), mentre
  `WardrivingManager::handleOpenAllowlistedAp` passa esplicitamente
  `ap.ssid + "_" + ap.bssid` — non può affidarsi al SSID "corrente" del
  WiFi perché nel momento in cui l'export parte il dispositivo potrebbe
  già essere in fase di riconnessione alla propria rete salvata. Il
  suffisso BSSID (non solo SSID) resta per continuare a distinguere due
  AP con lo stesso nome — lo stesso scenario di evil twin che questo
  modulo già segnala altrove (`ApSighting::suspicious`).
- **`wardrive.csv`** (il log continuo, sempre in append, di ogni AP
  incontrato in sessione) si sposta da `/wardrive/wardrive.csv` a
  `/netrunner/wardrive.csv` — stesso file, stesso comportamento
  (accumula per tutta la vita del dispositivo, non un file per run),
  solo cartella diversa.
- **Gli export per-AP** (discovery + port scan su un AP aperto in
  allowlist) si spostano da `/wardrive/scans/<ssid>_<bssid>.json/.csv` a
  `/netrunner/<timestamp>_<ssid>_<bssid>.json/.csv` — ora con timestamp
  come ogni altro report, invece di essere tenuti a un solo file per AP
  (una seconda incursione sullo stesso AP non sovrascrive più la prima).
- **`ReportGenerator`**: rimossa la voce fissa `/wardrive/wardrive.csv`
  da COMPANION ARTIFACTS (non esiste più a quel path) — già coperta dalla
  voce generica `/netrunner/` aggiunta in Fase 28, il cui testo ora
  menziona esplicitamente anche il log di war driving.

### Fase 30: sweep LDAP (anon-bind + rootDSE) e disclosure NTLM-over-HTTP

Tre proposte (su dieci per l'area "offensive security", poi ristrette
alle sole LDAP/NTLM su richiesta dell'utente) implementate nel sotto-menu
DISCOVERY, stesso registro read-only/non gated di SNMP SWEEP e DATASTORE
SWEEP — mai scritture, mai credenziali vere, mai un handshake completato:

- **`LDAP SWEEP`** (`scan/LdapProbe`, `net/LdapWire`): per ogni host vivo
  che risponde sulla porta 389, tenta un **bind anonimo semplice** (DN e
  password vuoti) e, indipendentemente dal risultato, una **ricerca sul
  rootDSE** (`namingContexts`, `defaultNamingContext`, `dnsHostName`) —
  l'RFC 4511 §5.1 prevede che il rootDSE resti leggibile anche senza bind
  riuscito, e molti DC reali lo rispettano pur rifiutando il bind
  anonimo generico, quindi i due controlli sono deliberatamente
  indipendenti, non in sequenza condizionata. `net/LdapWire.h` implementa
  il minimo indispensabile di BER (ASN.1, RFC 4511 §5.1/X.690) per
  costruire le due richieste e leggere le risposte — **verificato prima
  di scrivere una riga di C++** codificando le stesse identiche richieste
  con la libreria Python `ldap3` (il suo modulo `ldap3.protocol.rfc4511`,
  un'implementazione ASN.1 reale, non una derivazione a mano) e
  confrontando byte a byte; il parsing (bind response + search result
  entry, incluse lunghezze BER long-form su un rootDSE realistico multi-
  valore) è stato testato contro messaggi generati dalla stessa libreria
  prima di considerarlo corretto. Nessun server LDAP reale né build ESP32
  reale coinvolti — vedi "Limiti noti".
- **`NTLM DISCLOSURE`** (`scan/NtlmHttpProbe`, `net/NtlmWire`): per ogni
  host vivo con una porta HTTP già nota (da `PORT SCAN`/`AUTO ASSESS` —
  `NETWORK SCAN` da sola non basta, vedi sotto), invia un messaggio NTLM
  Type 1 (NEGOTIATE) in un header `Authorization: NTLM <...>` e, se il
  server risponde con un Type 2 (CHALLENGE) in `WWW-Authenticate: NTLM
  <...>`, ne decodifica i `TargetInfo` AV_PAIR per rivelare dominio
  NetBIOS/DNS e nome macchina — spesso il dominio AD e l'hostname reali
  dietro un'app web che altrimenti non li mostra. **Non completa mai
  l'handshake** (nessun messaggio Type 3/AUTHENTICATE viene mai costruito
  o inviato), **non usa mai una credenziale vera**: il Type 2 è la parte
  del protocollo che ogni server invia a chiunque negozi NTLM, autenticato
  o no — leggerlo è un banner grab evoluto, non furto di credenziali o
  di hash. Anche qui, `net/NtlmWire.h` è stato verificato prima dell'uso:
  il layout del messaggio Type 1 contro la libreria Python `ntlm-auth`,
  e il parsing del Type 2 (incluso il decode UTF-16LE→ASCII dei campi)
  contro un CHALLENGE_MESSAGE realistico costruito con le classi
  `TargetInfo`/`AvId` della stessa libreria (stesso genere di AV_PAIR che
  manda un vero domain controller/IIS). **Solo HTTP semplice** (porta/
  servizio `"http"`) — HTTPS (`"https"`) è fuori scope: servirebbe un
  client TLS, un'aggiunta non banale lasciata per una fase futura.
- **`RUN ALL DISCOVERY`**: entrambi aggiunti alla sequenza (dopo
  DATASTORE SWEEP) con le stesse percentuali di avanzamento
  ridistribuite. `NTLM DISCLOSURE` in quel contesto trova quasi sempre
  zero host (RUN ALL non fa un port scan per-host), a meno che uno scan
  porte non sia già stato fatto in sessione — comportamento accettato,
  non un bug: si degrada allo stesso "niente da fare" pulito di ogni
  altra fase quando mancano i prerequisiti.

Nessuna delle due tocca disponibilità del servizio, non tenta exploit,
non intercetta né rilancia traffico di terzi — coerente con l'esclusione
esplicita, già decisa in una fase precedente, di un modulo NTLM-relay/
Responder-style (avvelenamento LLMNR/NBT-NS + cattura hash): quello
raccoglierebbe credenziali di macchine terze che non hanno mai scelto di
interagire col dispositivo sotto test, categoria di rischio diversa da
tutto il resto di questo firmware — resta fuori scope.

### Fase 31: seconda passata UX/UI trasversale (scroll marker, dettaglio full-value, DISCOVERY raggruppato, attività in background)

Seconda passata di usabilità (dopo la Fase 24), su un elenco di dieci aree
individuate riguardando l'intero layer UI — cinque scelte dall'utente più
la rimozione del donut chart ormai inutilizzato in `PORT SCAN`. Anche
questa, come la Fase 24, è puramente additiva: nessuna firma di `Screen`
cambiata, solo nuovi override opzionali e nuovi helper condivisi in
`chrome::`.

- **Indicatori di scorrimento ovunque**: la logica ^/v che prima esisteva
  solo, inline, nel menu principale è stata estratta in
  `chrome::drawScrollMarkers()` e applicata a tutte le 21 schermate con
  liste scorrevoli (`first`/`kMaxRows`) che prima non davano alcun segnale
  di "ci sono altre righe sopra/sotto" — port scan, host list, service
  scan, beacon/probe intel, SNMP/SSDP/LDAP/NTLM sweep, war driving, threats,
  cronologia scan, ricerca, e altre.
- **`helpText()` su tutte le schermate**: prima solo 12 delle 44 schermate
  avevano una legenda tasti per l'overlay `?` introdotto in Fase 24; le
  restanti 32 ora la hanno tutte, con la stessa convenzione (titolo, righe
  vuote, poi comandi).
- **Vista a valore completo per i campi troncati**: molte liste tagliano
  banner/URL/attributi lunghi con `"..."` per stare nella riga. Il tasto
  `I` (Info), libero su ogni schermata coinvolta, apre ora un overlay a
  pagina intera (`chrome::drawDetailOverlay()`, riusa il word-wrap di
  `TextWrap.h`) col valore per esteso, non troncato; un tasto qualsiasi lo
  richiude, stessa convenzione dell'help. Cablato su `PORT SCAN`, `LDAP
  SWEEP`, `NTLM DISCLOSURE`, `DATASTORE SWEEP`, `LAN TOPOLOGY` (CDP/LLDP),
  `BEACON/PROBE INTEL`, `SERVICE SCAN`, `SNMP SWEEP`, `UPNP DISCOVERY`,
  `WAR DRIVING` (che guadagna anche la navigazione su/giù degli
  avvistamenti mentre è in esecuzione, prima assente) e `THREATS`.
- **Sotto-menu DISCOVERY raggruppato**: gli 11 strumenti, prima un unico
  elenco piatto senza alcuna gerarchia, sono ora organizzati in sezioni
  (righe separatore non selezionabili, saltate automaticamente dalle
  frecce su/giù): `RUN ALL DISCOVERY` da solo in cima, poi *ONE-SHOT*
  (`UPNP DISCOVERY`, `SERVICE SCAN`), *NEEDS NETWORK SCAN* (`SNMP SWEEP`,
  `DATASTORE SWEEP`, `LDAP SWEEP`), *NEEDS PORT SCAN* (`NTLM DISCLOSURE`),
  *PASSIVE LISTENERS* (`LAN TOPOLOGY`, `PASSIVE HOSTS`, `ROGUE DHCP`,
  `BEACON/PROBE INTEL`).
- **Overview delle attività in background nell'header**: l'indicatore
  `RF:xxx` esistente (`ui/ActivityStatus`) segnalava solo quale funzione
  in modalità promiscua possiede il callback WiFi — nulla diceva se, per
  esempio, uno SNMP SWEEP o il war driving continuavano a girare dopo aver
  lasciato la loro schermata. L'indicatore ora copre anche quel caso con
  un secondo tag `BG:xxx`/`BG:N` (ciano) per tutto il resto che gira in
  background (war driving, `NETWORK`/`PORT`/`SERVICE SCAN`, SNMP/LDAP/
  NTLM/DataStore/SMB/cred sweep, evil-twin, `RUN ALL DISCOVERY`); quando
  sia un conflitto radio sia altre attività sono in corso insieme, il tag
  `RF:` (che resta prioritario, essendo un vero conflitto) mostra anche il
  conteggio delle altre (`RF:DHCP+2`) invece di provare a stare in due
  posti sulla stessa riga.
- **Donut chart rimosso da `PORT SCAN`**: il grafico a torta decorativo
  nell'angolo in alto a destra (frazione porte aperte sul range
  configurato) non veniva più guardato da nessuno una volta introdotta la
  tabella risultati reale; rimosso insieme al suo helper dedicato,
  liberando spazio perché il banner nella tabella non venga più troncato
  dalla sovrapposizione.

### Fase 32: PORT SCAN copre anche le porte comuni sopra la 1024

Il range di default (1-1024, `SETTINGS`) è quello classico dei "well-known
port" IANA, ma lascia fuori un bel po' di servizi reali molto comuni che
vivono sopra quella soglia — database, pannelli d'amministrazione, accesso
remoto. `PortScanManager::startScan` ora unisce sempre al range
configurato un elenco curato di ~50 porte sopra la 1024
(`scan/WellKnownHighPorts.h`): database (`3306` MySQL, `5432` Postgres,
`6379` Redis, `27017`/`27018`/`28017` MongoDB, `9042` Cassandra, `11211`
Memcached, `1433` MSSQL, `1521` Oracle), accesso remoto (`3389` RDP,
`5900` VNC, `5985`/`5986` WinRM), pannelli/dev server (`8080`, `8000`,
`8888`, `3000`, `9000`, `9090`, `8443`, ...), e qualche porta storicamente
nota come backdoor/exotic (`31337`). L'elenco viene deduplicato contro il
range configurato in `startScan` — se qualcuno alza manualmente
`portRangeEnd` oltre 1024 fino a coprire tutto da sé, nessuna di queste
porte viene sondata due volte.

Nessuna modifica al comportamento per porta trovata aperta: banner
grabbing (`BannerGrabber`), lookup nome servizio (`PortServiceDb`, ~12.000
voci — già copre questi servizi per nome) e controllo firme vulnerabili
(`VulnSignatures`) restano identici, semplicemente vengono eseguiti anche
sulle porte nuove. La schermata `PORT MAPPING` mostra ora "+50 common
ports >1024" sotto il range configurato quando è idle, così è chiaro cosa
viene effettivamente sondato senza dover aprire `SETTINGS`. Effetto
collaterale positivo non richiesto ma già presente nel codice: l'evidenza
"legacy port" (ambra) su `3389`/RDP nella tabella risultati, che prima non
poteva mai scattare col range di default (3389 > 1024, mai sondata), ora è
raggiungibile.

### Fase 33: primo lotto verso un sistema WiFi cyber security più evoluto

Quattro delle quindici aree individuate riguardando l'intero firmware con
l'obiettivo esplicito dell'utente di farlo evolvere verso "un sistema
evoluto di WiFi cyber security" (punti 2, 4, 5, 7 di quella lista):

- **Rilevamento WPS** (`BeaconProbeSniffer`): oltre a SSID/cifratura,
  ogni beacon/probe-response viene ora controllato anche per l'IE
  vendor-specific WPS (tag 221, OUI `00:50:F2`, sub-type `04`) — stessa
  famiglia di IE già letta per il WPA1 legacy, solo un sub-type diverso.
  Quando presente, ne legge anche `AP Setup Locked` (attributo `0x1057`
  — un AP che si è già auto-bloccato dopo troppi PIN sbagliati, segno
  che qualcuno ha già tentato un attacco) e `Config Methods` (`0x1008`,
  bitmask PBC/PIN). **Solo detection**: questo firmware non implementa
  la registrazione WPS né tenta mai un PIN (niente Reaver/pixie-dust) —
  stessa linea "rileva, non attaccare un protocollo non verificato" già
  seguita per SSH e NTLM-relay. In `BEACON/PROBE INTEL` un indicatore
  "W" compare accanto agli AP con WPS visto (rosso se ancora sbloccato,
  ambra se già locked); il dettaglio (`I`) mostra lock state e metodi.
  Un AP con WPS sbloccato compare anche in `THREATS`.
- **GUARD MODE** (`scan/DeauthWatcher`, nuovo sotto-menu DISCOVERY sotto
  *PASSIVE LISTENERS*): il primo strumento **difensivo** invece che di
  ricognizione/offensivo in questo firmware — conta i frame di
  deauthentication/disassociation per BSSID, sul canale a cui il
  dispositivo è già associato (non hopping, a differenza di BEACON/
  PROBE INTEL — qui l'obiettivo è sorvegliare la rete su cui ti trovi
  ora, non fare un survey). Una finestra scorrevole di 10s e una soglia
  di 15 frame/finestra per BSSID separano un vero flood (uno strumento
  tipo `aireplay-ng --deauth`, decine di frame/sec continui) dai
  deauth/disassoc isolati che sono normale traffico 802.11 (un client
  che si allontana). Riga rossa + allarme sonoro quando scatta. Mai
  trasmette nulla — puro ascolto, nessun gate offensive necessario,
  stessa logica di BEACON/PROBE INTEL. Un flood attivo compare anche in
  `THREATS`.
- **Euristica evil-twin rivista** (`WardrivingManager`): la versione
  precedente (Fase 13) segnalava qualunque differenza nell'enum di
  cifratura fra due sighting con lo stesso SSID — troppo grezzo (WPA2
  puro vs WPA/WPA2 misto sullo stesso router reale scattava come falso
  positivo) e troppo cieco (un clone con la stessa identica cifratura
  non veniva mai rilevato). Ora confronta un **tier di sicurezza**
  (open/wep/wpa-misto/wpa3/enterprise — vedi `securityTier()`) invece
  dell'enum grezzo, e aggiunge un secondo segnale indipendente: **vendor
  OUI diverso** fra due BSSID con lo stesso SSID e lo stesso tier
  (confrontato solo quando entrambi i lookup OUI hanno dato un
  risultato, per non generare rumore sui MAC non riconosciuti) — un
  attaccante può facilmente copiare la cifratura di un AP, molto meno
  facilmente il fatto che l'hardware reale sia dello stesso produttore.
  Il canale resta deliberatamente fuori da questa euristica — vedi
  "Limiti noti" per il perché.
- **CHANNEL SCAN**: nuova voce del menu principale, grafico a barre live
  dell'affollamento dei 13 canali 2.4GHz (quanti AP trasmettono su
  ciascuno, colore verde/ambra/rosso in base al conteggio), con il
  proprio canale connesso marcato per confronto — la stessa domanda "che
  canale è più libero" di un'app WiFi-analyzer da telefono. Riusa lo
  stesso pattern di scan continuo di `SIGNAL FINDER` (Fase 26/precedente
  a questa lista: `WifiManager::beginScan()/scanStatus()/getScanResult()`
  in loop), non introduce nessuna nuova capacità radio.

Le altre dieci aree della lista (dictionary check on-device sui PMKID
catturati, mappa client↔AP, sezione WIRELESS nel report, rilevamento
KRACK, ecc. — la sentinel mode è stata implementata in Fase 34, sotto)
restano proposte non implementate — vedi la conversazione che le ha
originate; il punto sul dictionary check in particolare era stato
segnalato come una scelta filosofica da confermare esplicitamente prima
di implementarla, non scontata.

### Fase 34: SENTINEL MODE

Su richiesta esplicita dell'utente, il punto 13 della lista di Fase 33
("sentinel mode sulla propria rete") diventa una voce a sé del menu
principale — `scan/SentinelManager` — con due comportamenti in
esecuzione insieme finché resta attiva:

- **Ri-scoperta host periodica con allarme su dispositivo nuovo**:
  rilancia lo stesso sweep di `NETWORK SCAN` (`g_scanManager`) ogni 30
  secondi e confronta ogni host vivo con MAC noto contro la baseline
  "mai visto su questa rete" già usata da `HOST LIST`
  (`ScanHistory::loadKnownMacs`, fino a `kMaxEntries`=20 scan passati di
  quella stessa rete). Un MAC che non compare in quella baseline è un
  dispositivo nuovo: bip sonoro (`sound::playAlert`, stesso usato per
  una rete aperta nuova in war driving) più una riga nel log con IP/MAC/
  hostname/vendor, consultabile per esteso con `I`. Deliberatamente
  **non** salva uno snapshot (`ScanHistory::saveSnapshot`) a ogni ciclo:
  farlo ogni 30 secondi riempirebbe le 20 posizioni della cronologia
  condivisa con scansioni quasi identiche, restringendo la finestra
  reale della baseline "mai vista" a soli ~10 minuti invece che alla
  vera storia dei scan manuali di quella rete — l'esatto contrario dello
  scopo di una baseline stabile. La baseline viene caricata una sola
  volta all'avvio e poi cresciuta solo in memoria per il resto della
  sessione.
- **Dump del traffico su pcap**: cattura ogni frame 802.11
  management+data visto sul canale a cui il dispositivo è già associato
  (niente channel-hopping, stesso principio "non disturbare la propria
  connessione STA" di `CdpLldpSniffer`/`RogueDhcpDetector`/
  `PassiveHostDiscovery`/`GUARD MODE` — a differenza di quelli, qui
  vengono catturati anche i frame dati, non solo quelli di gestione, per
  un vero dump di traffico) in un file `.pcap` sotto `/netrunner`,
  troncato a 256 byte per frame come `DeauthManager`/`PmkidManager`.
  **Non decripta nulla**: il payload di un frame dati su rete WPA2/WPA3
  resta cifrato esattamente come lo era sull'aria — quello che finisce
  nel pcap è comunque utile (chi parla, quanto, quando: header MAC
  sempre leggibili in modalità promiscua indipendentemente dalla
  cifratura), analizzabile offline con Wireshark/tshark, mai un modo per
  leggere il contenuto reale di qualcosa. (Fase 35 aggiunge la rotazione
  automatica per file, vedi sotto — resta comunque senza tetto sul
  numero totale di parti in una sessione, vedi "Limiti noti".)

Nessuna delle due parti tocca dispositivi di terzi in modo diverso da
quanto fanno già `NETWORK SCAN`/`CDP LLDP`/`ROGUE DHCP`: solo ascolto e
scansione della propria rete già connessa, mai nulla inviato a un
dispositivo che non abbia già scelto di essere su quella rete. Nessun
gate offensive richiesto per lo stesso motivo.

### Fase 35: SENTINEL MODE si evolve — dispositivi scomparsi, deauth flood integrato, rotazione pcap, riepilogo di sessione

Su richiesta esplicita dell'utente, quattro delle cinque funzionalità
aggiuntive proposte per SENTINEL MODE dopo la Fase 34 (punti 1, 2, 3, 5
— la 4, whitelist dispositivi fidati indipendente dalla cronologia, resta
non implementata):

- **Rilevamento "dispositivo scomparso"** — il simmetrico di
  "dispositivo nuovo": `SentinelManager` ora tiene traccia (`_tracked`,
  solo in memoria per la sessione) di ogni host visto vivo durante i
  propri cicli, non solo di quelli marcati "nuovi". Un host tracciato
  che manca per `kMissedCyclesThreshold`=2 cicli consecutivi (~60s)
  genera un evento `DeviceGone` — stesso bip sonoro di un dispositivo
  nuovo, così "la stampante si è spenta inaspettatamente" è visibile
  quanto "un telefono nuovo si è unito alla rete". Non esiste un evento
  di "ritorno": lo stato si azzera silenziosamente quando il dispositivo
  ricompare — vedi "Limiti noti" per il ragionamento.
- **Rilevamento flood deauth/disassoc integrato** — invece di dover
  scegliere fra GUARD MODE e SENTINEL MODE (che condividerebbero comunque
  lo stesso, unico, callback promiscuo e si ruberebbero i frame a
  vicenda), la stessa logica a finestra scorrevole di `DeauthWatcher`
  (10s, 15 frame/finestra per BSSID) è ora richiamata direttamente dentro
  `SentinelManager::onCapturedFrame`, sullo stream di frame già in
  ricezione per il dump del traffico — nessuna sessione promiscua
  aggiuntiva necessaria. Un evento `DeauthFlood` genera lo stesso
  allarme sonoro degli altri due tipi di evento. `GUARD MODE` resta
  comunque disponibile come strumento a sé per chi vuole solo il
  rilevamento flood senza il resto.
- **Rotazione automatica del pcap** — ogni file `.pcap` di sessione è
  ora limitato a `kMaxPcapBytes`=5MB; oltre quella soglia se ne apre uno
  nuovo con lo stesso nome base (timestamp+SSID) e un suffisso
  progressivo `_p1.pcap`, `_p2.pcap`, ecc. — nessun singolo file può più
  crescere senza limite, anche se il totale su una sessione lunghissima
  ancora non ha un tetto (vedi "Limiti noti").
- **Riepilogo di fine sessione** — quando SENTINEL MODE si ferma
  (`ENTER` o uscita dal firmware), `SentinelManager::writeSummary()`
  scrive `<stesso base>_summary.txt` sotto `/netrunner`: durata sessione,
  cicli eseguiti, frame catturati, elenco delle parti pcap prodotte, e
  ogni evento (nuovo/scomparso/flood) con IP/MAC/hostname/vendor o BSSID
  a seconda del tipo — un riepilogo leggibile senza dover essere rimasti
  a guardare lo schermo in tempo reale durante la sessione.

Nel frattempo la schermata `SENTINEL MODE` è stata aggiornata da una
semplice lista "nuovi dispositivi" a un log eventi unificato (`I` per il
dettaglio di ciascuno), con un'etichetta e un colore diversi per NEW
(magenta), GONE (ambra) e FLOOD (rosso).

### Fase 36: rilevamento EAPOL sul dispositivo + PMKID sweep su più AP

Risposta a una domanda esplicita dell'utente ("cosa potresti implementare
riguardo la raccolta di PMKID/handshake?"), con un vincolo dato altrettanto
esplicitamente prima di scrivere una riga di codice: **"cattura solo, non
craccare mai"**. Le due funzionalità implementate rispettano quella linea
alla lettera — nessuna delle due deriva, indovina o verifica mai una
passphrase.

- **`net/EapolWire`: classificatore strutturale EAPOL-Key** — nuovo
  parser che legge SOLO i bit di frame-control, l'EtherType LLC/SNAP
  (0x888E), il byte Type dell'header EAPOL e i flag del campo Key
  Information (Install/Ack/MIC/Secure) per capire quale dei quattro
  messaggi del 4-way handshake un frame sembra essere — la stessa
  euristica standard usata da Wireshark/aircrack-ng/hcxdumptool (Message1:
  Ack=1,Install=0; Message3: Ack=1,Install=1; Message2: Ack=0,MIC=1,
  Secure=0; Message4: Ack=0,MIC=1,Secure=1). Per il solo Message1, cerca
  anche la presenza (non il valore) di un KDE PMKID (elemento vendor 0xDD,
  OUI `00:0F:AC` tipo 4) nel Key Data. **Non legge mai** Nonce, MIC, IV,
  RSC, Key ID, né i 16 byte del PMKID stesso una volta trovato il
  marcatore — solo la sua presenza/assenza. Riusa
  `ieee80211::parseDataFrame`/`parseSnap` già esistenti (stesso codice già
  verificato e condiviso da CdpLldpSniffer/RogueDhcpDetector/
  PassiveHostDiscovery), non ri-deriva l'header 802.11 da zero.
- **`PmkidManager`/`DeauthManager`: verdetto "cattura riuscita" a bordo**
  — ogni frame catturato viene ora classificato con `EapolWire` (sul
  frame completo, prima del troncamento a 256 byte usato per il pcap —
  un PMKID KDE tardivo nel Key Data non viene perso). `PmkidManager`
  espone `pmkidLikelyCaptured()` (almeno un Message1 con KDE PMKID
  visto); `DeauthManager` espone `handshakeLikelyCaptured()` (almeno un
  Message1 E un Message2 visti — la coppia minima che hashcat/aircrack
  richiedono per un attacco a dizionario offline). Le schermate `PMKID
  CAPTURE`/`DEAUTH + CAPTURE` mostrano ora questo verdetto a fine cattura
  (verde "likely captured!" / ambra "no PMKID/handshake seen"), invece di
  lasciare che sia solo il numero di pacchetti a suggerire se è valsa la
  pena. Il file pcap resta scritto verbatim esattamente come prima — la
  classificazione è un'analisi aggiuntiva sullo stesso frame già in
  arrivo, non un filtro su cosa viene salvato.
- **PMKID SWEEP** (`scan/PmkidSweepManager`, nuovo tasto `S` da WAR
  DRIVING) — invece di ripetere PMKID CAPTURE a mano su ogni sighting,
  questo orchestratore (stesso schema di `DiscoveryRunner`/
  `AssessmentRunner`: guida solo l'API pubblica di `PmkidManager`, non
  reimplementa nulla della cattura) prende uno snapshot degli AP
  attualmente noti a WAR DRIVING con cifratura reale (esclude le reti
  aperte, che non hanno nulla da catturare, ed esclude gli SSID nascosti,
  che `WiFi.begin()` non può raggiungere per nome) e ci gira PMKID CAPTURE
  in sequenza, un AP alla volta — necessariamente sequenziale, non
  parallelo, per lo stesso motivo di ogni altro modulo che condivide il
  callback promiscuo (vedi `ui/ActivityStatus.h`). Ogni risultato (SSID,
  BSSID, verdetto, conteggio frame, percorso pcap) resta consultabile
  nella schermata `PMKID SWEEP` (`I` per il dettaglio). Stesso gate
  `OffensiveDisclaimerScreen` di EVIL TWIN/DEAUTH/PMKID singolo.

Le altre nove aree della lista originale di 15 restano proposte non
implementate. Il dictionary-check on-device sui PMKID catturati in
particolare — l'unica idea di quella lista che avrebbe cambiato il
principio "mai craccare" — non è stato toccato, coerentemente col vincolo
posto per questa fase.

### Fase 37: terza passata UX/UI trasversale — tutti i 15 punti proposti

Su richiesta esplicita dell'utente ("proponi 15 modifiche UX/UI" seguito
da "Implementa tutto!"), un audit del codice esistente ha prodotto 15
proposte concrete, radicate in gap reali (non ipotetici) trovati nelle
schermate già esistenti — e tutte e 15 sono state implementate in questa
fase.

- **1-2. Breadcrumb/help text mancanti**: `ChannelScanScreen` non aveva
  `title()`, quindi non compariva come breadcrumb del genitore quando
  aperta da un'altra schermata (`chrome::drawHeader`) — aggiunto `"CHAN"`.
  L'help text di `WardrivingScreen` non menzionava le frecce per muovere
  la selezione tra i sighting — aggiunta la riga.
- **3. Audit messaggistica "serve il WiFi"**: verificato a tampone che
  ogni schermata che richiede una connessione WiFi attiva mostri già un
  messaggio chiaro quando non è connessa (stesso pattern di verifica-
  prima-di-reimplementare già usato in questa sessione per il suono
  SENTINEL/war-driving) — nessuna schermata mancante trovata, nessuna
  modifica di codice necessaria.
- **4-5. `THREATS` integra SENTINEL MODE e PMKID SWEEP**: `collectFindings()`
  ora include anche gli eventi SENTINEL MODE (dispositivo nuovo → ambra
  "new on network", dispositivo scomparso → ambra "went dark", flood
  deauth rilevato in sessione Sentinel → rosso) e un riepilogo
  informativo (ciano) del numero di hit trovati dall'ultimo PMKID SWEEP,
  se ne è girato uno — prima `THREATS` non sapeva nulla di quello che
  succedeva in quelle due schermate, per quanto entrambe già scrivessero
  i propri log dedicati.
- **6. `FILE MANAGER`: scorciatoie a `/netrunner` e `/handshakes`**: `N`/`H`
  saltano direttamente alle due cartelle dove finisce quasi ogni artefatto
  prodotto da questo firmware, invece di dover scendere manualmente da
  `/` ogni volta.
- **7. Nuova schermata `CAPTURES`** (`ui/screens/CapturesScreen`, tasto
  `C` da `SETTINGS`): browser unificato di sola visualizzazione/cancellazione
  per ogni file `.pcap` prodotto dal firmware — scansiona sia
  `/handshakes` (PMKID CAPTURE, DEAUTH+CAPTURE) sia `/netrunner`
  (SENTINEL MODE) e li presenta in un'unica lista con nome/dimensione,
  `I` per il percorso completo, `X` per cancellare (con conferma). Prima
  bisognava ricordarsi quale cartella avesse scritto quale strumento e
  passare da `FILE MANAGER` per trovarli.
- **8. `DISCOVERY`: indicatore di prontezza per riga**: un pallino verde/
  rosso accanto a ogni voce che dipende da un prerequisito (SNMP/
  DATASTORE/LDAP SWEEP vogliono un `NETWORK SCAN` già girato; NTLM
  DISCLOSURE vuole anche una porta HTTP nota da `PORT SCAN`) mostra a
  colpo d'occhio se è già soddisfatto, invece di scoprirlo solo aprendo
  lo strumento e vedendolo terminare a vuoto.
- **9. `WAR DRIVING`: sottomenu offensivo raggruppato** (tasto `O`,
  additivo — `E`/`X`/`P`/`S` diretti restano invariati per chi li
  conosce già): apre un piccolo menu con le quattro azioni offensive
  (EVIL TWIN/DEAUTH/PMKID/PMKID SWEEP) etichettate con la loro lettera,
  navigabile con le frecce, per chi non le ricorda a memoria — stesso
  target/stesso gate `OffensiveDisclaimerScreen` di prima, solo un punto
  d'ingresso in più.
- **10. Chiarito il rapporto GUARD MODE / SENTINEL MODE**: `GUARD MODE`
  ora mostra "(SENTINEL MODE includes this)" quando non è in esecuzione
  in autonomia, per rendere esplicito che avviare SENTINEL MODE copre
  già lo stesso rilevamento flood senza dover avviare entrambi.
- **11. Indicatore "molto occupato" nell'header**: la linea separatrice
  già disegnata sotto l'header (`chrome::drawHeader`) cambia colore —
  grigio normale, ambra, o rosso se è in corso un vero conflitto radio —
  quando tre o più task in background/promiscui girano insieme, invece
  di lasciare che l'unico indizio sia il tag testuale `RF:`/`BG:` che
  mostra un solo nome più un contatore. `activity::draw()` ora ritorna
  quel colore invece di essere `void`, e internamente costruisce la
  propria tabella di manager tramite un helper condiviso (`buildTaskTable`)
  invece di due array letterali separati — la stessa tabella alimenta
  anche il punto 12 qui sotto.
- **12. Nuova schermata `ACTIVITY`** (`ui/screens/ActivityScreen`, voce
  nel menu principale): l'espansione a schermo intero del tag compatto
  dell'header — ogni manager che questo firmware traccia, con il suo
  stato attuale (in esecuzione o no) e se compete per il callback radio
  promiscuo (`RF`) o no (`BG`), quelli in esecuzione elencati per primi.
  Sola lettura: non avvia/ferma nulla, serve solo a non perdere di vista
  cosa sta ancora girando dopo essere passati ad altro.
- **13. `GUARD MODE`: tasso della finestra corrente per incidente**: ogni
  riga ora mostra sia il conteggio totale (`Nt`) sia quello della sola
  finestra scorrevole attuale (`Nt/w`) — prima si vedeva solo il totale
  cumulato, che non distingue un incidente ormai concluso da uno ancora
  in corso.
- **14. `PMKID SWEEP`: anteprima target con RSSI prima di avviare**:
  `PmkidSweepManager::previewTargets()` (nuovo, sola lettura, richiamabile
  anche a riposo) espone lo stesso filtro di eleggibilità usato da
  `start()` (non aperto, SSID non nascosto); la schermata lo usa per
  mostrare SSID/barre di segnale/canale di ogni AP che lo sweep
  colpirebbe, prima che l'utente si impegni in una sequenza che può
  richiedere diversi minuti — invece di partire alla cieca.
- **15. Legenda tasti globali nell'overlay di aiuto**: `UiManager::
  drawHelpOverlay()` ora chiude sempre con una riga fissa che ricorda che
  `I`/`TAB` variano per schermata e `?` riapre questo stesso overlay,
  prima di "any key: close" — prima ogni schermata doveva ripetere quella
  spiegazione nel proprio `helpText()` (molte non lo facevano). Il
  budget di righe di contenuto si è ristretto di conseguenza (vedi
  "Limiti noti" per l'effetto su schermate con `helpText()` già lunghi).

Tutte e 15 le proposte erano già radicate in gap concreti trovati per
lettura diretta del codice esistente, non ipotizzati — stesso approccio
usato per le due passate UX/UI precedenti di questo progetto (Fase 24,
Fase 31).

### Fase 38: sweep IoT/OT + PLAYBOOK (sequenze scriptabili)

Su richiesta esplicita dell'utente, dopo una proposta di 15 evoluzioni
in ottica red/blue teaming: due di quelle proposte implementate, le
altre restano proposte aperte (vedi la sezione più sotto per l'elenco
completo).

- **`net/../scan/IotOtProbe`: rilevamento MQTT/Modbus/CoAP senza
  autenticazione** — stesso schema e stesso livello di rischio di
  `DataStoreProbe` (sola lettura, nessun gate): sweepa la tabella host
  vivi e controlla i tre protocolli più comunemente trovati esposti
  senza autenticazione sui segmenti IoT/OT:
  - **MQTT (1883/TCP)**: invia un vero pacchetto CONNECT (clean
    session, nessuna credenziale) — un CONNACK con return code 0
    significa che il broker accetta client anonimi; disconnette
    subito dopo in modo pulito, non pubblica né si iscrive a nulla.
  - **Modbus TCP (502/TCP)**: invia una Read Device Identification
    (funzione 0x2B/0x0E, oggetto 0 = VendorName) — sola lettura per
    definizione. Modbus non ha ALCUN concetto di autenticazione in
    tutto il protocollo, quindi qualunque risposta valida (successo O
    un'eccezione di protocollo) è già di per sé il finding: un
    protocollo OT che per progettazione non può mai richiedere una
    password è raggiungibile da questo segmento.
  - **CoAP (5683/UDP)**: un GET NON-confirmable alla risorsa standard
    di discovery CoRE (`/.well-known/core`, RFC 6690) — la richiesta di
    sola lettura "cosa offri" che ogni client CoAP deve poter inviare.
  Wired in `DISCOVERY` (gruppo "NEEDS NETWORK SCAN", stesso indicatore
  di prontezza della Fase 37) e in `RUN ALL DISCOVERY`
  (`DiscoveryRunner`, nuova fase tra DATASTORE e LDAP).
- **`scan/PlaybookRunner` + schermata `PLAYBOOK`: sequenze scriptabili**
  — non un vero linguaggio di scripting (avrebbe richiesto un parser/
  interprete completo per un guadagno che non giustifica quella
  superficie nuova su questo hardware), ma una piccola libreria di
  preset, ciascuno una sequenza ordinata di step che questo
  orchestratore guida tramite le API pubbliche `start()`/`isRunning()`/
  `stop()` di manager e orchestratori GIÀ esistenti — stesso principio
  di `DiscoveryRunner`/`AssessmentRunner`/`PmkidSweepManager`, applicato
  un livello più in alto (uno step può essere un singolo probe, o
  un intero orchestratore già esistente eseguito come un unico step).
  Tre preset iniziali:
  - **FULL RECON**: `AUTO ASSESS` poi `RUN ALL DISCOVERY` — i due
    "one-button" già esistenti, incatenati; oggi nulla li concatenava,
    bisognava lanciarli a mano uno dopo l'altro.
  - **QUICK IOT/OT**: `NETWORK SCAN` → `SNMP SWEEP` → `DATASTORE SWEEP`
    → `IOT/OT SWEEP` — una ricognizione rapida, senza porte estese né
    listener promiscui, per quando interessa solo il quadro IoT/OT.
  - **WIRELESS SURVEY**: l'unico preset che non richiede WiFi connesso
    — una finestra a tempo di `WAR DRIVING` seguita da una finestra a
    tempo di `BEACON/PROBE INTEL` (20s ciascuna), un campione rapido,
    non un sostituto di una sessione più lunga aperta a mano.
  Raggiunta dal menu principale (`PLAYBOOK`); la schermata mostra un
  selettore quando inattiva e progresso step-per-step (con log) mentre
  gira; `DEL` esce lasciandolo attivo in background, stesso pattern di
  ogni altro orchestratore in questo firmware.

### Fase 39: IOT/OT SWEEP copre anche BACnet/DNP3 + integrazione in THREATS

Due delle evoluzioni proposte dopo la Fase 38, implementate su richiesta
esplicita:

- **`IotOtProbe` esteso a BACnet/IP e DNP3** — stesso principio già
  applicato a MQTT/Modbus/CoAP:
  - **BACnet/IP (47808/UDP)**: un Who-Is unicast (BVLC
    Original-Unicast-NPDU, Unconfirmed-Request/Who-Is) — lo stesso "chi
    c'è" che ogni workstation BACnet invia, solo indirizzato a un host
    invece che a tutto il segmento. Una risposta I-Am strutturalmente
    valida (verificata solo per i campi BVLC/APDU rilevanti, non un
    parser TAG-value BACnet completo) conferma un dispositivo live.
    BACnet, come Modbus, non richiede alcuna autenticazione per questo
    scambio.
  - **DNP3 (20000/TCP)**: una Link Status Request a livello Data Link
    (function code 9, indirizzo di destinazione broadcast `0xFFFF` —
    stessa tecnica usata dallo script `dnp3-info` di nmap), con un CRC-16
    specifico di DNP3 (polinomio 0xA6BC, IEEE 1815) calcolato
    correttamente sull'header. Il Data Link Layer di DNP3 non ha
    autenticazione (Secure Authentication è un'estensione opzionale,
    raramente distribuita), quindi qualunque risposta che inizia con i
    byte di sync DNP3 (`0x05 0x64`) è già il finding — il CONTENUTO della
    risposta non viene validato oltre questo, deliberatamente, per non
    perdere finding reali per la variabilità di formato tra vendor
    diversi (stesso principio "una risposta è prova sufficiente" già
    usato da `UdpProbe`).
- **`THREATS` integra `IOT/OT SWEEP`** — i finding non autenticati sui
  protocolli OT (Modbus/BACnet/DNP3) vengono mostrati Critical/rosso: a
  differenza di un servizio applicativo con l'autenticazione
  semplicemente disattivata, questi tre protocolli non hanno ALCUN
  concetto di autenticazione nella loro progettazione — la loro sola
  raggiungibilità è già il problema, non una configurazione. I
  protocolli IoT (MQTT/CoAP) restano Warning/ambra, dato che questi
  possiedono un meccanismo di autenticazione reale che è stato lasciato
  disattivato — un finding vero ma di un ordine diverso.

### Fase 40: bug fix — prima build reale, primi warning reali

La prima build effettivamente eseguita da un utente su Mac (vedi la nota
in "Compilare e flashare" più sotto) ha prodotto dei warning del
compilatore, analizzati e corretti qui — inclusa una regressione
funzionale reale che nessuna revisione manuale del codice aveva
individuato.

- **BUG REALE — `SentinelManager::kCaptureLen` overflow (256 → 0)**:
  la costante era dichiarata `static constexpr uint8_t kCaptureLen =
  256;`, ma `uint8_t` arriva solo fino a 255 — la conversione
  troncava silenziosamente il valore a 0. `CapturedFrame::data`
  (`uint8_t data[kCaptureLen]`) diventava quindi un array di lunghezza
  zero, e `onCapturedFrame()` clampava `capturedLen` a
  `sizeof(frame.data)` = 0 per OGNI frame catturato — nessun crash
  (il `memcpy` a lunghezza 0 è innocuo, e la size della coda FreeRTOS
  restava comunque coerente), ma il dump `.pcap` di SENTINEL MODE
  scriveva silenziosamente un record vuoto per ogni pacchetto, sempre:
  una delle due funzioni portanti di SENTINEL MODE (l'altra è la
  discovery periodica) risultava di fatto inoperante dalla Fase 34 in
  poi, senza che nulla lo segnalasse a schermo. **Corretto** cambiando
  il tipo in `uint16_t` (lo stesso di `capturedLen`/`originalLen`, gli
  altri due campi dello stesso struct) — `DeauthManager`/`PmkidManager`
  non condividevano questo bug perché usano `256` come literal diretto
  nella dichiarazione dell'array, non tramite una costante `uint8_t`
  intermedia.
- **Warning di troncamento format-string** (`net/TimeSync.cpp`,
  `ui/ActivityStatus.cpp`): GCC assume, in assenza di un range noto a
  compile-time, che ogni `%d` possa arrivare alla larghezza massima di
  un `int` (11 cifre col segno) e ogni `%s` possa essere di lunghezza
  arbitraria — quindi segnala un possibile troncamento anche quando i
  valori reali (un anno a 4 cifre, un'ora a 2 cifre, un tag di 3-6
  caratteri) non si avvicinano mai a quel caso peggiore. Corretto
  ingrandendo i buffer oltre il caso peggiore calcolato da GCC (nessun
  cambio di comportamento, il testo prodotto è identico) e, in
  `ActivityStatus.cpp`, aggiungendo una precisione `%.6s` che sia
  vincola davvero la stringa al limite già documentato nel commento di
  `TaskEntry` (3-6 caratteri) sia dà a GCC un bound concreto su cui
  ragionare.
- **Audit più ampio per altre cause di crash**: nessun'altra istanza
  dello stesso pattern (`constexpr uint8_t` con un valore che eccede
  255) trovata nel resto del codice; i parser byte-a-byte più a rischio
  aggiunti di recente (`IotOtProbe`'s Modbus/BACnet/DNP3, il file più
  giovane e meno esercitato del firmware) sono stati riverificati caso
  per caso e risultano correttamente delimitati; il pattern
  `container[container.size() - 1 - index]` usato da ogni `get()`
  "più-recente-prima" in questo firmware (una ventina di moduli) resta
  sempre protetto da un `index < size()` calcolato immediatamente prima
  — nessun underflow possibile.

### Fase 41: percorso di navigazione visibile ovunque

Su segnalazione esplicita dell'utente — dopo aver dovuto chiedere come
raggiungere BEACON/PROBE INTEL — che molte funzionalità di questo
firmware risultavano di fatto nascoste dietro una gerarchia di menu non
documentata da nessuna parte sullo schermo. Due interventi complementari,
entrambi richiesti esplicitamente:

- **Breadcrumb dell'header esteso a TUTTI i livelli** — prima mostrava
  solo il genitore immediato (es. "DISC/SNMP SWEEP"); `UiManager::
  breadcrumbPath()` (sostituisce il precedente `parentTitle()`) ora
  percorre l'intero stack di navigazione e concatena il `title()` di
  ogni schermata antenata (es. "MENU/NET/DISC/" prima di "BCN"), non
  solo quella immediatamente sopra. Perché funzionasse senza buchi
  visibili nella catena, `title()` — prima presente solo su 11
  schermate — è stato aggiunto a tutte le restanti 35 che possono
  comparire come genitore di un'altra (le uniche 4 rimaste senza sono
  `BootScreen`/`CredDisclaimerScreen`/`OffensiveDisclaimerScreen`, che
  usano sempre `replaceScreen()` e quindi non restano mai sullo stack
  come antenate, e `PlaceholderScreen`, oggi irraggiungibile).
- **Percorso completo (con tasti) nell'help overlay di ogni schermata**
  — ogni schermata raggiungibile con più di un salto dal menu principale
  ora include nel proprio `helpText()` una riga con il percorso esatto
  per arrivarci, nella stessa notazione dell'esempio dato dall'utente:
  `MENU>NET>D>Ent(BCN)` si legge "dal menu principale vai su NET(WORK
  SCAN), premi D per aprire DISC(OVERY), poi ENTER sulla riga BCN". Le
  11 schermate raggiungibili con un solo ENTER dal menu principale non
  hanno bisogno di questa riga (sono già ovvie) e restano invariate. La
  riga sostituisce la riga vuota che separava titolo e contenuto in ogni
  `helpText()` toccato — stesso numero di righe totali di prima, nessun
  peggioramento del budget di spazio già stretto dalla Fase 37.

### Fase 42: il backup configurazione vive sotto /netrunner

Su richiesta esplicita dell'utente: `ConfigBackup` (il backup/restore
raggiungibile con `B`/`R` da SETTINGS — già copriva tutta la
configurazione persistita da questo firmware: `AppConfig`, le reti WiFi
salvate con relativa password, e l'allowlist di WAR DRIVING) scriveva
il proprio file a `/config_backup.json`, alla radice della SD, isolato
da ogni altro artefatto. Spostato a `/netrunner/config_backup.json` —
la stessa cartella condivisa dove finiscono già tutti gli altri export
di questo firmware (report di scan, `wardrive.csv`, `.pcap`) — così
compare anche nel salto rapido `N` di `FILE MANAGER` invece di dover
sapere che vive da solo alla radice. `ConfigBackup::backup()` crea la
cartella se non esiste ancora (`mkdir` è un no-op innocuo se c'è già),
stesso pattern già usato da `storage/NetrunnerPaths.h`.

Nessun percorso di migrazione da un vecchio backup alla radice: dato
che questo firmware non è ancora stato distribuito a nessun utente
oltre a chi lo sta testando in questa sessione, non esiste un backup
pre-Fase-42 da preservare — chi ha già un `/config_backup.json` alla
radice della SD da una build precedente dovrà rifare il backup una
volta con `B` dopo l'aggiornamento.

### Fase 43: bug fix — la scansione WiFi non partiva con una rete già salvata

Segnalazione diretta dell'utente: con una rete WiFi già salvata, `ENTER`
su WIFI SCAN non avviava mai una scansione — bisognava prima fare
"forget" perché ricominciasse a funzionare. La lista delle reti salvate
(tasto `S`) esisteva già (vedi `WifiSetupScreen::State::SavedList`), ma
di fatto era raggiungibile solo forgettando prima, che è l'opposto di
quello che dovrebbe fare una lista di reti salvate.

- **Causa**: `WifiManager::autoConnect()` (chiamato al boot se esiste
  una rete salvata) avvia `WiFi.begin()` in modo asincrono. Se quella
  rete non è raggiungibile in quel momento (fuori portata,
  temporaneamente spenta, password cambiata sul router), il driver WiFi
  dell'ESP32 continua a ritentare la connessione in background da solo —
  e quel tentativo perpetuo monopolizza il radio, impedendo a
  `WiFi.scanNetworks()` di completarsi mai (resta bloccato su
  `kScanRunning`, o fallisce subito). Non a caso `forgetSavedCredentials()`
  chiama esplicitamente `WiFi.disconnect()`: è quello che sblocca la
  scansione, non il "dimenticare" in sé.
- **Fix**: `WifiManager::beginScan()` ora chiama `WiFi.disconnect()`
  prima di scansionare, ma **solo se non si è già connessi** — una
  connessione già stabilita e funzionante non viene mai interrotta solo
  per cercare altre reti (questo hardware scansiona tranquillamente
  attorno a una connessione già associata). `WifiSetupScreen::onExit()`
  poi richiama `autoConnect()` se si esce dalla schermata senza essersi
  connessi a nulla, per non lasciare il dispositivo scollegato fino al
  prossimo riavvio solo per aver dato un'occhiata alle reti vicine.

### Fase 44: l'overlay di aiuto non tronca più su 10 schermate

Audit richiesto esplicitamente dall'utente ("vedi altri miglioramenti
UX/UI?"), con conteggio reale (non stimato) delle righe: il budget di
contenuto di `UiManager::drawHelpOverlay` è **8 righe esatte**, non le
~7 approssimate menzionate nella Fase 37 — geometria precisa: parte da
`y=26`, passo di 10px, taglio a `y < altezza-32` (103px su schermo
135px alto), quindi 8 iterazioni prima di fermarsi. Dieci schermate
superavano questo limite, alcune di molto:

| Schermata | Righe prima | Righe dopo |
|---|---|---|
| BEACON/PROBE INTEL | 19 | 8 |
| SENTINEL MODE | 15 | 8 |
| PMKID CAPTURE | 12 | 8 |
| DEAUTH + CAPTURE | 11 | 8 |
| WAR DRIVING | 10 | 7 |
| PORT MAPPING | 10 | 8 |
| GUARD MODE | 10 | 8 |
| HOST DETAIL | 9 | 8 |
| DISCOVERY | 9 | 7 |
| RUN ALL DISCOVERY | 9 | 8 |

Nessuna informazione realmente utile è stata rimossa — solo
condensata (frasi accorciate, spiegazioni su più righe unite in una,
righe vuote separatrici eliminate dove non servivano). Un effetto
collaterale positivo: il testo di RUN ALL DISCOVERY era anche
*obsoleto* (menzionava solo UPnP/mDNS/SNMP/data-store/CDP-LLDP/
passive-hosts/rogue-DHCP, senza IOT/OT SWEEP, LDAP, NTLM o
BEACON/PROBE aggiunti nelle fasi successive) — corretto insieme al
resto.

### Fase 45: WIFI SETUP mostra quante reti salvate restano

Ultimo dei miglioramenti UX/UI proposti nello stesso audit, implementato
su richiesta esplicita: la lista delle reti salvate (`S` da WIFI SETUP)
mostrava le reti ma non quante ce ne stanno — il tetto
`WifiManager::kMaxSavedNetworks = 3` restava invisibile fino al momento
in cui salvarne una quarta evinceva silenziosamente la meno usata di
recente, senza alcun avviso che fosse successo. `drawSavedList()` ora
mostra sempre "saved: N/3" in alto, anche a lista vuota (0/3), non solo
quando c'è già qualcosa da contare.

### Fase 46: NAME SPOOF — poisoning LLMNR/NBT-NS ("Responder-lite")

Terzo di 10 sviluppi offensive proposti in chat (esclusi bruteforce/
dizionari/DoS), implementato su richiesta esplicita. Stesso principio
di `Responder`: molti host Windows, quando la risoluzione DNS di un
nome fallisce, ripiegano su due protocolli LAN legacy — LLMNR
(multicast UDP 224.0.0.252:5355) e NBT-NS (broadcast UDP/137) — che
chiedono "chi possiede questo nome?" a chiunque sia in ascolto, senza
alcuna autenticazione su chi può rispondere. `NameSpoofManager`
risponde a OGNI query che vede rivendicando l'IP di questo dispositivo,
loggando ogni nome avvelenato e l'host che l'ha chiesto — un finding
concreto e riportabile in un pentest ("questi host accettano risposte
di risoluzione nome non autenticate"), a prescindere da cosa succede
dopo.

A differenza di `ArpSpoofManager`/`DeauthManager`/`PmkidManager`, non
serve la modalità promiscua: LLMNR/NBT-NS sono normale traffico UDP
multicast/broadcast, quindi gira con un socket `WiFiUDP` ordinario e
non porta via la radio a WiFi/altri strumenti — può girare insieme a
una normale connessione STA.

Wire format nuovo in `net/`:
- `LlmnrWire` — riusa il formato messaggio DNS (stesso di `DnsWire`,
  vedi Fase 10) via multicast invece che unicast/53: parsa una query in
  arrivo, costruisce una risposta A-record che riusa i byte
  header+question originali (stesso id, stessa domanda) più un RR
  risposta con puntatore di compressione all'offset 12.
- `NbnsWire` — formato NetBIOS Name Service (RFC 1002 §4.2), diverso
  da DNS: nome "first-level encoded" a 32 byte (ogni byte originale
  diviso in due nibble, ogni nibble mappato su 'A'..'P'). Decodifica il
  nome, costruisce una Name Query Response che riusa gli stessi byte
  nome codificato (nessun puntatore di compressione qui — non
  affidabile tra implementazioni diverse, a differenza del trucco DNS).

`NameSpoofScreen` (`MENU>Ent(NSPF)`, nuova voce di primo livello)
segue esattamente lo stesso schema di `MitmScreen`: Idle mostra solo
durata regolabile (`</>`) e un avviso ambra su cosa fa la sessione;
Running mostra un banner rosso a piena larghezza "NAME SPOOF ACTIVE"
(mai nascosto, stessa scelta di MITM AUDIT — non deve sembrare uno
strumento silenzioso), contatore di quante query sono state avvelenate,
secondi rimanenti, log live. Come le altre voci top-level *offensive*
(finora solo raggiungibili da HOST DETAIL/WAR DRIVING), passa da
`OffensiveDisclaimerScreen` la prima volta in questa sessione — per
supportarlo su una voce di menu di primo livello, `MenuItem` ha
guadagnato un campo `offensive` che `MainMenuScreen::onKey` controlla
prima di aprire la schermata, invece di richiedere che ogni schermata
offensiva passi da un target intermedio come fanno oggi deauth/evil-
twin/PMKID.

**Tagli di scope deliberati** (vedi anche "Limiti noti"): nessuna
cattura di credenziali. Il vero valore di Responder viene dal servire
un finto server SMB/HTTP dietro il nome avvelenato per ricevere e
loggare l'handshake NTLMv2 del client — costruire un responder SMB2
NEGOTIATE/SESSION_SETUP corretto è un pezzo di lavoro grande e
separato, fuori scope per questa fase. Qui ci si ferma alla prova che
il poisoning funziona, non alla cattura dell'hash.

### Fase 47: OS FINGERPRINT — TTL/finestra/opzioni TCP da un SYN-ACK

Sesto di 10 sviluppi offensive proposti in chat, implementato su
richiesta esplicita. Fingerprinting passivo dello stack TCP/IP in stile
`p0f`, ma volutamente ridotto all'osso: ascolta in modalità promiscua
pacchetti TCP SYN-ACK e legge, per host sorgente, l'unico segnale
davvero affidabile senza un vero database di firme — il TTL iniziale,
arrotondato al più vicino tra i tre valori che gli stack reali usano
davvero (Linux/BSD/macOS/Android di default 64, Windows 128,
apparati di rete/Unix datati 255) — insieme alla finestra TCP grezza e
all'ordine delle opzioni TCP osservate.

Scelta deliberata: **non** tenta di indovinare un OS/versione specifico
da finestra+ordine opzioni come farebbe un vero file di firme p0f —
servirebbero migliaia di firme verificate che questo progetto non ha
modo di costruire o controllare, quindi i due campi grezzi vengono
mostrati così come sono (via `I:detail`) per chi conosce p0f e vuole
interpretarli da sé, invece di trasformarli in un'etichetta con falsa
precisione. Stessa filosofia "sola lettura, mai millantare" di
`UdpProbe.h` (aperto vs silenzioso) e delle etichette "best-effort" già
usate per WPS.

`scan/OsFingerprint` riusa `ieee80211::parseDataFrame`/`parseSnap`
(già condivisi da `PassiveHostDiscovery`/`CdpLldpSniffer`/
`RogueDhcpDetector`) per arrivare all'header IP, poi fa un parsing
diretto di IP+TCP mai fatto prima in questo progetto: verifica
protocollo TCP, filtra sui flag SYN+ACK, legge il TTL e la finestra a
offset fissi, e cammina sulle opzioni TCP codificando l'ordine come una
stringa di lettere (M=MSS, W=Window Scale, S=SACK Permitted,
T=Timestamps, N=NOP). Tenuto come lista a sé con schermata propria
invece che dentro la tabella host di `ScanManager`, stessa scelta
architetturale di `PassiveHostDiscovery` (vedi il suo commento in
testa al file per il ragionamento completo).

Nuova voce `OS FINGERPRINT` nel gruppo "PASSIVE LISTENERS" di
`DISCOVERY` (`MENU>NET>D>Ent(OSFP)`) — nessun prerequisito, funziona in
standalone come PASSIVE HOSTS/GUARD MODE. Serve un host che tenti/
completi un handshake TCP mentre è in ascolto: capita naturalmente
durante un PORT SCAN su un target, o quando qualunque dispositivo
sulla LAN apre una propria connessione in uscita.

### Fase 48: VLAN HOP — leak detector 802.1Q + probe double-tagging best-effort

Ottavo dei 10 sviluppi offensive proposti in chat, implementato su
richiesta esplicita. Adattamento onesto del classico "VLAN hopping" a
un dispositivo che è una stazione WiFi, non una porta switch cablata —
**leggere prima di fidarsi di un risultato negativo**: il VLAN hopping
vero (switch spoofing via DTP, o double-tagging attraverso il native
VLAN permissivo di un trunk) è un attacco contro una porta switch
CABLATA. Questo Cardputer non ha alcuna PHY Ethernet: può vedere o
iniettare tag 802.1Q solo se l'AP stesso fa da bridge di frame taggati
sul segmento wireless, cosa che l'802.11 normalmente non fa (il
tagging VLAN è un concetto lato cablato; un AP ben configurato toglie i
tag prima/dopo il salto wireless). Restano quindi due capacità più
strette, ma oneste:

1. **PASSIVO (la parte affidabile)**: `scan/VlanHopProbe` ascolta in
   modalità promiscua qualunque frame che porti un tag 802.1Q
   (EtherType 0x8100) dentro il payload SNAP-incapsulato che un frame
   dati WiFi trasporta. Vederne anche solo UNO è già di per sé un
   finding — un client non dovrebbe MAI vedere tag VLAN sul segmento
   wireless — indipendentemente dal fatto che un vero double-tag hop
   funzionerebbe.
2. **ATTIVO (best-effort, non confermabile)**: tasto `P` (gate
   `OffensiveDisclaimerScreen`, come deauth/PMKID/evil-twin/NAME SPOOF)
   costruisce e invia UN frame ARP broadcast con due tag 802.1Q
   impilati (esterno = VLAN nativo presunto, interno = VLAN target),
   lo stesso trucco di un vero attacco double-tagging. Se raggiunga
   davvero il VLAN target dipende interamente da come è configurata la
   porta switch a monte dell'AP — cosa che questo dispositivo non ha
   modo di osservare da qui. Un invio riuscito viene riportato come
   "inviato", MAI come "riuscito": solo un ascoltatore già seduto sul
   VLAN target potrebbe confermarlo.

Nuova voce `VLAN HOP` nel gruppo "PASSIVE LISTENERS" di `DISCOVERY`
(`MENU>NET>D>Ent(VLAN)`). La schermata è per lo più passiva (nessun
gate per l'ascolto leak) — solo il tasto `P` è l'azione offensiva, gate
applicato in linea sulla singola pressione invece che sull'ingresso
nella schermata (a differenza di NAME SPOOF, dove l'intera schermata
È l'azione offensiva). Per evitare una voce duplicata nello stack di
navigazione quando il gate non è ancora superato, il codice usa
`g_ui.replaceScreen()` (non `pushScreen()`) per sostituire sé stessa
con `OffensiveDisclaimerScreen` — altrimenti l'accettazione del
disclaimer richiamerebbe `replaceScreen(VlanHopScreen)` sopra una
`VlanHopScreen` già presente più in basso nello stack, lasciando due
voci per la stessa schermata.

### Fase 49: EVIL TWIN guadagna KARMA mode

Secondo dei 10 sviluppi offensive proposti in chat, implementato su
richiesta esplicita — con una correzione di premessa importante fatta
durante l'implementazione: la proposta originale diceva "EvilTwinManager
oggi rileva solo evil twin altrui", ma non era corretto — `EvilTwinManager`
è già uno strumento attivo (crea un AP look-alike vero, non solo lo
rileva; la *rilevazione* passiva vive separatamente nell'euristica di
`WardrivingManager`). Quello che mancava davvero era il KARMA mode.

Un vero attacco Karma risponde a OGNI probe request di un client,
individualmente e all'istante, con una probe response contraffatta che
rivendica qualunque SSID il client abbia appena chiesto — richiede
controllo a basso livello sulle probe response per-client che l'API
softAP standard di Arduino/esp-idf non espone. `startKarma()` fa
un'approssimazione più grezza ma realizzabile con l'hardware
disponibile, ed è onesto nel dirlo: fotografa le SSID che
`BeaconProbeSniffer` ha già visto probare da client vicini (la loro
PNL — serve aver girato BEACON/PROBE INTEL prima), poi cicla il SINGOLO
softAP di questo dispositivo attraverso quella lista di candidate, 8
secondi ciascuna (`kKarmaDwellMs`), sperando che un dispositivo in
portata con quella rete già nella propria PNL si riassoci durante
quella finestra. Più lento e meno certo di un vero Karma, ma riusa
tutto il codice di lifecycle AP + log associazioni che il modo a SSID
fisso aveva già.

Da `EVIL TWIN` (`MENU>WD>E(TWIN)`, stesso gate offensivo di sempre):
`TAB` invece di `ENTER` avvia KARMA senza bisogno di digitare nulla —
`TAB` e non una lettera perché l'inserimento testo per l'SSID è attivo
in quello stato, e una SSID potrebbe legittimamente contenere quella
lettera. La schermata mostra il conteggio di candidate disponibili
prima di partire, e durante l'esecuzione l'SSID attualmente trasmesso
con indice/totale (`KARMA (i/n): <ssid>`). `Association` ora porta
anche l'SSID a cui un client si è associato (prima solo MAC+timestamp),
utile in KARMA mode dove l'SSID broadcast cambia nel tempo — a modo
fisso il valore resta semplicemente costante come prima.

### Fase 50: MITM AUDIT cattura per davvero cookie/credenziali in chiaro

Terzo dei 10 sviluppi offensive proposti in chat, implementato su
richiesta esplicita. `ArpSpoofManager::analyzeFrame()` rilevava già da
tempo quattro tipi di leak in chiaro (header `Cookie:`, `Authorization:
Basic`, comandi FTP/Telnet `USER `/`PASS `) — ma solo come segnalazione
riassuntiva ("target -> IP:porta leaked HTTP session cookie"), senza
mai catturare il VALORE reale. Per un audit che deve dimostrare il
rischio (o permettere una review seria) serve il contenuto vero, non
solo la categoria.

`findAsciiLine()` sostituisce il vecchio `containsAscii()` booleano:
stessa ricerca, ma ora ritorna l'intera riga in cui il match è stato
trovato (fino a CR/LF, fine buffer, o `kMaxLineLen` = 160 byte),
sostituendo i byte non stampabili con `.` invece di assumere un buffer
ASCII pulito — sta scansionando un frame catturato grezzo, non uno
stream già validato. Ogni riga catturata finisce in un nuovo
`HarvestedItem` (kind/riga/MAC sorgente/IP+porta destinazione/
timestamp), tenuto in RAM (fino a `kMaxHarvested` = 50, stesso ring
buffer FIFO del log) E appeso live su SD in `/mitm/harvest.csv` — così
sopravvive anche se la sessione finisce in modo brusco, non solo
quello che sta nel buffer in RAM.

Da `MITM AUDIT` in esecuzione, `H` passa dal log live a una lista
scorrevole delle credenziali catturate (kind + anteprima riga), `I`
mostra il dettaglio completo (riga intera, non troncata), `H`/`DEL`
torna al log. Materiale realmente sensibile — stesso livello di
attenzione già riservato a un pcap PMKID/handshake, non un giocattolo.

### Fase 51: EAP IDENTITY — harvesting passivo di username 802.1X in chiaro

Settimo dei 9 sviluppi offensive di seconda analisi proposti in chat,
implementato su richiesta esplicita. Durante l'associazione a una rete
WPA-Enterprise (802.1X), la primissima risposta EAP del client contiene
l'"outer identity" — lo username scelto per l'esterno (`user@corp`,
`DOMAIN\user`, o un placeholder anonimo `anonymous@corp`) — inviato in
CHIARO prima ancora che parta il tunnel TLS PEAP/TTLS che protegge le
credenziali vere. Una rete che rivela username reali nel clear-text
prima del tunnel è un finding classico e riportabile negli audit WiFi
aziendali, esattamente ciò che il dissector EAP di Wireshark mostra
gratis dagli stessi byte.

`net/EapolWire` guadagna `parseEapIdentity()` accanto al preesistente
`classify()` — stesso file perché è la stessa famiglia di frame (EAPOL
sopra 802.11), solo un EAPOL Type diverso (0 = EAP-Packet invece di 3 =
Key). Fail-closed nello stesso stile del resto: rifiuta frame protetti,
buffer troncati, capitalizzazioni sbagliate di Code/Type EAP, e
riconosce solo EAP-Response/Identity (Code=2, Type=1) — mai la Request
del server, mai altre estensioni EAP, mai niente dentro il tunnel TLS.

`scan/EapIdentityHarvester` è il consumatore promiscuo (stesso schema
di `OsFingerprint`/`PassiveHostDiscovery`): dedup su `(MAC, identity)`
per lasciare visibile lo stesso client con outer identity diverse su
reti diverse, log SD in `/eap/identities.csv` per sopravvivere a fine
sessione brusco. Nuova voce `EAP IDENTITY` nel gruppo "PASSIVE
LISTENERS" di `DISCOVERY` (`MENU>NET>D>Ent(EAP)`), `I` per il dettaglio.

**Limite di canale**, non un bug: come tutti gli altri ascoltatori
passivi non-hopper (`PassiveHostDiscovery`/`OsFingerprint`/
`CdpLldpSniffer`), sta sul canale a cui la STA è associata. Le
autenticazioni 802.1X sono eventi brevi e infrequenti — hopping li
farebbe perdere di sicuro e nel frattempo dropperebbe la connessione
STA (vedi `BeaconProbeSniffer.h`). Parcheggiare questo dispositivo sul
canale dell'AP enterprise target è la via corretta.

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
>
> **Aggiornamento (dopo la Fase 39)**: una build successiva, con tutto
> il codice fino alla Fase 39 incluso, ha completato la compilazione
> senza errori — solo alcuni warning del compilatore, analizzati e
> corretti in Fase 40 (vedi sopra), incluso un bug funzionale reale
> (`SentinelManager::kCaptureLen`) che nessuna revisione manuale aveva
> individuato. Il flash/boot con questo codice non è stato ancora
> confermato su hardware.

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
      Moduli exploit/RCE, brute-force SSH, rilevamento SMBv1/UPnP
      deliberatamente non implementati — vedi sopra.
- [x] **Fase 7 — Radar + donut + dashboard**: richiesti di nuovo
      dall'utente dopo la Fase 6; radar animato su `HOST DETAIL`
      (puramente decorativo), donut chart su `PORT MAPPING` (dati
      reali: aperte vs. range configurato), homepage con skyline
      statica e status bar (uptime/READY/IP) su `MAIN MENU`. Prima
      verifica su hardware reale di `drawCircle`/`fillCircle`/
      `drawLine`/`fillArc` — vedi sopra.
- [x] **Fase 8 — Database porte/servizi completo**: ~12.000 porte
      TCP/UDP con nome servizio reale (fatti estratti indipendentemente,
      non il file nmap-services stesso — vedi sopra per la licenza),
      stesso design a binary-search-su-flash del DB OUI. Corretto anche
      un bug reale nel path di default di `OuiDatabase` (cercava
      `/oui.bin` invece di `/oui/oui.bin`, fallendo silenziosamente) e
      una regressione nell'audit credenziali sulle porte HTTP
      alternative introdotta dal nuovo DB.
- [x] **Fase 9 — Restyle splash + NETWORK SCAN**: skyline condivisa
      estratta in `chrome::drawSkyline`, boot screen esteso con una
      vista dashboard (titolo `NETRUNNER`, sottotitolo, versione,
      skyline, status bar) dopo il boot log, `NETWORK SCAN` con stat
      strip `HOSTS FOUND`/`SCAN TIME` e tabella host incorniciata con
      intestazione colonne — vedi sopra. `PORT MAPPING` non toccata
      (non richiesta). Skyline rimossa da `MAIN MENU` in un addendum
      successivo, su richiesta dell'utente.
- [x] **Fase 10 — Cronologia scan/diff, SD, WiFi multi-rete, probe UDP,
      mDNS, firme vulnerabilità note, OTA**: otto migliorie richieste in
      blocco dall'utente da una lista di proposte di questo assistente —
      vedi sopra per il dettaglio di ciascuna. SD card ora cablata di
      default (era rimandata dalla Fase 5); mDNS ora implementato come
      fallback a NBNS (era esplicitamente rimandato dalla Fase 2).
- [x] **Fase 11 — War driving passivo + allowlist, orario NTP**: scan
      WiFi continuo con log di ogni AP visto su SD (mai connessione,
      salvo per SSID esplicitamente autorizzati dall'utente in una
      allowlist dedicata — vedi sopra per il perché di questo scope
      rispetto alla richiesta originale) + orario reale via NTP pubblico,
      usato per timestampare la cronologia scan e sostituire l'uptime
      nelle status bar una volta sincronizzato.
- [x] **Fase 12 — Splash da mockup, audio, rinomina, scroll, screen
      timeout, detection**: sette migliorie indipendenti — splash
      ridisegnata (skyline graduata + griglia prospettica), melodia di
      avvio + beep su nuova rete aperta in war driving (`ui/Sound`,
      finalmente collegato ad `AppConfig::uiSoundEnabled`), "WIFI SETUP"
      → "WIFI SCAN", scroll sulla lista AP di `WAR DRIVING`, timeout
      schermo 30s (dimming, non blocca nulla in background), rilevamento
      WiFi più completo (SSID nascosti, dwell più lungo) — vedi sopra
      per il dettaglio di ciascuna.
- [x] **Fase 13 — Scan BLE passivo, evil-twin, audio audit, STATS,
      backup config, baseline dispositivi, signal finder**: sette
      migliorie scelte dall'utente da una lista di dieci proposte da
      questo assistente — vedi sopra per il dettaglio di ciascuna. Lo
      scan BLE (`BleScanManager`) è stato il primo modulo di questa fase
      passato da una build reale, che su due tentativi consecutivi ha
      trovato tre bug veri (vedi sopra) e infine fatto sforare gli slot
      OTA — poi **rimosso interamente in Fase 14** su richiesta
      dell'utente.
- [x] **Fase 14 — Rimozione scan BLE**: modulo tolto per risparmiare
      spazio flash, su richiesta esplicita dell'utente — vedi sopra.
      Le altre sei migliorie della Fase 13 restano tutte attive.
- [x] **Fase 15 — Melodia di boot cyberpunk**: il jingle di 4 note in
      maggiore sostituito da un riff synthwave di 13 note in la minore
      (~1,5s) — vedi sopra per il dettaglio musicale e per il ritocco al
      timing del prompt lampeggiante che ne è conseguito.
- [x] **Fase 16 — Strumenti offensive per reti locali**: otto
      funzionalità scelte dall'utente da una lista di dieci proposte di
      questo assistente — ARP spoofing/MITM monodirezionale senza
      relay, sniffing/cookie-sniffing passivo e DNS spoofing durante il
      MITM, evil twin attivo, deauth mirato + cattura handshake WPA su
      pcap, path brute-forcer HTTP, credential guessing esteso a
      POP3/IMAP/SMTP (SSH deliberatamente escluso) — vedi sopra per il
      dettaglio di ciascuna e per il gate di autorizzazione rafforzato
      condiviso dai tre strumenti attivi. `ArpSpoofManager`/
      `DeauthManager` restano il codice meno verificato di tutto il
      progetto — la prima build vera ha già trovato due bug reali
      (firma della callback promiscua, `String` mancante in
      `EvilTwinManager.h`), entrambi corretti, vedi sopra.
- [x] **Fase 17 — Musica di boot in loop + nebbia digitale**: il riff
      di boot della Fase 15 sostituito da un loop continuo stile
      "Nightcall" (composizione originale, non una trascrizione — vedi
      sopra) che suona finché non si entra in `MAIN MENU`, più un
      leggero effetto di nebbia/statica puntinata sullo sfondo dello
      splash.
- [x] **Fase 18 — Discovery passiva, negotiate SMB e report kill-chain**:
      sei funzionalità scelte dall'utente (2,3,4,5,7,10 di una lista di
      dieci) tenute sul lato difensivo/benigno su richiesta esplicita
      *"non implementare funzionalità rischiose"* — cattura PMKID senza
      deauth (dietro il gate offensive, unica offensiva del giro),
      sniffing passivo CDP/LLDP (`LAN TOPOLOGY`), discovery UPnP/SSDP
      (`UPNP DISCOVERY`), rilevamento passivo di rogue DHCP (`ROGUE
      DHCP`), check SMB1 Negotiate (solo Security Mode, non enumeration —
      `S` su `HOST DETAIL`) e report HTML kill-chain su SD (`R` su
      `NETWORK SCAN`). Più il refactor condiviso `net/Ieee80211Frame` +
      `storage/PcapWriter` per i cinque consumatori promiscui — vedi
      sopra per il dettaglio e per il limite della radio condivisa che
      ora vale per cinque funzioni.
- [x] **Fase 19 — UX/discovery: status bar radio, THREATS, auto-assess,
      host passivi, servizi mDNS, SNMP**: sei migliorie scelte dall'utente
      (1,2,5,6,7,8 di una lista di dieci), tutte difensive/benigne —
      barra di stato in header che segnala il proprietario della radio
      promiscua (`RF:<nome>` / `RF:N!` rosso in conflitto), dashboard
      `THREATS` che aggrega live i finding, workflow `AUTO ASSESS` a un
      tasto (discovery→port scan→report, audit credenziali escluso di
      proposito), scoperta host passiva (`PASSIVE HOSTS`), enumerazione
      servizi mDNS/DNS-SD (`SERVICE SCAN`) e sweep SNMP `public`
      (`SNMP SWEEP`). Il punto 10 (Responder-lite/NetNTLM) escluso su
      istruzione esplicita perché ad alto rischio. Il menu principale è
      ora a 15 voci (lo scroll aggiunto in Fase 18 le regge).
- [x] **Fase 20 — Connessione alle reti aperte dal war driving +
      rilevamento captive portal**: `c` su una sighting aperta apre
      `JOIN OPEN NET`, che conferma l'autorizzazione, si unisce alla rete
      aperta e lancia il rilevamento captive portal (`net/
      CaptivePortalDetector`, la stessa GET a un endpoint `generate_204`
      che fa ogni OS). Il **bypass** del portale è stato **declinato di
      proposito** (accesso non autorizzato a reti di terzi) — vedi sopra e
      "Limiti noti".
- [x] **Fase 21 — Data-store esposti + audit servizi per-host**: cinque
      sviluppi offensive (1,2,4,5,8 di una lista di dieci) per assessment
      autorizzato — sweep `DATASTORE SWEEP` dei data store senza auth
      (Redis/Memcached/Elasticsearch/MongoDB, read-only) e `SERVICE AUDIT`
      per-host (`V` da HOST DETAIL, gated) con anon-access + default-creds
      su FTP/SMB/Redis/MySQL/PostgreSQL/VNC/HTTP. Crypto via `mbedtls`
      (SHA1/MD5/DES), non artigianale. MSSQL/NFS/SMB-NetShareEnum/MySQL-8
      caching_sha2/PG-SCRAM/HTTP-form fuori scope — vedi sopra e "Limiti
      noti".
- [x] **Fase 27 — Beacon/probe intelligence + PNL harvesting, mDNS/DNS-SD
      correlato agli host, RTC a batteria**: tre evoluzioni scelte da una
      lista di dieci proposte in fase di analisi — sniffer passivo di
      Beacon/Probe-Request/Response (`BEACON/PROBE INTEL`, mai un frame
      trasmesso, channel-hopping su tutti i canali 2.4GHz, reveal SSID
      nascosti + PNL dei client con rilevamento MAC randomizzato);
      correlazione del browser DNS-SD (già completo dalla Fase 19) agli
      host scoperti via l'IP sorgente della risposta mDNS, con hostname
      derivato dal nome istanza quando NBNS/reverse-PTR non trovano
      nulla; supporto RTC a batteria (unità Grove tipo M5Stack RTC Unit)
      per un orario reale disponibile da subito al boot, senza aspettare
      il WiFi — vedi sopra e "Limiti noti".
- [x] **Fase 28 — Ogni report di scansione su SD sotto `/netrunner`**:
      l'export JSON/CSV e il report HTML di `NETWORK SCAN`/`AUTO ASSESS`
      non sovrascrivono più tre nomi fissi (`/export.json`, `/export.csv`,
      `/report.html`) ma finiscono uno per run in `/netrunner/`, col nome
      `<timestamp>_<SSID>.<ext>` (`storage/NetrunnerPaths`) — storico
      completo invece di un solo snapshot sempre sovrascritto.
- [x] **Fase 29 — Anche i file del war driving sotto `/netrunner`**: su
      richiesta esplicita, il log continuo `wardrive.csv` e gli export
      per-AP di `WardrivingManager` (prima in `/wardrive/`) si spostano
      anch'essi sotto `/netrunner/` — un solo posto per ogni artefatto di
      scansione. Gli export per-AP guadagnano anche un timestamp, che
      prima non avevano.
- [x] **Fase 30 — Sweep LDAP (anon-bind + rootDSE) e disclosure NTLM-
      over-HTTP**: `LDAP SWEEP` (bind anonimo + lettura rootDSE, porta
      389) e `NTLM DISCLOSURE` (negoziazione NTLM su HTTP, dominio/
      hostname dal Type 2 challenge, mai un handshake completato) nel
      sotto-menu DISCOVERY, entrambi non gated (read-only, nessuna
      credenziale vera). `net/LdapWire` e `net/NtlmWire` implementano il
      minimo di BER/NTLM necessario, verificati prima dell'uso contro
      librerie Python reali (`ldap3`/`pyasn1`, `ntlm-auth`) — vedi sopra.
- [x] **Fase 31 — Seconda passata UX/UI trasversale**: indicatori di
      scorrimento (`chrome::drawScrollMarkers`) su tutte le 21 liste
      scorrevoli, `helpText()` su tutte le 44 schermate, vista a valore
      completo (tasto `I`) per i campi troncati su 11 schermate, sotto-
      menu DISCOVERY raggruppato per prerequisito invece di elenco piatto,
      indicatore `BG:xxx` nell'header per le attività in background non
      promiscue, rimozione del donut chart inutilizzato da `PORT SCAN`.
- [x] **Fase 32 — `PORT SCAN` copre anche le porte comuni sopra la
      1024**: ~50 porte curate (`scan/WellKnownHighPorts.h`) — database,
      RDP/VNC/WinRM, pannelli/dev server, `31337` — sondate su ogni scan
      insieme al range configurato in `SETTINGS`, deduplicate contro di
      esso. Nessuna modifica a banner grabbing/lookup servizio/firme
      vulnerabili, che restano identici sulle porte nuove.
- [x] **Fase 33 — Primo lotto verso un sistema WiFi cyber security più
      evoluto**: rilevamento WPS (enabled/locked/config methods) su
      `BEACON/PROBE INTEL`, nuovo `GUARD MODE` per il rilevamento
      passivo di flood deauth/disassoc altrui, euristica evil-twin
      rivista (tier di sicurezza + vendor OUI invece del solo confronto
      di enum di cifratura), nuova schermata `CHANNEL SCAN` per
      l'affollamento dei 13 canali 2.4GHz. Entrambi i nuovi rilevamenti
      (WPS sbloccato, flood in corso) alimentano anche `THREATS`.
- [x] **Fase 34 — SENTINEL MODE**: nuova voce del menu principale,
      `scan/SentinelManager` — ri-scoperta periodica (30s) della propria
      rete connessa con allarme sonoro su ogni dispositivo mai visto
      prima (baseline `ScanHistory::loadKnownMacs`, caricata una volta e
      tenuta solo in memoria per non inquinare la cronologia condivisa),
      più un dump continuo del traffico 802.11 (management+data, header
      sempre leggibili, payload mai decriptato) su `.pcap` sotto
      `/netrunner`.
- [x] **Fase 35 — SENTINEL MODE si evolve**: rilevamento "dispositivo
      scomparso" (simmetrico al "dispositivo nuovo", 2 cicli mancati
      consecutivi), rilevamento flood deauth/disassoc integrato
      direttamente (stessa logica di GUARD MODE, nessuna sessione
      promiscua separata), rotazione automatica del pcap ogni 5MB per
      parte, riepilogo di sessione (`_summary.txt`) scritto allo stop
      con ogni evento nuovo/scomparso/flood. Schermata aggiornata a un
      log eventi unificato con colore per tipo.
- [x] **Fase 36 — Rilevamento EAPOL a bordo + PMKID sweep**:
      `net/EapolWire` classifica strutturalmente i frame EAPOL-Key
      catturati (Message1-4, presenza KDE PMKID) senza mai leggere
      nonce/MIC/PMKID stessi; `PmkidManager`/`DeauthManager` mostrano ora
      un verdetto "cattura riuscita" a fine sessione; nuovo `PMKID SWEEP`
      (`scan/PmkidSweepManager`, tasto `S` da WAR DRIVING) esegue PMKID
      CAPTURE in sequenza su ogni AP non aperto già noto invece di uno
      alla volta a mano. Vincolo esplicito rispettato: cattura solo, mai
      craccare.
- [x] **Fase 37 — Terza passata UX/UI trasversale (15 punti)**: breadcrumb/
      help text mancanti, `THREATS` integra SENTINEL MODE e PMKID SWEEP,
      scorciatoie `FILE MANAGER` a `/netrunner`/`/handshakes`, nuova
      schermata `CAPTURES` (browser unificato dei `.pcap`), indicatore di
      prontezza per riga in `DISCOVERY`, sottomenu offensivo raggruppato
      in `WAR DRIVING`, chiarito il rapporto GUARD MODE/SENTINEL MODE,
      indicatore "molto occupato" nell'header, nuova schermata `ACTIVITY`
      (dashboard di ogni task in background), tasso per finestra in
      `GUARD MODE`, anteprima target con RSSI in `PMKID SWEEP`, legenda
      tasti globali nell'overlay di aiuto.
- [x] **Fase 38 — Sweep IoT/OT + PLAYBOOK**: `scan/IotOtProbe` rileva
      MQTT/Modbus TCP/CoAP raggiungibili senza autenticazione (stesso
      rischio/stesso schema di `DATASTORE SWEEP`), wired in `DISCOVERY`
      e `RUN ALL DISCOVERY`; nuovo `scan/PlaybookRunner` + schermata
      `PLAYBOOK` (menu principale) esegue sequenze scriptabili di
      manager/orchestratori già esistenti — tre preset: FULL RECON
      (AUTO ASSESS poi RUN ALL DISCOVERY), QUICK IOT/OT (network scan +
      SNMP/datastore/IoT-OT), WIRELESS SURVEY (war driving + beacon/
      probe, l'unico senza bisogno di WiFi connesso).
- [x] **Fase 39 — IOT/OT SWEEP copre BACnet/DNP3 + integrazione THREATS**:
      `IotOtProbe` rileva anche BACnet/IP (Who-Is/I-Am) e DNP3 (Link
      Status Request con CRC-16 nativo del protocollo), entrambi
      protocolli OT senza alcuna autenticazione per progettazione;
      `THREATS` ora integra tutti i finding IOT/OT SWEEP (Critical per
      Modbus/BACnet/DNP3, Warning per MQTT/CoAP).
- [x] **Fase 40 — Bug fix dalla prima build reale**: corretto un bug
      funzionale reale (`SentinelManager::kCaptureLen` dichiarato
      `uint8_t`, `256` che tronca silenziosamente a `0` — il dump pcap
      di SENTINEL MODE scriveva record vuoti per ogni frame dalla Fase
      34) più i warning di troncamento format-string in
      `net/TimeSync.cpp`/`ui/ActivityStatus.cpp`; audit più ampio senza
      altre istanze dello stesso pattern trovate altrove.
- [x] **Fase 41 — Percorso di navigazione visibile ovunque**: breadcrumb
      dell'header esteso a tutti i livelli dello stack (non solo il
      genitore immediato), `title()` aggiunto alle 35 schermate che ne
      erano prive, e una riga di percorso completo (con i tasti esatti,
      es. `MENU>NET>D>Ent(BCN)`) nell'help overlay di ogni schermata
      raggiungibile con più di un salto dal menu principale.
- [x] **Fase 42 — Backup configurazione sotto /netrunner**: `ConfigBackup`
      (`B`/`R` da SETTINGS — config, reti WiFi salvate con password,
      allowlist WAR DRIVING) scrive ora a `/netrunner/config_backup.json`
      invece che alla radice della SD, coerente con ogni altro export di
      questo firmware.
- [x] **Fase 43 — Bug fix: scansione WiFi bloccata da una rete salvata
      irraggiungibile**: `WifiManager::beginScan()` interrompe ora un
      tentativo di connessione bloccato (mai riuscito) prima di
      scansionare, invece di lasciare che monopolizzi il radio — una
      connessione già stabilita non viene mai toccata.
      `WifiSetupScreen::onExit()` ripristina il tentativo se si esce
      senza esserci connessi a nulla.
- [x] **Fase 44 — Overlay di aiuto: fine del troncamento su 10
      schermate**: budget reale misurato a 8 righe esatte (non ~7 come
      stimato in Fase 37); tutti i `helpText()` che lo superavano
      (BEACON/PROBE INTEL a 19 righe, SENTINEL MODE a 15, fino a HOST
      DETAIL a 9) condensati per starci, nessuna informazione persa.
- [x] **Fase 45 — WIFI SETUP: contatore reti salvate**: la lista reti
      salvate (`S`) mostra ora "saved: N/3", visibile anche a lista
      vuota, invece di lasciare invisibile il tetto
      `kMaxSavedNetworks` fino all'eviction silenziosa di una rete.
- [x] **Fase 46 — NAME SPOOF (poisoning LLMNR/NBT-NS)**: nuovo
      `scan/NameSpoofManager` risponde a ogni query LLMNR/NBT-NS sulla
      LAN rivendicando l'IP di questo dispositivo (stessa tecnica di
      Responder), con nuovo wire format `net/LlmnrWire`+`net/NbnsWire`
      e nuova voce di menu top-level `NAME SPOOF` (gate
      `OffensiveDisclaimerScreen`, come deauth/PMKID/evil-twin). Nessuna
      cattura di credenziali in questa fase — solo prova/log del
      poisoning riuscito, vedi "Limiti noti".
- [x] **Fase 47 — OS FINGERPRINT (TTL/finestra/opzioni TCP da SYN-ACK)**:
      nuovo `scan/OsFingerprint` ascolta in modalità promiscua pacchetti
      TCP SYN-ACK e mostra, per host, il bucket TTL (64/128/255 —
      l'unica etichetta OS che si permette di dare per certa), finestra
      TCP grezza e ordine delle opzioni osservate, stile `p0f` ma senza
      fingere una precisione di firma che non ha. Nuova voce `OS
      FINGERPRINT` nel gruppo passivo di `DISCOVERY`.
- [x] **Fase 48 — VLAN HOP (leak 802.1Q + probe double-tag best-effort)**:
      nuovo `scan/VlanHopProbe` — ascolto passivo di tag 802.1Q che
      trapelano sul segmento wireless (finding già di per sé), più
      probe attivo (`P`, gate offensivo) che invia un frame ARP con due
      tag 802.1Q impilati. Onesto sul limite hardware: WiFi, non porta
      switch cablata — un invio riuscito è riportato come "inviato",
      mai come "riuscito". Nuova voce `VLAN HOP` nel gruppo passivo di
      `DISCOVERY`.
- [x] **Fase 49 — EVIL TWIN: KARMA mode**: `EvilTwinManager::startKarma()`
      cicla il softAP attraverso le SSID che `BeaconProbeSniffer` ha
      visto probare da client vicini (8s ciascuna), invece di un solo
      SSID fisso digitato dall'utente — approssimazione onesta di un
      vero attacco Karma, dato il limite dell'API softAP standard. `TAB`
      da `EVIL TWIN` (invece di digitare) avvia KARMA; `Association` ora
      registra anche l'SSID a cui un client si è associato.
- [x] **Fase 50 — MITM AUDIT: harvesting reale cookie/credenziali**:
      `findAsciiLine()` cattura ora la riga intera (non solo "trovato
      un cookie") per Cookie/Basic Auth/FTP-Telnet, in un nuovo
      `HarvestedItem` (RAM + `/mitm/harvest.csv`). `H` da MITM AUDIT
      in esecuzione apre la lista scorrevole, `I` il dettaglio completo.
- [x] **Fase 51 — EAP IDENTITY (username 802.1X in chiaro)**: nuovo
      `scan/EapIdentityHarvester` + `net/EapolWire::parseEapIdentity()`
      leggono l'outer identity EAP-Response/Identity dei client
      WPA-Enterprise, inviata in chiaro prima del tunnel PEAP/TTLS.
      Solo ricezione, dedup su (MAC, identity), log SD in
      `/eap/identities.csv`. Nuova voce nel gruppo passivo di
      `DISCOVERY`. Limite di canale documentato (non hopper).

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
   deliberatamente — se `LittleFS` è quasi piena (DB OUI ~1.2 MB + DB
   porte/servizi ~300 KB + wordlist credenziali, restano ~2.3 MB),
   verificare che un export fallito riporti "export FAILED" invece di
   un crash o un file troncato silenzioso.

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

## Test plan — Fase 7 (radar, donut, dashboard)

Questa è la prima verifica hardware di `drawCircle`/`fillCircle`/
`drawLine`/`fillArc` in questo progetto — se qualcosa va storto, è il
primo sospetto.

1. **Build**: verificare per prima cosa che compili — `fillArc` in
   particolare è l'unica di queste quattro API non ancora vista
   funzionare da nessuna parte in questo codebase.
2. **MAIN MENU**: deve mostrare intestazione "NETRUNNER", una fila di
   edifici stilizzati (rettangoli vuoti alternati ciano/magenta) sotto
   l'header, il menu di 5 voci sotto ancora, e in fondo uptime a
   sinistra ("00:00:12" e crescente), "READY." al centro, IP a destra
   (o "no ip" se non connesso). Verificare che non ci sia
   sovrapposizione tra skyline/menu/status bar.
3. **Radar**: su `HOST DETAIL` di un host qualsiasi, verificare in alto
   a destra un pannello con cerchi concentrici, un mirino, una linea
   che ruota (un giro ogni ~3 secondi — cronometra a occhio), e 4 punti
   magenta fissi (stessa posizione se rientri ed esci dalla schermata
   per lo stesso host; posizione diversa per host diversi). Verificare
   che il pannello non lasci artefatti grafici quando cambi selezione
   nella lista prima di entrare (nessun residuo di testo sotto).
4. **Donut**: su `PORT MAPPING` dopo uno scan con almeno una porta
   aperta, verificare in alto a destra un anello ciano (proporzione
   aperte) + grigio (resto del range) — con 1 porta aperta su un range
   1-1024 il segmento ciano sarà minuscolo ma presente; con un range
   ristretto (es. cambia `PORT END` a 100 in `SETTINGS` prima dello
   scan) dovrebbe essere più visibile.
5. **Performance**: con radar e donut visibili, verificare che
   l'input da tastiera resti reattivo (nessun rallentamento percepibile
   rispetto alle altre schermate) — sono una manciata di chiamate di
   disegno a schermata, non dovrebbero pesare, ma è la prima volta che
   vengono esercitate su hardware reale.

## Test plan — Fase 8 (database porte/servizi)

1. **uploadfs aggiornato**: `pio run -t uploadfs` deve caricare anche
   `data/ports/services.bin` (~300 KB) oltre al DB OUI — verificare nel
   log seriale l'assenza di `PortServiceDb: could not open
   /ports/services.bin`.
2. **Nomi più ricchi**: fare un port scan su un host con servizi noti
   (es. un router con `80/tcp`, `443/tcp`, `53/udp` se lo scanner UDP
   fosse mai aggiunto) e verificare che compaiano nomi specifici invece
   di `?` (sconosciuto) per porte che prima non erano nello switch
   hardcoded — es. `8080/tcp` dovrebbe mostrare `http-proxy` invece di
   restare senza nome.
3. **Regressione HTTP alt-port risolta**: su un servizio web di test in
   ascolto sulla porta `8080` con credenziali note nel dizionario,
   avviare `CREDENTIAL AUDIT`: deve tentarci contro (verificabile dal
   log tentativi live) — se non tenta nulla su quella porta, la
   correzione della Fase 8 non ha funzionato come previsto.
4. **Vendor OUI di nuovo funzionante**: su `NETWORK SCAN`/`HOST DETAIL`,
   verificare che il campo `VENDOR:` mostri nomi reali (es. "Apple,
   Inc.", "TP-Link Corporation") invece di "unknown" per dispositivi
   noti — se prima di questa fase mostrava sempre "unknown", era il bug
   del path descritto sopra.

## Test plan — Fase 9 (restyle splash + NETWORK SCAN)

1. **Boot screen**: dopo il log "in typing", deve comparire l'header
   `>> CARDPUTER ADV`, il titolo `NETRUNNER` in magenta, il sottotitolo
   `ADVANCED NETWORK TOOLKIT` in ciano, la versione `v1.0.0-ADV` in
   grigio, la banda skyline (stessa silhouette del `MAIN MENU`), il
   prompt `[ PRESS ENTER ]` lampeggiante e in fondo uptime/`SYSTEM
   READY`/IP — verificare che nessun elemento si sovrapponga (in
   particolare skyline vs. versione sopra, prompt vs. status bar sotto).
2. **`MAIN MENU`**: **aggiornato dall'addendum post-Fase 9** — la skyline
   non c'è più su questa schermata (resta solo sullo splash), le voci di
   menu iniziano subito sotto la riga dell'header.
3. **`NETWORK SCAN` — stat strip**: durante uno scan, in alto deve
   comparire `HOSTS FOUND: N` a sinistra (che cresce mano a mano che
   trova host) e una percentuale a destra; a scan finito la destra deve
   diventare `SCAN TIME mm:ss` fisso.
4. **`NETWORK SCAN` — tabella incorniciata**: gli host trovati devono
   apparire dentro un riquadro con bordo, con una riga di intestazione
   `IP  TYPE  VENDOR` allineata alle colonne sottostanti — verificare che
   IP lunghi (es. `192.168.100.100`) non si sovrappongano alla colonna
   `TYPE`, e che vendor lunghi vengano troncati senza sforare il bordo
   destro del riquadro.
5. **Scroll e selezione**: con più host di quanti ne stiano nel
   riquadro, Su/Giù deve scorrere la lista mantenendo la riga
   selezionata sempre visibile dentro il bordo (nessuna riga "a
   cavallo" del bordo inferiore).

## Test plan — Fase 10 (cronologia/diff, SD, WiFi multi-rete, UDP, mDNS, firme vuln, OTA)

Otto feature indipendenti — si possono testare una alla volta, non c'è
bisogno di verificarle tutte nella stessa sessione:

1. **SD card (#3)**: con una microSD inserita e formattata FAT32,
   controllare nel log seriale che NON compaia `sdcard: no SD card
   detected`. Fare un export (`E` su `NETWORK SCAN`): il messaggio di
   stato deve dire `exported (SD) ...` invece di `(flash)`, e
   `/export.json`/`.csv` devono comparire sulla SD quando la si legge su
   un PC. **Se il log mostra sempre "no SD card detected" anche con la
   scheda inserita**: il sospetto numero uno sono i 4 pin SPI in
   `SdCard.cpp` — vedi il commento lì.
2. **Cronologia scan (#1)**: fare 2-3 `NETWORK SCAN` completi, poi
   aprire `SCAN HISTORY` dal menu principale — deve mostrare una riga
   per scan (più recente in cima, marcata "latest"), `ENTER` su una
   riga mostra la tabella host di quello scan specifico.
3. **Diff host nuovi (#2)**: fare uno scan, spegnere/scollegare un
   dispositivo dalla rete (o accenderne uno nuovo), rifare lo scan — il
   nuovo host deve apparire in magenta nella tabella di `NETWORK SCAN` e
   la stat strip deve mostrare `HOSTS FOUND: N (+1 new)`.
4. **Diff porte nuove (#2)**: su un host con un servizio che puoi
   avviare/fermare a piacere (es. un web server locale), fare un port
   scan senza il servizio attivo, poi avviarlo e rifare il port scan —
   la nuova porta deve apparire in magenta su `PORT MAPPING` e il
   footer mostrare `open:N (+1 new)`.
5. **Probe UDP (#4)**: puntare un port scan verso un host che sicuramente
   risponde ad almeno uno tra DNS/NTP/SNMP (es. il router stesso, spesso
   ha un resolver DNS su 53/udp) — deve comparire una riga con suffisso
   `/u` nella tabella porte.
6. **mDNS (#5)**: fare `NETWORK SCAN` su una rete con almeno un iPhone/
   Mac/Chromecast collegato — il campo hostname di quel dispositivo in
   `HOST DETAIL` dovrebbe essere popolato (prima di questa fase sarebbe
   rimasto vuoto, dato che NBNS da solo non li copre). **Se il firmware
   non compila**, il sospetto numero uno è la firma di
   `WiFiUdp::beginMulticast()` in `MdnsReverseResolver.cpp` — vedi il
   commento lì.
7. **Firme di vulnerabilità (#6)**: la verifica pulita richiede un
   servizio di test con uno dei banner esatti in `VulnSignatures.cpp`
   (es. un vecchio vsftpd 2.3.4 in un container Docker isolato, MAI
   esposto a una rete reale). Con un match: `HOST DETAIL` deve mostrare
   una riga `VULN: ...` e il risk level dell'host deve diventare
   Critical (rosso).
8. **WiFi multi-rete (#7)**: da `WIFI SETUP`, connettersi a due reti
   diverse in sequenza (es. una rete di casa e un hotspot del telefono).
   Tornare su `WIFI SETUP` da Idle e premere `S`: devono comparire
   entrambe le reti, quella usata più di recente marcata `*`. Selezionare
   quella vecchia e premere `ENTER`: deve riconnettersi **senza chiedere
   la password**. Su quella schermata, `F` sulla voce selezionata deve
   dimenticare solo quella rete (l'altra deve restare salvata).
9. **OTA (#10)**: compilare (`pio run -e cardputer-adv`), servire il
   `.bin` risultante (`.pio/build/cardputer-adv/firmware.bin`) con
   `python3 -m http.server` da un PC sulla stessa rete del Cardputer, poi
   da `SETTINGS` premere `O`, digitare `http://<ip-del-pc>:8000/firmware.bin`
   e `ENTER`. Il device deve mostrare "downloading + flashing...",
   riavviarsi da solo, e tornare a funzionare con lo stesso firmware
   (o uno nuovo, se nel frattempo è cambiato qualcosa). **Prima di
   questo test**, verificare che il passo `pio run -t upload` (flash via
   USB-C con la nuova `partitions.csv`) sia andato a buon fine senza
   errori "app image is too big" — se fallisce lì, vedi il commento in
   `partitions.csv` per come correggere le dimensioni degli slot.

## Test plan — Fase 11 (war driving, allowlist, NTP)

**Da testare SOLO in un ambiente dove tutte le reti coinvolte sono tue o
esplicitamente autorizzate** (es. un tuo hotspot secondario aperto, un
laboratorio isolato) — non in giro per strada con reti di sconosciuti.

1. **War driving passivo**: da `WAR DRIVING`, `ENTER` per avviare — i
   contatori `seen`/`open` devono crescere man mano che vengono trovati
   AP nei dintorni, e la tabella sotto deve popolarsi (verde = cifrata,
   ambra = aperta). Verificare che compaia `/netrunner/wardrive.csv`
   (path dalla Fase 29 — era `/wardrive/wardrive.csv`) su SD/LittleFS
   con una riga per AP.
2. **Nessuna connessione senza allowlist**: con la allowlist vuota,
   lasciare il war driving attivo vicino a una rete aperta di test — deve
   comparire nella tabella (ambra) ma il device non deve mai risultare
   connesso ad essa (verificabile da `WIFI SETUP` → Idle, che mostra
   sempre e solo la propria rete salvata o "not connected").
3. **Allowlist e conferma**: da `WAR DRIVING` → `A`, poi `N`, digitare
   l'SSID di una rete aperta di test **tua**, `ENTER` — deve comparire
   l'avviso rosso/ambra con il testo dell'SSID; solo `Y` la aggiunge
   (qualunque altro tasto/`DEL` annulla senza aggiungerla).
4. **Scoperta attiva su AP allow-listato**: con quell'SSID in allowlist
   e aperto, riavviare il war driving — il log deve mostrare
   "connecting to allow-listed AP", poi "discovered N host(s)", poi
   "reconnecting to your own network"; verificare che
   `/netrunner/<timestamp>_<ssid>_<bssid>.json` e `.csv` compaiano su SD
   (path dalla Fase 29 — era `/wardrive/scans/<ssid>_<bssid>.json|csv`),
   e che al termine il device sia di nuovo connesso alla rete di sempre
   (non a quella di test).
5. **`MAIN MENU` a 7 voci**: verificare che tutte e 7 le voci (inclusa
   `WAR DRIVING`) siano leggibili senza sovrapporsi alla status bar in
   fondo.
6. **NTP**: dopo la connessione WiFi, attendere qualche secondo e
   controllare `WIFI SCAN` o la status bar di `MAIN MENU` — l'ora deve
   sostituire l'uptime una volta sincronizzata (formato `HH:MM:SS`,
   UTC — non l'ora locale). Fare un `NETWORK SCAN` dopo la
   sincronizzazione e verificare in `SCAN HISTORY` che compaia un orario
   reale invece di "latest" sulla riga più recente.

## Test plan — Fase 12 (splash, audio, rinomina, scroll, timeout, detection)

1. **Splash**: al boot, dopo il log "in typing", verificare l'header
   `-( CARDPUTER ADV )-` centrato, lo skyline con più edifici e un
   gradiente ciano→magenta visibile (edifici bassi più ciano, alti più
   magenta, un paio con una piccola antenna), e sotto una griglia
   prospettica magenta che si allarga verso il basso convergendo su un
   punto centrale in alto — verificare che nulla si sovrapponga
   (sottotitolo/skyline, griglia/versione, versione/prompt).
2. **Audio boot**: nello stesso istante in cui appare la vista
   brandizzata, deve sentirsi un breve arpeggio di 4 note ascendenti.
   Con `SETTINGS` → `SOUND` impostato su `OFF`, il prossimo riavvio non
   deve produrre alcun suono.
3. **`WIFI SCAN`**: il menu principale e l'header della schermata
   devono mostrare "WIFI SCAN", non più "WIFI SETUP".
4. **Scroll war driving**: avviare `WAR DRIVING`, aspettare che vengano
   trovati più di 8 AP (o restare in una zona con molte reti), tornare a
   Idle e verificare che Su/Giù scorrano la tabella oltre le prime 8
   righe, con la riga selezionata sempre visibile ed evidenziata.
5. **Timeout schermo**: lasciare il device fermo per 30+ secondi su
   qualunque schermata — la retroilluminazione deve abbassarsi
   visibilmente (non spegnersi del tutto). Premere un tasto qualsiasi:
   deve tornare subito a piena luminosità. Durante il dimming, avviare
   prima un `NETWORK SCAN` o `WAR DRIVING`: deve continuare a
   progredire normalmente (contatori/log che aggiornano) anche a schermo
   fioco.
6. **Reti nascoste**: se hai un access point che puoi configurare per
   non trasmettere l'SSID, verificare che compaia in `WAR DRIVING` come
   `<hidden>` (prima di questa fase non sarebbe comparso affatto) e che
   NON venga mai considerato per la scoperta attiva anche se
   "<hidden>" fosse per assurdo nella tua allowlist.

## Test plan — Fase 13 (evil-twin, audio audit, STATS, backup, baseline, signal finder)

> Lo scan BLE, originariamente parte di questa fase, è stato rimosso in
> Fase 14 — vedi il test plan dedicato subito sotto invece di cercare
> `BLE SCAN` nel menu.

1. **Evil-twin**: con due AP (anche uno dei due un hotspot telefonico)
   configurati con lo **stesso SSID** ma cifratura diversa (es. uno
   aperto, uno WPA2), avviare `WAR DRIVING` e verificare che entrambe le
   voci diventino rosse con `!EVIL`, il contatore `evil:N` compaia in
   rosso, e si senta l'allarme a due toni.
2. **Audio audit**: con `CREDENTIAL AUDIT` abilitato (disclaimer `Y`) su
   un host con una credenziale di default nota, verificare che al
   momento del login riuscito si senta l'allarme a due toni (diverso dal
   singolo beep di war driving). Con `SOUND` su `OFF` in `SETTINGS`,
   nessun suono.
3. **STATS**: da `SCAN HISTORY`, premere `S`. Con almeno 2-3 scan salvati
   in cronologia, verificare il grafico a barre (altezza proporzionale
   al numero di host), le barre rosse sugli scan con un host Critical,
   il contorno magenta sulla barra più recente, e il testo di andamento
   sopra.
4. **Backup/restore**: in `SETTINGS` con una SD inserita, premere `B` —
   deve apparire "backed up to SD" e comparire `/config_backup.json`
   sulla SD. Cambiare qualche impostazione, poi premere `R` — deve
   tornare ai valori del backup. Senza SD inserita, sia `B` che `R`
   devono mostrare un messaggio d'errore chiaro invece di crashare.
5. **Baseline dispositivi noti**: fare un `NETWORK SCAN` completo su una
   rete, aspettare il salvataggio automatico in cronologia, poi
   spegnere/riaccendere un dispositivo noto (o allontanarlo) e rifare lo
   scan — quel dispositivo, se assente, semplicemente non compare (atteso);
   se invece è presente un dispositivo il cui MAC non era mai comparso
   prima su questa rete, deve apparire in rosso nella tabella con `!N`
   nello stat-strip. Ripetere lo scan sulla stessa rete senza nuovi
   dispositivi: nessuna riga rossa.
6. **Signal finder**: da `WAR DRIVING` (stato Idle, con almeno una
   sighting), selezionare una riga e premere `Tab`. Verificare che la
   barra RSSI si aggiorni continuamente e che avvicinandosi/
   allontanandosi fisicamente dall'AP il testo `GETTING CLOSER`/
   `GETTING FARTHER` e il colore della barra (verde/ambra/rosso)
   cambino di conseguenza.

## Test plan — Fase 14 (rimozione scan BLE)

1. **Build e dimensione**: `pio run` deve compilare senza errori né
   riferimenti residui a BLE. Annotare la dimensione finale del binario
   (riga "Flash:" nell'output) — se risulta comodamente sotto 1.638.400
   byte, gli slot OTA possono tornare a 1,6 MB (vedi sopra); se no, la
   configurazione attuale (2,25 MB/slot) resta necessaria così com'è.
2. **`WAR DRIVING` senza BLE**: aprire `WAR DRIVING`, sia in stato Idle
   che Running — il footer non deve più mostrare `B:ble`, e il tasto `B`
   non deve fare nulla (nessuna schermata `BLE SCAN` da aprire, perché
   non esiste più).
3. **Nessuna regressione sulle altre migliorie della Fase 13**: evil-
   twin, audio audit, STATS, backup/restore, baseline dispositivi e
   signal finder devono continuare a funzionare esattamente come nel
   test plan sopra — erano moduli indipendenti dal BLE, non dovrebbero
   essere stati toccati dalla rimozione.

## Test plan — Fase 15 (melodia di boot cyberpunk)

1. **Riconoscibilità**: al boot, esattamente nel momento in cui il log
   "in typing" lascia il posto alla vista NETRUNNER, deve partire il
   nuovo riff (~1,5s) — un pulse ripetuto grave, poi un arpeggio che
   sale, poi una discesa che torna alla nota grave iniziale — chiaramente
   diverso e più lungo del vecchio arpeggio di 4 note.
2. **Timing del prompt**: il prompt `[ PRESS ENTER ]` non deve iniziare
   a lampeggiare prima che il riff sia finito, né restare in silenzio
   troppo a lungo dopo — dovrebbe comparire a breve distanza dall'ultima
   nota, non subito attaccato né con un vuoto lungo.
3. **`SOUND` su `OFF`**: da `SETTINGS`, disattivare `SOUND` e riavviare —
   nessun suono al boot, ma il timing visivo (comparsa titolo → prompt)
   deve restare invariato rispetto a quando il suono è attivo (la durata
   del silenzio non dipende dal fatto che la melodia suoni davvero o
   meno, dato che il ritardo è calcolato sul tempo trascorso, non
   sull'aver effettivamente chiamato `Speaker.tone()`).

## Test plan — Fase 16 (strumenti offensive)

> Da eseguire SOLO sulla tua rete/i tuoi dispositivi di test — ogni
> punto qui sotto interrompe o intercetta traffico di un dispositivo
> reale.

1. **Build**: `pio run` deve compilare pulito. `ArpSpoofManager.cpp`/
   `DeauthManager.cpp`/`EvilTwinManager.h` sono già stati corretti una
   volta contro una build reale (vedi sopra); se falliscono ancora, il
   sospetto principale resta `esp_wifi_80211_tx` (firma/header) o un
   altro tipo mancante da `esp_wifi_types.h`.
2. **Gate rafforzato**: da `HOST DETAIL` premi `M`, o da `WAR DRIVING`
   premi `E`/`X` — la prima volta per sessione deve comparire
   `OffensiveDisclaimerScreen` e richiedere di scrivere per intero
   AUTHORIZED (premere solo `ENTER` non deve bastare). Dopo averla
   accettata una volta, tutte e tre le schermate si aprono direttamente
   per il resto della sessione.
3. **ARP MITM**: su un host già scoperto con `NETWORK SCAN`, apri `MITM
   AUDIT` (`M`), avvia una sessione — l'header deve diventare rosso
   "MITM ACTIVE" per tutta la durata. Da un altro dispositivo (o con
   Wireshark sul target, se disponibile) verifica che la cache ARP del
   target cambi. Alla fine sessione (timeout o `ENTER`/`DEL`), verifica
   che la cache ARP del target torni al MAC reale del gateway.
4. **Sniffing/cookie/DNS spoof**: su una rete APERTA di test con un
   client che fa una richiesta HTTP con Basic Auth o un cookie in
   chiaro, verifica che compaia nel log del MITM. Con una voce nella
   lista DNS spoof (`D`) che punta a un hostname che il client
   interroga, verifica che il client riceva l'IP forgiato invece di
   quello reale.
5. **Evil twin**: da una sighting in `WAR DRIVING`, premi `E`, avvia —
   verifica che l'SSID clonato compaia nella scansione WiFi di un
   telefono/laptop di test come rete APERTA. Connettiti con quel
   dispositivo e verifica che compaia nel log associazioni del
   Cardputer (solo MAC, nessuna credenziale richiesta).
6. **Deauth + capture**: da una sighting con un client di test connesso
   (MAC noto), premi `X`, digita il MAC del client, avvia — il client
   deve disconnettersi entro pochi secondi. Verifica che
   `/handshakes/*.pcap` compaia su SD e si apra in Wireshark senza
   errori; se il client si riconnette durante la finestra di cattura,
   verifica che l'handshake WPA sia effettivamente presente nel file.
7. **HTTP path brute**: su un host con un server web di test che ha
   `/admin` o `/.git/config` raggiungibili, da `HOST DETAIL` premi `H` —
   verifica che compaiano nel log con lo status code corretto (non 404).
8. **Credential guessing POP3/IMAP/SMTP**: su un server di test con
   POP3/IMAP/SMTP e una credenziale nota nella wordlist, verifica che
   `CREDENTIAL AUDIT` la trovi esattamente come già fa per FTP.

## Test plan — Fase 17 (musica in loop + nebbia digitale)

1. **Loop continuo**: al boot, dal momento in cui appare la vista
   NETRUNNER, deve partire la musica e continuare a ripetersi (pulse di
   basso, poi hook melodico, poi risoluzione, poi da capo) senza
   fermarsi da sola.
2. **Si ferma a `MAIN MENU`**: premendo `ENTER` sul prompt, la musica
   deve fermarsi (al massimo con una brevissima coda, non un'altra nota
   intera) nel momento in cui compare `MAIN MENU` — non deve continuare
   a suonare in background una volta usciti dallo splash.
3. **`SOUND` su `OFF`**: nessun suono al boot, ma il timing visivo
   (comparsa titolo → prompt) deve restare invariato — stessa logica
   già verificata in Fase 15. Riattivando `SOUND` mentre il loop
   sarebbe altrimenti in corso (es. tornando su `MAIN MENU` → `SETTINGS`
   e poi di nuovo sullo splash a un riavvio) il loop deve ripartire dal
   proprio inizio, non da dove si era "fermato silenziosamente".
4. **Nebbia digitale**: sulla vista NETRUNNER, verifica che compaiano
   puntini fiochi sparsi sullo sfondo che cambiano leggermente
   posizione ogni circa 400ms (deriva/shimmer lenta, non un flicker
   frenetico né un pattern statico) — e che titolo/sottotitolo/versione/
   prompt restino perfettamente leggibili, senza puntini visibili sopra il
   testo.
5. **Sentori di Nightcall, non una copia** (dopo il terzo ritocco):
   ascolta il loop (~10s) — deve percepirsi chiaramente un ostinato di
   basso pulsante sotto ogni accordo (La minore-Fa-Do-Sol), un hook
   melodico lento e minore sopra, e un outro discendente malinconico
   prima che il giro ricominci. Deve evocare l'atmosfera, non suonare
   come una citazione riconoscibile della melodia vera del brano.

## Test plan — Fase 18 (discovery passiva, negotiate SMB, report kill-chain)

Nessuna di queste è ancora passata da una build reale al momento della
scrittura — tutti i file hanno superato solo il controllo di bilanciamento
parentesi in locale. Da verificare su hardware:

1. **Menu con scroll**: `MAIN MENU` ha ora 10 voci (WIFI SCAN … WAR
   DRIVING, LAN TOPOLOGY, UPNP DISCOVERY, ROGUE DHCP, SETTINGS). La lista
   ne mostra 7 alla volta: scendendo oltre la settima deve scrollare, con
   un marcatore `^`/`v` a destra quando ci sono voci fuori schermo. Il
   wraparound su/giù deve continuare a funzionare.
2. **UPNP DISCOVERY**: connesso a una rete con almeno una smart TV/NAS/
   router UPnP, `ENTER` avvia la M-SEARCH; entro qualche secondo la lista
   deve popolarsi con IP + header `SERVER`, e la seconda riga con l'`USN`.
   Zero dispositivi non è di per sé un errore (dipende dalla rete).
3. **LAN TOPOLOGY**: `ENTER` avvia l'ascolto passivo (indicatore
   `[listening]`); su una rete con switch/router che emettono CDP/LLDP
   *e* un AP che fa bridge di quei multicast, i vicini devono comparire
   (ambra = CDP, verde = LLDP). Su molte reti domestiche non comparirà
   nulla: è il limite documentato, non un bug.
4. **ROGUE DHCP**: `ENTER` avvia la sorveglianza passiva. Su una rete
   **aperta** con un solo server DHCP, quando un client rinnova il lease
   il server deve comparire in verde. Introducendo un secondo server DHCP
   (test controllato) il suo IP deve comparire in **rosso** (diverso dal
   gateway). Su rete WPA cifrata non si vede nulla (limite atteso).
5. **SMB NEGOTIATE**: su un host con porta 445/139 aperta (scoperta prima
   con `TAB`), il footer di `HOST DETAIL` mostra `S:smb-neg`; premendo `S`
   e poi `ENTER`, entro pochi secondi devono comparire le tre righe
   Security Mode (user-level/share-level, plaintext/challenge-response,
   signing). Le condizioni legacy deboli (share-level, plaintext) in
   rosso. Un server SMB2-only deve dare "response not SMB1", non un crash.
6. **PMKID CAPTURE**: da una sighting di `WAR DRIVING`, `P` porta al gate
   `OffensiveDisclaimerScreen` (prima volta della sessione), poi alla
   schermata; `ENTER` tenta l'associazione con password errata e cattura.
   Verifica che al termine la connettività WiFi normale del device venga
   ripristinata (autoConnect) e che, se catturato qualcosa, esista un
   `.pcap` in `/handshakes/`. **Non** deve mai disconnettere altri client.
7. **Report kill-chain**: dopo un `NETWORK SCAN` (meglio con qualche host
   port-scansionato e magari un audit credenziali), `R` su `NETWORK SCAN`
   deve scrivere `/report.html` su SD (o LittleFS in fallback) e mostrare
   la riga di stato. Aprendo il file su un PC: layout cyberpunk leggibile,
   sezione ATTACK SURFACE che elenca i finding, tabella host completa, e
   la lista COMPANION ARTIFACTS che referenzia solo i file davvero
   presenti sulla card.
8. **Radio condivisa**: avviando due funzioni promiscue insieme (es. LAN
   TOPOLOGY e ROGUE DHCP) ci si aspetta che solo l'ultima avviata riceva
   frame — comportamento atteso e documentato, non un bug. Usare una
   funzione promiscua alla volta.

## Test plan — Fase 19 (status bar, THREATS, auto-assess, host passivi, servizi, SNMP)

Anche questa fase non è ancora passata da una build reale — solo controllo
di bilanciamento parentesi in locale. Da verificare su hardware:

1. **Barra di stato radio**: con nessuna funzione promiscua attiva
   l'header non mostra niente di nuovo (solo `W <batt>%`). Avviando UNA
   funzione promiscua (es. `PASSIVE HOSTS` → ENTER) deve comparire
   `RF:PSV` in ambra a sinistra della batteria, su **tutte** le schermate.
   Avviandone una seconda (es. anche `ROGUE DHCP`) deve diventare `RF:2!`
   in rosso. Fermandole, l'indicatore sparisce.
2. **Menu a 15 voci con scroll**: dal menu, scorrendo in giù oltre la
   settima voce deve scrollare con i marcatori `^`/`v`; tutte e 15 le voci
   (incluse AUTO ASSESS, THREATS, SERVICE SCAN, PASSIVE HOSTS, SNMP SWEEP)
   devono essere raggiungibili e aprire la schermata giusta.
3. **AUTO ASSESS**: connesso a una rete, `ENTER` avvia la sequenza; la
   barra di avanzamento deve passare per DISCOVERY → PORT SCAN (con
   contatore host x/y) → REPORT → DONE, e alla fine mostrare il path
   `/report.html`. `ENTER` durante l'esecuzione ferma; `DEL` esce
   lasciandolo girare in background. Verifica che il report esista su SD.
4. **THREATS**: dopo uno scan con almeno un host cred-vulnerabile o con
   telnet/ftp aperti, la dashboard deve elencarli (rosso per critical,
   ambra per warning). Con un server rogue DHCP rilevato, deve comparire
   anche quello. A rete "pulita": messaggio "nothing flagged".
5. **PASSIVE HOSTS**: su rete **aperta**, `ENTER` avvia l'ascolto; man mano
   che gli host trasmettono devono comparire IP + MAC + conteggio frame,
   **senza** aver lanciato alcuno scan attivo. Su rete WPA: lista vuota
   (atteso).
6. **SERVICE SCAN**: `ENTER` avvia la browse DNS-SD; su una rete con
   Chromecast/AirPlay/stampanti/NAS devono comparire istanze con tipo
   servizio e, dove annunciata, la porta. Zero servizi non è un errore.
7. **SNMP SWEEP**: dopo un `NETWORK SCAN`, `ENTER` sonda gli host vivi con
   community `public`; quelli che rispondono compaiono con il loro
   sysDescr. La barra di progresso avanza fino al 100%. Nessun host vivo →
   messaggio che invita a fare prima `NETWORK SCAN`.

## Test plan — Fase 21 (data-store esposti + audit servizi per-host)

Molto codice di protocollo nuovo e mai compilato — solo controllo parentesi
in locale. Serve un lab di test **autorizzato** con i servizi accesi. Da
verificare su hardware:

1. **DATASTORE SWEEP**: con un Redis/Memcached/Elasticsearch/MongoDB senza
   password sulla rete (dopo un `NETWORK SCAN`), `ENTER` deve elencarli con
   la label **NO-AUTH** in rosso e la versione; un Redis con `requirepass`
   deve apparire come "auth required" (ambra). La barra arriva al 100%.
2. **SERVICE AUDIT gate**: `V` su `HOST DETAIL`. Se il CREDENTIAL AUDIT non
   è mai stato autorizzato in sessione, deve comparire prima il disclaimer
   (Y per procedere); una volta autorizzato, parte l'audit.
3. **FTP anonimo**: su un FTP che consente `anonymous`, il finding deve
   dire "anonymous login ALLOWED" (e "(WRITABLE)" se si riesce a fare MKD).
4. **Redis default-pw**: Redis con `requirepass foobared` → finding
   "default password 'foobared'".
5. **MySQL/PostgreSQL**: contro MySQL 5.7/MariaDB con `root` a password
   nota della lista, e Postgres con `postgres` trust/default, il finding
   deve mostrare la coppia valida. MySQL 8 (caching_sha2) → finding "auth
   plugin ... unsupported" (atteso, fuori scope). Postgres SCRAM → "SCRAM
   (not brute-forced)".
6. **VNC**: server VNC senza password → "NO authentication"; con una
   password della lista → "default password '...'".
7. **HTTP basic-auth**: pannello con realm Basic e credenziali di default
   → finding con la coppia; senza realm Basic non deve comparire nulla
   (il form-brute è fuori scope).
8. **SMB null session**: contro un server che accetta la sessione anonima
   (Samba mal configurato) → "anonymous (null) session ACCEPTED"; server
   con extended security → "extended security (null session not tested)".

## Test plan — Fase 27 (beacon/probe intel, mDNS→host, RTC)

Codice di parsing 802.11 management e integrazione RTC mai compilati né
provati su hardware reale — solo controllo parentesi/tipi in locale. Da
verificare su hardware, in un ambiente **autorizzato**:

1. **BEACON/PROBE INTEL — AP**: `ENTER` da `BEACON/PROBE INTEL` deve far
   comparire, entro pochi secondi, le AP note nei dintorni con SSID,
   cifratura e canale corretti (confrontare con `WIFI SCAN`); l'header
   deve mostrare `ch<N>` che cambia visibilmente nel tempo (hopping) e
   `RF:BCN` nello status bar condiviso.
2. **BEACON/PROBE INTEL — hidden reveal**: con un AP di test configurato
   SSID nascosto, un client reale che si connette ad esso durante
   l'ascolto deve far comparire l'SSID vero al posto di `<hidden>`,
   evidenziato in magenta.
3. **BEACON/PROBE INTEL — PNL client**: con un telefono/laptop di test
   con WiFi attivo ma non connesso a nulla (o in modalità aereo con WiFi
   riacceso), deve comparire come client nella vista CLIENTS; se il
   dispositivo non randomizza il MAC (o lo fa e va verificato che compaia
   "R" e vendor vuoto), le reti che prova a cercare da probe request
   dirette devono comparire come SSID probati.
4. **BEACON/PROBE INTEL — riconnessione**: fermare l'ascolto (`ENTER`) e
   verificare che il WiFi proprio del Cardputer si riconnetta da solo
   entro qualche secondo alla rete salvata.
5. **SERVICE SCAN → HOST DETAIL**: dopo un `NETWORK SCAN` seguito da
   `SERVICE SCAN` (o `RUN ALL DISCOVERY`) sulla stessa rete, un host che
   risponde a DNS-SD (es. una Apple TV/Chromecast/stampante) deve
   mostrare la riga `MDNS:` valorizzata in `HOST DETAIL`, e se l'host non
   aveva già un hostname da `NETWORK SCAN`, il campo `HOST:` deve ora
   mostrare il nome istanza mDNS.
6. **Export mDNS**: un export JSON/CSV da `NETWORK SCAN` (tasto `E`) dopo
   il punto 5 deve includere il campo `mdnsServices`/`mdns_services`
   valorizzato per l'host in questione.
7. **RTC assente (caso comune)**: su un Cardputer/Cardputer ADV senza
   nulla collegato alla porta Grove, `DIAGNOSTICS` deve mostrare `RTC:
   absent` (ambra) e il comportamento di `TimeSync` deve restare
   identico a prima di questa fase (solo NTP, nessun crash/rallentamento
   al boot dovuto al probe `external_rtc`).
8. **RTC presente**: con un'unità RTC a batteria (es. M5Stack RTC Unit,
   BM8563) collegata alla porta Grove, `DIAGNOSTICS` deve mostrare `RTC:
   present`; spegnendo il dispositivo (batteria RTC comunque inserita) e
   riaccendendolo SENZA WiFi disponibile, l'ora mostrata in header deve
   essere già quella reale (non uptime) prima di qualunque tentativo di
   connessione. Con WiFi disponibile, dopo qualche minuto scollegare
   l'RTC, riavviare e verificare che l'ultimo orario NTP scritto sia
   stato effettivamente persistito (l'RTC riparte da lì, non da zero).

## Test plan — Fase 28 (report su /netrunner)

1. **Export manuale**: da `NETWORK SCAN` dopo uno scan, `E` deve creare
   `/netrunner/<timestamp>_<SSID>.json` e `.csv` su SD (o LittleFS se
   nessuna SD); ripetere l'export sulla stessa rete deve produrre un
   NUOVO file (timestamp diverso), non sovrascrivere il precedente.
2. **Export automatico**: con `AUTO-EXPORT` attivo in `SETTINGS`, ogni
   scan completato deve produrre la stessa coppia di file in
   `/netrunner`, senza intervento dell'utente.
3. **Report HTML manuale e da AUTO ASSESS**: `R` da `NETWORK SCAN` e un
   run completo di `AUTO ASSESS` devono entrambi produrre un
   `/netrunner/<timestamp>_<SSID>.html` che si apre correttamente in un
   browser, con la sezione COMPANION ARTIFACTS che elenca `/netrunner/`
   (non i vecchi nomi fissi) quando ce ne sono già altri sulla card.
4. **SSID con caratteri problematici**: connettersi a una rete con SSID
   contenente `/`, spazi o `:` e verificare che l'export non fallisca e
   il nome file sia sanificato (niente sottocartelle spurie create per
   sbaglio da uno `/` non sanificato).
5. **Nessun WiFi/ora non sincronizzata**: non dovrebbe essere raggiungibile
   in pratica (l'export richiede una rete scansionata), ma verificare
   comunque che senza sync l'export usi il fallback `uptime-<secondi>`
   invece di fallire o produrre un nome vuoto.

## Test plan — Fase 29 (war driving su /netrunner)

1. **Log continuo**: avviare `WAR DRIVING`, lasciarlo girare finché non
   viene loggata almeno una AP, verificare che compaia
   `/netrunner/wardrive.csv` (non più `/wardrive/wardrive.csv`) e che
   righe di sessioni precedenti (se il file esisteva già) siano ancora
   presenti (append, non troncamento).
2. **Export per-AP**: con una AP aperta in allowlist nei paraggi,
   verificare che l'incursione produca
   `/netrunner/<timestamp>_<ssid>_<bssid>.json` e `.csv`; una seconda
   incursione sulla stessa AP in un momento diverso deve produrre un
   NUOVO file (timestamp diverso), non sovrascrivere il primo.
3. **COMPANION ARTIFACTS**: un report HTML generato dopo il punto 1 o 2
   deve elencare `/netrunner/` (non più una voce `/wardrive/wardrive.csv`
   separata) nella sezione COMPANION ARTIFACTS.

## Test plan — Fase 30 (LDAP sweep, NTLM disclosure)

**Da testare SOLO in un ambiente autorizzato** — contro un DC/server
LDAP e servizi web con NTLM abilitato di tua proprietà o esplicitamente
autorizzati. Codice mai eseguito contro un server reale (solo verificato
a tavolino contro librerie Python — vedi sopra):

1. **LDAP, bind anonimo consentito**: contro un LDAP/AD di test con bind
   anonimo abilitato (o un OpenLDAP di default), `LDAP SWEEP` → `ENTER`
   deve mostrare l'host con "anon-bind OPEN" in rosso, e la seconda riga
   deve mostrare `dnsHostName` o `defaultNamingContext` se il rootDSE è
   stato letto.
2. **LDAP, bind anonimo rifiutato ma rootDSE leggibile**: contro un AD
   moderno con bind anonimo disabilitato (il default), deve comparire
   "bind rejected" in verde ma la seconda riga deve comunque mostrare
   `dnsHostName`/naming context se il server li espone senza bind —
   verifica che i due controlli siano davvero indipendenti come
   documentato, non che il secondo salti quando il primo fallisce.
3. **LDAP, porta chiusa/non-LDAP**: un host senza nulla in ascolto sulla
   389 (la maggioranza) non deve comparire affatto nell'elenco — non un
   falso positivo con campi vuoti.
4. **NTLM, servizio con NTLM abilitato**: contro un sito IIS/servizio con
   autenticazione NTLM (es. un file share Windows esposto via WebDAV, o
   un pannello interno configurato per Integrated Windows Auth), dopo un
   `PORT SCAN` sull'host, `NTLM DISCLOSURE` → `ENTER` deve mostrare
   IP:porta e, nella seconda riga, dominio/hostname disclosi dal Type 2.
5. **NTLM, nessun NTLM offerto**: un servizio HTTP qualunque senza NTLM
   (la maggioranza) non deve comparire nell'elenco.
6. **NTLM senza PORT SCAN**: `NTLM DISCLOSURE` senza aver mai fatto un
   port scan su nessun host deve mostrare "no HTTP hosts - run NETWORK
   SCAN/PORT SCAN first", non restare bloccato o mostrare un elenco vuoto
   senza spiegazione.
7. **RUN ALL DISCOVERY**: verificare che le due nuove fasi compaiano
   nell'etichetta di fase (`LDAP SWEEP`, `NTLM DISCLOSURE`) con la barra
   di avanzamento che cresce correttamente tra DATASTORE e LAN TOPOLOGY.

## Test plan — Fase 31 (scroll marker, dettaglio full-value, DISCOVERY raggruppato, attività background)

1. **Scroll marker**: su qualunque lista che superi `kMaxRows` (es. `HOST
   LIST` con più host di quanti ne stiano a schermo, `PORT SCAN` con molte
   porte aperte), scorrere fino in cima e verificare che compaia solo `v`
   (mai `^`); scorrere fino in fondo e verificare il contrario; a metà
   lista devono comparire entrambi.
2. **Help overlay su tutte le schermate**: aprire `?` da almeno una
   schermata per gruppo (menu principale, DISCOVERY e ognuno dei suoi
   sotto-strumenti, HOST DETAIL, SETTINGS, FILE MANAGER, ricerca,
   cronologia, ecc.) e verificare che compaia sempre una legenda
   specifica, mai il fallback generico "No screen-specific help.".
3. **Dettaglio full-value (`I`)**: su `PORT SCAN` con almeno un risultato,
   selezionare una riga con un banner lungo (troncato a schermo) e premere
   `I` — deve aprirsi un overlay a schermo intero col banner per esteso,
   non troncato; un tasto qualsiasi lo chiude e torna alla lista con la
   stessa riga ancora selezionata. Ripetere su `LDAP SWEEP`, `NTLM
   DISCLOSURE`, `THREATS` (finding con testo lungo) e `WAR DRIVING`
   (verificare anche che le frecce su/giù muovano la selezione mentre lo
   scan è ancora in esecuzione, cosa che prima non funzionava).
4. **`I` su lista vuota**: su una schermata delle precedenti senza ancora
   nessun risultato, premere `I` non deve aprire alcun overlay né causare
   crash.
5. **DISCOVERY raggruppato**: entrare in DISCOVERY e scorrere con le
   frecce dall'inizio alla fine — la selezione deve saltare
   automaticamente le righe separatore (`-- ONE-SHOT --` ecc.), mai
   fermarsi su una di esse; `ENTER` su ogni voce reale deve continuare ad
   aprire lo strumento corretto.
6. **Indicatore `BG:` nell'header**: avviare `SNMP SWEEP` (o `LDAP SWEEP`/
   `WAR DRIVING`) e, mentre è ancora in corso, tornare al menu principale
   con `DEL` — l'header deve mostrare `BG:SNMP` (ciano) finché lo sweep
   non termina. Avviare due sweep in sequenza rapida (prima di far
   terminare il primo, se possibile) e verificare che diventi `BG:2`.
7. **Indicatore `RF:` con attività di background insieme**: avviare una
   funzione promiscua (es. `LAN TOPOLOGY`) e, mentre è attiva, uno sweep
   non promiscuo (es. `LDAP SWEEP`) — l'header deve mostrare `RF:CDP+1`
   (ambra), non provare a mostrare due tag separati.
8. **Donut rimosso da PORT SCAN**: avviare un port scan con più porte
   aperte di quante ne servano a riempire lo schermo — verificare che non
   compaia più alcun grafico a torta nell'angolo in alto a destra e che il
   testo del banner nella tabella non venga più tagliato da una
   sovrapposizione.

## Test plan — Fase 32 (porte comuni sopra la 1024)

1. **Idle screen**: aprire `PORT MAPPING` su un host senza ancora scan
   fatti — sotto "range: 1-1024" deve comparire "+50 common ports
   >1024".
2. **Porta comune sopra 1024 trovata**: contro un host di test con un
   servizio in ascolto su una delle porte aggiunte (es. un MySQL/Postgres/
   Redis locale su `3306`/`5432`/`6379`, o un web server su `8080`),
   avviare `ENTER` e verificare che compaia nella tabella risultati con
   nome servizio corretto (da `PortServiceDb`) anche se fuori dal range
   configurato in `SETTINGS`.
3. **Deduplica con range esteso**: in `SETTINGS`, alzare `portRangeEnd`
   oltre una delle porte comuni (es. a 9000, che copre `8080`) e rifare lo
   scan — verificare che il tempo totale di scan non aumenti in modo
   percepibile e che quella porta compaia una sola volta nei risultati,
   non due.
4. **RDP ora raggiungibile**: contro un host con RDP (`3389`) aperto, col
   range di default 1-1024, verificare che compaia nella tabella
   evidenziato in ambra ("legacy port") — prima di questa fase non
   sarebbe mai stato sondato.
5. **Nessuna porta aperta tra quelle nuove**: su un host senza alcuno di
   questi servizi, verificare che lo scan finisca comunque in tempo
   ragionevole (le ~50 porte extra restano una piccola frazione del
   totale) e che l'elenco risultati non contenga falsi positivi.

## Test plan — Fase 33 (WPS, GUARD MODE, evil-twin v2, CHANNEL SCAN)

**Solo in ambiente autorizzato**, stessa regola di ogni altro strumento
che osserva o (per GUARD MODE, solo in ascolto) reagisce a traffico
altrui:

1. **WPS rilevato**: contro un AP di test con WPS attivo, avviare
   `BEACON/PROBE INTEL` e lasciarlo girare finché l'AP compare — deve
   mostrare una "W" accanto alla riga (rossa se WPS è sbloccato, ambra
   se locked). `I` sulla riga deve mostrare lock state e metodi
   (PBC/PIN) nel dettaglio.
2. **WPS assente**: contro un AP senza WPS, nessuna "W" deve comparire
   sulla riga, e il dettaglio deve mostrare "WPS: not seen".
3. **WPS in THREATS**: con almeno un AP WPS-sbloccato visto in questa
   sessione, `THREATS` deve elencare un finding "<ssid> WPS unlocked" in
   ambra.
4. **GUARD MODE, traffico normale**: avviarlo su una rete normale e
   lasciarlo girare qualche minuto — non deve comparire "FLOOD
   DETECTED" né alcuna riga rossa per un singolo/pochi deauth isolati
   (roaming normale di un client).
5. **GUARD MODE, flood reale**: contro un AP di test, generare un vero
   flood di deauth (es. `aireplay-ng --deauth`, SOLO sulla propria rete)
   — entro la finestra di 10s deve comparire "FLOOD DETECTED", la riga
   del BSSID target deve diventare rossa, e deve suonare l'allarme.
   Fermando il flood, verificare che dopo la finestra successiva la riga
   torni verde (rate tornato sotto soglia).
6. **GUARD MODE in THREATS**: durante un flood rilevato, `THREATS` deve
   mostrare "<bssid> deauth flood" in rosso.
7. **GUARD MODE nell'header**: avviandolo e navigando altrove, l'header
   deve mostrare `RF:GRD` (ambra) — è un consumer promiscuo, non un
   background sweep, quindi compare nel gruppo `RF:`, non `BG:`.
8. **Evil-twin, stesso tier niente falso positivo**: in war driving,
   simulare (o trovare) due BSSID con lo stesso SSID entrambi WPA2 (uno
   puro, uno mixed WPA/WPA2) — nessuno dei due deve essere marcato
   sospetto (stesso tier).
9. **Evil-twin, downgrade rilevato**: due BSSID con lo stesso SSID, uno
   WPA2 e uno OPEN — entrambi devono comparire sospetti, con la nota che
   menziona la cifratura più debole/forte.
10. **Evil-twin, vendor mismatch rilevato**: due BSSID con lo stesso
    SSID, stessa cifratura, ma vendor OUI risolti diversi — entrambi
    sospetti, con la nota che menziona i due vendor.
11. **CHANNEL SCAN**: aprirlo vicino ad almeno un AP noto — la barra del
    suo canale deve crescere nel giro di pochi secondi (scan continuo),
    colorata in base al conteggio (verde/ambra/rosso). Se il dispositivo
    è connesso a una rete, il canale di quella rete deve mostrare il
    marcatore ciano "v" sopra la barra.
12. **CHANNEL SCAN, selezione**: Left/Right deve muovere il riquadro di
    selezione fra i 13 canali e la riga informativa in basso deve
    aggiornarsi con conteggio ed RSSI medio del canale selezionato.

## Test plan — Fase 34 (SENTINEL MODE)

1. **Avvio senza WiFi**: da spento/scollegato, aprire `SENTINEL MODE` —
   deve mostrare "connect to WiFi first" in ambra e `ENTER` non deve
   avviare nulla (`isRunning()` resta false).
2. **Avvio normale**: connesso a una rete, `ENTER` deve mostrare "net:
   <ssid>" in ciano e l'header deve mostrare `RF:SNT` navigando altrove.
3. **Dispositivo nuovo rilevato**: con Sentinel attivo, accendere/
   connettere un dispositivo con MAC mai visto su questa rete (o
   aspettare che uno esistente venga rilevato al primo ciclo se la
   cronologia di questa rete non esiste ancora) — entro ~30s deve
   comparire nella lista con un bip sonoro; `I` sulla riga deve mostrare
   IP/MAC/hostname/vendor per esteso.
4. **Nessun falso positivo dopo il primo ciclo**: lo stesso dispositivo
   non deve ricomparire come "nuovo" in un ciclo successivo nella stessa
   sessione.
5. **Dump pcap**: dopo qualche minuto di esecuzione, fermare con `ENTER`
   e verificare su SD/LittleFS che `/netrunner/<timestamp>_<ssid>_p1.pcap`
   esista, abbia dimensione > 0 e si apra correttamente in Wireshark
   (frame leggibili come 802.11 anche se il payload dati resta cifrato
   su una rete WPA2/WPA3).
6. **Convivenza con NETWORK SCAN manuale**: mentre Sentinel è attivo,
   aprire `NETWORK SCAN` e avviare uno scan manuale — non deve bloccarsi
   né corrompere nulla; uno dei due sweep semplicemente troverà
   `g_scanManager` già occupato e riproverà al ciclo successivo (stessa
   collisione già accettata per WAR DRIVING vs NETWORK SCAN).
7. **DEL mantiene la sessione**: uscire con `DEL` e rientrare più tardi
   — Sentinel deve essere ancora in esecuzione, con cicli/dispositivi/
   frame accumulati nel frattempo.

## Test plan — Fase 35 (dispositivo scomparso, flood integrato, rotazione pcap, riepilogo)

1. **Dispositivo scomparso**: con Sentinel attivo e almeno un dispositivo
   già rilevato (nuovo o presente al primo ciclo), spegnere/disconnettere
   quel dispositivo — dopo ~2 cicli (~60s) deve comparire un evento
   `GONE` in ambra con bip sonoro, consultabile per esteso con `I`.
2. **Nessun evento GONE prematuro**: un dispositivo che manca per un
   solo ciclo (es. un timeout ARP occasionale) non deve generare un
   evento — solo dopo 2 mancati consecutivi.
3. **Ritorno silenzioso**: far ricomparire un dispositivo già segnalato
   `GONE` — non deve generare alcun evento di "ritorno"; se sparisce di
   nuovo in seguito, deve poter essere ri-segnalato normalmente.
4. **Deauth flood rilevato senza GUARD MODE**: con solo `SENTINEL MODE`
   attivo (non `GUARD MODE`), generare un vero flood di deauth contro un
   AP di test (SOLO sulla propria rete) — entro la finestra di 10s deve
   comparire un evento `FLOOD` in rosso con allarme sonoro, stesso
   comportamento di GUARD MODE ma senza doverlo avviare separatamente.
5. **Rotazione pcap**: forzare (o attendere, su una rete molto
   trafficata) il superamento dei 5MB sul file corrente — deve comparire
   un nuovo file `..._p2.pcap` e il contatore "parts" nella schermata
   deve salire a 2; il file `_p1.pcap` precedente deve restare valido e
   apribile (chiuso correttamente, non troncato a metà record).
6. **Riepilogo di sessione**: fermare Sentinel dopo aver accumulato
   almeno un evento di ciascun tipo — verificare che
   `..._summary.txt` esista sotto `/netrunner` e contenga durata, cicli,
   frame totali, elenco delle parti pcap, e ogni evento con i suoi dati
   (IP/MAC/hostname/vendor per new/gone, BSSID per flood).
7. **Log eventi unificato nella UI**: con eventi di più tipi presenti,
   verificare che ogni riga mostri l'etichetta corretta (NEW/GONE/FLOOD)
   col colore giusto (magenta/ambra/rosso) e che `I` mostri il dettaglio
   pertinente al tipo (BSSID per FLOOD, IP/MAC/hostname/vendor per gli
   altri due).

## Test plan — Fase 36 (rilevamento EAPOL, PMKID sweep)

**Solo in ambiente autorizzato**, stessa regola di ogni altro strumento
che tenta un'associazione/cattura verso un AP:

1. **PMKID rilevato**: contro un AP di test WPA2-PSK che offre il PMKID
   nel primo messaggio EAPOL (molti router consumer moderni), avviare
   `PMKID CAPTURE` — a fine cattura deve mostrare "PMKID likely
   captured!" in verde, non solo il conteggio pacchetti.
2. **PMKID assente**: contro un AP che non lo offre (o WPA3-only/
   Enterprise), la cattura deve terminare mostrando "no PMKID seen this
   time" in ambra, senza falsi positivi.
3. **Handshake rilevato**: contro un client reale connesso a un AP di
   test, avviare `DEAUTH + CAPTURE` — se il client si riassocia entro la
   finestra di cattura, deve mostrare "handshake likely captured!" in
   verde (Message1 E Message2 entrambi visti).
4. **Verifica incrociata col PC**: per almeno una cattura con verdetto
   positivo, aprire il pcap risultante con Wireshark (filtro `eapol`) o
   `hcxpcapngtool` e confermare che il verdetto del dispositivo era
   corretto — non deve mai dichiarare un falso positivo (mai un vero
   negativo dichiarato falso positivo).
5. **PMKID SWEEP, nessun target**: da `WAR DRIVING` senza ancora nessun
   sighting non-aperto, `S` deve mostrare "no WAR DRIVING sightings yet"
   e non avviare nulla.
6. **PMKID SWEEP, sequenza multi-AP**: con almeno due sighting non
   aperti/non nascosti noti, avviare `S` — deve mostrare "1/N", poi
   "2/N", ecc. via via che ogni AP viene provato in sequenza (mai in
   parallelo), popolando la lista risultati con SSID/verdetto per
   ciascuno.
7. **PMKID SWEEP, header**: durante lo sweep, l'header deve mostrare
   `RF:PMK` mentre una cattura per-AP è attiva (stesso tag del PMKID
   CAPTURE singolo) — `PMKID SWEEP` stesso non è un consumer promiscuo,
   solo un orchestratore.
8. **PMKID SWEEP, DEL e rientro**: uscire con `DEL` mentre lo sweep gira
   e rientrare — deve mostrare ancora il progresso corretto (indice/
   totale/hit) accumulato nel frattempo.

## Test plan — Fase 37 (terza passata UX/UI, 15 punti)

1. **Breadcrumb `CHANNEL SCAN`**: aprire `CHANNEL SCAN` da un percorso che
   mostra il breadcrumb del genitore — deve comparire `CHAN` invece che
   nulla.
2. **`WAR DRIVING` help text**: premere `?` sulla schermata — deve
   comparire la riga sulle frecce per muovere la selezione tra i
   sighting.
3. **`THREATS` + SENTINEL MODE**: con SENTINEL MODE avviato e almeno un
   evento nuovo/scomparso/flood generato, aprire `THREATS` — deve
   comparire il finding corrispondente con la severità attesa
   (ambra/ambra/rosso).
4. **`THREATS` + PMKID SWEEP**: dopo uno sweep con almeno un hit, aprire
   `THREATS` — deve comparire il riepilogo informativo ciano col numero
   di hit.
5. **`FILE MANAGER` scorciatoie**: da `/`, premere `N` — deve saltare a
   `/netrunner` (o mostrare "empty directory" se non esiste ancora);
   `H` (da `/`) deve fare lo stesso per `/handshakes`.
6. **`CAPTURES`**: da `SETTINGS`, `C` apre la nuova schermata — con
   almeno un `.pcap` già scritto (da PMKID CAPTURE, DEAUTH+CAPTURE o
   SENTINEL MODE) deve comparire nella lista con nome e dimensione; `I`
   mostra il percorso completo; `X` chiede conferma e poi cancella.
7. **`DISCOVERY` indicatore di prontezza**: senza mai aver girato
   `NETWORK SCAN`, aprire `DISCOVERY` — SNMP/DATASTORE/LDAP SWEEP e NTLM
   DISCLOSURE devono mostrare il pallino rosso; dopo un `NETWORK SCAN`
   completato (con almeno un host con porta HTTP nota per NTLM
   DISCLOSURE), il pallino deve diventare verde senza dover riaprire la
   schermata.
8. **`WAR DRIVING` sottomenu offensivo**: con almeno un sighting
   selezionato, premere `O` — deve aprire il menu con le quattro voci;
   `ENTER` su una lancia la stessa schermata che lancerebbe il tasto
   diretto corrispondente (E/X/P/S), rispettando lo stesso gate
   offensivo; `DEL` torna indietro senza lanciare nulla.
9. **`GUARD MODE` vs `SENTINEL MODE`**: aprire `GUARD MODE` da solo
   (senza SENTINEL MODE attivo) — nessuna nota; avviare SENTINEL MODE,
   poi riaprire `GUARD MODE` senza averlo avviato in autonomia — deve
   comparire "(SENTINEL MODE includes this)".
10. **Header "molto occupato"**: avviare tre o più task in
    background/promiscui insieme (es. `WAR DRIVING` + `NETWORK SCAN` +
    `SENTINEL MODE`) — la linea sotto l'header deve diventare ambra (o
    rossa se due promiscui competono per lo stesso momento); fermarne
    abbastanza da scendere sotto tre — deve tornare grigia.
11. **`ACTIVITY`**: dal menu principale, aprire `ACTIVITY` — ogni
    manager avviato deve comparire in cima alla lista con il pallino
    verde e l'etichetta `RF`/`BG` corretta; il contatore in basso deve
    corrispondere al numero effettivo di task attivi.
12. **`GUARD MODE` tasso per finestra**: generare un incidente flood,
    lasciarlo scendere sotto soglia, poi farlo ripartire — la colonna
    finestra (`.../w`) deve tornare a un numero basso mentre quella
    totale (`...t`) continua a salire.
13. **`PMKID SWEEP` anteprima**: con almeno un sighting non aperto/non
    nascosto noto a WAR DRIVING e lo sweep mai avviato in questa
    sessione, aprire `PMKID SWEEP` — deve comparire la lista di anteprima
    con SSID, barre di segnale e canale, non "no results yet" a vuoto.
14. **Overlay di aiuto globale**: premere `?` su una qualunque schermata
    — deve comparire sempre, subito sopra "any key: close", la riga
    `I/TAB vary by screen  ?:this help`.

## Test plan — Fase 38 (IoT/OT sweep, PLAYBOOK)

**Solo in ambiente autorizzato**, stessa regola di ogni altro strumento
di questo firmware:

1. **MQTT anonimo accettato**: contro un broker Mosquitto/EMQX di test
   configurato senza autenticazione, `IOT/OT SWEEP` deve mostrare
   `mqtt` con NO-AUTH in rosso.
2. **MQTT richiede auth**: contro lo stesso broker riconfigurato con
   `allow_anonymous false`, deve mostrare "auth required" in ambra, non
   NO-AUTH.
3. **Modbus TCP**: contro un simulatore Modbus TCP di test (es.
   `pymodbus` in modalità server), deve comparire `modbus` con il nome
   vendor se il simulatore risponde a Read Device Identification, o
   "responds (device id not supported)" se risponde con un'eccezione di
   protocollo — in entrambi i casi NO-AUTH, perché Modbus non ha
   autenticazione per definizione.
4. **CoAP**: contro un server CoAP di test (es. `aiocoap`) che espone
   `/.well-known/core`, deve comparire `coap` con parte della risposta
   come dettaglio.
5. **IoT/OT sweep senza NETWORK SCAN**: senza host vivi noti, `ENTER`
   deve mostrare "no alive hosts - run NETWORK SCAN first" e terminare
   subito, senza falsi risultati.
6. **RUN ALL DISCOVERY include IoT/OT**: durante `RUN ALL DISCOVERY`,
   la fase deve mostrare "IOT/OT SWEEP" tra DATASTORE e LDAP, con la
   barra di progresso che avanza di conseguenza.
7. **PLAYBOOK, selettore**: aprendo `PLAYBOOK` da inattivo, le frecce
   devono scorrere i tre preset (FULL RECON/QUICK IOT/OT/WIRELESS
   SURVEY) con nome e descrizione leggibili.
8. **PLAYBOOK, FULL RECON**: avviarlo con WiFi connesso — deve mostrare
   "step 1/2: AUTO ASSESS" e poi, al termine di quello, "step 2/2: RUN
   ALL DISCOVERY", con la barra di progresso che avanza a metà tra i
   due step.
9. **PLAYBOOK, WIRELESS SURVEY senza WiFi**: avviarlo senza essere
   connessi a nessuna rete — a differenza degli altri due preset, deve
   partire comunque (mostrare "step 1/2: WAR DRIVING"), non bloccarsi
   su "no WiFi - connect first".
10. **PLAYBOOK, DEL e rientro**: uscire con `DEL` mentre un preset gira
    e rientrare — deve mostrare ancora lo step/progresso corretto,
    accumulato nel frattempo, stesso comportamento di ogni altro
    orchestratore in background di questo firmware.
11. **PLAYBOOK, ENTER ferma**: durante l'esecuzione, `ENTER` deve
    fermare il preset alla fine dello step corrente (non a metà) e
    mostrare "cancelled" nel log.

## Test plan — Fase 39 (BACnet/DNP3, integrazione THREATS)

**Solo in ambiente autorizzato**, stessa regola di ogni altro strumento
di questo firmware:

1. **BACnet I-Am**: contro un dispositivo/simulatore BACnet di test (es.
   BACnet stack di riferimento, o un building controller reale SOLO se
   autorizzato), `IOT/OT SWEEP` deve mostrare `bacnet` con "responds
   (I-Am)".
2. **DNP3 link status**: contro un outstation/simulatore DNP3 di test
   (es. `pydnp3` in modalità outstation), deve mostrare `dnp3` con
   "responds (link status)". Se non risponde nulla nonostante
   l'outstation sia raggiungibile: verificare innanzitutto se il CRC
   calcolato da questo firmware è quello che il dispositivo si aspetta
   (vedi "Limiti noti" — non verificato contro hardware reale in fase
   di sviluppo).
3. **BACnet/DNP3 senza risposta**: contro un host che non parla nessuno
   dei due protocolli, `IOT/OT SWEEP` non deve produrre alcun finding
   `bacnet`/`dnp3` per quell'host (nessun falso positivo).
4. **`THREATS` mostra i finding OT in rosso**: dopo un `IOT/OT SWEEP`
   con almeno un finding Modbus/BACnet/DNP3, aprire `THREATS` — quel
   finding deve comparire in rosso (Critical), non in ambra.
5. **`THREATS` mostra i finding IoT in ambra**: dopo un `IOT/OT SWEEP`
   con almeno un finding MQTT/CoAP no-auth, aprire `THREATS` — deve
   comparire in ambra (Warning), non in rosso.
6. **`THREATS` non mostra i finding "auth required"**: un finding
   `IOT/OT SWEEP` con `noAuth=false` (es. MQTT che ha rifiutato la
   connessione anonima) non deve comparire affatto in `THREATS` — solo
   i finding realmente senza autenticazione sono un "threat".

## Test plan — Fase 41 (percorso di navigazione)

1. **Breadcrumb multi-livello**: da MENU, aprire NETWORK SCAN, poi
   DISCOVERY (`D`), poi BEACON/PROBE INTEL — l'header di quest'ultima
   deve mostrare "NET/DISC/" (dim) prima di "BCN" (acceso), non solo
   "DISC/".
2. **Breadcrumb vuoto alla radice**: su MAIN MENU stesso, l'header non
   deve mostrare alcun breadcrumb (stack di un solo elemento).
3. **Help overlay con percorso**: aprire una schermata raggiunta con più
   di un salto (es. IOT/OT SWEEP) e premere `?` — deve comparire la riga
   `MENU>NET>D>Ent(IOT)` subito sotto il titolo.
4. **Help overlay senza percorso sui top-level**: aprire una delle 11
   voci dirette del menu principale (es. THREATS) e premere `?` — non
   deve comparire nessuna riga di percorso (non serve, è già a un salto
   dal menu).
5. **Nessuna riga persa**: per almeno tre schermate il cui `helpText()`
   era già lungo prima di questa fase (es. BEACON/PROBE INTEL, GUARD
   MODE), verificare che l'overlay non mostri meno informazione utile di
   prima — la riga di percorso ha sostituito una riga vuota, non
   aggiunto una riga.

## Test plan — Fase 42 (backup sotto /netrunner)

1. **Backup su SD vuota**: con una SD che non ha ancora `/netrunner`,
   premere `B` da SETTINGS — deve creare la cartella e scrivere
   `/netrunner/config_backup.json` (visibile anche da `FILE MANAGER`
   con `N`), non fallire per cartella mancante.
2. **Restore**: dopo aver salvato una rete WiFi e un'allowlist WAR
   DRIVING, cancellarle (FORGET / rimozione manuale), poi premere `R` —
   devono tornare esattamente come nel backup.
3. **Nessun backup trovato**: su una SD che non ha mai avuto un backup
   scritto da questa build, `R` deve mostrare "no backup found on SD",
   non un errore di parsing.

## Test plan — Fase 43 (bug fix: scansione WiFi bloccata)

1. **Scansione con rete salvata irraggiungibile**: salvare una rete
   WiFi, poi spegnerla/portare il dispositivo fuori portata, riavviare
   (così `autoConnect()` resta a ritentare invano) e aprire WIFI SCAN —
   `ENTER` deve produrre una lista di reti entro pochi secondi, non
   restare bloccato su "scanning...".
2. **Nessuna interruzione se già connessi**: con una connessione già
   stabilita e funzionante, `ENTER` su WIFI SCAN deve comunque produrre
   una lista di reti, e la connessione esistente non deve mai cadere
   durante la scansione.
3. **Ripristino all'uscita**: dopo una scansione senza essersi connessi
   a nulla (es. `DEL` da NETWORK LIST), il dispositivo deve tornare a
   tentare la rete salvata da solo (verificabile aspettando che
   `isConnected()` torni vero se la rete rientra in portata), non
   restare scollegato fino al riavvio successivo.
4. **Lista reti salvate**: con almeno una rete salvata, `S` da WIFI SCAN
   deve mostrare la lista (fino a 3, più recente prima) indipendentemente
   dallo stato della scansione — non deve più essere necessario fare
   "forget" per raggiungerla.

## Test plan — Fase 44 (overlay di aiuto non più troncato)

1. **Nessun troncamento sulle 10 schermate corrette**: per ognuna delle
   dieci elencate nella sezione "Fase 44" sopra, premere `?` e
   verificare che l'ultima riga del testo sia completamente visibile
   (non tagliata dal bordo inferiore del pannello).
2. **Nessuna informazione persa**: per almeno tre schermate (es. BEACON/
   PROBE INTEL, PMKID CAPTURE, GUARD MODE), verificare che ogni tasto/
   comportamento documentato prima della Fase 44 sia ancora menzionato
   da qualche parte nel testo condensato.
3. **Le altre schermate restano invariate**: aprire l'help overlay su
   una schermata NON toccata da questa fase (es. THREATS, già a 7 righe)
   — deve apparire identica a prima.

## Test plan — Fase 45 (contatore reti salvate in WIFI SETUP)

1. **Lista vuota**: senza reti salvate, aprire la lista (`S` da WIFI
   SETUP) — deve comparire "saved: 0/3" in alto, seguito dal messaggio
   "no saved networks", senza sovrapposizioni.
2. **Conteggio corretto**: salvare una rete e riaprire la lista — deve
   mostrare "saved: 1/3"; ripetere fino a 3 reti salvate, verificando che
   il contatore segua ("2/3", poi "3/3").
3. **Layout a lista piena**: con 3 reti salvate (il massimo), verificare
   che il contatore in alto e le tre righe della lista non si
   sovrappongano e restino tutte leggibili (lo spostamento di `top` da 20
   a 30 lascia spazio sufficiente).
4. **Eviction della quarta rete**: connettersi a una quarta rete diversa
   e salvarla — il contatore deve restare "3/3" (la meno recente viene
   evinta), rendendo visibile a schermo il comportamento che prima era
   silenzioso.

## Test plan — Fase 46 (NAME SPOOF: poisoning LLMNR/NBT-NS)

1. **Gate offensivo**: alla prima apertura di `NAME SPOOF` in questa
   sessione (menu principale), deve comparire `OffensiveDisclaimerScreen`
   e richiedere di digitare AUTHORIZED per intero prima di procedere,
   esattamente come per deauth/PMKID/evil-twin/MITM.
2. **Poisoning LLMNR**: con un secondo dispositivo sulla stessa rete
   che genera una query LLMNR per un nome che non risolve via DNS (es.
   un hostname inventato), avviare NAME SPOOF — deve comparire una riga
   di log "LLMNR '<nome>' <- <ip>" e il dispositivo che ha interrogato
   deve ricevere l'IP di questo Cardputer come risposta (verificabile
   con `ping <nome>` su quel dispositivo, che deve risolvere all'IP del
   Cardputer).
3. **Poisoning NBT-NS**: stesso test con una query NBT-NS (es. da un
   host Windows che tenta di risolvere un nome NetBIOS non in DNS) —
   deve comparire una riga di log "NBT-NS '<nome>' <- <ip>".
4. **Cap di durata**: impostare la durata al massimo consentito (`</>`
   fino a fondo scala) e verificare che non superi `kMaxDurationS` =
   300s; la sessione deve fermarsi da sola allo scadere anche senza
   premere ENTER/DEL.
5. **Nessun impatto sulla connessione WiFi**: durante una sessione NAME
   SPOOF attiva, la connessione WiFi del Cardputer stesso deve restare
   funzionante (a differenza di WAR DRIVING/BEACON-PROBE che la
   sospendono per fare channel hopping) — verificabile controllando che
   l'IP mostrato nello status bar del menu principale non cambi.

## Test plan — Fase 47 (OS FINGERPRINT: TTL/finestra/opzioni TCP)

1. **Bucket TTL corretto**: avviare OS FINGERPRINT e far completare un
   handshake TCP a un dispositivo Windows noto (es. aprire un PORT SCAN
   verso di lui, o lasciarlo generare traffico proprio) — il bucket
   mostrato deve essere "t128"; ripetere con un dispositivo Linux/
   Android/macOS noto — deve essere "t64".
2. **Nessuna finta precisione**: il dettaglio (`I`) deve mostrare
   l'etichetta generica ("Windows", "Linux/BSD/macOS/Android", ...) più
   i valori grezzi (TTL esatto, finestra, ordine opzioni) — mai un nome
   di OS/versione specifico che il codice non può verificare.
3. **Ordine opzioni leggibile**: verificare che la stringa nel dettaglio
   (es. "MSWT") corrisponda a un ordine plausibile di opzioni TCP
   (M/W/S/T/N) e non contenga caratteri "?" a raffica su un host con
   uno stack TCP/IP standard (un singolo "?" occasionale è accettabile
   — un'opzione TCP non tra le quattro riconosciute).
4. **Nessun impatto su altri strumenti radio**: come per gli altri
   ascoltatori passivi, verificare che non giri insieme a un secondo
   consumatore promiscuo (es. WAR DRIVING) senza il tag "RF:N!" in
   header che segnala il conflitto.
5. **Sola lettura**: verificare che nessun pacchetto venga inviato da
   questo dispositivo durante l'ascolto (a differenza di NAME SPOOF/
   MITM AUDIT) — solo osservazione di traffico TCP altrui già in corso.

## Test plan — Fase 48 (VLAN HOP: leak 802.1Q + probe double-tag)

1. **Gate offensivo solo sul tasto P**: aprire VLAN HOP e verificare
   che l'ascolto passivo (`ENTER`) funzioni SENZA alcun disclaimer;
   premere `P` la prima volta in questa sessione — deve comparire
   `OffensiveDisclaimerScreen` e richiedere AUTHORIZED prima di
   procedere.
2. **Nessuna voce duplicata nello stack**: dopo aver accettato il
   disclaimer da `P`, premere `DEL` una sola volta — deve tornare
   direttamente a `DISCOVERY`, non restare su `VLAN HOP` una seconda
   volta (verifica del fix `replaceScreen` vs `pushScreen` descritto in
   "Fase 48").
3. **Rilevamento leak passivo**: se disponibile un ambiente di test con
   un AP che effettivamente bridga traffico taggato (es. un laboratorio
   con trunk misconfigurato) sul segmento wireless, verificare che
   appaia una riga "tag leak: vlan N" nel log e una entry nella lista;
   altrimenti (caso comune), verificare che la lista resti vuota con il
   messaggio "no VLAN tags seen yet" — un risultato vuoto è il caso
   atteso sulla stragrande maggioranza delle reti, non un fallimento.
4. **Probe inviato, non "riuscito"**: dopo `P`, verificare che il log
   dica "double-tag probe sent" — mai "worked"/"reached"/parole che
   implichino conferma, in nessun percorso del codice.
5. **Campi VLAN regolabili**: `TAB` sposta il focus tra native/target,
   `</>` decrementa/incrementa il valore a fuoco tra 1 e 4094 senza
   andare sotto/sopra questi limiti.

## Test plan — Fase 49 (EVIL TWIN: KARMA mode)

1. **Nessuna candidata senza BEACON/PROBE INTEL**: da `EVIL TWIN` con
   `BeaconProbeSniffer` mai avviato in questa sessione (0 client
   probati noti), verificare che il conteggio candidate mostri "0 -
   none yet" e che `TAB` fallisca con "no candidates - run BEACON/PROBE
   INTEL first" senza avviare alcun AP.
2. **Digitare una SSID contenente 'k'/'K' resta possibile**: in stato
   di inserimento SSID, digitare una stringa con la lettera K (es.
   "Kevin") — deve comparire nel campo normalmente, senza attivare
   KARMA (verifica del fix TAB-vs-Char di questa fase).
3. **Ciclo candidate corretto**: dopo aver raccolto almeno 2 SSID
   distinte in BEACON/PROBE INTEL, avviare KARMA (`TAB`) e verificare
   che l'intestazione mostri "KARMA (1/N): <ssid>", che cambi in
   "KARMA (2/N): <altra ssid>" dopo `kKarmaDwellMs` (8s), e così via a
   ciclo chiuso (torna a 1/N dopo l'ultima).
4. **Associazioni registrano l'SSID corretto**: far associare un
   client di test durante una finestra KARMA e verificare che il log
   mostri `client connected: <mac> -> "<ssid corrente in quel momento>"`
   — non l'SSID iniziale se nel frattempo si è già ciclato oltre.
5. **Modo a SSID fisso resta invariato**: avviare EVIL TWIN con `ENTER`
   (non `TAB`) su una SSID digitata — deve comportarsi esattamente come
   prima di questa fase (nessuna etichetta "KARMA", SSID costante).

## Test plan — Fase 50 (MITM AUDIT: harvesting cookie/credenziali)

1. **Valore reale catturato, non solo la categoria**: su una rete
   APERTA di test, generare una richiesta HTTP con header `Cookie:
   session=abc123` verso un host qualunque mentre MITM AUDIT gira con
   sniff traffico attivo — la lista harvest (`H`) deve mostrare "HTTP
   Cookie" con un'anteprima che inizia con "Cookie: session=abc123",
   non solo il conteggio.
2. **Dettaglio non troncato**: selezionare quella entry e premere `I` —
   la riga intera deve essere leggibile per esteso nell'overlay,
   incluso oltre i 22 caratteri già visibili nella lista.
3. **Persistenza su SD**: dopo la cattura, verificare che
   `/mitm/harvest.csv` contenga una riga con timestamp, MAC sorgente,
   IP:porta destinazione, kind e la riga catturata, anche se la sessione
   MITM viene interrotta bruscamente (es. rimuovendo il target dalla
   portata) invece di fermata con `ENTER`.
4. **Reti cifrate restano illeggibili**: su una rete WPA2/WPA3 di test,
   verificare che nessun `HarvestedItem` compaia mai — il Protected
   Frame bit deve continuare a fermare l'analisi prima che
   `findAsciiLine` venga anche solo chiamata (comportamento invariato
   rispetto a prima di questa fase).
5. **Navigazione H/I non interferisce con lo stop**: durante la
   navigazione della lista harvest, verificare che `DEL` torni al log
   (non fermi la sessione MITM) — solo `DEL`/`ENTER` nel log normale
   devono fermare `ArpSpoofManager`.

## Test plan — Fase 51 (EAP IDENTITY: username 802.1X in chiaro)

1. **Nessun frame Request registrato**: verificare che una richiesta
   EAP-Request/Identity (Code=1) inviata dall'AP non generi mai una
   entry — solo le Response (Code=2) contano, per non catturare i
   prompt del server come se fossero identità del client.
2. **Nessun frame protetto letto**: su una rete WPA2/WPA3 di test già
   associata, verificare che il Protected Frame bit continui a
   scartare i frame prima di `parseEapIdentity` — la lista deve
   restare vuota anche con traffico intenso, come per gli altri
   parser passivi.
3. **Dedup su (MAC, identity)**: forzare un client di test a
   riassociarsi più volte con la stessa outer identity — la lista
   deve mostrare una sola riga con contatore che sale, non una
   nuova riga per ogni tentativo.
4. **Identità diverse dallo stesso MAC restano separate**: se un
   client cambia outer identity tra due associazioni (es. anon per
   una rete, `user@corp` per un'altra), entrambe devono comparire
   come righe distinte.
5. **Persistenza SD**: verificare che ogni identità NUOVA venga
   appesa a `/eap/identities.csv` con timestamp, MAC e identity,
   anche interrompendo la sessione senza `stop()` esplicito.

## Limiti noti e tagli di scope deliberati

Riepilogo di quanto già menzionato nelle sezioni sopra, in un unico
posto:

- **War driving: solo passivo per default, mai per reti non
  autorizzate** (Fase 11) — questa è una scelta di scope deliberata, non
  un limite tecnico: la richiesta originale era "collegati a ogni rete
  aperta trovata", implementata invece come scan-e-log passivo sempre +
  connessione automatica solo per SSID che l'utente aggiunge di persona
  a un'allowlist con conferma esplicita. Vedi la sezione "Fase 11"
  sopra per il ragionamento completo.
- **War driving e gli altri moduli di scansione condividono un solo
  radio WiFi** (Fase 11): usarli insieme (es. `NETWORK SCAN` manuale
  mentre il war driving sta facendo un'incursione su un AP allow-
  listato) può far sì che uno dei due diventi silenziosamente un no-op
  — stesso genere di limite già accettato per `PortScanManager` a un
  solo scan alla volta.
- **NTP senza RTC a batteria, RISOLTO in Fase 27 se un'unità è collegata**:
  fino alla Fase 26 l'orario si perdeva sempre a ogni spegnimento e andava
  risincronizzato al boot successivo (richiede WiFi) — un limite hardware
  di questa scheda (nessun RTC di serie), non del codice. Dalla Fase 27,
  collegando un'unità RTC a batteria alla porta Grove (es. M5Stack RTC
  Unit, BM8563) e con `cfg.external_rtc=true` (`main.cpp`), `TimeSync`
  la rileva da sola e semina l'orologio da lì al boot, senza aspettare
  il WiFi — vedi la sezione "Fase 27" sopra. **Resta un limite hardware
  puro sul Cardputer/Cardputer ADV senza nulla collegato alla porta
  Grove**: in quel caso (il caso comune, scheda "nuda") il comportamento
  è quello di sempre, orario perso a ogni spegnimento, uptime finché NTP
  non risincronizza.
- **Nessun editor manuale di subnet**: `NETWORK SCAN` usa sempre la
  subnet DHCP-rilevata (non ha senso poterla cambiare finché non c'è
  un modo di specificare un range arbitrario in modo sicuro). Il range
  porte invece **è** ora editabile da `SETTINGS` (Fase 6).
- **`MdnsReverseResolver` (hostname per-host durante NETWORK SCAN) resta
  solo fallback a NBNS** (Fase 10, #5): interrogato solo quando NBNS non
  ha già trovato un nome, e solo con una singola query PTR reverse a
  bassa priorità — dispositivi che ignorano query reverse PTR (alcuni,
  non tutti, lo fanno) restano senza hostname da questo meccanismo. Un
  **browser DNS-SD completo esiste comunque dalla Fase 19**
  (`ServiceEnumerator`, `SERVICE SCAN`) — separato da questo fallback,
  interroga `_services._dns-sd._udp.local` e poi ogni tipo di servizio
  trovato — e dalla Fase 27 i suoi risultati vengono **correlati agli
  host** (per IP sorgente della risposta) e usati come fonte di hostname
  aggiuntiva quando sia NBNS sia la reverse-PTR non trovano nulla — vedi
  la sezione "Fase 27" sopra. Questa voce descriveva già solo il
  meccanismo di fallback per-host, non l'assenza di un browser mDNS
  completo altrove nel firmware — la formulazione precedente era
  ambigua su questo punto anche prima della Fase 27.
- **Euristica Telnet non affidabile al 100%**: il rilevamento di login
  riuscito è basato su pattern-matching testuale, non su un parser di
  protocollo — vedi Fase 4. FTP invece usa i codici di risposta
  numerici del protocollo (RFC 959), più affidabile.
- **Moduli exploit/RCE, brute-force SSH, rilevamento SMBv1/UPnP,
  grafica avanzata (radar/grafici a ciambella) non implementati** —
  scelte deliberate motivate in dettaglio nella sezione "Fase 6"
  sopra, non dimenticanze.
- **SD card ora cablata di default** (Fase 10, #3), ma con il pin CS
  meno verificabile di tutto il firmware — vedi il commento in
  `storage/SdCard.cpp`. Se non hai una SD inserita, tutto continua a
  funzionare su LittleFS esattamente come prima.
- **Probe UDP/mDNS non compilati né testati qui** (Fase 10): le due
  parti più a rischio di questa fase (pin SD, firma
  `beginMulticast()`) sono segnalate esplicitamente nei rispettivi
  sorgenti/commit — vedi il test plan dedicato sopra. La terza parte a
  rischio, la tabella partizioni OTA, **è stata testata su hardware
  reale** ed è stato trovato e corretto un bug reale (offset di
  `otadata` sbagliato, causava un boot loop) — vedi il bullet dedicato
  nella sezione "Fase 10" sopra.
- **Copertura caratteri per la password WiFi non garantita al 100%**:
  `WifiSetupScreen` accetta qualunque carattere che `M5Cardputer.Keyboard`
  consegni in `status.word` (lettere, cifre, simboli comuni raggiunti
  con `Fn`/`Opt`). Non è stato possibile verificare su hardware reale
  se **ogni** combinazione di simboli usabile in una passphrase WPA2 sia
  effettivamente raggiungibile dalla tastiera fisica del Cardputer — se
  la tua password contiene un carattere che non riesci a digitare, è un
  limite della mappatura tastiera di M5Cardputer, non di questo codice
  (che si limita a inoltrare quello che la libreria gli consegna).
- **Sound design minimo, non un tema audio completo** (Fase 12-13-15-17):
  tre suoni esistono — il loop di boot stile Nightcall (Fase 17,
  sostituisce il riff one-shot della Fase 15), il beep su nuova rete
  aperta in war driving, e l'allarme a due toni condiviso da audit
  credenziali riuscito ed evil-twin sospetto — non un feedback sonoro
  per ogni azione dell'interfaccia. `M5Cardputer.Speaker` resta
  disponibile per chi voglia estenderlo, ma resta monofonico (un solo
  `tone()` alla volta) — niente accordi reali, solo linee melodiche
  sequenziali.
- **Musica di boot: di nuovo una composizione originale, non una
  trascrizione** (Fase 17, tre ritocchi): la cronologia completa —
  originale "in stile Nightcall" → trascrizione a voce sola dello
  spartito dell'utente (fatta solo dopo conferma esplicita dei diritti)
  → di nuovo originale, su richiesta dell'utente, ma stavolta costruita
  sulla stessa progressione armonica dello spartito (La minore-Fa-Do-
  Sol, riutilizzabile anche a prescindere dall'autorizzazione dato
  quanto è comune) — mostra il criterio guida usato per qualunque
  musica in questo progetto: comporre "in stile X"/evocare un'atmosfera
  è sempre permesso, trascrivere letteralmente la melodia di qualcun
  altro richiede prima un'autorizzazione esplicita e verificabile.
- **Nebbia digitale: puramente decorativa, densità legata all'area, non
  un vero effetto di trasparenza** (Fase 17): `chrome::drawDigitalFog`
  disegna punti singoli opachi, non una vera sovrapposizione
  alpha-blended (RGB565 su questo hardware non la rende economica) —
  l'illusione di "nebbia leggera" viene dalla bassa densità e dal
  colore fioco, non da una reale trasparenza.
- **Scan BLE: rimosso in Fase 14**, su richiesta esplicita dell'utente
  per risparmiare spazio flash — vedi la sezione "Fase 14" sopra per il
  dettaglio (incluso lo storico dei tre bug API reali che il modulo
  aveva fatto emergere prima di essere tolto).
- **Evil-twin: euristica su SSID+tier di cifratura+vendor OUI, non una
  prova crittografica** (Fase 13, euristica rivista in Fase 33): due AP
  con lo stesso nome vengono segnalati come sospetti se il tier di
  sicurezza differisce in modo significativo (vedi `securityTier()` in
  `WardrivingManager.cpp` — WPA2 e WPA/WPA2 misto contano come lo stesso
  tier apposta, per non generare falsi positivi su un singolo router
  reale in mixed-mode) OPPURE se il vendor OUI risolto differisce fra i
  due (solo quando entrambi i lookup hanno dato un risultato — un
  vendor sconosciuto da solo non è un segnale). Nessuna pretesa di
  stabilire quale dei due sia quello legittimo. Resta un'euristica, non
  una prova: un attaccante che clona sia la cifratura sia usa hardware
  dello stesso vendor (raro, ma non impossibile) non verrebbe rilevato,
  e un ambiente con più vendor legittimi sullo stesso SSID (roaming
  misto, raro ma esiste) potrebbe generare un falso positivo sul solo
  segnale vendor. Il canale è stato deliberatamente escluso da questa
  euristica — vedi il commento nel codice per il perché.
- **Backup impostazioni: password WiFi in chiaro nel JSON su SD**
  (Fase 13): scelta deliberata (vedi sopra), ma significa che
  `/config_backup.json` va trattato con la stessa cura della SD stessa
  — chiunque possa leggere quel file ha le tue password WiFi salvate.
- **LoRa/GPS**: non presenti di serie sul Cardputer ADV (confermato
  nella ricerca hardware iniziale di questo progetto), quindi non
  affrontati.
- **ARP MITM: nessun relay/forwarding del traffico intercettato**
  (Fase 16): scelta deliberata, non un limite tecnico rimandato — vedi
  la sezione "Fase 16" sopra per il ragionamento completo. Sulla pratica
  significa: su rete cifrata, prova solo se l'avvelenamento ARP ha
  successo, non legge contenuti; su rete aperta, lo sniffing passivo
  vede tutto comunque, senza bisogno del MITM.
- **Deauth: una raffica fissa e un solo client per sessione, mai un
  loop** (Fase 16): scelta deliberata per tenere lo strumento nel
  registro "audit WPA con handshake capture" (pratica standard del
  settore) invece che "strumento di disturbo generico" — non esiste,
  da nessuna parte nell'interfaccia, un modo di deauthare più di un
  client alla volta o di ripetere automaticamente.
- **Evil twin sempre aperto, mai una password reale** (Fase 16): non
  potendo conoscere la passphrase WPA2 vera della rete clonata, l'AP
  fasullo è sempre senza password — vedi sopra per perché questo è
  comunque il test interessante, non solo un compromesso di comodo.
- **Credential guessing: SSH deliberatamente assente** (Fase 16): un
  client SSH vero richiede un handshake crittografico completo — vedi
  sopra per il ragionamento, stesso principio già applicato in Fase 4.
- **`ArpSpoofManager`/`DeauthManager`: parsing 802.11/IP grezzo mai
  verificato contro traffico reale** (Fase 16): il codice più a rischio
  di tutto il progetto, oltre BLE — ogni passaggio fallisce in modo
  sicuro (bounds-check, skip silenzioso) invece di assumere il
  successo, ma la build reale resta l'unico modo per sapere se gli
  offset sono davvero giusti. Vedi il commento in cima a
  `ArpSpoofManager.h`.
- **Fase 18 tenuta sul lato difensivo/benigno, su richiesta esplicita**
  (*"non implementare funzionalità rischiose"*): delle sei funzioni di
  questo batch, cinque non agiscono su terze parti (CDP/LLDP e rogue DHCP
  ascoltano soltanto; UPnP/SSDP è discovery standard non invasiva; SMB
  Negotiate è un banner grab; il report rielabora dati già raccolti). La
  sola offensiva richiesta, la cattura PMKID, resta ma dietro lo stesso
  gate `OffensiveDisclaimerScreen` degli strumenti Fase 16.
- **CDP/LLDP: si vede qualcosa solo se l'AP fa da bridge dei multicast, e
  solo su reti aperte** (Fase 18): limite reale della modalità promiscua
  su WiFi, non del parser — molti AP domestici non inoltrano i frame
  CDP/LLDP sul segmento wireless, e i frame WPA cifrati non sono comunque
  leggibili. Su molte reti la schermata `LAN TOPOLOGY` resterà vuota: è
  atteso.
- **Rogue DHCP: rilevamento passivo, solo su reti aperte, euristica
  "diverso dal gateway"** (Fase 18): non risponde mai al DHCP, si limita a
  segnalare. Il flag "sospetto" scatta quando l'IP del server DHCP è
  diverso dal gateway in uso — un segnale, non una prova (alcune reti
  fanno girare legittimamente il DHCP su un host separato dal gateway).
  Come tutta la famiglia promiscua, non vede nulla su rete WPA cifrata.
- **SMB: solo Negotiate, non enumeration** (Fase 18): il check legge il
  Security Mode annunciato dal server con un singolo `SMB_COM_NEGOTIATE`
  e si ferma lì — niente Session Setup, login a credenziali nulle, Tree
  Connect o NetShareEnum/DCE-RPC. Enumerare share/utenti è lavoro di
  protocollo giudicato di rischio comparabile al login SSH declinato in
  Fase 16; qui si è scelto l'equivalente SMB di un banner grab.
- **Report kill-chain: indice descrittivo, non uno scanner di
  vulnerabilità** (Fase 18): la sezione ATTACK SURFACE ordina finding
  euristici già presenti nella tabella host (credenziali deboli, servizi
  in chiaro, banner con firma nota, SMB esposto) e referenzia gli artifact
  degli altri moduli — non esegue nuovi test né assegna punteggi CVSS.
  "Nessun finding" non significa "rete sicura", solo "niente ha fatto
  scattare le euristiche".
- **Radio promiscua condivisa: il limite ora vale per NOVE funzioni**
  (Fase 18-19, poi Fase 27, 33 e 34): `esp_wifi` accetta una sola
  callback promiscua alla volta — ARP/MITM, deauth, PMKID, CDP/LLDP,
  rogue DHCP, scoperta host passiva (Fase 19), beacon/probe intel (Fase
  27), GUARD MODE (Fase 33) e SENTINEL MODE (Fase 34) non vanno usate in
  parallelo (l'ultima avviata ruba i frame alle altre). Stesso genere di
  limite già accettato per WAR DRIVING vs NETWORK SCAN — con l'aggravante
  che SENTINEL MODE guida anche cicli periodici di NETWORK SCAN al suo
  interno, quindi usarlo insieme a un NETWORK SCAN manuale è lo stesso
  tipo di collisione, non un problema nuovo. La barra di stato radio in
  header (Fase 19, estesa in Fase 31) esiste proprio per rendere questo
  conflitto visibile invece che silenzioso.
- **Responder-lite/NetNTLM (punto 10) escluso di proposito** (Fase 19):
  era in una delle liste di proposte ed è stato escluso su istruzione
  esplicita dell'utente perché ad alto rischio (impersona servizi verso
  client di terzi per raccogliere hash). Implementabile in futuro solo su
  richiesta esplicita e dietro il gate `OffensiveDisclaimerScreen`. **Non
  va confuso con `NTLM DISCLOSURE` (Fase 30)**, che non impersona nulla e
  non cattura hash: si limita a leggere il Type 2 challenge che un
  server invia comunque a chiunque negozi NTLM, senza mai completare
  l'handshake — stessa categoria di rischio di un banner grab, non di
  Responder.
- **Auto-assess: catena volutamente parziale** (Fase 19): concatena solo
  i passi non-gated (discovery, port scan, report). L'audit credenziali
  NON è automatizzato — sta dietro un consenso per-sessione e lanciarlo su
  ogni host aggirerebbe quel modello. Va ancora fatto a mano per host.
- **Captive portal: si rileva, non si bypassa** (Fase 20): `c` nel war
  driving si connette a una rete aperta e `CaptivePortalDetector`
  *identifica* la presenza di un captive portal (come fa ogni OS), ma non
  esiste alcuna funzione per *aggirarlo*. Il bypass generico di un portale
  scoperto in war driving significherebbe accesso non autorizzato a una
  rete di terzi (furto di servizio) — fuori scope per design, a differenza
  degli strumenti offensivi gated che sono inquadrati come test della
  propria rete. Un tester a singolo target esplicito e autorizzato, dietro
  il gate `OFFENSIVE`, sarebbe l'unica forma legittima (non implementato).
- **Audit servizi/DB: scope contenuto e crypto di libreria** (Fase 21):
  `ServiceAuditManager` copre FTP/SMB/Redis/MySQL/PostgreSQL/VNC/HTTP con
  anon-access + credenziali di default, usando `mbedtls` (SHA1/MD5/DES),
  **non** crypto artigianale. Fuori scope deliberatamente: **MSSQL** (TDS),
  **NFS `showmount`** (RPC), **`NetShareEnum` SMB completo** (DCE-RPC, già
  declinato in Fase 18), **MySQL 8 `caching_sha2_password`** (funziona su
  5.7/MariaDB native), **PostgreSQL SCRAM** (rilevato, non brute-forzato) e
  il **form-login brute HTTP** (basic-auth è pieno). I brute usano un set
  compatto di credenziali di default, non l'intera wordlist. Nessuna
  operazione distruttiva: solo tentativi di login/anonimi, rate-limited, e
  dietro lo stesso gate di consenso del CREDENTIAL AUDIT. La detection dei
  data-store esposti (`DataStoreProbe`) è read-only e non gated, come lo
  sweep SNMP.
- **Scoperta host passiva: lista separata, solo reti aperte** (Fase 19):
  non viene fusa nella tabella di `ScanManager` (per non toccare la sua
  logica di generazione, già verificata) e, come tutta la famiglia
  promiscua, non vede nulla su WiFi WPA cifrato.
- **Enumerazione servizi mDNS: `fromIp` è l'IP sorgente della risposta,
  non un vero record A/AAAA risolto** (Fase 19, IP aggiunto in Fase 27):
  `ServiceEnumerator` non insegue mai i target SRV fino ai record A per
  risolvere l'IP di backing con una query dedicata — ogni round-trip in
  più è tempo su radio condiviso. L'IP ora annotato per ogni servizio
  (`fromIp`, usato da `ScanManager::mergeMdnsService` per la correlazione
  con la tabella host) è letto gratis dall'indirizzo sorgente del pacchetto
  UDP della risposta stessa: un'ottima approssimazione (di norma è
  proprio il dispositivo che offre il servizio a rispondere), ma non una
  garanzia formale — un ambiente con relay/proxy mDNS insoliti potrebbe
  restituire un `fromIp` fuorviante.
- **Sweep SNMP: solo GET di sysDescr con community "public"** (Fase 19):
  read-only, mai SET, un solo scalare, una sola community di default — è
  un banner grab SNMP, non un walk completo della MIB né un test di
  community multiple.
- **Beacon/probe intel: PNL harvesting parziale sui dispositivi moderni**
  (Fase 27): iOS/Android recenti randomizzano di default il MAC delle
  probe request quando non associati proprio per rendere inefficace
  questa tecnica — `BeaconProbeSniffer` la rileva (bit locally-
  administered) e lo segnala, ma non c'è modo di aggirarla: un
  dispositivo che randomizza resta tracciabile solo per la durata di
  quel singolo MAC "usa e getta", non a lungo termine. La cifratura AP
  ricostruita dalle IE grezze (RSN/vendor-WPA) non distingue in modo
  affidabile WPA2 puro da WPA3/transition mode — vedi il commento in
  `BeaconProbeSniffer.cpp`.
- **Beacon/probe intel: il channel-hopping disconnette il WiFi proprio
  del dispositivo per tutta la sessione di ascolto** (Fase 27): a
  differenza degli altri sniffer passivi (LAN TOPOLOGY, PASSIVE HOSTS,
  ROGUE DHCP) che restano sul canale già associato, questo salta su
  tutti i 13 canali 2.4GHz — un compromesso deliberato per una copertura
  reale, non un bug; la riconnessione allo stop è automatica.
- **LDAP sweep: solo bind SIMPLE anonimo, mai SASL/Kerberos, mai LDAPS**
  (Fase 30): `LdapProbe` prova solo un bind semplice DN/password vuoti su
  porta 389 in chiaro — non tenta SASL, GSSAPI/Kerberos, né si connette
  su 636 (LDAPS, richiederebbe un client TLS). Un server che risponde
  solo su LDAPS o richiede SASL non verrà rilevato come "LDAP" da questo
  sweep, anche se è pienamente in ascolto.
- **NTLM disclosure: solo HTTP semplice, mai HTTPS** (Fase 30):
  `NtlmHttpProbe` filtra sui soli host con un servizio di porta
  classificato `"http"` dal port scan — endpoint NTLM su `"https"` (molto
  comuni: OWA/Exchange, portali IIS interni) restano fuori scope finché
  questo firmware non ha un client TLS.
- **SENTINEL MODE: pcap ruota per file, ma senza limite sul totale della
  sessione; mai decripta nulla** (Fase 34, rotazione aggiunta in Fase
  35): ogni singolo file `.pcap` è ora limitato a 5MB (`kMaxPcapBytes`),
  oltre i quali se ne apre uno nuovo con lo stesso nome base e un
  suffisso `_pN` — ma non c'è ancora un tetto sul NUMERO di parti: una
  sessione lasciata attiva per giorni su una rete trafficata continua a
  produrre file aggiuntivi senza limite; fermare manualmente resta
  l'unico modo di limitare lo spazio totale occupato. La baseline
  "dispositivo mai visto" viene caricata una volta sola all'avvio dalla
  cronologia già salvata da scan precedenti di quella rete
  (`ScanHistory`, fino a 20 voci) e poi tenuta solo in memoria per il
  resto della sessione — non scrive nuovi snapshot nella cronologia
  condivisa a ogni ciclo (vedi la sezione "Fase 34" sopra per il
  perché), quindi le proprie osservazioni non arricchiscono quella
  baseline per sessioni Sentinel future, solo per quella in corso. Come
  ogni cattura promiscua in questo firmware, il payload dei frame dati
  resta cifrato esattamente come lo era sull'aria su una rete WPA2/WPA3
  — nessun tentativo di decifrarlo, mai.
- **SENTINEL MODE: "dispositivo scomparso" richiede due cicli mancati
  (~60s), nessun evento di "ritorno"** (Fase 35): la soglia
  `kMissedCyclesThreshold=2` evita falsi allarmi da un singolo sweep
  ARP/ping fallito per motivi transitori, ma significa che un
  dispositivo che sparisce e ritorna in meno di un minuto non genera mai
  un evento. Quando un dispositivo già segnalato "gone" ricompare, lo
  stato si azzera silenziosamente — non esiste un evento "device back"
  dedicato, scelta deliberata per non raddoppiare gli allarmi su
  qualcosa che di per sé non è una scoperta nuova; se sparisce di nuovo
  in seguito, viene ri-segnalato normalmente.
- **SENTINEL MODE: la fusione con GUARD MODE è logica duplicata, non
  codice condiviso** (Fase 35): `SentinelManager::checkDeauthFlood`
  reimplementa la stessa finestra scorrevole/soglia di
  `DeauthWatcher::onManagementFrame` (10s, 15 frame/finestra) invece di
  richiamare quella classe — le due classi restano indipendenti (GUARD
  MODE resta utile da solo se si vuole solo il rilevamento flood senza
  discovery/traffic dump), a costo di dover mantenere la stessa logica
  in due punti se la soglia cambierà in futuro.
- **`net/EapolWire`: euristica standard, non verificata contro una
  cattura reale** (Fase 36): la classificazione Message1-4 usa gli stessi
  bit del Key Information già documentati pubblicamente (IEEE 802.11-2020
  §12.7.2) e usati da Wireshark/aircrack-ng/hcxdumptool, ma — a
  differenza di `net/LdapWire`/`net/NtlmWire`, verificati byte-a-byte
  contro `ldap3`/`ntlm-auth` prima dell'uso — qui non è stato possibile
  costruire un handshake WPA2 reale a tavolino per un confronto
  altrettanto diretto: serve un vero scambio EAPOL fra AP e client, non
  solo una libreria che codifica messaggi. Il rischio pratico più
  probabile in caso di bug: un frame classificato come tipo sbagliato (es.
  Message2 scambiato per Message4), non un crash — la funzione fallisce
  chiuso (bounds-check ovunque) su qualunque cosa più corta del previsto.
  Il pcap scritto su SD resta comunque il dato originale, verbatim,
  intatto anche se il verdetto a bordo fosse impreciso — verificabile
  sempre con Wireshark/hashcat come prima di questa fase.
- **PMKID SWEEP: nessuna soglia di RSSI/distanza, sequenziale non
  parallelo** (Fase 36): prova ogni AP eleggibile indipendentemente da
  quanto sia debole il segnale (una cattura contro un AP troppo lontano
  probabilmente fallisce e basta, senza essere filtrata in anticipo), e
  — come ogni altro modulo che condivide il callback promiscuo — un AP
  alla volta, mai in parallelo: uno sweep su molti sighting può richiedere
  diversi minuti (~8s + tempo di associazione per AP).
- **RISOLTO in Fase 44 — Overlay di aiuto globale: budget di righe
  ristretto, alcuni `helpText()` già esistenti restavano troncati**
  (Fase 37, punto 15): aggiungere la riga fissa sui tasti globali aveva
  ridotto lo spazio di contenuto disponibile, e diversi `helpText()` già
  scritti in fasi precedenti (`SentinelScreen`, `BeaconProbeScreen` fra
  gli altri) ne avevano già più del budget — le righe in eccesso
  venivano tagliate silenziosamente. La Fase 44 ha misurato il budget
  reale (8 righe esatte, non la stima di ~7 qui sopra) e condensato
  tutti e 10 i `helpText()` che lo superavano — vedi la sezione "Fase
  44" più sopra per l'elenco completo e i numeri prima/dopo.
- **`CAPTURES`: sola visualizzazione/cancellazione, non ricorsiva, solo
  `.pcap`** (Fase 37, punto 7): scansiona il primo livello di
  `/handshakes` e `/netrunner` (non entra in eventuali sottocartelle) e
  filtra solo per estensione `.pcap` — i report JSON/CSV/HTML e i log
  `.csv`/`.txt` che finiscono nelle stesse cartelle restano visibili solo
  da `FILE MANAGER`. Nessuna funzione di rinomina/spostamento: non è mai
  sembrato un'operazione sensata per un artefatto di cattura.
- **`ACTIVITY`: sola lettura, non avvia/ferma nulla da lì** (Fase 37,
  punto 12): mostra lo stato di ogni manager tracciato ma non offre
  scorciatoie per fermarlo — per farlo bisogna comunque raggiungere la
  schermata proprietaria di quel task (stesso principio già valido per
  il tag `RF:`/`BG:` dell'header che questa schermata espande).
- **`IOT/OT SWEEP`: nessun MQTT/CoAP/Modbus su TLS** (Fase 38): MQTT su
  8883, CoAPS (DTLS) e Modbus/TCP-Security restano fuori scope per lo
  stesso motivo già documentato per NTLM-over-HTTPS e LDAPS — nessun
  client TLS in questo firmware. Un deployment che espone SOLO le
  varianti cifrate di questi protocolli non viene rilevato.
- **`PLAYBOOK`: preset fissi scritti in C++, non un vero linguaggio di
  scripting** (Fase 38): i tre preset (FULL RECON/QUICK IOT/OT/WIRELESS
  SURVEY) sono l'unica scelta possibile — non esiste un formato di
  script caricabile da SD, componibile dall'utente. Costruire un vero
  parser/interprete (con relativa gestione errori su script malformati)
  sarebbe stata una superficie nuova enorme per un firmware embedded,
  a fronte di un guadagno che i tre preset già coprono per i casi d'uso
  più comuni — se serve una sequenza diversa resta più semplice
  concatenare le schermate a mano.
- **`PLAYBOOK`: barra di progresso a granularità di step, non
  sotto-step** (Fase 38): quando uno step è a sua volta un intero
  orchestratore (es. `AUTO ASSESS` dentro FULL RECON), `PLAYBOOK` non
  guarda il progresso interno di quell'orchestratore — la barra avanza
  solo quando l'intero step finisce, quindi può restare ferma per
  diversi minuti su uno step lungo prima di scattare al successivo.
  Aprire la schermata dedicata di quello step (es. `AUTO ASSESS` stessa,
  che continua a girare in background) mostra il progresso fine.
- **DNP3: CRC-16 nativo del protocollo NON verificato contro un
  outstation reale** (Fase 39): a differenza di `net/LdapWire`/
  `net/NtlmWire` (verificati byte-a-byte contro librerie Python di
  riferimento) e persino del Modbus/BACnet appena aggiunti (protocolli
  più semplici, meno a rischio di un singolo bit sbagliato), il calcolo
  del CRC-16 specifico di DNP3 (polinomio 0xA6BC, IEEE 1815) è stato
  implementato seguendo l'algoritmo pubblicato ma non testato contro un
  vero outstation o simulatore in questo ambiente di sviluppo — nessun
  hardware/simulatore DNP3 disponibile qui. Se il CRC calcolato non è
  quello che un dispositivo si aspetta, il dispositivo scarta
  silenziosamente la richiesta (fallisce "chiuso": un finding mancato,
  mai un falso positivo, e nessun impatto sul dispositivo target) — ma
  il probe DNP3 andrebbe considerato non verificato fino a un test reale.
  BACnet e Modbus non condividono questo rischio: le loro risposte sono
  verificate strutturalmente byte per byte, non serve calcolare nulla
  lato client.
- **`IOT/OT SWEEP`: BACnet I-Am verificato solo strutturalmente, non un
  parser TAG-value completo** (Fase 39): il probe controlla che i byte
  BVLC/APDU della risposta siano quelli attesi per un I-Am (stesso
  principio "estrai/verifica solo quanto serve" già usato per CoAP), ma
  non decodifica Device Instance, Vendor ID o gli altri parametri BACnet
  tag-encoded — la schermata mostra solo "responds (I-Am)", non un
  vendor o un instance number come invece avviene per Modbus (che ha un
  formato di risposta più semplice da estrarre in sicurezza).
- **Header: breadcrumb multi-livello non troncato per catene lunghe**
  (Fase 41): `chrome::drawHeader` non limita esplicitamente la
  lunghezza del breadcrumb — con `title()` corti (3-6 caratteri, la
  convenzione seguita ovunque) e i percorsi tipici di questo firmware
  (2-4 livelli) resta comodamente nel budget dei 240px dell'header, ma
  una catena di navigazione insolitamente profonda potrebbe in teoria
  sovrapporsi al tag `RF:`/`BG:` a destra. Nessun troncamento/ellissi
  aggiunto deliberatamente — stessa filosofia "puramente additivo" già
  seguita per la versione a un solo livello che questa fase ha esteso.
- **Build reale confermata fino alla Fase 39; Fase 40/41 non ancora
  verificate**: a differenza di quanto scritto nelle fasi precedenti
  ("nessuna build mai eseguita in questo ambiente" — vero per il
  sandbox di sviluppo, che non ha mai avuto accesso al registry
  PlatformIO), una build reale dell'utente su Mac ha effettivamente
  compilato tutto il codice fino alla Fase 39 inclusa (vedi la nota in
  "Compilare e flashare" più sopra) — con solo warning, nessun errore,
  poi corretti in Fase 40. Il codice della Fase 40 (i fix stessi) e
  della Fase 41 (percorsi di navigazione, tocca ~40 file) non è stato
  ancora ricompilato da nessuno: **una nuova build resta il passo
  successivo prima di fidarsi di queste due fasi specifiche.** Tutto il
  codice non hardware-dipendente resta comunque verificato dove
  possibile con test standalone su host (aritmetica IP, formato DB OUI,
  encoder Base64, i messaggi BER/LDAP di `net/LdapWire` contro la
  libreria Python `ldap3`, i messaggi NTLM di `net/NtlmWire` contro
  `ntlm-auth`).
- **`NAME SPOOF`: nessuna cattura di credenziali, solo prova del
  poisoning** (Fase 46): a differenza di Responder, non serve un finto
  server SMB/HTTP dietro il nome avvelenato per ricevere e loggare
  l'handshake NTLMv2 del client — costruire un responder SMB2
  NEGOTIATE/SESSION_SETUP corretto (e un mini server HTTP per WPAD.dat)
  è un pezzo di lavoro grande, a sé stante, fuori scope per questa
  fase. Il valore qui è dimostrare — e loggare — che un host ha
  accettato la risposta forgiata, non craccare nulla a valle.
- **`NAME SPOOF`: risponde a TUTTE le query, non solo a una lista
  scelta** (Fase 46): a differenza di `DnsSpoofList` (usato da MITM
  AUDIT, con una lista di massimo 5 host scelti uno per uno), qui non
  c'è modo di limitare il poisoning a nomi specifici — stile Karma, non
  stile ARP spoof mirato. L'unico contenimento è il cap di durata
  (`kMaxDurationS` = 300s, più basso dei 600s di MITM AUDIT proprio per
  questo).
- **`NbnsWire`: nessun puntatore di compressione nel nome della
  risposta** (Fase 46): a differenza di `LlmnrWire` (che riusa il
  trucco standard DNS), la risposta NBT-NS copia per intero i 34 byte
  del nome codificato dalla query — la compressione non è affidabile
  tra le implementazioni NBT-NS reali, quindi non vale il rischio per
  risparmiare poche decine di byte.
- **`net/LlmnrWire` e `net/NbnsWire`: non verificati contro un
  riferimento reale** (Fase 46): a differenza di `net/LdapWire`/
  `net/NtlmWire` (verificati contro `ldap3`/`ntlm-auth` prima dell'uso),
  questi due non sono mai stati testati contro un client LLMNR/NBT-NS
  vero (es. Windows, Responder stesso) — solo scritti e riletti a mano
  seguendo RFC 4795/RFC 1002. Se il poisoning non sembra funzionare in
  test reale, è il primo posto da controllare.
- **`OS FINGERPRINT`: solo il bucket TTL è una vera etichetta, il resto
  è dato grezzo** (Fase 47): a differenza di un vero `p0f` con file di
  firme verificate, non tenta di distinguere Linux da macOS/Android/BSD
  (tutti a TTL~64) né una versione Windows specifica — servirebbe un
  database di firme che questo progetto non ha modo di costruire o
  verificare. Finestra TCP e ordine opzioni sono mostrati grezzi (`I`)
  per un umano che conosce p0f, non trasformati in un secondo guess.
- **`OS FINGERPRINT`: solo host che completano un handshake TCP visto
  da questo dispositivo** (Fase 47): come `PassiveHostDiscovery`, serve
  traffico che passi per l'aria in modalità promiscua su una rete
  aperta (WPA cifra il payload) — un host che non tenta mai una
  connessione TCP mentre l'ascolto è attivo non compare mai, e non c'è
  probe attivo di alcun tipo per stanarlo.
- **`net/Ieee80211Frame`'s parseDataFrame/parseSnap ora hanno un quinto
  chiamante** (Fase 47): `OsFingerprint` si aggiunge a
  `PassiveHostDiscovery`/`CdpLldpSniffer`/`RogueDhcpDetector`/
  `ArpSpoofManager` nel riusare questo parsing condiviso — nessuna
  modifica al parsing stesso, solo un nuovo consumatore, ma vale la
  pena notarlo perché un bug reale lì (vedi Fase 16) si propagherebbe
  ora a cinque moduli invece di quattro.
- **`VLAN HOP`: non è un vero attacco a una porta switch cablata**
  (Fase 48) — il limite più importante di questa fase, ripetuto qui
  perché è facile dimenticarlo: il VLAN hopping classico (DTP spoofing,
  o double-tagging attraverso un native VLAN permissivo) presuppone
  accesso fisico a una porta switch Ethernet. Questo dispositivo è una
  stazione WiFi senza PHY cablata — vede/inietta tag 802.1Q solo se
  l'AP stesso fa da bridge di traffico taggato sul segmento wireless
  (raro, ma non impossibile in setup enterprise mal segmentati). Un
  risultato "nessun leak visto" NON significa che la rete cablata a
  monte sia priva di problemi di segmentazione VLAN — significa solo
  che questo dispositivo, da qui, non ne ha visti.
- **`VLAN HOP`: il probe double-tag non può mai confermare il proprio
  successo** (Fase 48) — per costruzione: il double-tagging è un
  attacco cieco/monodirezionale anche nella sua forma classica (il
  traffico iniettato nel VLAN target non torna verso l'attaccante per
  il normale percorso di switching). "Inviato" è l'unico verdetto che
  questo dispositivo può dare onestamente; confermare che sia arrivato
  richiederebbe un secondo dispositivo in ascolto già collegato al VLAN
  target.
- **`VLAN HOP`: il contenuto ARP del probe è sintetico, non mirato**
  (Fase 48): `tha`/`tpa` restano azzerati ("who has 0.0.0.0") — il
  contenuto della richiesta ARP non conta per testare se uno switch
  toglie il tag esterno e inoltra quello interno, solo la struttura dei
  tag conta, quindi non c'è motivo di provare a indovinare una subnet
  del VLAN target che questo dispositivo non può conoscere.
- **KARMA mode non è un vero attacco Karma** (Fase 49): risponde con UN
  SSID alla volta, ciclato ogni 8s, non con una probe response
  individuale istantanea per ogni client come farebbe un vero
  responder Karma/MANA — l'API softAP standard di Arduino/esp-idf non
  espone il controllo per-client necessario per quello. Un dispositivo
  la cui PNL contiene la SSID candidata potrebbe non riassociarsi mai
  se la finestra di 8s passa prima che il suo OS ritenti la scansione.
- **KARMA candidate list: fotografia una tantum, non aggiornata dal
  vivo** (Fase 49): stesso principio di `PmkidSweepManager` verso
  `WardrivingManager` — `startKarma()` legge `BeaconProbeSniffer` UNA
  volta all'avvio; nuove SSID probate mentre KARMA è già in esecuzione
  non vengono aggiunte al ciclo (bisogna fermare e far ripartire).
- **`EvilTwinManager`: dedup associazioni per MAC, non per MAC+SSID**
  (Fase 49): un client che si riassocia a una SECONDA SSID candidata
  durante lo stesso ciclo KARMA non viene ri-loggato (stesso MAC già
  visto) — scelta deliberata per restare semplice, a costo di perdere
  la visibilità su un client che "abbocca" a più di una rete candidata
  nella stessa sessione.
- **`/mitm/harvest.csv`: nessun escaping CSV** (Fase 50): stessa
  convenzione informale già usata da `/eviltwin/associations.csv` — se
  la riga catturata contiene una virgola (raro per un valore di cookie
  tipico, ma non impossibile), sposta le colonne successive in un
  editor CSV standard. La vista in-app (`H`/`I`) non ha questo problema
  perché non fa parsing per colonne.
- **`findAsciiLine`: quattro pattern fissi, non un parser HTTP/FTP
  vero** (Fase 50) — stesso limite che c'era già prima di questa fase
  per `containsAscii`, solo più visibile ora che il valore viene
  davvero catturato: cerca `Cookie:`/`Authorization: Basic`/`USER
  `/`PASS ` come sottostringhe letterali ovunque nel payload TCP
  catturato, non parsando gli header HTTP o il protocollo FTP/Telnet
  per struttura. Un valore che contiene per coincidenza una di queste
  stringhe (raro) produrrebbe un falso positivo; un header con
  capitalizzazione diversa (es. `cookie:` minuscolo, valido per HTTP)
  non verrebbe riconosciuto — nessun controllo case-insensitive.
- **`EAP IDENTITY`: solo outer identity, mai nulla dentro il tunnel
  TLS** (Fase 51): il taglio di scope più importante di questa fase e
  la ragione per cui la feature è fattibile onestamente — la vera
  password/hash NTLM/certificato viaggia dentro PEAP/TTLS, cifrato,
  non catturabile senza compromettere il tunnel (che qui non si tenta
  mai). L'outer identity è comunque il finding: reti che lasciano il
  vero username nell'outer invece di usarne uno anonimo
  (`anonymous@corp`) leakano l'identità dell'account in chiaro anche
  con il tunnel perfettamente sano.
- **`EAP IDENTITY`: solo sul canale della STA, non hopper** (Fase 51):
  come `PassiveHostDiscovery`/`OsFingerprint`/`CdpLldpSniffer`, resta
  sul canale a cui questo dispositivo è associato. Una lista vuota su
  una rete enterprise nota di solito significa semplicemente che
  nessun client ha fatto una nuova associazione 802.1X sul canale
  giusto durante l'ascolto, non che il parser non funzioni. Per
  aumentare le probabilità: parcheggiare la STA di questo dispositivo
  sullo stesso canale dell'AP enterprise target prima di partire.
