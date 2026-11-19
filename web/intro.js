'use strict';
// Gilded Ace opening sequence, built on GADeck (deck.js):
//   rule draws, deck settles -> riffle -> four-packet cut -> ribbon & wave -> fan and
//   ace reveal -> the ace becomes the crest -> curtains part.
// Plays once per browser session. ?intro replays it; ?intro=carousel,spring
// swaps in other flourishes. Click, any key or "Skip" ends it; reduced-motion skips it.
(() => {
  const intro = document.getElementById('intro');
  if (!intro) return;
  const root = document.documentElement;
  const params = new URLSearchParams(location.search);

  let seen = false;
  try { seen = sessionStorage.getItem('ga-intro') === '1'; } catch (e) { /* storage blocked */ }
  const forced = params.has('intro');
  const reduce = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  if ((seen && !forced) || reduce || !Element.prototype.animate || !window.GADeck) { intro.remove(); return; }
  try { sessionStorage.setItem('ga-intro', '1'); } catch (e) { /* ignore */ }

  root.classList.add('intro-on');
  const q = (s) => intro.querySelector(s);
  const line = q('.intro-line');
  const crest = q('.intro-crest');
  const word = q('.intro-word');
  const tag = q('.intro-tag');
  const stage = q('.intro-stage');
  const curtainTop = q('.intro-curtain.top');
  const curtainBottom = q('.intro-curtain.bottom');
  const EASE = 'cubic-bezier(.2,.8,.2,1)';
  const { done } = window.GADeck;
  const wait = (ms) => window.GADeck.wait(ms * window.GADeck.SPEED);

  const deck = new window.GADeck(q('#introDeck'));
  let ended = false;

  function pickFlourishes() {
    const all = window.GADeck.FLOURISHES;
    const asked = (params.get('intro') || '').split(',').filter((n) => all.includes(n));
    return asked.length ? asked : ['packets', 'ribbon'];
  }

  async function run() {
    if (document.fonts && document.fonts.ready) await Promise.race([document.fonts.ready, wait(900)]);
    if (ended) return;

    line.animate([{ transform: 'scaleX(0)', opacity: 0 }, { transform: 'scaleX(1)', opacity: 1 }], { duration: 800, easing: EASE, fill: 'forwards' });
    await deck.deal();
    if (ended) return;
    line.animate([{ opacity: 1 }, { opacity: 0.3 }], { duration: 700, fill: 'forwards' });

    await deck.riffle();
    for (const name of pickFlourishes()) {
      if (ended) return;
      await wait(140);
      await deck[name]();
    }
    if (ended) return;
    await wait(140);

    const ace = await deck.fanReveal();
    if (ended) return;
    await wait(650);

    ace.el.animate([{ opacity: 1, filter: 'blur(0)' }, { opacity: 0, filter: 'blur(6px)' }], { duration: 600, easing: 'ease-in', fill: 'forwards' });
    deck.move(ace, { s: 1.75, y: 0 }, { duration: 650, easing: 'ease-in' });
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
    deck.stopped = true;
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
  setTimeout(() => finish(true), 25000);  // safety net
  run().catch(() => finish(true));
})();
