import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';

global.window = {
  location: {search: ''},
  dispatchEvent(event) { this.lastEvent = event; },
};
global.CustomEvent = class { constructor(type, options) { this.type = type; this.detail = options.detail; } };
const {applyBrowserView, createWasmBridge, encodeSandboxConfig,
  reactPrototypeRequested,
  logoutCompleted, screenAfterAuthoritativeState, withSandboxHunterCount} =
  await import('../src/bridge.js');

test('prototype is opt-in and SDL remains the default', () => {
  assert.equal(reactPrototypeRequested(''), false);
  assert.equal(reactPrototypeRequested('?ui=react'), true);
  assert.equal(reactPrototypeRequested('?ui=sdl'), false);
});

test('actions cross the narrow WASM bridge exactly once', () => {
  const bridge = createWasmBridge();
  bridge.action('join-sandbox', 'SBX-ABC123');
  assert.equal(window.lastEvent.type, 'basilisk:action');
  assert.deepEqual(window.lastEvent.detail,
    {action: 'join-sandbox', arguments: ['SBX-ABC123']});
});

test('state requests use the same isolated event boundary', () => {
  createWasmBridge().requestState();
  assert.equal(window.lastEvent.detail.action, '__request-state');
});

test('successful authentication leaves the React auth screen', () => {
  assert.equal(screenAfterAuthoritativeState('auth', {
    view: 'menu', authenticated: true,
  }), 'main');
});

test('failed or pending authentication remains retryable on the auth screen', () => {
  assert.equal(screenAfterAuthoritativeState('auth', {
    view: 'authentication', authenticated: false, authWaiting: false,
    authError: 'Invalid credentials.',
  }), 'auth');
  assert.equal(screenAfterAuthoritativeState('auth', {
    view: 'authentication', authenticated: false, authWaiting: true,
  }), 'auth');
});

test('registration success uses the same authoritative transition', () => {
  assert.equal(screenAfterAuthoritativeState('auth', {
    view: 'menu', authenticated: true, username: 'new-hunter',
  }), 'main');
});

test('gameplay view activates the existing canvas shell', () => {
  const classes = new Set();
  let focused = false;
  const document = {
    body: {classList: {toggle(name, active) {
      if (active) classes.add(name); else classes.delete(name);
    }}},
    getElementById: () => ({focus: () => { focused = true; }}),
  };
  applyBrowserView('gameplay', document);
  assert.equal(classes.has('react-gameplay'), true);
  assert.equal(focused, true);
  applyBrowserView('menu', document);
  assert.equal(classes.has('react-gameplay'), false);
});

test('authoritative gameplay quit returns React to the Main Menu', () => {
  assert.equal(screenAfterAuthoritativeState('ai-standard', {
    view: 'menu', authenticated: true, gameplayExitRevision: 1,
  }, 0), 'main');
  assert.equal(screenAfterAuthoritativeState('ai-sandbox', {
    view: 'menu', authenticated: false, gameplayExitRevision: 2,
  }, 1), 'main');
  assert.equal(screenAfterAuthoritativeState('settings', {
    view: 'menu', authenticated: true, gameplayExitRevision: 2,
  }, 2), 'settings');
});

test('gameplay quit lifecycle preserves authoritative ordering and SDL fallback', () => {
  const main = readFileSync(
    new URL('../../../src/main.cpp', import.meta.url), 'utf8');
  const pauseHandler = main.slice(main.indexOf('SDL_AppResult handlePauseResult'),
    main.indexOf('#if defined(__EMSCRIPTEN__)',
      main.indexOf('SDL_AppResult handlePauseResult')));
  const finish = main.slice(main.indexOf('SDL_AppResult finishGameplayQuit'),
    main.indexOf('SDL_AppResult handlePauseResult'));
  assert.match(pauseHandler,
    /state\.session->quit\(\)[\s\S]*?finishGameplayQuit\(state\)/);
  assert.match(finish,
    /localAiDriver\.reset\(\)[\s\S]*?localSandboxDriver\.reset\(\)/);
  assert.match(finish,
    /networkSession->clearGameplaySession\(\)[\s\S]*?state\.session = nullptr/);
  assert.match(finish, /state\.mainMenu = basilisk::game::MainMenuState\{\}/);
  assert.match(finish, /state\.view = AppView::MainMenu/);
  assert.match(finish, /\+\+state\.gameplayExitRevision/);
});

