'use strict';
// Signaling server for DST proximity voice (Phase 2 MVP).
//
// Room-based WebRTC mesh relay. Clients join a room (the DST session id) and the server
// relays SDP/ICE between peers in that room. It carries NO audio — voice goes peer-to-peer
// over WebRTC. Stateless except for in-memory room membership; horizontal scaling (Redis
// pub/sub) is Phase 4.
//
// Protocol (JSON over WebSocket):
//   client -> server: { type:"join", room, uid, name? }
//                      { type:"signal", to:<uid>, payload:<any> }
//                      { type:"leave" }
//   server -> client: { type:"welcome", uid, peers:[{uid,name}] }
//                      { type:"peer-joined", uid, name }
//                      { type:"peer-left", uid }
//                      { type:"signal", from:<uid>, payload:<any> }
//                      { type:"error", message }

const http = require('http');
const { WebSocketServer } = require('ws');

function argval(key, def) {
  const i = process.argv.indexOf(key);
  return (i >= 0 && i + 1 < process.argv.length) ? process.argv[i + 1] : def;
}

const PORT = parseInt(argval('--port', process.env.PORT || '8080'), 10);
const MAX_ROOM = parseInt(argval('--max-room', process.env.MAX_ROOM || '16'), 10); // abuse guard

// ICE/TURN config handed to clients (Phase 4): STUN always, TURN if configured.
// TURN_URL example: turn:user:pass@turn.example.com:3478?transport=udp
const STUN_URL = process.env.STUN_URL || 'stun:stun.l.google.com:19302';
const TURN_URL = process.env.TURN_URL || '';
const JOIN_SECRET = process.env.JOIN_SECRET || '';       // if set, clients must send a matching token
const MAX_PER_IP = parseInt(process.env.MAX_PER_IP || '50', 10);

function iceServers() {
  const arr = [STUN_URL];
  if (TURN_URL) arr.push(TURN_URL);
  return arr;
}

// room -> Map<uid, ws>
const rooms = new Map();
// remote IP -> live connection count (abuse guard)
const ipCounts = new Map();

function send(ws, obj) {
  if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj));
}

function roomPeers(room, exceptUid) {
  const m = rooms.get(room);
  if (!m) return [];
  const out = [];
  for (const [uid, ws] of m) if (uid !== exceptUid) out.push({ uid, name: ws._name || '' });
  return out;
}

const server = http.createServer((req, res) => {
  if (req.url === '/health') { res.writeHead(200); res.end('ok'); return; }
  if (req.url === '/stats') {
    let conns = 0;
    const roomSizes = {};
    for (const [room, m] of rooms) { roomSizes[room] = m.size; conns += m.size; }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ rooms: rooms.size, connections: conns, roomSizes }));
    return;
  }
  res.writeHead(426); res.end('upgrade required');
});

const wss = new WebSocketServer({ server });

wss.on('connection', (ws, req) => {
  const ip = (req && req.socket && req.socket.remoteAddress) || 'unknown';
  ws._ip = ip;
  const ipn = (ipCounts.get(ip) || 0) + 1;
  ipCounts.set(ip, ipn);
  if (ipn > MAX_PER_IP) {
    ipCounts.set(ip, ipn - 1);
    send(ws, { type: 'error', message: 'too many connections from your address' });
    ws.close();
    return;
  }

  ws._room = null;
  ws._uid = null;
  ws._name = '';

  ws.on('message', (raw) => {
    let msg;
    try { msg = JSON.parse(raw.toString()); } catch { return send(ws, { type: 'error', message: 'bad json' }); }

    if (msg.type === 'join') {
      const room = String(msg.room || '');
      const uid = String(msg.uid || '');
      if (!room || !uid) return send(ws, { type: 'error', message: 'join needs room and uid' });
      if (JOIN_SECRET && String(msg.token || '') !== JOIN_SECRET)
        return send(ws, { type: 'error', message: 'invalid token' });

      let m = rooms.get(room);
      if (!m) { m = new Map(); rooms.set(room, m); }
      if (m.size >= MAX_ROOM && !m.has(uid)) return send(ws, { type: 'error', message: 'room full' });

      // Reconnect with same uid: drop the stale socket.
      const old = m.get(uid);
      if (old && old !== ws) { try { old.close(); } catch (e) {} }

      ws._room = room;
      ws._uid = uid;
      ws._name = String(msg.name || '');
      m.set(uid, ws);

      send(ws, { type: 'welcome', uid, peers: roomPeers(room, uid), iceServers: iceServers() });
      for (const [pUid, pWs] of m) {
        if (pUid !== uid) send(pWs, { type: 'peer-joined', uid, name: ws._name });
      }
      return;
    }

    if (msg.type === 'signal') {
      if (!ws._room || !ws._uid) return;
      const m = rooms.get(ws._room);
      if (!m) return;
      const target = m.get(String(msg.to || ''));
      if (target) send(target, { type: 'signal', from: ws._uid, payload: msg.payload });
      return;
    }

    if (msg.type === 'leave') { ws.close(); return; }
  });

  ws.on('close', () => {
    if (ws._ip) {
      const c = (ipCounts.get(ws._ip) || 1) - 1;
      if (c <= 0) ipCounts.delete(ws._ip); else ipCounts.set(ws._ip, c);
    }
    const room = ws._room;
    const uid = ws._uid;
    if (!room || !uid) return;
    const m = rooms.get(room);
    if (!m) return;
    if (m.get(uid) === ws) {
      m.delete(uid);
      for (const [, pWs] of m) send(pWs, { type: 'peer-left', uid });
      if (m.size === 0) rooms.delete(room);
    }
  });
});

function onFatalError(err) {
  if (err && err.code === 'EADDRINUSE') {
    console.error(`[signaling] port ${PORT} is already in use.`);
    console.error(`Pick a free one:  node server.js --port 9000`);
    console.error(`...then point clients to it:  prox_voice --signal ws://<host>:9000`);
    process.exit(1);
  }
  console.error('[signaling] server error:', err && err.message);
  process.exit(1);
}

// EADDRINUSE surfaces on the WebSocketServer (it wraps the http server), so handle both.
server.on('error', onFatalError);
wss.on('error', onFatalError);

server.listen(PORT, () => {
  console.log(`[signaling] listening on :${PORT}  (clients: ws://<host>:${PORT})`);
});
