'use strict';
// GADeck - a small choreography engine for Gilded Ace card flourishes.
//
// Every routine starts from a neat pile in the middle of its container and
// ends with one (except fanReveal, the finale). Each card remembers its own
// position, so every move animates from exactly where the card is.
//
//   deal()       cards settle into a pile
//   riffle()     split wide, cards arc in alternately and land with a ring
//   spring()     cards arc one by one from one hand to the other
//   packets()    burst into four packets, orbit, cross-cut, slam back down
//   ribbon()     rainbow arch, bobbing domino flip, accordion close
//   carousel()   a turning 3D ring of cards
//   fanReveal()  wide fan, top card lifts and turns into the ace of spades
//
// deck.speed scales every duration, delay and pause (lower is faster).
(() => {
  const EASE = 'cubic-bezier(.2,.8,.2,1)';
  const SPADE = 'M32 6c-7.5 9.8-18 15.2-18 24.8 0 5.9 4.6 10 9.8 10 3.3 0 5.7-1.3 7-3.4-.6 4.9-2.9 9-6.4 11.9h15.2c-3.5-2.9-5.8-7-6.4-11.9 1.3 2.1 3.7 3.4 7 3.4 5.2 0 9.8-4.1 9.8-10C50 21.2 39.5 15.8 32 6z';
  const RANKS = ['A', 'K', 'Q', 'J', '10', '9', '8', '7', '6', '5', '4', '3', '2'];
  const SUITS = ['♠', '♥', '♣', '♦'];  // spade heart club diamond
  const RAD = Math.PI / 180;
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const done = (anims) => Promise.all(anims.filter(Boolean).map((a) => a.finished)).catch(() => {});

  const faceHTML = (rank, suit) => {
    const red = suit === '♥' || suit === '♦';
    return `<div class="ic-face${red ? ' red' : ''}"><span class="corner tl">${rank}<small>${suit}</small></span>` +
      `<span class="pip">${suit}</span><span class="corner br">${rank}<small>${suit}</small></span></div>`;
  };
  const ACE_FACE = '<div class="ic-face ace"><span class="corner tl">A<small>♠</small></span>' +
    `<svg viewBox="0 0 64 64" aria-hidden="true"><path d="${SPADE}" fill="#0e2119"/></svg>` +
    '<span class="corner br">A<small>♠</small></span></div>';

  class GADeck {
    constructor(container, n = 13, speed = GADeck.SPEED) {
      this.box = container;
      this.n = n;
      this.speed = speed;
      this.stopped = false;
      this.cards = [];
      container.textContent = '';
      for (let i = 0; i < n; i++) {
        const el = document.createElement('div');
        el.className = 'ic';
        el.innerHTML = '<div class="ic-inner"><div class="ic-back"></div>' + faceHTML(RANKS[i % 13], SUITS[i % 4]) + '</div>';
        container.append(el);
        const c = { el, inner: el.firstChild, x: 0, y: 40, z: 0, ry: 0, r: 0, tz: 0, s: 1, d: i, up: false };
        el.style.transform = this.tf(c);
        el.style.zIndex = i;
        this.cards.push(c);
      }
      this.W = this.cards[0].el.offsetWidth || 100;
      this.H = this.W * 1.4;
    }

    // ---- timing ----
    t(ms) { return ms * this.speed; }
    wait(ms) { return sleep(this.t(ms)); }
    timing(opts, base) {
      const o = { ...base, ...opts };
      o.duration = this.t(o.duration);
      if (o.delay) o.delay = this.t(o.delay);
      return o;
    }

    // ---- primitives ----
    tf(c) {
      return `translate3d(${c.x}px, ${c.y}px, ${c.z + c.d * 0.6}px) rotateY(${c.ry}deg) rotateZ(${c.r}deg) translateZ(${c.tz}px) scale(${c.s})`;
    }
    move(c, to, opts) {
      const from = this.tf(c);
      Object.assign(c, to);
      c.el.style.zIndex = c.d;
      return c.el.animate([{ transform: from }, { transform: this.tf(c) }], this.timing(opts, { duration: 400, easing: EASE, fill: 'forwards' }));
    }
    // Several waypoints; each state may carry an `offset` (0..1) for its keyframe.
    path(c, states, opts) {
      const frames = [{ transform: this.tf(c) }];
      for (const st of states) {
        const { offset, ...rest } = st;
        Object.assign(c, rest);
        const f = { transform: this.tf(c) };
        if (offset !== undefined) f.offset = offset;
        frames.push(f);
      }
      c.el.style.zIndex = c.d;
      return c.el.animate(frames, this.timing(opts, { duration: 400, easing: EASE, fill: 'forwards' }));
    }
    // Turn a card over. It lifts toward the viewer mid-turn so it never cuts through its
    // neighbours. opts.spins adds extra full turns.
    flip(c, up, opts = {}) {
      const { spins = 0, lift = this.W * 0.62, ...rest } = opts;
      const a0 = c.up ? 180 : 0;
      const a1 = (up ? 180 : 0) + 360 * spins;
      c.up = up;
      return c.inner.animate([
        { transform: `translateZ(0px) rotateY(${a0}deg)` },
        { transform: `translateZ(${lift}px) rotateY(${(a0 + a1) / 2}deg)`, offset: 0.5 },
        { transform: `translateZ(0px) rotateY(${a1}deg)` },
      ], this.timing(rest, { duration: 600, easing: 'cubic-bezier(.45,0,.25,1)', fill: 'forwards' }));
    }
    order() { return [...this.cards].sort((a, b) => a.d - b.d); }
    // Rotations past a half turn look identical folded back; keeps later moves from unwinding.
    normalize(c) { c.r = ((c.r % 360) + 540) % 360 - 180; }

    // ---- effects ----
    fx(cls, x, y, size, frames, opts) {
      const el = document.createElement('div');
      el.className = cls;
      Object.assign(el.style, { left: `${x - size / 2}px`, top: `${y - size / 2}px`, width: `${size}px`, height: `${size}px` });
      this.box.append(el);
      const a = el.animate(frames, this.timing(opts, {}));
      a.finished.then(() => el.remove(), () => el.remove());
      return a;
    }
    ring(x = 0, y = 0, scale = 3.4) {
      return this.fx('ga-ring', x, y, this.W * 1.2,
        [{ transform: 'scale(.25)', opacity: 0.95 }, { transform: `scale(${scale})`, opacity: 0 }],
        { duration: 900, easing: 'cubic-bezier(.1,.7,.3,1)' });
    }
    burst(x = 0, y = 0, strength = 1) {
      return this.fx('ga-burst', x, y, this.W * 4,
        [{ transform: 'scale(.4)', opacity: 0 }, { transform: 'scale(1)', opacity: strength, offset: 0.4 }, { transform: 'scale(1.3)', opacity: 0 }],
        { duration: 1800, easing: 'ease-out' });
    }
    thump(power = 6) {
      return this.box.animate([
        { transform: 'translateY(0) scale(1)' },
        { transform: `translateY(${power}px) scale(.985)`, offset: 0.25 },
        { transform: `translateY(${-power * 0.3}px) scale(1.004)`, offset: 0.6 },
        { transform: 'translateY(0) scale(1)' },
      ], this.timing({ duration: 340, easing: 'ease-out' }, {}));
    }
    fadeIn() {
      this.cards.forEach((c, i) => c.el.animate([{ opacity: 0 }, { opacity: 1 }], this.timing({ duration: 400, delay: i * 22, fill: 'forwards' }, {})));
    }

    // ---- routines ----
    async deal() {
      this.fadeIn();
      await done(this.cards.map((c, i) => this.move(c, { y: -i * 0.8 }, { duration: 650, delay: 150 + i * 22 })));
    }

    async riffle() {
      const o = this.order();
      const half = Math.ceil(o.length / 2);
      const left = o.slice(0, half);
      const right = o.slice(half);
      const dx = this.W * 0.9;
      await done([
        ...left.map((c, i) => this.move(c, { x: -dx, y: -i * 0.8, r: -11 }, { duration: 340, delay: i * 6 })),
        ...right.map((c, i) => this.move(c, { x: dx, y: -i * 0.8, r: 11 }, { duration: 340, delay: i * 6 })),
      ]);
      if (this.stopped) return;
      const pile = [];
      for (let i = 0; i < Math.max(left.length, right.length); i++) {
        if (left[i]) pile.push(left[i]);
        if (right[i]) pile.push(right[i]);
      }
      await done(pile.map((c, k) => this.path(c, [
        { x: c.x * 0.3, y: -k * 0.8 - this.H * 0.32, r: c.r * 0.3, z: 40, s: 1.05, offset: 0.5 },
        { x: 0, y: -k * 0.8, r: 0, z: 0, s: 1, d: k },
      ], { duration: 300, delay: k * 38, easing: 'cubic-bezier(.3,.7,.3,1)' })));
      this.thump(4);
      this.ring(0, 0, 2.6);
    }

    async spring() {
      const dx = this.W * 0.95;
      await done(this.order().map((c, k) => this.move(c, { x: -dx, y: -k * 0.8, r: -9 }, { duration: 380 })));
      if (this.stopped) return;
      const fromTop = this.order().reverse();
      await done(fromTop.map((c, j) => this.path(c, [
        { x: 0, y: -this.H * 0.8 - j * 0.8, r: 0, z: 70, s: 1.04, offset: 0.5 },
        { x: dx, y: -j * 0.8, r: 9, z: 0, s: 1, d: j },
      ], { duration: 540, delay: j * 46, easing: 'cubic-bezier(.35,.55,.4,1)' })));
      if (this.stopped) return;
      await done(this.order().map((c, k) => this.move(c, { x: 0, y: -k * 0.8, r: 0 }, { duration: 420 })));
    }

    async packets() {
      const o = this.order();
      const a = this.W * 1.6;
      const b = this.H * 0.95;
      const at = (deg, k = 1) => [a * k * Math.cos(deg * RAD), b * k * Math.sin(deg * RAD)];
      const groups = [o.slice(0, 3), o.slice(3, 6), o.slice(6, 9), o.slice(9)];
      let ang = [180, 270, 0, 90];  // left, top, right, bottom

      // 1. Burst apart, overshooting; each packet fanned so its thickness shows.
      this.thump(7);
      this.ring(0, 0, 3.8);
      await done(groups.flatMap((g, i) => g.map((c, j) => {
        const [x, y] = at(ang[i]);
        const tilt = i % 2 ? -8 : 8;
        return this.path(c, [
          { x: x * 1.18, y: y * 1.18 - j * 0.8, r: tilt * 2.2, z: 90, s: 1.08, offset: 0.55 },
          { x: x + j * 5, y: y - j * 1.6, r: tilt + j * 3, z: 24, s: 1 },
        ], { duration: 680, delay: j * 22, easing: 'cubic-bezier(.2,.9,.25,1)' });
      })));
      if (this.stopped) return;

      // 2. One and a half turns, accelerating, each packet spinning as it travels.
      const legs = [560, 430, 340, 290, 300, 440];
      for (let s = 0; s < legs.length; s++) {
        if (this.stopped) return;
        const first = s === 0;
        const last = s === legs.length - 1;
        await done(groups.flatMap((g, i) => {
          const [mx, my] = at(ang[i] + 45, 1.1);
          const [tx, ty] = at(ang[i] + 90);
          return g.map((c, j) => this.path(c, [
            { x: mx + j * 5, y: my - j * 1.6, r: c.r + 45, z: 24 + (s % 2 ? 50 : 10), offset: 0.5 },
            { x: tx + j * 5, y: ty - j * 1.6, r: c.r + 90, z: 24 },
          ], { duration: legs[s], delay: j * 5, easing: first ? 'cubic-bezier(.55,0,1,.6)' : last ? 'cubic-bezier(0,.4,.3,1)' : 'linear' }));
        }));
        ang = ang.map((v) => v + 90);
      }
      this.cards.forEach((c) => this.normalize(c));
      if (this.stopped) return;

      // 3. Cross cuts: opposite packets swap through the centre, one over, one under.
      for (const axis of ['h', 'v']) {
        if (this.stopped) return;
        const pair = groups.map((g, i) => i).filter((i) => {
          const horizontal = Math.round(Math.cos(ang[i] * RAD)) !== 0;
          return axis === 'h' ? horizontal : !horizontal;
        });
        await done(pair.flatMap((i, n) => {
          const [tx, ty] = at(ang[i] + 180);
          const over = n === 0;
          return groups[i].map((c, j) => this.path(c, [
            { x: j * 5, y: -j * 1.6, r: c.r + (over ? 90 : -90), z: over ? 190 : -60, s: over ? 1.18 : 0.92, offset: 0.5 },
            { x: tx + j * 5, y: ty - j * 1.6, r: c.r + (over ? 180 : -180), z: 24, s: 1 },
          ], { duration: 620, delay: j * 12, easing: 'cubic-bezier(.6,0,.3,1)' }));
        }));
        pair.forEach((i) => { ang[i] += 180; });
        this.cards.forEach((c) => this.normalize(c));
      }
      if (this.stopped) return;

      // 4. The packets rise and slam down onto the pile, one at a time.
      let k = 0;
      for (const g of [groups[2], groups[0], groups[3], groups[1]]) {
        if (this.stopped) return;
        const anims = g.map((c, j) => this.path(c, [
          { x: c.x * 0.4, y: c.y * 0.4 - this.H * 0.55 - j * 1.6, r: c.r * 0.4, z: 220, s: 1.16, offset: 0.55 },
          { x: 0, y: -(k + j) * 0.8, r: 0, z: 0, s: 1, d: k + j },
        ], { duration: 520, delay: j * 14, easing: 'cubic-bezier(.5,0,.9,.4)' }));
        k += g.length;
        await done(anims);
        this.thump(5 + k * 0.4);
        this.ring(0, 0, 2.4 + k * 0.12);
        await this.wait(90);
      }
    }

    async ribbon() {
      const o = this.order();
      const n = o.length;
      const mid = (n - 1) / 2;
      const gap = Math.max(10, Math.min(this.W * 0.46, (window.innerWidth * 0.9 - this.W) / (n - 1)));
      const arch = this.H * 0.45;
      const left = -mid * gap;
      const spot = (k) => {
        const t = (k - mid) / mid;
        const slope = (2 * arch * t) / (mid * gap);
        return { x: left + k * gap, y: -arch * (1 - t * t) + arch * 0.5, r: Math.atan(slope) / RAD };
      };
      await done(o.map((c, k) => this.move(c, { x: left, y: arch * 0.5 - k * 0.8, r: -14 }, { duration: 420 })));
      if (this.stopped) return;
      await done(o.map((c, k) => this.move(c, { ...spot(k), d: k }, { duration: 700, delay: k * 42, easing: 'cubic-bezier(.2,.9,.3,1.1)' })));
      if (this.stopped) return;
      // Wave face up: each card bobs up as it turns.
      await done(o.flatMap((c, k) => {
        const p = spot(k);
        return [
          this.flip(c, true, { duration: 620, delay: k * 75, lift: this.W * 0.9 }),
          this.path(c, [{ y: p.y - this.H * 0.28, s: 1.08, offset: 0.45 }, { y: p.y, s: 1 }], { duration: 620, delay: k * 75, easing: 'cubic-bezier(.3,.6,.3,1)' }),
        ];
      }));
      if (this.stopped) return;
      this.ring(0, -arch * 0.2, 3.6);
      await this.wait(650);
      // Wave back, quicker, from the other end.
      await done(o.map((c, k) => this.flip(c, false, { duration: 480, delay: (n - 1 - k) * 42 })));
      if (this.stopped) return;
      // Accordion: both ends fold into the middle and land hard.
      await done(o.map((c, k) => this.move(c, { x: 0, y: -k * 0.8, r: 0 },
        { duration: 560, delay: (mid - Math.abs(k - mid)) * 28, easing: 'cubic-bezier(.6,-0.25,.4,1)' })));
      this.thump(7);
      this.ring(0, 0, 3);
    }

    async carousel() {
      const o = this.order();
      const n = o.length;
      const R = this.W * 1.7;
      const total = this.t(3200);
      const t0 = performance.now();
      const spin = this.box.animate([
        { transform: 'rotateX(0deg) rotateY(0deg)' },
        { transform: 'rotateX(-14deg) rotateY(80deg)', offset: 0.22 },
        { transform: 'rotateX(-14deg) rotateY(290deg)', offset: 0.78 },
        { transform: 'rotateX(0deg) rotateY(360deg)' },
      ], { duration: total, easing: 'cubic-bezier(.45,0,.55,1)' });
      await done(o.map((c, k) => this.move(c, { x: 0, y: 0, ry: (k * 360) / n, tz: R, r: 0 }, { duration: 720, delay: k * 25 })));
      await sleep(Math.max(0, total - this.t(820) - (performance.now() - t0)));
      await done(o.map((c, k) => this.move(c, { ry: c.ry > 180 ? 360 : 0, tz: 0, y: -k * 0.8 }, { duration: 780, delay: (n - 1 - k) * 12 })));
      await spin.finished.catch(() => {});
    }

    // Finale. Resolves with the ace, lifted and face up; the rest sweep away.
    async fanReveal() {
      const o = this.order();
      const n = o.length;
      const top = o[n - 1];
      const rest = o.slice(0, -1);
      const mid = Math.floor((n - 1) / 2);
      const slots = [...rest.slice(0, mid), top, ...rest.slice(mid)];
      const spread = window.innerWidth < 600 ? 96 : 140;
      slots.forEach((c, slot) => { c.el.style.transformOrigin = '50% 118%'; c.slot = slot; });
      await done(slots.map((c, slot) => this.move(c, { x: 0, y: 0, r: -spread / 2 + slot * (spread / (n - 1)), d: c === top ? n + 5 : slot },
        { duration: 760, delay: Math.abs(slot - mid) * 26, easing: 'cubic-bezier(.2,.9,.3,1.12)' })));
      if (this.stopped) return top;
      await this.wait(200);
      top.inner.querySelector('.ic-face').outerHTML = ACE_FACE;
      rest.forEach((c) => c.el.animate([{ opacity: 1 }, { opacity: 0.45 }], this.timing({ duration: 500, fill: 'forwards' }, {})));
      top.el.style.transformOrigin = '50% 50%';
      await done([this.move(top, { y: -this.W * 0.25, z: 40, s: 1.3 }, { duration: 850, easing: 'cubic-bezier(.25,.8,.3,1)' })]);
      if (this.stopped) return top;
      const turn = this.flip(top, true, { duration: 1700, lift: this.W * 0.45, easing: 'cubic-bezier(.45,0,.25,1)' });
      setTimeout(() => this.burst(0, -this.W * 0.25, 0.45), this.t(1000));
      await this.wait(300);
      rest.forEach((c) => {
        const side = c.slot < mid ? -1 : 1;
        const delay = Math.abs(c.slot - mid) * 25;
        this.move(c, { x: side * window.innerWidth * 0.62, y: this.W * 0.3, r: c.r + side * 30 }, { duration: 700, delay, easing: 'cubic-bezier(.6,0,.4,1)' });
        c.el.animate([{ opacity: 0.45 }, { opacity: 0 }], this.timing({ duration: 700, delay, fill: 'forwards' }, {}));
      });
      await done([turn]);
      return top;
    }
  }

  GADeck.SPEED = 0.7;  // global tempo: 1 = original pace, lower = faster
  GADeck.FLOURISHES = ['spring', 'packets', 'ribbon', 'carousel'];
  GADeck.wait = sleep;
  GADeck.done = done;
  window.GADeck = GADeck;
})();
