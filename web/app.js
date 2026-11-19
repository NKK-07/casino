'use strict';
// Gilded Ace front end. One class per job:
//   Api        - talks to the C++ server
//   Session    - remembers who this browser tab is
//   Card       - one playing card and how it is drawn
//   LogView    - the table log plaque
//   TableView  - the felt, the cards in the middle and the seats
//   Prompt     - base class for a question the engine asks; one subclass per kind
//   DealPanel  - the dock between rounds (choose a game, press Deal)
//   Drawer     - rules, leaderboard and hand-history panels
//   App        - wires everything together and polls the server

// Small helper: build a DOM element without innerHTML.
function h(tag, props, ...children) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(props || {})) {
    if (value === null || value === undefined || value === false) continue;
    if (key === 'class') el.className = value;
    else if (key === 'text') el.textContent = value;
    else if (key.startsWith('on')) el.addEventListener(key.slice(2), value);
    else el.setAttribute(key, value === true ? '' : value);
  }
  for (const child of children.flat()) {
    if (child === null || child === undefined || child === false) continue;
    el.append(child instanceof Node ? child : String(child));
  }
  return el;
}

const money = (n) => Number(n).toLocaleString('en-US');

// Computer players are named per room ("Bot_Ada_K7Q2X"); show them as "Bot Ada".
const BOT_NAME = /\bBot_([A-Za-z]+)_[A-Z2-9]{5}\b/g;
const displayName = (name) => String(name).replace(BOT_NAME, 'Bot $1');

// ---------------------------------------------------------------- Api
class Api {
  async call(method, path, params) {
    const query = new URLSearchParams(params || {});
    const url = '/api/' + path + (method === 'GET' ? '?' + query : '');
    const options = method === 'GET' ? { cache: 'no-store' } : { method: 'POST', body: query };
    const response = await fetch(url, options);
    const data = await response.json();
    if (!data.ok) {
      const error = new Error(data.error || 'Request failed');
      error.notSeated = /not seated/i.test(data.error || '');
      throw error;
    }
    return data;
  }
  get(path, params) { return this.call('GET', path, params); }
  post(path, params) { return this.call('POST', path, params); }
}

// ---------------------------------------------------------------- Session
// sessionStorage: each browser tab is its own player, and a refresh keeps the seat.
class Session {
  static load() {
    try { return JSON.parse(sessionStorage.getItem('casino.session') || 'null'); } catch { return null; }
  }
  static save(name, token) {
    try { sessionStorage.setItem('casino.session', JSON.stringify({ name, token })); } catch { /* storage off */ }
  }
  static clear() {
    try { sessionStorage.removeItem('casino.session'); } catch { /* storage off */ }
  }
}

// ---------------------------------------------------------------- Card
class Card {
  static SUITS = { S: '♠', H: '♥', D: '♦', C: '♣' };
  static NAMES = { S: 'spades', H: 'hearts', D: 'diamonds', C: 'clubs' };

  constructor(code) { this.code = code; }           // "AS", "10H" or "??"
  get hidden() { return this.code === '??'; }
  get rank() { return this.code.slice(0, -1); }
  get suitLetter() { return this.code.slice(-1); }
  get red() { return this.suitLetter === 'H' || this.suitLetter === 'D'; }

  render(size) {
    if (this.hidden) return h('div', { class: 'card back ' + (size || ''), role: 'img', 'aria-label': 'face-down card' });
    return h('div', {
      class: ['card', size, this.red ? 'r' : ''].filter(Boolean).join(' '),
      'data-r': this.rank, 'data-s': Card.SUITS[this.suitLetter] || '?',
      role: 'img', 'aria-label': this.rank + ' of ' + (Card.NAMES[this.suitLetter] || '?'),
    });
  }

  renderInline() {
    return h('span', { class: 'chipcard' + (this.red ? ' r' : '') }, this.rank + (Card.SUITS[this.suitLetter] || '?'));
  }

  static row(codes, size) { return (codes || []).map((code) => new Card(code).render(size)); }
}

// ---------------------------------------------------------------- LogView
class LogView {
  constructor(element) { this.element = element; }

  clear() { this.element.replaceChildren(); }

