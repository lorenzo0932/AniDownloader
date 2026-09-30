import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { api, posterUrl, BASE } from './api.js';

function jsonResponse(data, status = 200, statusText = '') {
  return {
    ok: status >= 200 && status < 300,
    status,
    statusText,
    text: () => Promise.resolve(JSON.stringify(data)),
  };
}

function textResponse(text, status = 500, statusText = '') {
  return {
    ok: status >= 200 && status < 300,
    status,
    statusText,
    text: () => Promise.resolve(text),
  };
}

describe('api.series', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', vi.fn());
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('list: dispatch GET senza query', async () => {
    fetch.mockResolvedValue(jsonResponse([]));
    const result = await api.series.list();
    expect(fetch).toHaveBeenCalledWith(
      BASE + '/api/series',
      expect.objectContaining({ method: 'GET' }),
    );
    expect(result).toEqual([]);
  });

  it('list: dispatch GET con sort+dir in query', async () => {
    fetch.mockResolvedValue(jsonResponse([]));
    await api.series.list({ sort: 'name', dir: 'desc' });
    expect(fetch).toHaveBeenCalledWith(
      BASE + '/api/series?sort=name&dir=desc',
      expect.objectContaining({ method: 'GET' }),
    );
  });

  it('fetchName: dispatch POST con url nel body', async () => {
    fetch.mockResolvedValue(jsonResponse({ name: 'Serie' }));
    const result = await api.series.fetchName('https://example.com');
    expect(fetch).toHaveBeenCalledWith(
      BASE + '/api/series/fetch-name',
      expect.objectContaining({ method: 'POST', body: JSON.stringify({ url: 'https://example.com' }) }),
    );
    expect(result.name).toBe('Serie');
  });

  it('add: dispatch POST con item nel body', async () => {
    fetch.mockResolvedValue(jsonResponse({}));
    const item = { name: 'S' };
    await api.series.add(item);
    expect(fetch).toHaveBeenCalledWith(
      BASE + '/api/series',
      expect.objectContaining({ method: 'POST', body: JSON.stringify(item) }),
    );
  });

  it('update: dispatch PUT con _file_index nel path', async () => {
    fetch.mockResolvedValue(jsonResponse({}));
    const item = { name: 'S2' };
    await api.series.update(7, item);
    expect(fetch).toHaveBeenCalledWith(
      BASE + '/api/series/7',
      expect.objectContaining({ method: 'PUT', body: JSON.stringify(item) }),
    );
  });

  it('remove: dispatch DELETE con _file_index nel path', async () => {
    fetch.mockResolvedValue(jsonResponse({}));
    await api.series.remove(3);
    expect(fetch).toHaveBeenCalledWith(
      BASE + '/api/series/3',
      expect.objectContaining({ method: 'DELETE' }),
    );
  });

  it('errore {success, error} su non-2xx: throw ApiError con status', async () => {
    fetch.mockResolvedValue(jsonResponse({ success: false, error: 'boom' }, 400));
    await expect(api.series.list()).rejects.toMatchObject({
      message: 'boom',
      status: 400,
    });
  });

  it('errore JSON senza error/code su non-2xx: fallback statusText', async () => {
    fetch.mockResolvedValue(jsonResponse({ success: false }, 500, 'Internal Server Error'));
    await expect(api.series.list()).rejects.toMatchObject({
      message: 'Internal Server Error',
      status: 500,
    });
  });

  it('body non-JSON su errore: ApiError col testo grezzo', async () => {
    fetch.mockResolvedValue(textResponse('gateway timeout', 502, 'Bad Gateway'));
    await expect(api.series.list()).rejects.toMatchObject({
      message: 'gateway timeout',
      status: 502,
    });
  });

  it('body non-JSON vuoto su errore: fallback statusText', async () => {
    fetch.mockResolvedValue(textResponse('', 503, 'Service Unavailable'));
    await expect(api.series.list()).rejects.toMatchObject({
      message: 'Service Unavailable',
      status: 503,
    });
  });

  it('risposta 2xx non-JSON: throw ApiError', async () => {
    fetch.mockResolvedValue(textResponse('<html>', 200, 'OK'));
    await expect(api.series.list()).rejects.toMatchObject({
      message: '<html>',
      status: 200,
    });
  });
});

