'use strict';
// Gilded Ace opening sequence.
//   1. a brass rule draws across the room and a deck settles on it
//   2. two riffle shuffles
//   3. the deck fans out, the top card lifts and turns: the ace of spades
//   4. the ace dissolves into the club crest, the name appears
//   5. the curtains part onto the site
// Plays once per browser session (add ?intro to the URL to see it again).
// Click, any key or "Skip" ends it at once. Skipped for reduced-motion users.
(() => {
  const intro = document.getElementById('intro');
  if (!intro) return;
  const root = document.documentElement;

  let seen = false;
  try { seen = sessionStorage.getItem('ga-intro') === '1'; } catch (e) { /* storage blocked */ }
  const forced = /[?&]intro\b/.test(location.search);
  const reduce = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  if ((seen && !forced) || reduce || !Element.prototype.animate) { intro.remove(); return; }
  try { sessionStorage.setItem('ga-intro', '1'); } catch (e) { /* ignore */ }

  root.classList.add('intro-on');
  const q = (s) => intro.querySelector(s);
  const deck = q('#introDeck');
  const line = q('.intro-line');
  const crest = q('.intro-crest');
  const word = q('.intro-word');
  const tag = q('.intro-tag');
  const stage = q('.intro-stage');
  const curtainTop = q('.intro-curtain.top');
  const curtainBottom = q('.intro-curtain.bottom');

  const EASE = 'cubic-bezier(.2,.8,.2,1)';
  const N = 13;  // odd, so the top card sits dead centre in the fan
  const wait = (ms) => new Promise((r) => setTimeout(r, ms));
  const done = (anims) => Promise.all(anims.map((a) => a.finished)).catch(() => {});

  // Each card remembers where it is, so every move animates from its true position.
  const cards = [];
  for (let i = 0; i < N; i++) {
    const el = document.createElement('div');
    el.className = 'ic';
    el.innerHTML = '<div class="ic-inner"><div class="ic-back"></div></div>';
    el.style.zIndex = i;
    deck.append(el);
    cards.push({ el, inner: el.firstChild, x: 0, y: 40, r: 0, s: 1, z: i });
  }
  const tf = (c) => `translate(${c.x}px, ${c.y}px) rotate(${c.r}deg) scale(${c.s})`;
  const move = (c, to, opts) => {
    const from = tf(c);
    Object.assign(c, to);
    return c.el.animate([{ transform: from }, { transform: tf(c) }], { duration: 400, easing: EASE, fill: 'forwards', ...opts });
  };
  const byZ = () => [...cards].sort((a, b) => a.z - b.z);

  let ended = false;

  async function riffle(W) {
    const order = byZ();
    const half = Math.ceil(order.length / 2);
    const left = order.slice(0, half);
    const right = order.slice(half);
    const dx = W * 0.72;    await done([
      ...left.map((c, i) => move(c, { x: -dx, y: -i * 0.8, r: -7 }, { duration: 340, delay: i * 6 })),
      ...right.map((c, i) => move(c, { x: dx, y: -i * 0.8, r: 7 }, { duration: 340, delay: i * 6 })),
    ]);
       if (ended) return;
    // Cards drop alternately from the bottom of each half onto the new pile.
    const pile = [];
    for (let i = 0; i < Math.max(left.length, right.length); i++) {
      if (left[i]) pile.push(left[i]);
      if (right[i]) pile.push(right[i]);
    }
       const anims = pile.map((c, k) => {
      const from = tf(c);
      const mid = `translate(${c.x * 0.35}px, ${-k * 0.8 - 22}px) rotate(${c.r * 0.35}deg) scale(1.02)`;
      Object.assign(c, { x: 0, y: -k * 0.8, r: 0, s: 1, z: k });
      c.el.style.zIndex = k;
      return c.el.animate([{ transform: from }, { transform: mid, offset: 0.55 }, { transform: tf(c) }],
        { duration: 260, delay: k * 36, easing: 'cubic-bezier(.3,.7,.3,1)', fill: 'forwards' });
    });
       await done(anims);
     }

  async function run() {
    if (document.fonts && document.fonts.ready) await Promise.race([document.fonts.ready, wait(900)]);
    if (ended) return;
    const W = cards[0].el.offsetWidth || 100;
    // 1. Rule and deck.
    line.animate([{ transform: 'scaleX(0)', opacity: 0 }, { transform: 'scaleX(1)', opacity: 1 }], { duration: 800, easing: EASE, fill: 'forwards' });
    await done(cards.map((c, i) => {
      c.el.animate([{ opacity: 0 }, { opacity: 1 }], { duration: 400, delay: 150 + i * 22, fill: 'forwards' });
      return move(c, { y: -i * 0.8 }, { duration: 650, delay: 150 + i * 22 });
    }));
    if (ended) return;
    line.animate([{ opacity: 1 }, { opacity: 0.3 }], { duration: 700, fill: 'forwards' });

       // 2. Two riffles.
    await riffle(W);
    if (ended) return;
    await wait(80);
    await riffle(W);
    if (ended) return;
    await wait(120);

    // 3. Fan, with the top card in the centre slot.
    const order = byZ();
    const top = order[order.length - 1];
    const rest = order.slice(0, -1);
    const mid = (N - 1) / 2;
    const slots = [...rest.slice(0, mid), top, ...rest.slice(mid)];
    const spread = window.innerWidth < 600 ? 80 : 104;
    slots.forEach((c, slot) => {
      c.el.style.transformOrigin = '50% 118%';
      c.el.style.zIndex = c === top ? 100 : slot;
      c.slot = slot;
    });
    await done(slots.map((c, slot) => move(c, { x: 0, y: 0, r: -spread / 2 + slot * (spread / (N - 1)) }, { duration: 700, delay: Math.abs(slot - mid) * 22 })));
    if (ended) return;
    await wait(250);

    // The top card becomes the ace.
    top.inner.insertAdjacentHTML('beforeend',
      '<div class="ic-face"><span class="corner tl">A<small>&spades;</small></span>' +
      '<svg viewBox="0 0 64 64" aria-hidden="true"><use href="#spade" width="64" height="64" fill="#0e2119"/></svg>' +
      '<span class="corner br">A<small>&spades;</small></span></div>');
    rest.forEach((c) => c.el.animate([{ opacity: 1 }, { opacity: 0.45 }], { duration: 500, fill: 'forwards' }));
    top.el.style.transformOrigin = '50% 50%';
    await done([move(top, { y: -W * 0.22, s: 1.3 }, { duration: 520 })]);
    if (ended) return;
    const flip = top.inner.animate([{ transform: 'rotateY(0deg)' }, { transform: 'rotateY(180deg)' }],
      { duration: 760, easing: 'cubic-bezier(.55,0,.2,1)', fill: 'forwards' });
    await wait(300);
    // The rest of the deck sweeps off either side.
    rest.forEach((c) => {
      const side = c.slot < mid ? -1 : 1;
      move(c, { x: side * window.innerWidth * 0.62, y: W * 0.3, r: c.r + side * 30 }, { duration: 700, delay: Math.abs(c.slot - mid) * 25, easing: 'cubic-bezier(.6,0,.4,1)' });
      c.el.animate([{ opacity: 0.45 }, { opacity: 0 }], { duration: 700, delay: Math.abs(c.slot - mid) * 25, fill: 'forwards' });
    });
    await done([flip]);
    if (ended) return;
    await wait(650);

    // 4. Ace into crest, then the name.
    top.el.animate([{ opacity: 1, filter: 'blur(0)' }, { opacity: 0, filter: 'blur(6px)' }], { duration: 600, easing: 'ease-in', fill: 'forwards' });
    move(top, { s: 1.75, y: 0 }, { duration: 650, easing: 'ease-in' });
    crest.animate([{ opacity: 0, transform: 'scale(.72) rotate(-40deg)' }, { opacity: 1, transform: 'scale(1) rotate(0deg)' }],
      { duration: 1000, delay: 250, easing: EASE, fill: 'forwards' });
    line.animate([{ opacity: 0.3 }, { opacity: 0 }], { duration: 500, fill: 'forwards' });
    word.animate([{ opacity: 0, letterSpacing: '.6em', transform: 'translateY(8px)' }, { opacity: 1, letterSpacing: '.14em', transform: 'none' }],
      { duration: 1200, delay: 700, easing: EASE, fill: 'forwards' });
    await done([tag.animate([{ opacity: 0 }, { opacity: 1 }], { duration: 800, delay: 1300, fill: 'forwards' })]);
    if (ended) return;
    await wait(900);
    finish(false);
  }

  function finish(fast) {
    if (ended) return;
    ended = true;
    root.classList.remove('intro-on');
    intro.style.pointerEvents = 'none';
    const curtain = { duration: fast ? 600 : 1000, easing: 'cubic-bezier(.75,0,.2,1)', fill: 'forwards', delay: fast ? 0 : 250 };
    stage.animate([{ opacity: 1 }, { opacity: 0 }], { duration: fast ? 250 : 450, fill: 'forwards' });
    q('.intro-skip').animate([{ opacity: 1 }, { opacity: 0 }], { duration: 200, fill: 'forwards' });
    curtainTop.animate([{ transform: 'translateY(0)' }, { transform: 'translateY(-101%)' }], curtain);
    curtainBottom.animate([{ transform: 'translateY(0)' }, { transform: 'translateY(101%)' }], curtain)
      .finished.then(() => intro.remove(), () => intro.remove());
    document.removeEventListener('keydown', onKey);
  }
  function onKey(e) { if (e.key !== 'Tab') finish(true); }

  intro.addEventListener('click', () => finish(true));
  document.addEventListener('keydown', onKey);
  setTimeout(() => finish(true), 15000);  // safety net
  run().catch(() => finish(true));
})();