  add(lines) {
    if (!lines.length) return;
    const nearBottom = this.element.scrollHeight - this.element.scrollTop - this.element.clientHeight < 60;
    for (const line of lines) this.element.append(this.renderLine(line));
    while (this.element.children.length > 250) this.element.firstChild.remove();
    if (nearBottom) this.element.scrollTop = this.element.scrollHeight;
  }

  renderLine(raw) {
    let text = displayName(raw.trim());
    let kind = '';
    if (/^(-{3}|={3})/.test(text)) {
      kind = 'd';
      text = text.replace(/^[-=\s]+|[-=\s]+$/g, '');
    } else if (/wins|BLACKJACK|IN BETWEEN|pays \d+ to 1!/.test(text)) {
      kind = 'h';
    } else if (/seed revealed|commitment/i.test(text)) {
      kind = 'q';
    }
    const p = h('p', { class: kind });
    const pattern = /\[(\?\?|(?:10|[2-9JQKA])[CDHS])\]/g;
    let last = 0;
    let match;
    while ((match = pattern.exec(text)) !== null) {
      p.append(text.slice(last, match.index));
      p.append(new Card(match[1]).renderInline());
      last = pattern.lastIndex;
    }
    p.append(text.slice(last));
    return p;
  }
}

// ---------------------------------------------------------------- TableView
class TableView {
  // Seat positions around the racetrack (percent of the table box), the viewer always at the bottom.
  static SPOTS = {
    0: [],
    1: [[50, -10]],
    2: [[22, -8], [78, -8]],
    3: [[0, 50], [50, -10], [100, 50]],
    4: [[0, 50], [24, -8], [76, -8], [100, 50]],
    5: [[6, 92], [0, 26], [50, -10], [100, 26], [94, 92]],
  };

  constructor(element) {
    this.element = element;
    this.lastKey = '';
  }

  render(state) {
    const key = JSON.stringify([state.table, state.players, state.round, state.me.name]);
    if (key === this.lastKey) return;  // nothing changed: keep the DOM (and its animations) as it is
    this.lastKey = key;

    const table = state.table;
    const balances = new Map(state.players.map((p) => [p.name, p.balance]));
    let seats;
    let center;
    if (table && table.game === 'Poker') {
      seats = table.seats.map((s) => this.pokerSeat(s, balances));
      center = this.pokerCenter(table);
    } else if (table && table.game === 'Blackjack') {
      seats = table.seats.map((s) => this.blackjackSeat(s, balances));
      center = this.blackjackCenter(table);
    } else if (table && table.game === 'Red Dog') {
      seats = table.seats.map((s) => this.redDogSeat(s, balances));
      center = this.redDogCenter(table);
    } else {
      seats = state.players.filter((p) => p.present).map((p) => ({
        name: p.name, balance: p.balance, status: p.human ? 'Ready' : 'Computer',
      }));
      center = [h('div', { class: 'insc', text: 'CHOOSE A GAME · PRESS DEAL' }),
        h('div', { class: 'felt-title', text: 'The table is open' }),
        h('div', { class: 'felt-sub', text: seats.length + ' at the table' })];
    }

    const mine = seats.findIndex((s) => s.name === state.me.name);
    const ordered = mine >= 0 ? [seats[mine], ...seats.slice(mine + 1), ...seats.slice(0, mine)] : seats;
    const me = mine >= 0 ? ordered.shift() : null;
    if (me && me.toAct) me.status = 'Your move';
    const spots = TableView.SPOTS[Math.min(ordered.length, 5)];

    const seatEls = ordered.slice(0, 5).map((seat, i) => this.seatElement(seat, spots[i], false));
    if (me) seatEls.unshift(this.seatElement(me, [50, 100], true));

    this.element.replaceChildren(
      h('div', { class: 'felt' }, h('div', { class: 'felt-center' }, center)),
      h('div', { class: 'seats' }, seatEls),
    );
  }

