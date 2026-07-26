export const BASE = import.meta.env.DEV ? '' : 'http://127.0.0.1:8989';

export function posterUrl(path) {
  if (!path) return '';
  return BASE + '/api/poster?path=' + encodeURIComponent(path);
}

async function request(method, path, body) {
  const headers = { 'Content-Type': 'application/json' };
  const opts = { method, headers };
  if (body !== undefined) opts.body = JSON.stringify(body);
  const res = await fetch(BASE + path, opts);
  const text = await res.text();
  try {
    const data = JSON.parse(text);
    if (!res.ok) throw new ApiError(data.error || data.code || res.statusText, res.status);
    return data;
  } catch (e) {
    if (e instanceof ApiError) throw e;
    throw new ApiError(text || res.statusText, res.status);
  }
}

class ApiError extends Error {
  constructor(msg, status) {
    super(msg);
    this.status = status;
  }
}

export const api = {
  series: {
    list: (opts = {}) => {
      let path = '/api/series';
      const params = [];
      if (opts.sort) params.push('sort=' + encodeURIComponent(opts.sort));
      if (opts.dir) params.push('dir=' + encodeURIComponent(opts.dir));
      if (params.length) path += '?' + params.join('&');
      return request('GET', path);
    },
    add: (item) => request('POST', '/api/series', item),
    update: (index, item) => request('PUT', `/api/series/${index}`, item),
    remove: (index) => request('DELETE', `/api/series/${index}`),
  },

  config: {
    get: () => request('GET', '/api/config'),
    set: (data) => request('PUT', '/api/config', data),
  },

  download: {
    start: (burst = true) => request('POST', '/api/download/start', { burst }),
    stop: () => request('POST', '/api/download/stop'),
    status: () => request('GET', '/api/download/status'),
  },

  logs: (lines = 100) => request('GET', `/api/log?lines=${lines}`),

  status: () => request('GET', '/api/status'),

  sse: () => new EventSource(BASE + '/api/download/events'),
};
