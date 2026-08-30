import React, {useEffect, useMemo, useRef, useState} from 'react';
import {createRoot} from 'react-dom/client';
import {applyBrowserView, createWasmBridge, encodeSandboxConfig,
  reactPrototypeRequested,
  logoutCompleted, screenAfterAuthoritativeState,
  withSandboxHunterCount} from './bridge.js';
import basiliskLogo from '../../../assets/ui/objective-basilisk.svg?inline';
import arrowRightBlack from '../../../assets/calling-cards/arrow-right-black.svg';
import arrowRightWhite from '../../../assets/calling-cards/arrow-right-white.svg';
import diamondsFlagBlack from '../../../assets/calling-cards/diamonds-flag-black.svg';
import diamondsFlagWhite from '../../../assets/calling-cards/diamonds-flag-white.svg';
import honeycombFlagBlack from '../../../assets/calling-cards/honeycomb-flag-black.svg';
import honeycombFlagWhite from '../../../assets/calling-cards/honeycomb-flag-white.svg';
import slantedRectanglesBlack from '../../../assets/calling-cards/slanted-rectangles-black.svg';
import slantedRectanglesWhite from '../../../assets/calling-cards/slanted-rectangles-white.svg';
import circleBlack from '../../../assets/emblems/circle-black.svg';
import circleGreen from '../../../assets/emblems/circle-green.svg';
import roundedSquareBlack from '../../../assets/emblems/rounded-square-black.svg';
import roundedSquareGreen from '../../../assets/emblems/rounded-square-green.svg';
import './styles.css';

const bridge = createWasmBridge();
const defaults = {hunters: 2, humans: 2, caves: 30, jackals: 2,
  cadence: 5, starting: 3, max: 5, difficulty: 1, behavior: 0};
const difficulties = ['EASY', 'MEDIUM', 'HARD'];
const behaviors = ['BALANCED', 'EXPLORER', 'AGGRESSIVE', 'OBJECTIVE',
  'SURVIVALIST', 'OPPORTUNIST', 'RANDOM'];
const cards = [
  {id: 'arrow-right-black', name: 'Arrow Right · Black', asset: arrowRightBlack},
  {id: 'arrow-right-white', name: 'Arrow Right · White', asset: arrowRightWhite},
  {id: 'diamonds-flag-black', name: 'Diamonds Flag · Black', asset: diamondsFlagBlack},
  {id: 'diamonds-flag-white', name: 'Diamonds Flag · White', asset: diamondsFlagWhite},
  {id: 'honeycomb-flag-black', name: 'Honeycomb Flag · Black', asset: honeycombFlagBlack},
  {id: 'honeycomb-flag-white', name: 'Honeycomb Flag · White', asset: honeycombFlagWhite},
  {id: 'slanted-rectangles-black', name: 'Slanted Rectangles · Black', asset: slantedRectanglesBlack},
  {id: 'slanted-rectangles-white', name: 'Slanted Rectangles · White', asset: slantedRectanglesWhite},
];
const emblems = [
  {id: 'circle-black', name: 'Circle · Black', asset: circleBlack},
  {id: 'circle-green', name: 'Circle · Green', asset: circleGreen},
  {id: 'rounded-square-black', name: 'Rounded Square · Black', asset: roundedSquareBlack},
  {id: 'rounded-square-green', name: 'Rounded Square · Green', asset: roundedSquareGreen},
];
const defaultCard = cards[0];
const defaultEmblem = emblems[0];
const cosmeticOption = (options, id, fallback) =>
  options.find(option => option.id === id) ?? fallback;