  seatElement(seat, spot, isMe) {
    const classes = ['seat'];
    if (seat.folded) classes.push('folded');
    if (seat.toAct) classes.push('turn');
    const label = displayName(seat.name);
    const avatar = h('div', { class: 'avatar', 'aria-hidden': 'true' }, label.replace(/^Bot /, '').charAt(0).toUpperCase(),
      seat.dealer ? h('span', { class: 'dealer-btn', title: 'Dealer button' }, 'D') : null);
    return h('div', { class: classes.join(' '), style: `left:${spot[0]}%;top:${spot[1]}%` },
      seat.cards || null,
      avatar,
      h('div', { class: 'plate' }, h('b', { text: isMe ? label + ' · you' : label }),
        h('em', { text: seat.detail || money(seat.balance ?? 0) })),
      seat.status ? h('span', { class: 'status' + (seat.win ? ' win' : ''), text: seat.status }) : null);
  }

  // --- Poker
  pokerSeat(s, balances) {
    let cards = null;
    if (s.cards.length) cards = h('div', { class: 'hand' }, Card.row(s.cards, 'sm'));
    else if (s.holeCount && !s.folded) cards = h('div', { class: 'hand stacked' }, Card.row(Array(s.holeCount).fill('??'), 'mini'));
    return {
      name: s.name, balance: balances.get(s.name), folded: s.folded, toAct: s.toAct, dealer: s.dealer, cards,
      status: s.hand ? s.hand + (s.note.startsWith('Wins') ? ' · ' + s.note : '') : (s.toAct ? 'Thinking…' : s.note),
      win: s.note.startsWith('Wins'),
      detail: s.bet > 0 ? money(balances.get(s.name) ?? 0) + ' · bet ' + s.bet : null,
    };
  }
  pokerCenter(t) {
    return [h('div', { class: 'insc', text: "TEXAS HOLD'EM · " + t.street.toUpperCase() }),
      h('div', { class: 'cards-row' }, t.board.length ? Card.row(t.board) : Card.row(['??', '??', '??'])),
      h('div', { class: 'pot', text: 'POT ' + money(t.pot) })];
  }

  // --- Blackjack
  blackjackSeat(s, balances) {
    const hands = s.hands.map((hand) => h('div', { class: 'hand' }, Card.row(hand.cards, 'sm')));
    const first = s.hands[0];
    const totals = s.hands.map((hand) => (hand.soft ? 'soft ' : '') + hand.total + (hand.bust ? ' bust' : '')).join(' / ');
    const notes = s.hands.map((hand) => hand.note).filter(Boolean).join(' / ');
    return {
      name: s.name, balance: balances.get(s.name), toAct: s.toAct,
      cards: hands.length ? h('div', { class: 'hand' }, hands) : null,
      detail: totals + ' · stake ' + s.hands.reduce((sum, hand) => sum + hand.stake, 0),
      status: notes || (s.toAct ? 'Thinking…' : ''),
      win: /wins|BLACKJACK/.test(notes),
      folded: first && first.bust,
    };
  }
  blackjackCenter(t) {
    const hidden = t.dealer.cards.includes('??');
    return [h('div', { class: 'insc', text: 'BLACKJACK PAYS 3 TO 2 · DEALER STANDS ON 17' }),
      h('div', { class: 'cards-row' }, Card.row(t.dealer.cards)),
      h('div', { class: 'pot', text: (hidden ? 'DEALER SHOWS ' : 'DEALER ') + t.dealer.total })];
  }

  // --- Red Dog
  redDogSeat(s, balances) {
    return {
      name: s.name, balance: balances.get(s.name), toAct: s.toAct,
      detail: 'ante ' + (s.stake - s.raise) + (s.raise ? ' + raise ' + s.raise : ''),
      status: s.note || (s.toAct ? 'Thinking…' : ''),
      win: /wins|pays/.test(s.note),
    };
  }
  redDogCenter(t) {
    let line = '';
    if (t.cards.length >= 2) {
      const pair = t.cards[0].slice(0, -1) === t.cards[1].slice(0, -1);
      if (pair) line = 'Pair · three of a kind pays 11 to 1';
      else if (t.spread === 0) line = 'Consecutive cards · push';
      else line = 'Spread ' + t.spread + ' · pays ' + t.payout + ' to 1';
    }
    return [h('div', { class: 'insc', text: 'RED DOG · IN BETWEEN' }),
      h('div', { class: 'cards-row' }, t.cards.length ? Card.row(t.cards) : Card.row(['??', '??', '??'])),
      line ? h('div', { class: 'felt-sub', text: line }) : null];
  }
}