test('React hiding keeps the SDL canvas in layout', () => {
  const css = readFileSync(new URL('../src/styles.css', import.meta.url), 'utf8');
  assert.match(css, /body\.react-ui #canvas \{ visibility:hidden;/);
  assert.doesNotMatch(css, /body\.react-ui #canvas \{ display:none;/);
});

test('WASM binds SDL keyboard capture to the gameplay canvas', () => {
  const main = readFileSync(
    new URL('../../../src/main.cpp', import.meta.url), 'utf8');
  assert.match(main,
    /SDL_SetHint\(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas"\)/);
  assert.match(main,
    /browserPreGameBridge\(\)\.enabled\(\)\) return;/);
});

test('React auth fields keep browser-editable text controls and password masking', () => {
  const source = readFileSync(
    new URL('../src/main.jsx', import.meta.url), 'utf8');
  const css = readFileSync(new URL('../src/styles.css', import.meta.url), 'utf8');
  assert.match(source, /label="EMAIL" type="text" value=\{form\.email\}/);
  assert.match(source, /label="PASSWORD" type="text" className="password-input"/);
  assert.match(css, /\.password-input \{ -webkit-text-security:disc; \}/);
});

test('Play AI uses mode selection and separate Standard and Sandbox screens', () => {
  const source = readFileSync(
    new URL('../src/main.jsx', import.meta.url), 'utf8');
  assert.match(source, /setScreen\('ai-mode'\)[^>]*>PLAY AI/);
  assert.match(source, /screen === 'ai-standard'[\s\S]*?<AiOptions/);
  assert.match(source, /screen === 'ai-standard'[\s\S]*?start-ai[\s\S]*?>START GAME/);
  assert.match(source, /screen === 'ai-sandbox'[\s\S]*?<Config/);
  assert.match(source, /screen === 'ai-sandbox'[\s\S]*?start-local-sandbox[\s\S]*?>START GAME/);
});

test('Online Sandbox separates host configuration from join code entry', () => {
  const source = readFileSync(
    new URL('../src/main.jsx', import.meta.url), 'utf8');
  assert.match(source, /screen === 'sandbox-online-mode'/);
  assert.match(source, /title="HOST SANDBOX"[\s\S]*?sandbox-online-host/);
  assert.match(source, /title="JOIN SANDBOX"[\s\S]*?sandbox-online-join/);
  assert.match(source, /screen === 'sandbox-online-host'[\s\S]*?host-sandbox/);
  assert.match(source, /screen === 'sandbox-online-join'[\s\S]*?join-sandbox/);
});

test('Online Sandbox hunter selection keeps supported cave counts requestable', () => {
  const base = {hunters: 2, humans: 2, caves: 30, jackals: 2,
    cadence: 5, starting: 3, max: 5, difficulty: 1, behavior: 0};
  const expectedCaves = [30, 30, 40, 50, 60];
  for (let hunters = 2; hunters <= 6; ++hunters) {
    const config = withSandboxHunterCount(base, hunters);
    assert.equal(config.hunters, hunters);
    assert.equal(config.caves, expectedCaves[hunters - 2]);
    assert.ok(config.humans <= config.hunters);
    const bridge = createWasmBridge();
    bridge.action('host-sandbox', encodeSandboxConfig(config));
    assert.deepEqual(window.lastEvent.detail, {action: 'host-sandbox',
      arguments: [[hunters, 2, config.caves, 2, 5, 3, 5, 1, 0].join(',')]});
  }
  assert.equal(withSandboxHunterCount({...base, caves: 60}, 4).caves, 60);
});

test('Online Sandbox host surfaces authoritative configuration errors', () => {
  const source = readFileSync(
    new URL('../src/main.jsx', import.meta.url), 'utf8');
  const nativeState = readFileSync(
    new URL('../../../src/main.cpp', import.meta.url), 'utf8');
  assert.match(source,
    /screen === 'sandbox-online-host'[\s\S]*?state\.sandboxError[\s\S]*?className="error"/);
  assert.match(nativeState,
    /,\\"sandboxError\\":\\"[\s\S]*?sandboxValidationError\(\)/);
});

test('Play Online reuses gated mode cards without changing routes', () => {
  const source = readFileSync(
    new URL('../src/main.jsx', import.meta.url), 'utf8');
  const css = readFileSync(new URL('../src/styles.css', import.meta.url), 'utf8');
  assert.match(source, /function ModeCard\([^)]*disabled = false/);
  assert.match(source, /<ModeCard title="STANDARD"[\s\S]*?disabled=\{!authenticated\}[\s\S]*?setScreen\('standard'\)/);
  assert.match(source, /<ModeCard title="SANDBOX"[\s\S]*?disabled=\{!authenticated\}[\s\S]*?setScreen\('sandbox-online-mode'\)/);
  assert.match(css, /\.mode-card:disabled \{[^}]*cursor:not-allowed;/);
});

test('Standard Online uses action cards with separated host and join screens', () => {
  const source = readFileSync(
    new URL('../src/main.jsx', import.meta.url), 'utf8');
  assert.match(source, /screen === 'standard'[\s\S]*?title="FIND GAME"[\s\S]*?find-match/);
  assert.match(source, /title="HOST GAME"[\s\S]*?setScreen\('standard-host'\)[\s\S]*?host-standard/);
  assert.match(source, /title="JOIN GAME"[\s\S]*?setScreen\('standard-join'\)/);

  const host = source.slice(source.indexOf("screen === 'standard-host'"),
    source.indexOf("screen === 'standard-join'"));
  assert.match(host, /state\.lobbyCode/);
  assert.doesNotMatch(host, /<Field label="LOBBY CODE"/);
  assert.doesNotMatch(host, /join-standard/);

  const join = source.slice(source.indexOf("screen === 'standard-join'"),
    source.indexOf("screen === 'sandbox-online-mode'"));
  assert.match(join, /<Field label="LOBBY CODE"/);
  assert.match(join, /join-standard/);
  assert.doesNotMatch(join, /host-standard/);
  assert.match(join, /leaveStandard\('standard'\)/);
});

test('Standard navigation cancels authoritative queue or host state before leaving', () => {
  const source = readFileSync(
    new URL('../src/main.jsx', import.meta.url), 'utf8');
  const main = readFileSync(
    new URL('../../../src/main.cpp', import.meta.url), 'utf8');
  assert.match(source, /const leaveStandard = \(destination\) => \{/);
  assert.match(source, /setStandardNavigationPending\(destination\);[\s\S]*?bridge\.action\('cancel-standard'\)/);
  assert.match(source, /if \(!standardNavigationPending \|\| state\.lobbyWaiting\) return;/);
  assert.match(source, /disabled=\{Boolean\(standardNavigationPending\)\}/);
  assert.match(source, /title="FIND GAME"[\s\S]*?disabled=\{state\.lobbyWaiting\}/);
  assert.match(source,
    /leaveStandard\(state\.lobbyWaiting \? 'standard' : 'online'\)/);
  assert.match(main, /command\.action == "cancel-standard"[\s\S]*?handleMainMenuResult\(state, state\.mainMenu\.back\(\)\)/);
});

test('Back controls use the compact bottom-left footer treatment', () => {
  const source = readFileSync(
    new URL('../src/main.jsx', import.meta.url), 'utf8');
  const css = readFileSync(new URL('../src/styles.css', import.meta.url), 'utf8');
  assert.match(source, /function Back\(\{onClick, disabled = false\}\)/);
  assert.equal((source.match(/>BACK<\/Action>/g) || []).length, 1);
  assert.match(css, /\.footer-actions \{[^}]*justify-content:flex-start;/);
  assert.match(css, /\.back-action \{[^}]*min-height:38px;[^}]*width:118px;/);
});

test('React branding uses the canonical Basilisk logo asset', () => {
  const source = readFileSync(
    new URL('../src/main.jsx', import.meta.url), 'utf8');
  assert.match(source,
    /import basiliskLogo from '\.\.\/\.\.\/\.\.\/assets\/ui\/objective-basilisk\.svg\?inline'/);
  assert.match(source, /<img src=\{basiliskLogo\}/);
  assert.doesNotMatch(source, /♜/);
});

test('Settings logout waits for authoritative completion and returns to Main Menu', () => {
  const bridge = createWasmBridge();
  bridge.action('logout');
  assert.equal(window.lastEvent.detail.action, 'logout');
  assert.equal(logoutCompleted(true, {
    view: 'authentication', authenticated: false, authWaiting: true,
  }), false);
  assert.equal(logoutCompleted(true, {
    view: 'authentication', authenticated: false, authWaiting: false,
  }), true);
  assert.equal(logoutCompleted(false, {
    view: 'authentication', authenticated: false, authWaiting: false,
  }), false);
});

test('Main Menu calling card uses authoritative public profile and cosmetics', () => {
  const source = readFileSync(
    new URL('../src/main.jsx', import.meta.url), 'utf8');
  const nativeState = readFileSync(
    new URL('../../../src/main.cpp', import.meta.url), 'utf8');
  const css = readFileSync(new URL('../src/styles.css', import.meta.url), 'utf8');
  assert.match(source, /function PlayerCallingCard\(\{username, trophies, callingCard, emblem/);
  assert.match(source,
    /screen === 'main' && authenticated && <aside className="main-player-card">/);
  assert.match(source, /callingCard=\{state\.callingCard\}/);
  assert.match(source, /emblem=\{state\.emblem\}/);
  assert.doesNotMatch(source,
    /PlayerCallingCard[\s\S]{0,700}(email|accountId|sessionToken)/);
  assert.match(css,
    /\.main-player-card \{[^}]*position:fixed;[^}]*top:24px;[^}]*right:24px;/);
  assert.match(css,
    /\.main-player-card \{[^}]*width:315px;[^}]*max-width:calc\(100vw - 48px\);[^}]*overflow:hidden;[^}]*contain:layout paint;/);
  assert.match(css,
    /\.compact \.emblem-slot \{[^}]*box-sizing:border-box;[^}]*flex-basis:54px;/);
  assert.match(css,
    /\.compact \.calling-card-art \{[^}]*flex:1 1 0;[^}]*width:auto;[^}]*max-width:none;/);
  assert.match(source, /defaultCard = cards\[0\]/);
  assert.match(source, /defaultEmblem = emblems\[0\]/);
  assert.match(nativeState, /,\\"callingCard\\":\\"/);
  assert.match(nativeState, /confirmedCosmeticLoadout->callingCardId\.value/);
  assert.match(nativeState, /,\\"emblem\\":\\"/);
  assert.match(nativeState, /confirmedCosmeticLoadout->emblemId\.value/);
});

