# Wdrożenie i skalowanie (Faza 4)

Audio leci **P2P (WebRTC mesh)** i NIE przechodzi przez serwer. Przez infrastrukturę idzie tylko:
**sygnalizacja** (lekka, zestawianie połączeń) i **TURN** (przekaz audio tylko gdy P2P nie przebije NAT —
tu jest realny koszt pasma).

## Serwer sygnalizacyjny — zmienne środowiskowe

| Zmienna | Domyślnie | Opis |
|---|---|---|
| `PORT` / `--port` | 8080 | port nasłuchu |
| `STUN_URL` | `stun:stun.l.google.com:19302` | STUN podawany klientom |
| `TURN_URL` | (brak) | pełny URL TURN, np. `turn:user:pass@host:3478?transport=udp` — wysyłany klientom w `welcome` |
| `JOIN_SECRET` | (brak) | jeśli ustawione, klient musi podać `--token <secret>` |
| `MAX_PER_IP` | 50 | limit równoległych połączeń z jednego IP (anti-abuse) |
| `MAX_ROOM` | 16 | limit graczy w pokoju |

Endpointy: `GET /health` (ok), `GET /stats` (JSON: liczba pokoi/połączeń/rozmiary pokoi → metryki).

Klient bierze listę ICE (STUN+TURN) z `welcome`, więc **TURN konfiguruje się centralnie na serwerze** —
gracze nic nie ustawiają.

## TURN (coturn)

Patrz `coturn.conf`. Najprościej: statyczne dane (Opcja A) + `TURN_URL=turn:user:pass@host:3478`.
Otwórz w firewallu 3478 (UDP/TCP) i zakres `49152-65535`. W produkcji rozważ `turns://` (TLS, port 5349).
Zalecane też **`wss://` (TLS)** dla sygnalizacji — postaw za reverse-proxy (nginx/Caddy) z certyfikatem.

## Skala: do tysięcy+ połączeń

- **Jeden węzeł** (Node + `ws`) realnie obsługuje kilka tysięcy równoległych połączeń sygnalizacyjnych
  (ruch jest mały — tylko setup). Przy obecnej architekturze (audio P2P) to często wystarcza.
- **Wiele węzłów** (gdy jeden to za mało): uruchom N **bezstanowych** replik `signaling` za load balancerem.
  Ponieważ pokoje są trzymane w pamięci węzła, potrzebny jest jeden z dwóch wzorców:
  1. **Sticky-by-room** na LB (konsekwentne hashowanie `room` → ten sam węzeł) — najprościej, bez Redis.
  2. **Redis pub/sub adapter** — współdzielone członkostwo pokoi (SET per room) + rozsyłanie
     `signal`/`peer-joined`/`peer-left` między węzłami. *To jeszcze nie jest zaimplementowane* — obecny
     serwer jest jednowęzłowy/sticky-ready; adapter Redis to następny krok dla pełnego horyzontalnego skalowania.
- **TURN** skaluje się niezależnie (klaster coturn, geo-rozproszony); to główny koszt pasma — monitoruj.
- **Anti-abuse**: `MAX_PER_IP`, `MAX_ROOM`, `JOIN_SECRET` już są; w produkcji dodaj rate-limiting na LB
  i czasowe credentiale TURN (REST/HMAC — wymaga obiektowej formy ICE po stronie klienta).

## Przykład

`docker-compose.yml` uruchamia sygnalizację + coturn (jeden węzeł). To dobry start; do skali poziomej
dołóż repliki za LB wg powyższego.