// ---------------------------------------------------------------- Prompts
// A Prompt is one question from the engine. Subclasses override cards(), title(),
// controls() and keys(); the base class draws the frame and the countdown.
class Prompt {
  static create(app, prompt) {
    const kinds = { bet: BetPrompt, blackjack: BlackjackPrompt, reddog: RedDogPrompt, poker: PokerPrompt };
    const Kind = kinds[prompt.kind];
    return Kind ? new Kind(app, prompt) : null;
  }

  constructor(app, prompt) {
    this.app = app;
    this.id = prompt.id;
    this.data = prompt.data;
    this.seconds = prompt.seconds;
    this.startedAt = Date.now();
    this.sent = false;
  }

  cards() { return null; }
  title() { return 'Your move'; }
  controls() { return []; }
  keys() { return {}; }

  answer(value) {
    if (this.sent) return;
    this.sent = true;
    this.app.act(this.id, value);
  }

  render() {
    this.secondsEl = h('span');
    this.barEl = h('i');
    this.timerEl = h('div', { class: 'timer' }, this.barEl);
    this.tick();
    return [
      this.cards() ? h('div', { class: 'dock-cards' }, this.cards()) : null,
      h('div', { class: 'dock-main' },
        h('div', { class: 'dock-line' }, h('span', {}, this.title()), this.secondsEl),
        this.timerEl,
        h('div', { class: 'controls' }, this.controls())),
    ];
  }

  tick() {
    const left = Math.max(0, this.seconds - (Date.now() - this.startedAt) / 1000);
    this.secondsEl.textContent = Math.ceil(left) + ' s';
    this.barEl.style.width = (100 * left / this.seconds) + '%';
    this.timerEl.classList.toggle('urgent', left <= 10);
  }

  button(label, value, opts) {
    const o = opts || {};
    return h('button', { class: 'btn' + (o.primary ? ' primary' : ''), disabled: o.disabled, onclick: () => this.answer(value) },
      label, o.key ? h('kbd', { text: o.key }) : null);
  }
}

class BetPrompt extends Prompt {
  title() { return [h('b', { text: this.data.game }), ' · place your bet (' + this.data.min + ' to ' + money(this.data.max) + ')']; }
  controls() {
    const start = Math.min(Math.max(this.app.lastBet || this.data.min, this.data.min), this.data.max);
    this.input = h('input', { class: 'num', type: 'number', min: this.data.min, max: this.data.max, value: start, 'aria-label': 'Bet amount' });
    const pick = (v) => h('button', { class: 'pill', type: 'button', onclick: () => { this.input.value = v; } }, String(v === this.data.max ? 'Max' : v));
    const presets = [...new Set([this.data.min, 50, 100, 250, this.data.max])].filter((v) => v >= this.data.min && v <= this.data.max);
    return [this.input, ...presets.map(pick),
      h('button', { class: 'btn', onclick: () => this.answer('0') }, 'Sit out'),
      h('button', { class: 'btn primary', onclick: () => this.placeBet() }, 'Bet', h('kbd', { text: 'Enter' }))];
  }
  placeBet() {
    const value = Math.round(Number(this.input.value));
    if (!(value >= this.data.min && value <= this.data.max)) {
      this.app.banner('Bet between ' + this.data.min + ' and ' + this.data.max + '.');
      return;
    }
    this.app.lastBet = value;
    this.answer(String(value));
  }
  keys() { return { Enter: () => this.placeBet() }; }
}

class BlackjackPrompt extends Prompt {
  cards() { return Card.row(this.data.cards, 'big'); }
  title() {
    const hand = this.data.handCount > 1 ? ' (hand ' + (this.data.handIndex + 1) + ' of ' + this.data.handCount + ')' : '';
    return [h('b', { text: (this.data.soft ? 'Soft ' : '') + this.data.total }), hand + ' · dealer shows ',
      new Card(this.data.dealer).renderInline(), ' · stake ' + this.data.stake];
  }
  controls() {
    return [this.button('Hit', 'hit', { primary: true, key: 'H' }), this.button('Stand', 'stand', { key: 'S' }),
      this.button('Double', 'double', { key: 'D', disabled: !this.data.canDouble }),
      this.button('Split', 'split', { key: 'P', disabled: !this.data.canSplit })];
  }
  keys() {
    const k = { h: () => this.answer('hit'), s: () => this.answer('stand') };
    if (this.data.canDouble) k.d = () => this.answer('double');
    if (this.data.canSplit) k.p = () => this.answer('split');
    return k;
  }
}

