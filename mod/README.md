# Proximity Voice Export (mod DST) — Faza 0

Mod **client-only** do Don't Starve Together. Eksportuje pozycję lokalnego gracza do
`client_log.txt`, skąd czyta ją aplikacja-towarzysz realizująca proximity voice chat.

> ⚠️ **Ten mod sam nie obsługuje głosu.** Sandbox Lua w DST nie daje dostępu do mikrofonu
> ani do sieci, więc faktyczny głos robi osobny program natywny (aplikacja-towarzysz).
> Mod tylko wystawia dane pozycyjne.

## Co robi

Co `1/update_hz` sekundy wypisuje do logu jedną linię:

```
[PROX] v=1 t=123.45 uid="KU_xxxxxxxx" name="Wilson" x=12.34 z=-56.78 h=45.0 room="..."
```

- `v` — wersja protokołu linii (parser aplikacji może po niej walidować).
- `t` — czas gry (`GetTime()`), do porządkowania/interpolacji po stronie aplikacji.
- `uid` — `TheNet:GetUserID()` lokalnego gracza (tożsamość w sieci głosowej).
- `name` — wyświetlana nazwa gracza.
- `x`, `z` — pozycja na płaszczyźnie świata (DST jest top-down; `y` to wysokość, pomijamy).
- `h` — heading kamery w stopniach (`TheCamera:GetHeading()`), do panningu kierunkowego.
- `room` — pierwszy niepusty kandydat na identyfikator sesji → nazwa pokoju do auto-parowania.

Gdy w opcjach włączona jest **Diagnostyka ID sesji**, dodatkowo co 5 s leci linia:

```
[PROX-IDS] sid="..." sid2="..." srv="..." chosen_room="..."
```

gdzie `sid` = `TheNet:GetSessionIdentifier()`, `sid2` = `TheWorld.meta.session_identifier`,
`srv` = `TheNet:GetServerName()`. Służy do **wyboru właściwego identyfikatora pokoju** (patrz test niżej).

## Instalacja do testów lokalnych

Lokalne (nie-Workshopowe) mody DST ładują się z folderu `mods` w **katalogu instalacji gry**
(Steam), a NIE z `Documents`. `Documents\Klei\DoNotStarveTogether` trzyma tylko zapisy/logi.

1. Skopiuj `modinfo.lua`, `modmain.lua` i folder `scripts/` do nowego folderu moda w instalacji gry, np.:
   ```
   <Steam>\steamapps\common\Don't Starve Together\mods\proximity_voice_export\
   ```
2. Uruchom DST → **Mods → Client Mods** → włącz „Proximity Voice Export".
3. Wejdź do dowolnej gry/świata.

> Workshop (publiczna dystrybucja) dojdzie później — wtedy gracz tylko subskrybuje.

## Gdzie jest log

Windows:
```
%USERPROFILE%\Documents\Klei\DoNotStarveTogether\client_log.txt
```
Jeśli go tam nie ma, sprawdź podfolder z numerem (Steam ID):
```
%USERPROFILE%\Documents\Klei\DoNotStarveTogether\<liczby>\client_log.txt
```
Podgląd na żywo (PowerShell):
```powershell
Get-Content "$env:USERPROFILE\Documents\Klei\DoNotStarveTogether\client_log.txt" -Wait -Tail 20 |
  Select-String "\[PROX"
```

## ✅ Weryfikacja Fazy 0

**Test pojedynczy:** wejdź do gry, ruszaj się — w logu powinny lecieć linie `[PROX]` z rosnącym `t`
i zmieniającym się `x/z`, a po obrocie kamery (Q/E) zmieniające się `h`.

**Test 2-klientowy (kluczowy — wybór identyfikatora pokoju):**
1. Wejdź **dwoma kontami** na **ten sam świat** (np. host + drugi klient, albo dwa klienty na cudzym serwerze).
2. Na obu sprawdź linię `[PROX-IDS]`.
3. Znajdź pole, które jest **identyczne i niepuste na OBU klientach** — to jest właściwy identyfikator pokoju.
   - Najpewniej `sid`. Jeśli `sid` jest pusty/różny, sprawdź `sid2`, potem `srv`.
4. **Zgłoś, który zadziałał** — wtedy w aplikacji (Faza 1) zafiksujemy parowanie po tym polu,
   a diagnostykę można wyłączyć w opcjach moda.

## Opcje (w menu Mods)

- **Częstotliwość eksportu** — 5/10/15/20 Hz (domyślnie 10).
- **Diagnostyka ID sesji** — włącz na czas testu, potem wyłącz, by nie zaśmiecać logu.
