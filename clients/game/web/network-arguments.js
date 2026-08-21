// Browser-only launch bridge: use the same C++ --connect/--token path as native.
if (typeof window !== 'undefined') {
    const parameters = new URLSearchParams(window.location.search);
    const connect = parameters.get('connect');
    const token = parameters.get('token');
    if (connect !== null && token !== null) {
        Module['arguments'] = (Module['arguments'] || []).concat([
            '--connect', connect, '--token', token
        ]);
    }
}