class RedDogPrompt extends Prompt {
  cards() { return Card.row([this.data.low, this.data.high]); }
  title() { return [h('b', { text: 'Spread ' + this.data.spread }), ' · pays ' + this.data.payout + ' to 1 · ante ' + this.data.ante]; }
  controls() {
    this.value = h('b', { text: String(this.data.maxRaise) });
    this.slider = h('input', { type: 'range', min: 0, max: this.data.maxRaise, value: this.data.maxRaise, 'aria-label': 'Raise amount',
      oninput: () => { this.value.textContent = this.slider.value; } });
    return [this.button('No raise', '0', { key: 'N' }),
      h('div', { class: 'range' }, '0', this.slider, String(this.data.maxRaise), this.value),
      h('button', { class: 'btn primary', onclick: () => this.answer(this.slider.value) }, 'Raise', h('kbd', { text: 'R' }))];
  }
  keys() { return { n: () => this.answer('0'), r: () => this.answer(this.slider.value) }; }
}

class PokerPrompt extends Prompt {
  cards() { return Card.row(this.data.hole, 'big'); }
  title() {
    return [h('b', { text: this.data.street }), ' · pot ' + money(this.data.pot) + ' · stack ' + money(this.data.stack) +
      (this.data.toCall ? ' · ' + this.data.toCall + ' to call' : '')];
  }
  controls() {
    const d = this.data;
    const list = [this.button('Fold', 'f', { key: 'F' }),
      this.button(d.toCall ? 'Call ' + d.toCall : 'Check', 'c', { primary: true, key: 'C' })];
    if (d.canRaise) {
      this.value = h('b', { text: String(d.minRaiseTo) });
      this.slider = h('input', { type: 'range', min: d.minRaiseTo, max: d.maxRaiseTo, value: d.minRaiseTo, 'aria-label': 'Raise to',
        oninput: () => { this.value.textContent = this.slider.value; } });
      list.push(h('div', { class: 'range' }, String(d.minRaiseTo), this.slider, String(d.maxRaiseTo), this.value),
        h('button', { class: 'btn', onclick: () => this.answer('r:' + this.slider.value) }, 'Raise', h('kbd', { text: 'R' })),
        h('button', { class: 'btn small', onclick: () => this.answer('r:' + d.maxRaiseTo) }, 'All in'));
    }
    return list;
  }
  keys() {
    const k = { f: () => this.answer('f'), c: () => this.answer('c') };
    if (this.data.canRaise) k.r = () => this.answer('r:' + this.slider.value);
    return k;
  }
}

// ---------------------------------------------------------------- DealPanel
class DealPanel {
  constructor(app) {
    this.app = app;
    this.game = 'poker';
    this.bots = 2;
  }

  render() {
    const games = this.app.games.length ? this.app.games : [
      { key: 'blackjack', name: 'Blackjack' }, { key: 'reddog', name: 'Red Dog' }, { key: 'poker', name: 'Poker' }];
    const pills = games.map((g) => h('button', { class: 'pill' + (g.key === this.game ? ' on' : ''), 'aria-pressed': String(g.key === this.game),
      onclick: () => { this.game = g.key; this.app.renderDock(true); } }, g.name));
    const count = h('span', { text: String(this.bots) });
    const step = (delta) => () => { this.bots = Math.max(0, Math.min(5, this.bots + delta)); count.textContent = String(this.bots); };
    return [
      h('div', {}, h('span', { class: 'field-label', text: 'Game' }), h('div', { class: 'controls' }, pills)),
      h('div', {}, h('span', { class: 'field-label', text: 'Computer opponents' }),
        h('div', { class: 'stepper' }, h('button', { 'aria-label': 'Fewer opponents', onclick: step(-1) }, '−'), count,
          h('button', { 'aria-label': 'More opponents', onclick: step(1) }, '+'))),
      h('div', { class: 'dock-main' }, h('div', { class: 'dock-line' }, 'Everyone at the table is dealt in.')),
      h('button', { class: 'btn primary', onclick: () => this.app.deal(this.game, this.bots) }, 'Deal'),
    ];
  }
}

