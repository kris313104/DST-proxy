'use strict';
// Integration test for the signaling server: two fake peers join a room and exchange a
// signaling message. Assumes the server is running (see how it's launched in CI/below).

const WebSocket = require('ws');

const PORT = process.env.PORT || 8080;
const URL = `ws://127.0.0.1:${PORT}`;

let failures = 0;
function check(cond, msg) {
  if (!cond) { console.error('FAIL: ' + msg); failures++; }
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function mkClient(uid, name) {
  const ws = new WebSocket(URL);
  ws._events = [];
  ws.on('message', (d) => ws._events.push(JSON.parse(d.toString())));
  return new Promise((resolve, reject) => {
    ws.on('open', () => {
      ws.send(JSON.stringify({ type: 'join', room: 'R', uid, name }));
      resolve(ws);
    });
    ws.on('error', reject);
  });
}

(async () => {
  const a = await mkClient('A', 'Alice');
  await sleep(150);
  const b = await mkClient('B', 'Bob');
  await sleep(200);

  const aw = a._events.find((e) => e.type === 'welcome');
  check(aw, 'A got welcome');
  check(aw && aw.peers.length === 0, 'A welcome has no peers (joined first)');
  check(a._events.some((e) => e.type === 'peer-joined' && e.uid === 'B'), 'A saw B join');

  const bw = b._events.find((e) => e.type === 'welcome');
  check(bw, 'B got welcome');
  check(bw && bw.peers.some((p) => p.uid === 'A' && p.name === 'Alice'), 'B welcome lists A (Alice)');

  a.send(JSON.stringify({ type: 'signal', to: 'B', payload: { hello: 'world' } }));
  await sleep(150);
  const sig = b._events.find((e) => e.type === 'signal' && e.from === 'A');
  check(sig, 'B received signal from A');
  check(sig && sig.payload && sig.payload.hello === 'world', 'signal payload intact');

  a.close();
  await sleep(200);
  check(b._events.some((e) => e.type === 'peer-left' && e.uid === 'A'), 'B saw A leave');

  b.close();
  await sleep(50);
  console.log(failures === 0 ? 'OK: signaling tests passed' : failures + ' failure(s)');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error('test crashed:', e); process.exit(1); });
