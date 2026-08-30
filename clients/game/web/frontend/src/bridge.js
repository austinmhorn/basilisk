export const reactPrototypeRequested = (search = window.location.search) =>
  new URLSearchParams(search).get('ui') === 'react';

export function applyBrowserView(view, targetDocument = document) {
  const playing = view === 'gameplay';
  targetDocument.body.classList.toggle('react-gameplay', playing);
  if (playing)
    targetDocument.getElementById('canvas')?.focus({preventScroll: true});
}

export function screenAfterAuthoritativeState(screen, state, previousExitRevision = 0) {
  if ((state.gameplayExitRevision ?? 0) > previousExitRevision) return 'main';
  if (screen === 'auth' && state.view === 'menu' && state.authenticated)
    return 'main';
  return screen;
}

export const logoutCompleted = (requested, state) => requested &&
  state.view === 'authentication' && !state.authenticated &&
  !state.authWaiting;

export function withSandboxHunterCount(config, hunterCount) {
  const hunters = Number(hunterCount);
  return {...config, hunters, humans: Math.min(config.humans, hunters),
    caves: Math.max(config.caves, 30, hunters * 10)};
}

export const encodeSandboxConfig = (config) => [config.hunters, config.humans,
  config.caves, config.jackals, config.cadence, config.starting, config.max,
  config.difficulty, config.behavior].join(',');

export function createWasmBridge() {
  const dispatch = (action, args = []) => window.dispatchEvent(new CustomEvent(
    'basilisk:action', {detail: {action, arguments: args.map(String)}}));
  return {
    action(action, ...args) {
      dispatch(action, args);
    },
    requestState() {
      dispatch('__request-state');
    },
  };
}