// ---------------------------------------------------------------- Drawer
class Drawer {
  constructor(app) {
    this.app = app;
    this.panel = document.getElementById('drawer');
    this.body = document.getElementById('drawerBody');
    this.dim = document.getElementById('dim');
    document.getElementById('drawerClose').addEventListener('click', () => this.close());
    this.dim.addEventListener('click', () => this.close());
  }

  get isOpen() { return !this.panel.hidden; }

  show(...content) {
    this.body.replaceChildren(...content);
    this.panel.hidden = false;
    this.dim.hidden = false;
    document.getElementById('drawerClose').focus();
  }

  close() {
    this.panel.hidden = true;
    this.dim.hidden = true;
  }

  async open(kind) {
    try {
      if (kind === 'rules') this.rules(0);
      if (kind === 'leaderboard') this.leaderboard(await this.app.api.get('leaderboard'));
      if (kind === 'history') this.history(await this.app.api.get('history'));
    } catch (e) {
      this.app.banner(e.message);
    }
  }

  rules(index) {
    const games = this.app.games;
    if (!games.length) return;
    const tabs = games.map((g, i) => h('button', { class: 'tab' + (i === index ? ' on' : ''), onclick: () => this.rules(i) }, g.name));
    this.show(h('h2', { class: 'gold', text: 'Rules' }), h('div', { class: 'tabs' }, tabs), h('pre', { text: games[index].rules }));
  }

  leaderboard(data) {
    const list = (entries, signed) => entries.map((e, i) => h('div', { class: 'rank-row' },
      h('span', { class: 'n', text: String(i + 1) }), h('span', { class: 'who', text: e.name }),
      h('span', { class: 'v' + (e.value < 0 ? ' neg' : ''), text: (signed && e.value > 0 ? '+' : '') + money(e.value) })));
    this.show(h('h2', { class: 'gold', text: 'Leaderboard' }),
      h('h3', { text: 'Richest players' }), list(data.richest, false),
      h('h3', { text: 'Biggest lifetime winners' }), list(data.winners, true),
      h('h3', { text: 'The house' }), h('div', { class: 'rank-row' }, h('span', { class: 'who', text: 'Poker rake collected' }),
        h('span', { class: 'v', text: money(data.rake) })));
  }

  history(data) {
    const rows = data.records.map((r) => h('tr', {},
      h('td', { text: new Date(r.time * 1000).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) }),
      h('td', { text: r.game + ' #' + r.round }), h('td', { text: r.player }),
      h('td', { class: r.net > 0 ? 'pos' : (r.net < 0 ? 'neg' : ''), text: (r.net > 0 ? '+' : '') + money(r.net) }),
      h('td', { text: money(r.balance) })));
    this.show(h('h2', { class: 'gold', text: 'Hand history' }),
      h('p', { class: 'help', text: data.total + ' records in the binary history file. Newest first.' }),
      h('table', { class: 'hist' }, h('thead', {}, h('tr', {}, ['Time', 'Round', 'Player', 'Net', 'Balance'].map((t) => h('th', { text: t })))),
        h('tbody', {}, rows)));
  }
}

// ---------------------------------------------------------------- App
class App {
  constructor() {
    this.api = new Api();
    this.log = new LogView(document.getElementById('log'));
    this.table = new TableView(document.getElementById('table'));
    this.dealPanel = new DealPanel(this);
    this.drawer = new Drawer(this);
    this.dock = document.getElementById('dock');
    this.games = [];
    this.session = null;
    this.state = null;
    this.prompt = null;
    this.dockKey = '';
    this.since = 0;
    this.failures = 0;
    this.lastBet = 0;
    this.polling = false;
    this.roomCode = null;
    this.bindEvents();
  }

