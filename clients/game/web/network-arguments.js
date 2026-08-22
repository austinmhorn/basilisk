// Browser-only launch bridge: use the same C++ --connect/--token path as native.
if (typeof window !== 'undefined') {
    const parameters = new URLSearchParams(window.location.search);
    const localHostnames = new Set(['localhost', '127.0.0.1', '::1', '[::1]']);
    const localDevelopment = window.location.protocol !== 'https:' ||
        localHostnames.has(window.location.hostname) ||
        window.location.hostname.endsWith('.localhost');
    const connect = parameters.has('connect')
        ? parameters.get('connect')
        : (localDevelopment ? null : 'wss://game.xiivestudio.com');
    const token = parameters.get('token');
    if (connect !== null) {
        const argumentsToAppend = ['--connect', connect];
        if (token !== null) {
            argumentsToAppend.push('--token', token);
        }
        Module['arguments'] = (Module['arguments'] || []).concat(
            argumentsToAppend
        );
    }
}