function Action({children, onClick, secondary = false, disabled = false,
  className = ''}) {
  const classes = ['action', secondary ? 'secondary' : '', className]
    .filter(Boolean).join(' ');
  return <button className={classes}
    disabled={disabled} onClick={onClick}>{children}</button>;
}
function Back({onClick, disabled = false}) {
  return <div className="footer-actions"><Action secondary
    className="back-action" disabled={disabled}
    onClick={onClick}>BACK</Action></div>;
}
function ModeCard({title, description, onClick, disabled = false}) {
  return <button className="mode-card" disabled={disabled} onClick={onClick}>
    <strong>{title}</strong><span>{description}</span>
  </button>;
}
function Field({label, ...props}) {
  return <label><span>{label}</span><input {...props}/></label>;
}
function Brand() {
  return <header><div className="sigil"><img src={basiliskLogo}
    alt="Basilisk"/></div><h1>BASILISK</h1>
    <p>ENTER THE CAVERNS</p></header>;
}
function PlayerCallingCard({username, trophies, callingCard, emblem, compact = false}) {
  const card = cosmeticOption(cards, callingCard, defaultCard);
  const badge = cosmeticOption(emblems, emblem, defaultEmblem);
  return <div className={`player-calling-card${compact ? ' compact' : ''}`}
    aria-label="Player calling card">
    <div className="emblem-slot"><img src={badge.asset} alt={badge.name}/></div>
    <div className="calling-card-art">
      <img src={card.asset} alt={card.name}/>
      <div className="calling-card-nameplate">
        <strong>{username}</strong>
        <span><b>TROPHIES</b>{trophies}</span>
      </div>
    </div>
  </div>;
}
function Config({config, setConfig, online}) {
  const set = (key, value) => setConfig({...config, [key]: Number(value)});
  return <div className="config">
    <Field label="HUNTERS" type="number" min="2" max="6" value={config.hunters}
      onChange={e => setConfig(withSandboxHunterCount(config, e.target.value))}/>
    {online && <Field label="HUMAN PLAYERS" type="number" min="2"
      max={config.hunters} value={config.humans}
      onChange={e => set('humans', e.target.value)}/>}
    <Field label="CAVES" type="number" min="30" max="60" step="10"
      value={config.caves} onChange={e => set('caves', e.target.value)}/>
    <Field label="JACKALS" type="number" min="0" max="12"
      value={config.jackals} onChange={e => set('jackals', e.target.value)}/>
    <label><span>ARROW FREQUENCY</span><select value={config.cadence}
      onChange={e => set('cadence', e.target.value)}>
      <option value="0">OFF</option><option value="3">FREQUENT</option>
      <option value="5">NORMAL</option><option value="8">RARE</option>
    </select></label>
    <Field label="STARTING ARROWS" type="number" min="0" max={config.max}
      value={config.starting} onChange={e => set('starting', e.target.value)}/>
    <Field label="MAX ARROWS" type="number" min="0" max="10"
      value={config.max} onChange={e => set('max', e.target.value)}/>
    <label><span>DIFFICULTY</span><select value={config.difficulty}
      onChange={e => set('difficulty', e.target.value)}>{difficulties.map((x, i) =>
      <option value={i} key={x}>{x}</option>)}</select></label>
    <label><span>BEHAVIOR</span><select value={config.behavior}
      onChange={e => set('behavior', e.target.value)}>{behaviors.map((x, i) =>
      <option value={i} key={x}>{x}</option>)}</select></label>
  </div>;
}

function AiOptions({config, setConfig}) {
  const set = (key, value) => setConfig({...config, [key]: Number(value)});
  return <div className="config ai-options">
    <label><span>AI DIFFICULTY</span><select value={config.difficulty}
      onChange={e => set('difficulty', e.target.value)}>{difficulties.map((x, i) =>
      <option value={i} key={x}>{x}</option>)}</select></label>
    <label><span>AI BEHAVIOR</span><select value={config.behavior}
      onChange={e => set('behavior', e.target.value)}>{behaviors.map((x, i) =>
      <option value={i} key={x}>{x}</option>)}</select></label>
  </div>;
}