  bindEvents() {
    document.getElementById('joinForm').addEventListener('submit', (e) => {
      e.preventDefault();
      this.join(document.getElementById('nameInput').value.trim(), '');
    });
    document.getElementById('createForm').addEventListener('submit', (e) => {
      e.preventDefault();
      this.roomAction('room/create', { room: document.getElementById('roomName').value.trim() });
    });
    document.getElementById('codeForm').addEventListener('submit', (e) => {
      e.preventDefault();
      this.roomAction('room/join', { code: document.getElementById('roomCode').value.trim() });
    });
    document.getElementById('leaveBtn').addEventListener('click', () => this.roomAction('room/leave', {}));
    document.getElementById('lobbyLeave').addEventListener('click', () => this.leave());
    document.getElementById('codeBtn').addEventListener('click', () => this.copyCode());
    document.querySelectorAll('[data-drawer]').forEach((b) => b.addEventListener('click', () => this.drawer.open(b.dataset.drawer)));
    document.addEventListener('keydown', (e) => this.onKey(e));
    setInterval(() => { if (this.prompt) this.prompt.tick(); }, 250);
  }

  start() {
    const saved = Session.load();
    if (saved) this.join(saved.name, saved.token, true);
    else this.showJoin('');
  }

  showScreen(id) {
    for (const screen of ['join', 'lobby', 'room']) document.getElementById(screen).hidden = screen !== id;
  }

  showJoin(name, error) {
    this.showScreen('join');
    const input = document.getElementById('nameInput');
    if (name) input.value = name;
    const help = document.getElementById('joinHelp');
    help.textContent = error || '2 to 16 letters, digits, underscore or dash. You start with 1,000 play chips.';
    help.classList.toggle('err', Boolean(error));
    input.focus();
  }

  async join(name, token, quiet) {
    try {
      const data = await this.api.post('join', { name, token });
      this.session = { name: data.name, token: data.token };
      Session.save(data.name, data.token);
      this.games = data.games;
      this.showScreen('lobby');
      this.enterRoom(null);
      if (data.message) this.banner(data.message, true);
      if (!this.polling) { this.polling = true; this.poll(); }
    } catch (e) {
      if (quiet) Session.clear();
      this.showJoin(name, quiet ? '' : e.message);
    }
  }

  async leave() {
    const s = this.session;
    this.session = null;
    Session.clear();
    if (s) { try { await this.api.post('leave', s); } catch { /* already gone */ } }
    this.showJoin(s ? s.name : '');
  }

  async poll() {
    if (this.session) {
      try {
        const state = await this.api.get('state', { ...this.session, since: this.since });
        this.failures = 0;
        this.hideBanner('offline');
        this.update(state);
      } catch (e) {
        if (e.notSeated) {
          this.session = null;
          Session.clear();
          this.showJoin(document.getElementById('nameInput').value, 'Your seat expired. Join again.');
        } else if (++this.failures >= 3) {
          this.banner('Reconnecting…', false, 'offline');
        }
      }
    }
    setTimeout(() => this.poll(), document.hidden ? 3000 : 700);
  }

  // Resets everything that belongs to one room (log cursor, table, dock).
  enterRoom(code) {
    this.roomCode = code;
    this.since = 0;
    this.log.clear();
    this.table.lastKey = '';
    this.dockKey = '';
    this.prompt = null;
  }

  update(state) {
    this.state = state;
    const balanceText = state.me.name.toUpperCase() + '  ' + money(state.me.balance);
    if (!state.room) {
      if (this.roomCode !== null) this.enterRoom(null);
      this.showLobby(state, balanceText);
      return;
    }
    if (state.room.code !== this.roomCode) {
      this.enterRoom(state.room.code);  // new room: fetch its log from the start on the next poll
      return;
    }
    this.showScreen('room');
    document.getElementById('balance').textContent = balanceText;
    document.getElementById('codeBtn').textContent = 'Code ' + state.room.code;
    document.getElementById('subtitle').textContent = state.room.name + ' · ' +
      (state.round.running ? state.round.game + ' round ' + state.round.number : 'between rounds');
    this.since = state.log.next;
    this.log.add(state.log.lines);
    this.table.render(state);
    this.renderDock(false);
  }

