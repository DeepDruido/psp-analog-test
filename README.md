# PSP Analog Test

Homebrew per PSP che verifica il funzionamento dello **stick analogico** e dei **tasti**.
Utile per capire se un problema di controlli dipende dall'hardware (stick consumato,
sporco o scollegato) oppure dalle impostazioni di un gioco.

## Cosa mostra

- Valori grezzi di **LX/LY** (da 0 a 255, centro a 128) e scarto dal centro
- **Minimo e massimo** raggiunti su ogni asse, con verifica dell'escursione completa
- **Posizione di riposo** e valutazione del drift (lo stick non torna al centro)
- **Oscillazione** della lettura nell'ultimo secondo (rumore del potenziometro)
- **Traccia grafica** di tutte le posizioni raggiunte dallo stick
- **Stato dei tasti**: verde = premuto ora, bianco = già provato, grigio = mai premuto

L'interfaccia è in italiano.

## Requisiti

- Una PSP con custom firmware (ad esempio PRO o ARK) per poter avviare homebrew
- Testato su PSP-3004 con CFW PRO

## Installazione

1. Collega la PSP al computer in modalità USB.
2. Copia il contenuto della cartella [`release/`](release/) nella radice della Memory Stick,
   in modo da ottenere `PSP/GAME/AnalogTest/EBOOT.PBP`.
3. Espelli la PSP dal computer prima di scollegare il cavo.
4. Sulla PSP attiva il custom firmware (se non è permanente) e avvia
   **Gioco → Memory Stick → Analog Test**.

## Uso

| Tasto  | Azione                                                        |
|--------|---------------------------------------------------------------|
| SELECT | Azzera statistiche e traccia, registra la posizione di riposo |
| HOME   | Esci                                                          |

Procedura consigliata:

1. Non toccare lo stick e premi **SELECT**: centro e oscillazione devono risultare verdi.
2. Ruota lo stick lentamente lungo tutto il bordo: la traccia verde deve essere piena e
   i valori minimi e massimi devono avvicinarsi a 0 e 255.
3. Premi tutti i tasti uno alla volta: devono diventare tutti bianchi.

## Come interpretare i risultati

| Cosa vedi                                              | Probabile causa                                   |
|--------------------------------------------------------|---------------------------------------------------|
| Valori fissi a 128, "Nessun movimento rilevato"        | Stick non letto: cavo flat scollegato o modulo guasto |
| Centro giallo o rosso ("DRIFT")                        | Modulo consumato, non torna al centro             |
| Oscillazione rossa a riposo, puntini sparsi o buchi    | Contatti sporchi o usurati                        |
| Tutto verde                                            | Hardware a posto: controlla le impostazioni del gioco |

Le soglie sono indicative:

- **Centro**: scarto massimo di 16 dal valore 128 (verde), fino a 32 (giallo)
- **Escursione**: minimo ≤ 25 e massimo ≥ 230 su ogni asse
- **Oscillazione** in un secondo: ≤ 4 (verde), ≤ 12 (giallo)

Nei menu di sistema della PSP (XMB) lo stick analogico non viene usato: è normale
che lì non risponda.

## Compilazione

Serve il toolchain [PSPSDK](https://github.com/pspdev/pspsdk). Il modo più semplice è
usare l'immagine Docker ufficiale di pspdev:

```sh
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD":/src -w /src \
    ghcr.io/pspdev/pspdev:latest make
```

Con il toolchain installato in locale basta `make`. Il risultato è `EBOOT.PBP`.

## Licenza

Il codice di questo progetto è distribuito con licenza [MIT](LICENSE).

Il file `EBOOT.PBP` compilato include codice di librerie di terze parti
(PSPSDK e newlib), le cui licenze si trovano nella cartella
[`THIRD_PARTY_LICENSES/`](THIRD_PARTY_LICENSES/).

## Note legali

Questo progetto non è affiliato né approvato da Sony.
PSP è un marchio di Sony Interactive Entertainment.