function App() {
  const [state, setState] = useState({view: 'menu', authenticated: false,
    leaderboard: [], roster: []});
  const [screen, setScreen] = useState('main');
  const [authMode, setAuthMode] = useState('signin');
  const [form, setForm] = useState({email: '', password: '', username: ''});
  const [code, setCode] = useState('');
  const [config, setConfig] = useState(defaults);
  const [card, setCard] = useState(defaultCard.id);
  const [emblem, setEmblem] = useState(defaultEmblem.id);
  const [logoutRequested, setLogoutRequested] = useState(false);
  const [standardNavigationPending, setStandardNavigationPending] = useState('');
  const handledGameplayExitRevision = useRef(0);
  const updateFormField = (field) => (event) => {
    const value = event.currentTarget.value;
    setForm(current => ({...current, [field]: value}));
  };

  useEffect(() => {
    const update = (event) => {
      const nextState = event.detail;
      const previousExitRevision = handledGameplayExitRevision.current;
      setScreen(current => screenAfterAuthoritativeState(
        current, nextState, previousExitRevision));
      handledGameplayExitRevision.current = nextState.gameplayExitRevision ?? 0;
      setState(nextState);
    };
    window.addEventListener('basilisk:state', update);
    bridge.requestState();
    return () => window.removeEventListener('basilisk:state', update);
  }, []);
  useEffect(() => {
    applyBrowserView(state.view);
  }, [state.view]);
  useEffect(() => {
    if (state.roster?.length) setScreen('lobby');
  }, [state.roster]);
  useEffect(() => {
    if (!logoutCompleted(logoutRequested, state)) return;
    setLogoutRequested(false);
    setScreen('main');
  }, [logoutRequested, state.view, state.authenticated, state.authWaiting]);
  useEffect(() => {
    if (!standardNavigationPending || state.lobbyWaiting) return;
    setScreen(standardNavigationPending);
    setStandardNavigationPending('');
  }, [standardNavigationPending, state.lobbyWaiting]);
  useEffect(() => {
    if (!state.authenticated) return;
    setCard(state.callingCard || defaultCard.id);
    setEmblem(state.emblem || defaultEmblem.id);
  }, [state.authenticated, state.callingCard, state.emblem]);

  const authenticated = state.authenticated;
  const submitAuth = (event) => {
    event.preventDefault();
    bridge.action('authenticate', form.email, form.password, authMode, form.username);
  };
  const leaveStandard = (destination) => {
    if (standardNavigationPending) return;
    if (!state.lobbyWaiting) {
      setScreen(destination);
      return;
    }
    setStandardNavigationPending(destination);
    bridge.action('cancel-standard');
  };
  const title = useMemo(() => screen.replaceAll('-', ' ').toUpperCase(), [screen]);
  if (state.view === 'gameplay') return null;

  let content;
  if (screen === 'auth') content = <form onSubmit={submitAuth} className="panel form">
    <h2>{authMode === 'register' ? 'CREATE ACCOUNT' : 'SIGN IN'}</h2>
    <Field label="EMAIL" type="text" value={form.email}
      onInput={updateFormField('email')}/>
    <Field label="PASSWORD" type="text" className="password-input"
      value={form.password}
      onInput={updateFormField('password')}/>
    {authMode === 'register' && <Field label="USERNAME" required value={form.username}
      onInput={updateFormField('username')}/>} 
    {state.authError && <p className="error">{state.authError}</p>}
    <Action disabled={state.authWaiting}>{state.authWaiting ? 'CONNECTING…' :
      authMode === 'register' ? 'CREATE ACCOUNT' : 'SIGN IN'}</Action>
    <button type="button" className="link" onClick={() =>
      setAuthMode(authMode === 'register' ? 'signin' : 'register')}>
      {authMode === 'register' ? 'Already have an account?' : 'Create an account'}
    </button><Back onClick={() => setScreen('main')}/>
  </form>;
  else if (screen === 'online') content = <section className="panel wide"><h2>PLAY ONLINE</h2>
    {!authenticated && <Action onClick={() => { bridge.action('open-online'); setScreen('auth'); }}>SIGN IN</Action>}
    <div className="mode-cards">
      <ModeCard title="STANDARD"
        description="Enter a classic online hunt against one rival hunter."
        disabled={!authenticated} onClick={() => setScreen('standard')}/>
      <ModeCard title="SANDBOX"
        description="Create or join a custom online hunt for up to six hunters."
        disabled={!authenticated} onClick={() => setScreen('sandbox-online-mode')}/>
    </div>
    <Back onClick={() => setScreen('main')}/></section>;
  else if (screen === 'standard') content = <section className="panel wide"><h2>STANDARD</h2>
    <div className="mode-cards standard-actions">
      <ModeCard title="FIND GAME"
        description="Search for an available hunter and start a standard match."
        disabled={state.lobbyWaiting}
        onClick={() => bridge.action('find-match')}/>
      <ModeCard title="HOST GAME"
        description="Create a private standard lobby and share its code."
        disabled={state.lobbyWaiting}
        onClick={() => {
          setScreen('standard-host');
          bridge.action('host-standard');
        }}/>
      <ModeCard title="JOIN GAME"
        description="Join a private standard lobby using a lobby code."
        disabled={state.lobbyWaiting}
        onClick={() => setScreen('standard-join')}/>
    </div>
    {state.lobbyWaiting && <p className="status">SEARCHING FOR A MATCH…</p>}
    {state.lobbyError && <p className="error">{state.lobbyError}</p>}
    <Back disabled={Boolean(standardNavigationPending)}
      onClick={() => leaveStandard(state.lobbyWaiting ? 'standard' : 'online')}/></section>;
  else if (screen === 'standard-host') content = <section className="panel">
    <h2>HOST GAME</h2>
    <div className="lobby-code">{state.lobbyCode ||
      (state.lobbyWaiting ? 'CREATING LOBBY…' : 'WAITING FOR LOBBY CODE')}</div>
    {state.lobbyError && <p className="error">{state.lobbyError}</p>}
    <Back disabled={Boolean(standardNavigationPending)}
      onClick={() => leaveStandard('standard')}/></section>;
  else if (screen === 'standard-join') content = <section className="panel">
    <h2>JOIN GAME</h2>
    <Field label="LOBBY CODE" value={code} onChange={e => setCode(e.target.value)}/>
    <Action onClick={() => bridge.action('join-standard', code)}>JOIN GAME</Action>
    {state.lobbyError && <p className="error">{state.lobbyError}</p>}
    <Back disabled={Boolean(standardNavigationPending)}
      onClick={() => leaveStandard('standard')}/></section>;
  else if (screen === 'sandbox-online-mode') content = <section className="panel wide">
    <h2>SANDBOX</h2><div className="mode-cards">
      <ModeCard title="HOST SANDBOX" description="Configure and host a custom online match."
        onClick={() => setScreen('sandbox-online-host')}/>
      <ModeCard title="JOIN SANDBOX" description="Join a host using their Sandbox lobby code."
        onClick={() => setScreen('sandbox-online-join')}/>
    </div><Back onClick={() => setScreen('online')}/></section>;
  else if (screen === 'sandbox-online-host') content = <section className="panel wide">
    <h2>HOST SANDBOX</h2><Config config={config} setConfig={setConfig} online/>
    <Action onClick={() => bridge.action('host-sandbox', encodeSandboxConfig(config))}>CREATE LOBBY</Action>
    {state.sandboxError && <p className="error">{state.sandboxError}</p>}
    <Back onClick={() => setScreen('sandbox-online-mode')}/></section>;
  else if (screen === 'sandbox-online-join') content = <section className="panel">
    <h2>JOIN SANDBOX</h2>
    <Field label="SANDBOX CODE" value={code} onChange={e => setCode(e.target.value)}/>
    <Action onClick={() => bridge.action('join-sandbox', code)}>JOIN LOBBY</Action>
    <Back onClick={() => setScreen('sandbox-online-mode')}/></section>;
  else if (screen === 'lobby') content = <section className="panel wide"><h2>LOBBY</h2>
    <div className="lobby-code">{state.lobbyCode || 'WAITING FOR CODE'}</div>
    <div className="roster">{(state.roster || []).map(slot => <div key={slot.slot}>
      <b>{slot.name || (slot.occupied ? `PLAYER ${slot.slot}` : 'WAITING FOR PLAYER')}</b>
      <span>{slot.ready ? 'READY' : slot.occupied ? 'NOT READY' : ''}</span></div>)}</div>
    {state.lobbyError && <p className="error">{state.lobbyError}</p>}
    <Action onClick={() => bridge.action('toggle-ready')}>READY / NOT READY</Action>
    <Action disabled={!state.sandboxLaunchEligible}
      onClick={() => bridge.action('start-sandbox-lobby')}>START MATCH</Action>
    <Back onClick={() => { bridge.action('leave-lobby'); setScreen('sandbox-online-mode'); }}/>
  </section>;
  else if (screen === 'ai-mode') content = <section className="panel wide"><h2>PLAY AI</h2>
    <div className="mode-cards">
      <ModeCard title="STANDARD" description="Face one AI hunter in a standard match."
        onClick={() => setScreen('ai-standard')}/>
      <ModeCard title="SANDBOX" description="Create a custom match with up to five AI hunters."
        onClick={() => setScreen('ai-sandbox')}/>
    </div><Back onClick={() => setScreen('main')}/></section>;
  else if (screen === 'ai-standard') content = <section className="panel wide">
    <h2>STANDARD</h2><AiOptions config={config} setConfig={setConfig}/>
    <Action onClick={() => bridge.action('start-ai', config.difficulty,
      config.behavior)}>START GAME</Action>
    <Back onClick={() => setScreen('ai-mode')}/></section>;
  else if (screen === 'ai-sandbox') content = <section className="panel wide">
    <h2>SANDBOX</h2>
    <Config config={{...config, humans: 1}} setConfig={setConfig}/>
    <Action onClick={() => bridge.action('start-local-sandbox',
      encodeSandboxConfig({...config, humans: 1}))}>START GAME</Action>
    <Back onClick={() => setScreen('ai-mode')}/></section>;
  else if (screen === 'leaderboard') content = <section className="panel wide"><h2>LEADERBOARD</h2>
    <div className="leaderboard">{state.leaderboard?.length ? state.leaderboard.map(row =>
      <div key={`${row.rank}-${row.username}`}><span>{row.rank}</span><b>{row.username}</b><span>{row.trophies}</span></div>) : <p>No rankings yet.</p>}</div>
    <Back onClick={() => setScreen('main')}/></section>;
  else if (screen === 'cosmetics') content = <section className="panel wide cosmetics-panel"><h2>COSMETICS</h2>
    <h3>YOUR CARD</h3>
    <PlayerCallingCard username={state.username || 'PLAYER PROFILE'}
      trophies={state.trophies ?? 0} callingCard={card} emblem={emblem}/>
    <h3>CALLING CARDS</h3>
    <div className="cosmetic-grid calling-card-grid">{cards.map(option =>
      <button type="button" key={option.id}
        className={`cosmetic-tile${card === option.id ? ' selected' : ''}`}
        aria-pressed={card === option.id} onClick={() => setCard(option.id)}>
        <img src={option.asset} alt=""/><span>{option.name}</span>
        {state.callingCard === option.id && <small>EQUIPPED</small>}
      </button>)}</div>
    <h3>EMBLEMS</h3>
    <div className="cosmetic-grid emblem-grid">{emblems.map(option =>
      <button type="button" key={option.id}
        className={`cosmetic-tile${emblem === option.id ? ' selected' : ''}`}
        aria-pressed={emblem === option.id} onClick={() => setEmblem(option.id)}>
        <img src={option.asset} alt=""/><span>{option.name}</span>
        {state.emblem === option.id && <small>EQUIPPED</small>}
      </button>)}</div>
    <Action disabled={card === state.callingCard && emblem === state.emblem}
      onClick={() => bridge.action('cosmetic', card, emblem)}>EQUIP</Action>
    <Back onClick={() => setScreen('main')}/></section>;
  else if (screen === 'settings') content = <section className="panel"><h2>SETTINGS</h2>
    <p>Browser settings will continue to use the existing C++ preferences.</p>
    {authenticated && <Action onClick={() => {
      setLogoutRequested(true);
      bridge.action('logout');
    }}>LOG OUT</Action>}
    <Back onClick={() => setScreen('main')}/></section>;
  else content = <section className="panel"><h2>{state.username ? `WELCOME, ${state.username}` : 'MAIN MENU'}</h2>
    <Action onClick={() => setScreen('online')}>PLAY ONLINE</Action>
    <Action onClick={() => setScreen('ai-mode')}>PLAY AI</Action>
    <Action onClick={() => { bridge.action('leaderboard'); setScreen('leaderboard'); }}>LEADERBOARD</Action>
    <Action onClick={() => setScreen('cosmetics')}>COSMETICS</Action>
    <Action onClick={() => setScreen('settings')}>SETTINGS</Action></section>;

  return <main>
    {screen === 'main' && authenticated && <aside className="main-player-card">
      <PlayerCallingCard compact username={state.username}
        trophies={state.trophies ?? 0} callingCard={state.callingCard}
        emblem={state.emblem}/>
    </aside>}
    <Brand/><div className="screen-title">{title === 'MAIN' ? '' : title}</div>
    {content}
  </main>;
}

if (reactPrototypeRequested()) {
  document.body.classList.add('react-ui');
  createRoot(document.getElementById('react-root')).render(<App/>);
}