  renderDock(force) {
    const s = this.state;
    if (!s) return;
    let key;
    if (s.prompt) key = 'prompt:' + s.prompt.id;
    else if (s.round.running) key = 'wait:' + this.waitingFor();
    else key = 'idle';
    if (key === this.dockKey && !force) return;
    this.dockKey = key;

    this.prompt = s.prompt ? Prompt.create(this, s.prompt) : null;
    this.dock.classList.toggle('mine-turn', Boolean(this.prompt));
    if (this.prompt) {
      this.dock.replaceChildren(...this.prompt.render().filter(Boolean));
      const first = this.dock.querySelector('.btn.primary, .btn');
      if (first && !this.drawer.isOpen) first.focus({ preventScroll: true });
    } else if (s.round.running) {
      const who = this.waitingFor();
      const seated = s.round.seated.includes(s.me.name);
      this.dock.replaceChildren(h('div', { class: 'waiting', text: !seated ? 'A round is in progress. You are dealt in from the next round.'
        : (who ? 'Waiting for ' + who + '…' : 'Dealing…') }));
    } else {
      this.dock.replaceChildren(...this.dealPanel.render());
    }
  }

  waitingFor() {
    const t = this.state && this.state.table;
    if (!t || !t.seats) return '';
    const seat = t.seats.find((x) => x.toAct);
    return seat ? displayName(seat.name) : '';
  }

  showLobby(state, balanceText) {
    this.showScreen('lobby');
    document.getElementById('lobbyBalance').textContent = balanceText;
    const key = JSON.stringify(state.rooms);
    if (key === this.lobbyKey) return;  // keep the list (and focus) steady between polls
    this.lobbyKey = key;
    const list = document.getElementById('roomList');
    if (!state.rooms.length) {
      list.replaceChildren(h('p', { class: 'empty', text: 'No rooms yet. Open one and share its code with your friends.' }));
      return;
    }
    list.replaceChildren(...state.rooms.map((r) => h('div', { class: 'room-row' },
      h('div', { class: 'info' }, h('b', { text: r.name }),
        h('small', { text: 'Host ' + r.host + ' · code ' + r.code + ' · ' + r.players + ' of 6 players' })),
      h('span', { class: 'tag', text: r.running ? 'Playing ' + r.game : 'Waiting' }),
      h('button', { class: 'btn small', disabled: r.players >= 6, onclick: () => this.roomAction('room/join', { code: r.code }) },
        r.players >= 6 ? 'Full' : 'Join'))));
  }

  async roomAction(path, params) {
    try {
      await this.api.post(path, { ...this.session, ...params });
      this.lobbyKey = '';
    } catch (e) {
      this.banner(e.message);
    }
  }

  async copyCode() {
    const code = this.roomCode || '';
    try {
      await navigator.clipboard.writeText(code);
      this.banner('Room code ' + code + ' copied. Send it to your friends.', true);
    } catch {
      this.banner('Room code: ' + code + ' (share it with your friends)', true);
    }
  }

  async act(id, answer) {
    try {
      await this.api.post('act', { ...this.session, id, answer });
    } catch (e) {
      this.banner(e.message);
    }
    this.dockKey = '';  // redraw on the next poll
  }

  async deal(game, bots) {
    try {
      await this.api.post('start', { ...this.session, game, bots });
    } catch (e) {
      this.banner(e.message);
    }
  }

  onKey(e) {
    if (e.key === 'Escape' && this.drawer.isOpen) { this.drawer.close(); return; }
    if (!this.prompt || this.drawer.isOpen) return;
    const typing = e.target instanceof HTMLInputElement && e.target.type !== 'range';
    if (typing && e.key !== 'Enter') return;
    const handler = this.prompt.keys()[e.key.length === 1 ? e.key.toLowerCase() : e.key];
    if (handler) { e.preventDefault(); handler(); }
  }

  banner(text, info, tag) {
    const el = document.getElementById('banner');
    el.textContent = text;
    el.className = 'banner' + (info ? ' info' : '');
    el.dataset.tag = tag || '';
    el.hidden = false;
    clearTimeout(this.bannerTimer);
    if (!tag) this.bannerTimer = setTimeout(() => { el.hidden = true; }, 6000);
  }

  hideBanner(tag) {
    const el = document.getElementById('banner');
    if (el.dataset.tag === tag) el.hidden = true;
  }
}

new App().start();
