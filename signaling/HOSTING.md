# Hosting serwera sygnalizacyjnego (raz, po stronie wydawcy)

Cel: **jeden publiczny adres `wss://…`**, który wklejasz do aplikacji. Gracze nic nie ustawiają.
Serwer jest malutki (przekazuje tylko SDP/ICE — audio leci P2P), więc darmowy/tani hosting wystarcza.

## Opcja A — darmowy PaaS (najprościej, od razu daje `wss://`)

Render.com (Railway/Fly.io analogicznie):
1. Wrzuć repo na GitHub.
2. Render → **New → Blueprint** → wskaż repo (użyje `signaling/render.yaml`).
   *(Albo ręcznie: New → Web Service → Root Directory `signaling`, Build `npm install --omit=dev`,
   Start `node server.js`.)*
3. Dostajesz `https://<nazwa>.onrender.com` → w aplikacji używasz **`wss://<nazwa>.onrender.com`**.
   Sprawdź: `https://<nazwa>.onrender.com/health` → `ok`, oraz `/stats` → JSON.

> Darmowy plan usypia po bezczynności (pierwsze połączenie ~30 s zimnego startu). Płatny = always-on.

## Opcja B — własny VPS (Docker)

```bash
scp -r signaling infra user@twoj-vps:~/
ssh user@twoj-vps
cd ~/infra && docker compose up -d        # signaling (+ coturn)
```
Dla `wss://` postaw reverse-proxy z TLS, np. Caddy (auto-HTTPS):
```
twojadomena.pl {
    reverse_proxy localhost:8080
}
```
→ używasz `wss://twojadomena.pl`. (Bez TLS możesz użyć `ws://twoj-vps:8080`, ale `wss` jest pewniejsze.)

## Wpięcie adresu w aplikację

W `app/src/main_voice.cpp`:
```cpp
const char* kDefaultSignaling = "wss://twoj-adres";
```
przebuduj raz (patrz `docs/RUN_VOICE.md`) i rozdaj `prox_voice.exe`. Gracze nie podają nic.
Bez przebudowy: ustaw graczom zmienną środowiskową `PROX_SIGNAL=wss://twoj-adres`.

`wss://` działa od ręki: obok exe leży `cacert.pem` (paczka CA Mozilli), której klient używa do
weryfikacji certyfikatu TLS — bezpiecznie, bez ustawień gracza (build kopiuje ją automatycznie, a
`tools/package.ps1` dokłada do paczki). Samo audio i tak jest szyfrowane end-to-end (DTLS-SRTP).

## TURN (dopiero gdy komuś nie działa przez trudny NAT)

PaaS nie uhostuje coturn (potrzebne porty UDP) — TURN postaw na VPS (`infra/coturn.conf`) i ustaw na
serwerze sygnalizacyjnym zmienną `TURN_URL=turn:user:pass@twoj-vps:3478?transport=udp`. Klient dostanie ją
automatycznie w `welcome`. STUN (domyślny) wystarcza dla większości sieci.
