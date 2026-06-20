# Uruchamianie proximity voice

## Dla gracza (docelowo — zero konfiguracji)

1. Pobierz i uruchom **`prox_voice.exe`**. Sam:
   - wgra mod do Twojej instalacji DST (Steam),
   - połączy się z wbudowanym serwerem,
   - poczeka aż wejdziesz do świata i automatycznie dołączy do głosu znajomych z tego świata.
2. **Raz** włącz mod: DST → Mods → Client Mods → „Proximity Voice Export" → Enable.
3. Wejdź do świata kolegi i gadajcie. **Używaj słuchawek.**

Wymaga dwóch rzeczy po stronie *wydawcy* (Ciebie), niewidocznych dla gracza: **(a)** publiczny serwer
sygnalizacyjny, którego adres wpisujesz w `kDefaultSignaling` (`app/src/main_voice.cpp`) lub przez env
`PROX_SIGNAL`; **(b)** opcjonalnie TURN dla trudnych NAT-ów. Audio leci P2P (WebRTC) — przez serwer idzie
tylko sygnalizacja.

---

## Szczegóły techniczne / dev

Elementy: **mod DST** (eksport pozycji do logu; **apka instaluje go sama**), **serwer sygnalizacyjny**
(Node), **aplikacja `prox_voice`** (głos). Audio P2P (WebRTC mesh); serwer tylko kojarzy graczy i przekazuje SDP/ICE.

## 1. Budowa aplikacji (MSVC + vcpkg)

```powershell
$repo = "C:\Users\Samsung Smart Fridge\Documents\repos\DST-proximity"
cmake -S "$repo\app" -B "$repo\app\build-msvc" -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DVCPKG_TARGET_TRIPLET=x64-windows -DPROX_WITH_VOICE=ON
cmake --build "$repo\app\build-msvc" --config Release
```
Wynik: `app\build-msvc\Release\prox_voice.exe` (DLL-e kopiowane obok automatycznie).

## 2. Serwer sygnalizacyjny

```powershell
cd "$repo\signaling"; npm install   # raz
node server.js                       # nasłuchuje na :8080
```

Port zajęty? Ustaw inny (serwer wypisze czytelny komunikat zamiast crasha):
```powershell
node server.js --port 9000           # lub:  $env:PORT=9000; node server.js
```
i wskaż go klientom: `prox_voice --signal ws://<host>:9000`.

## 3a. Szybki test bez gry (jedna maszyna, dwie instancje)

W dwóch osobnych terminalach (serwer musi działać):
```powershell
& "$repo\app\build-msvc\Release\prox_voice.exe" --sim --room T --uid AAA --x 0 --z 0
& "$repo\app\build-msvc\Release\prox_voice.exe" --sim --room T --uid BBB --x 8 --z 0
```
Statystyki powinny pokazać `peers=1/1` i rosnące `audioRecv`/`posRecv`. Zmień `--x` na > 30, żeby
sprawdzić wyciszenie (daleko = `gain 0`). (Uwaga: obie instancje używają tego samego mikrofonu/głośnika
na jednej maszynie — możliwe sprzężenie; to tylko test danych.)

## 3b. Prawdziwy test na 2 maszynach (z DST)

1. Na obu maszynach: zainstalowany mod, DST uruchomiony, **ten sam świat**.
2. Serwer sygnalizacyjny dostępny dla obu — uruchom go na jednej maszynie i użyj jej IP w sieci LAN
   (np. `--signal ws://192.168.0.10:8080`). Dla internetu serwer trzeba wystawić (Faza 4: hosting + TURN).
3. Na obu:
   ```powershell
   & "...\prox_voice.exe" --signal ws://192.168.0.10:8080 --name TwojaNazwa
   ```
   Tryb log sam znajdzie `client_log.txt`, odczyta `room` (= sid świata) i `uid`, i automatycznie
   dołączy do tego samego pokoju. Gdy się zbliżycie w grze — głośniej; oddalicie — ciszej, a poza
   zasięgiem cisza.

## Parametry `prox_voice`

Połączenie / tryb:
- `--signal <ws://host:port>` — serwer sygnalizacyjny (domyślnie `ws://127.0.0.1:8080`).
- `--name <nazwa>` — nazwa wyświetlana.  `--token <sekret>` — gdy serwer ma `JOIN_SECRET`.
- `--seconds <N>` — auto-wyjście po N s (0 = do Enter).
- `--log <ścieżka>` — jawna ścieżka do `client_log.txt`.
- `--sim --room R --uid U --x X --z Z` — symulowany gracz (test bez gry).

Proximity / audio (Faza 3):
- `--near <j>` / `--far <j>` — promień pełnej głośności / zasięg słyszalności (domyślnie 8 / 30).
- `--exp <e>` — kształt krzywej zaniku (1 = liniowo).
- `--pan <0..1>` — siła panningu kierunkowego (0 = wyłącz, wartość ujemna = odwróć — do kalibracji w grze).
- `--gate <rms>` — bramka szumu (0 = wyłączona; np. 0.01).
- `--ptt <KEY>` — push-to-talk (trzymaj klawisz): litera, `SPACE`, `CTRL` lub `SHIFT`.
- `--no-denoise` / `--no-agc` — wyłącz odszumianie / AGC (speexdsp).
- `--no-skip-far` — wysyłaj audio także do graczy poza zasięgiem (domyślnie pomijani).

> **Echo:** używaj słuchawek. Wbudowane jest odszumianie + AGC, ale pełnego echo-cancellation (AEC)
> między głośnikami a mikrofonem MVP nie robi — słuchawki to eliminują.

## Stan / dalsze kroki

- Działa: sygnalizacja, mesh WebRTC, audio Opus, wymiana pozycji, głośność wg odległości.
- Faza 3 (polish): kalibracja panningu kierunkowego w grze, AEC/odszumianie, push-to-talk, konfiguracja,
  instalator + podpis, opcja „nie wysyłaj audio do dalekich".
- Faza 4 (skala): hosting sygnalizacji (Redis), klaster TURN, anti-abuse.
