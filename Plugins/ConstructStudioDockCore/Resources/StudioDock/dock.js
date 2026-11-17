'use strict';

// Boots the agent's harness and relays its channel to the game and back. It never writes to the console.
(() => {
  const PROTOCOL = 5;
  const BRIDGE_WAIT_MS = 15000;
  const iframe = document.createElement('iframe');
  let bridge = null;
  let boot = null;
  let channel = null;
  let bootAsked = false;

  // An uncaught error would reach the console with whatever it carried.
  window.addEventListener('error', (event) => event.preventDefault());
  window.addEventListener('unhandledrejection', (event) => event.preventDefault());

  function wait(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  async function findBridge() {
    const deadline = performance.now() + BRIDGE_WAIT_MS;
    while (performance.now() < deadline) {
      const candidate = window.ue && window.ue.constructstudiodock;
      if (candidate && typeof candidate.boot === 'function' && typeof candidate.send === 'function') {
        return candidate;
      }
      await wait(100);
    }
    return null;
  }

  const bridgeFound = findBridge().then((found) => {
    bridge = found;
    return found;
  });

  function toGame(text) {
    try {
      Promise.resolve(bridge.send(text)).catch(() => {});
    } catch {
      // The game unbound the bridge: the dock closed.
    }
  }

  function notice(event, payload) {
    toGame(JSON.stringify({ v: PROTOCOL, n: boot.nonce, from: 'page', t: 'dock', event, payload: payload || {} }));
  }

  // The game sends a screenshot's bytes as base64; the harness takes them as binary.
  function toBinary(shot) {
    if (!shot || typeof shot !== 'object' || typeof shot.bytesBase64 !== 'string') {
      return;
    }
    const text = atob(shot.bytesBase64);
    const bytes = new Uint8Array(text.length);
    for (let i = 0; i < text.length; i += 1) {
      bytes[i] = text.charCodeAt(i);
    }
    shot.bytes = bytes.buffer;
    delete shot.bytesBase64;
  }

  function fromGame(frame) {
    if (!channel || !frame || typeof frame !== 'object') {
      return;
    }
    try {
      const body = frame.t === 'res' ? frame.result : frame.t === 'event' ? frame.payload : null;
      if (body && typeof body === 'object') {
        toBinary(body);
        toBinary(body.screenshot);
      }
      channel.postMessage(frame);
    } catch {
      // A frame that cannot be converted or posted is dropped.
    }
  }

  function fromHarness(event) {
    let text;
    try {
      text = JSON.stringify(event.data);
    } catch {
      return;
    }
    if (typeof text === 'string') {
      toGame(text);
    }
  }

  async function sendBoot() {
    if (!await bridgeFound) {
      return;
    }
    let reply;
    try {
      reply = JSON.parse(await bridge.boot());
    } catch {
      return;
    }
    if (!reply || reply.error || !reply.files || typeof reply.channel !== 'string' || typeof reply.nonce !== 'string') {
      return;
    }
    boot = reply;
    channel = new BroadcastChannel(boot.channel);
    channel.onmessage = fromHarness;
    iframe.contentWindow.postMessage({
      type: 'crowdy-dsh:boot',
      files: boot.files,
      nonce: boot.nonce,
      persistScope: boot.persistScope,
      mount: boot.mount,
    }, location.origin);
    document.body.classList.add('booting');
    notice('joined');
  }

  window.addEventListener('message', (event) => {
    if (event.origin !== location.origin || event.source !== iframe.contentWindow) {
      return;
    }
    const message = event.data;
    if (!message || message.source !== 'crowdy-dsh' || typeof message.type !== 'string') {
      return;
    }
    if (message.type === 'crowdy-dsh:ready' && !bootAsked) {
      bootAsked = true;
      sendBoot();
      return;
    }
    if (!boot) {
      return;
    }
    if (message.type === 'crowdy-dsh:booted') {
      notice('booted');
      return;
    }
    if (message.type === 'crowdy-dsh:failed') {
      notice('failed', { message: String(message.message || '').slice(0, 200) });
    }
  });

  Object.defineProperty(window, 'constructStudioDock', { value: Object.freeze({ deliver: fromGame }) });
  iframe.src = '/dsh/index.html';
  document.body.append(iframe);
})();
