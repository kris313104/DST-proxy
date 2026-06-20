# Aplikacja-towarzysz — Faza 1 (ścieżka danych)

Konsolowy podgląd danych eksportowanych przez mod DST. Tailuje `client_log.txt`, parsuje
linie `[PROX]` / `[PROX-IDS]` i wypisuje na bieżąco pozycję lokalnego gracza oraz kandydatów
na identyfikator pokoju.

> Stan: **Faza 1** — sama ścieżka danych (bez głosu i bez sieci). Głos dochodzi w Fazie 2
> (libdatachannel + Opus + miniaudio). Plik danego klienta zawiera **tylko jego własną**
> pozycję, więc widok wielu graczy pojawi się dopiero po dołożeniu wymiany pozycji po sieci.

## Budowanie

Wymagane: CMake ≥ 3.16 i kompilator C++17 (przetestowane na MinGW g++ 13 + Ninja).

```powershell
cmake -S app -B app/build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build app/build
ctest --test-dir app/build --output-on-failure   # testy jednostkowe parsera
```

Powstają: `app/build/prox_companion.exe` (podgląd) i `app/build/prox_tests.exe` (testy).

## Uruchomienie

```powershell
# auto-namierzenie client_log.txt
app/build/prox_companion.exe

# lub jawnie wskazana ścieżka
app/build/prox_companion.exe "$env:USERPROFILE\Documents\Klei\DoNotStarveTogether\client_log.txt"
```

Przykładowy output:
```
[ids]   sid="..." sid2="" srv="..." chosen="..."
[local] room="..." uid="KU_..." name="Wilson" pos=(12.34, -56.78) heading=45 t=123.45
```

## Użycie do testu 2-klientowego (Faza 0)

Uruchom `prox_companion.exe` na **obu** maszynach z włączonym modem. Porównaj linie `[ids]`:
pole, które jest **identyczne i niepuste** na obu, to właściwy identyfikator pokoju
(`sid` / `sid2` / `srv`). Zgłoś wynik — zafiksujemy po nim auto-parowanie w Fazie 2.

## Struktura

- `src/prox_parser.{h,cpp}` — parser linii (czysty, testowany jednostkowo).
- `src/log_tailer.{h,cpp}` — namierzanie + tailowanie `client_log.txt` (obsługa rotacji/restartu).
- `src/main.cpp` — konsolowy podgląd.
- `tests/test_parser.cpp` — testy parsera.