describe('api.config / download / logs / status', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', vi.fn());
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('config.get: GET /api/config', async () => {
    fetch.mockResolvedValue(jsonResponse({}));
    await api.config.get();
    expect(fetch).toHaveBeenCalledWith(BASE + '/api/config', expect.objectContaining({ method: 'GET' }));
  });

  it('config.set: PUT /api/config con body', async () => {
    fetch.mockResolvedValue(jsonResponse({}));
    const data = { threads: 2 };
    await api.config.set(data);
    expect(fetch).toHaveBeenCalledWith(BASE + '/api/config', expect.objectContaining({ method: 'PUT', body: JSON.stringify(data) }));
  });

  it('download.start: POST /api/download/start con burst', async () => {
    fetch.mockResolvedValue(jsonResponse({}));
    await api.download.start(false);
    expect(fetch).toHaveBeenCalledWith(BASE + '/api/download/start', expect.objectContaining({ method: 'POST', body: JSON.stringify({ burst: false }) }));
  });

  it('download.stop: POST /api/download/stop senza body', async () => {
    fetch.mockResolvedValue(jsonResponse({}));
    await api.download.stop();
    expect(fetch).toHaveBeenCalledWith(BASE + '/api/download/stop', expect.objectContaining({ method: 'POST' }));
  });

  it('download.status: GET /api/download/status', async () => {
    fetch.mockResolvedValue(jsonResponse({}));
    await api.download.status();
    expect(fetch).toHaveBeenCalledWith(BASE + '/api/download/status', expect.objectContaining({ method: 'GET' }));
  });

  it('logs: GET /api/log?lines=N', async () => {
    fetch.mockResolvedValue(jsonResponse({}));
    await api.logs(42);
    expect(fetch).toHaveBeenCalledWith(BASE + '/api/log?lines=42', expect.objectContaining({ method: 'GET' }));
  });

  it('status: GET /api/status', async () => {
    fetch.mockResolvedValue(jsonResponse({}));
    await api.status();
    expect(fetch).toHaveBeenCalledWith(BASE + '/api/status', expect.objectContaining({ method: 'GET' }));
  });
});

describe('api.browse', () => {
  beforeEach(() => {
    vi.stubGlobal('fetch', vi.fn());
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('list: GET /api/browse con path encoded, default senza file', async () => {
    fetch.mockResolvedValue(jsonResponse({ entries: [] }));
    await api.browse.list('/home/u/Video Anime');
    expect(fetch).toHaveBeenCalledWith(
      BASE + '/api/browse?path=' + encodeURIComponent('/home/u/Video Anime'),
      expect.objectContaining({ method: 'GET' }),
    );
  });

  it('list: files=true aggiunge files=1', async () => {
    fetch.mockResolvedValue(jsonResponse({ entries: [] }));
    await api.browse.list('/media', true);
    expect(fetch).toHaveBeenCalledWith(
      BASE + '/api/browse?path=' + encodeURIComponent('/media') + '&files=1',
      expect.objectContaining({ method: 'GET' }),
    );
  });

  it('mounts: GET /api/browse/mounts', async () => {
    fetch.mockResolvedValue(jsonResponse({ mounts: [] }));
    await api.browse.mounts();
    expect(fetch).toHaveBeenCalledWith(BASE + '/api/browse/mounts', expect.objectContaining({ method: 'GET' }));
  });

  it('mkdir/touch: POST con parent+name nel body', async () => {
    fetch.mockResolvedValue(jsonResponse({ path: '/media/Nuova' }));
    await api.browse.mkdir('/media', 'Nuova');
    expect(fetch).toHaveBeenCalledWith(
      BASE + '/api/browse/mkdir',
      expect.objectContaining({ method: 'POST', body: JSON.stringify({ parent: '/media', name: 'Nuova' }) }),
    );
    await api.browse.touch('/media', 'ep01.mkv');
    expect(fetch).toHaveBeenCalledWith(
      BASE + '/api/browse/touch',
      expect.objectContaining({ method: 'POST', body: JSON.stringify({ parent: '/media', name: 'ep01.mkv' }) }),
    );
  });

  it('remove: DELETE con path+recursive nel body', async () => {
    fetch.mockResolvedValue(jsonResponse({ success: true }));
    await api.browse.remove('/media/Alpha', true);
    expect(fetch).toHaveBeenCalledWith(
      BASE + '/api/browse',
      expect.objectContaining({ method: 'DELETE', body: JSON.stringify({ path: '/media/Alpha', recursive: true }) }),
    );
  });

  it('remove 409: ApiError con status e count del server', async () => {
    fetch.mockResolvedValue(
      jsonResponse({ success: false, error: "La cartella non e' vuota", code: 409, count: 12 }, 409),
    );
    const err = await api.browse.remove('/media/Alpha', false).catch((e) => e);
    expect(err.status).toBe(409);
    expect(err.count).toBe(12);
  });
});

describe('posterUrl', () => {
  it('path vuoto: ritorna stringa vuota', () => {
    expect(posterUrl('')).toBe('');
    expect(posterUrl(null)).toBe('');
    expect(posterUrl(undefined)).toBe('');
  });

  it('path valorizzato: BASE + /api/poster con path encoded', () => {
    expect(posterUrl('/a/b c.png')).toBe(BASE + '/api/poster?path=' + encodeURIComponent('/a/b c.png'));
  });
});