test('Cosmetics mirrors the SDL card preview and galleries', () => {
  const source = readFileSync(
    new URL('../src/main.jsx', import.meta.url), 'utf8');
  const vite = readFileSync(
    new URL('../vite.config.js', import.meta.url), 'utf8');
  assert.match(source, /<h3>YOUR CARD<\/h3>/);
  assert.match(source, /<h3>CALLING CARDS<\/h3>/);
  assert.match(source, /<h3>EMBLEMS<\/h3>/);
  assert.match(source, /state\.callingCard === option\.id && <small>EQUIPPED<\/small>/);
  assert.match(source, /state\.emblem === option\.id && <small>EQUIPPED<\/small>/);
  assert.match(source,
    /bridge\.action\('cosmetic', card, emblem\)[\s\S]*?>EQUIP<\/Action>/);
  assert.match(source,
    /setCard\(state\.callingCard \|\| defaultCard\.id\)[\s\S]*?setEmblem\(state\.emblem \|\| defaultEmblem\.id\)/);
  assert.match(vite, /base: '\/react\/'/);
});

test('calling-card presentation keeps authored geometry without a frame outline', () => {
  const css = readFileSync(new URL('../src/styles.css', import.meta.url), 'utf8');
  assert.match(css, /\.player-calling-card \{[^}]*align-items:center;/);
  assert.doesNotMatch(css, /\.player-calling-card \{[^}]*align-items:stretch;/);
  assert.match(css,
    /\.calling-card-art \{[^}]*width:400px;[^}]*aspect-ratio:400\/75;[^}]*border-radius:8px;/);
  assert.doesNotMatch(css, /\.calling-card-art::after/);
  assert.doesNotMatch(css, /\.calling-card-art \{[^}]*border:1px solid/);
  assert.match(css,
    /\.cosmetics-panel \.calling-card-art \{[^}]*background:transparent;/);
  assert.match(css, /\.cosmetic-tile \{[^}]*border:1px solid var\(--line\);/);
  assert.match(css,
    /\.cosmetic-tile:hover,\.cosmetic-tile:focus-visible,\.cosmetic-tile\.selected \{[^}]*border-color:var\(--gold\);/);
});

test('shared actions have stable hover and keyboard-focus treatment', () => {
  const css = readFileSync(new URL('../src/styles.css', import.meta.url), 'utf8');
  assert.match(css, /--gold-hover:#f3cd72/);
  assert.match(css, /\.action \{[^}]*transition:[^}]*180ms/);
  assert.match(css,
    /\.action:hover:not\(:disabled\),\.action:focus-visible:not\(:disabled\) \{/);
  assert.match(css, /\.action:disabled \{[^}]*cursor:not-allowed;/);
  assert.doesNotMatch(css, /\.action:hover(?!:not\(:disabled\))/);
});
