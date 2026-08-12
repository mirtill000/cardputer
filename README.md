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
  LittleFS — riusa `sdcard::exportFs()` della Fase 10). Per un AP aperto
  E in allowlist E non ancora scoperto in questa sessione: si
  disconnette da dove si trovava, si collega alla rete aperta (mai
  salvata tra le reti WiFi — non deve mai spodestare le reti vere
  dell'utente dai 3 slot MRU della Fase 10), lancia
  `ScanManager::startDiscoveryScan()` e, per i primi 5 host vivi
  trovati, `PortScanManager::startScan()` — **sono gli stessi identici
  moduli usati da `NETWORK SCAN`/`PORT SCANNER`**, nessuna logica di
  scansione duplicata — poi esporta i risultati con `ResultStore` sotto
  `/wardrive/scans/<ssid>_<bssid>.json|csv` (namespace separato dalla
  cronologia scan dell'utente, per non mischiare le due cose) e infine
  richiama `WifiManager::autoConnect()` per tornare alla propria rete.
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
con **`D` da NETWORK SCAN**. Il menu principale scende da 16 a **9 voci**
(WIFI SCAN, NETWORK SCAN, AUTO ASSESS, THREATS, PORT SCANNER, CREDENTIAL
AUDIT, SCAN HISTORY, WAR DRIVING, SETTINGS). I manager di background sono
sempre avviati in `setup()` come prima — è cambiata solo la navigazione,
non la logica. Ogni strumento gestisce da sé i propri prerequisiti (es.
SNMP/DATASTORE ricordano di lanciare prima un NETWORK SCAN).

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
   ambra = aperta). Verificare che compaia `/wardrive/wardrive.csv` su
   SD/LittleFS con una riga per AP.
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
   `/wardrive/scans/<ssid>_<bssid>.json` e `.csv` compaiano su SD, e che
   al termine il device sia di nuovo connesso alla rete di sempre (non a
   quella di test).
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
- **NTP senza RTC a batteria** (Fase 11): l'orario si perde a ogni spegnimento
  e va risincronizzato al boot successivo (richiede WiFi) — un limite
  hardware di questa scheda, non del codice. Finché non sincronizzato,
  ogni timestamp nel firmware resta l'uptime.
- **Nessun editor manuale di subnet**: `NETWORK SCAN` usa sempre la
  subnet DHCP-rilevata (non ha senso poterla cambiare finché non c'è
  un modo di specificare un range arbitrario in modo sicuro). Il range
  porte invece **è** ora editabile da `SETTINGS` (Fase 6).
- **mDNS ora implementato, ma solo come fallback a NBNS** (Fase 10, #5):
  interrogato solo quando NBNS non ha già trovato un nome, e solo con
  una singola query PTR reverse a bassa priorità — non è un browser di
  servizi mDNS completo, e dispositivi che ignorano query reverse PTR
  (alcuni, non tutti, lo fanno) restano senza hostname.
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
- **Evil-twin: euristica su SSID+cifratura, non una prova crittografica**
  (Fase 13): due AP con lo stesso nome ma cifratura diversa vengono
  entrambi segnalati come sospetti, senza alcuna pretesa di stabilire
  quale sia quello legittimo — un attaccante sufficientemente motivato
  potrebbe clonare anche la cifratura (nel qual caso questa euristica
  non lo rileva) o un ambiente con AP legittimi mal configurati
  potrebbe generare falsi positivi.
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
- **Radio promiscua condivisa: il limite ora vale per SEI funzioni**
  (Fase 18-19): `esp_wifi` accetta una sola callback promiscua alla volta
  — ARP/MITM, deauth, PMKID, CDP/LLDP, rogue DHCP e (Fase 19) scoperta
  host passiva non vanno usate in parallelo (l'ultima avviata ruba i frame
  alle altre). Stesso genere di limite già accettato per WAR DRIVING vs
  NETWORK SCAN. La barra di stato radio in header (Fase 19) esiste proprio
  per rendere questo conflitto visibile invece che silenzioso.
- **Responder-lite/NetNTLM (punto 10) escluso di proposito** (Fase 19):
  era in una delle liste di proposte ed è stato escluso su istruzione
  esplicita dell'utente perché ad alto rischio (impersona servizi verso
  client di terzi per raccogliere hash). Implementabile in futuro solo su
  richiesta esplicita e dietro il gate `OffensiveDisclaimerScreen`.
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
- **Enumerazione servizi mDNS: tipo + istanza (+ porta), non l'IP**
  (Fase 19): `ServiceEnumerator` non insegue i target SRV fino ai record A
  per risolvere l'IP di backing — il nome istanza più la tabella host già
  scoperta danno il contesto, e ogni round-trip in più è tempo su radio.
- **Sweep SNMP: solo GET di sysDescr con community "public"** (Fase 19):
  read-only, mai SET, un solo scalare, una sola community di default — è
  un banner grab SNMP, non un walk completo della MIB né un test di
  community multiple.
- **Nessuna build reale eseguita**: vale per ogni fase di questo
  progetto — il sandbox di sviluppo non ha accesso al registry
  PlatformIO. Tutto il codice è stato scritto con attenzione e, dove
  possibile, la logica non hardware-dipendente è stata verificata con
  test standalone su host (aritmetica IP, formato DB OUI, encoder
  Base64) — ma **una build (`pio run`) e un test su hardware reale
  restano il passo successivo prima di fidarsi di questo firmware.**
