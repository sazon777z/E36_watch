const CACHE_NAME = 'bmw-e36-obc-v5';
const ASSETS = [
  './',
  './index.html',
  './ble_app.html',
  './manifest.json',
  './manifest.json?v=3',
  './icon-192.png',
  './icon-192.png?v=3',
  './icon-512.png',
  './icon-512.png?v=3'
];

self.addEventListener('install', (event) => {
  event.waitUntil(
    caches.open(CACHE_NAME).then((cache) => {
      return cache.addAll(ASSETS);
    })
  );
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys().then((keys) => {
      return Promise.all(
        keys.filter((key) => key !== CACHE_NAME).map((key) => caches.delete(key))
      );
    })
  );
  self.clients.claim();
});

// РЎС‚СЂР°С‚РµРіРёСЏ Network-First СЃ Р°РІС‚РѕРјР°С‚РёС‡РµСЃРєРёРј РѕР±РЅРѕРІР»РµРЅРёРµРј РєСЌС€Р°:
// РџСЂРё РЅР°Р»РёС‡РёРё РёРЅС‚РµСЂРЅРµС‚Р° СЃРєР°С‡РёРІР°РµС‚СЃСЏ СЃРІРµР¶Р°СЏ РІРµСЂСЃРёСЏ СЃ GitHub Pages Рё РєСЌС€РёСЂСѓРµС‚СЃСЏ.
// Р’ Р°РІС‚РѕРјРѕР±РёР»Рµ (РѕС„С„Р»Р°Р№РЅ) РјРѕРјРµРЅС‚Р°Р»СЊРЅРѕ РѕС‚РґР°РµС‚СЃСЏ СЃРѕС…СЂР°РЅРµРЅРЅР°СЏ РІРµСЂСЃРёСЏ РёР· РїР°РјСЏС‚Рё С‚РµР»РµС„РѕРЅР°.
self.addEventListener('fetch', (event) => {
  if (event.request.method !== 'GET') return;

  event.respondWith(
    fetch(event.request)
      .then((networkResponse) => {
        if (networkResponse && networkResponse.status === 200) {
          const responseToCache = networkResponse.clone();
          caches.open(CACHE_NAME).then((cache) => {
            cache.put(event.request, responseToCache);
          });
        }
        return networkResponse;
      })
      .catch(() => {
        return caches.match(event.request, { ignoreSearch: true }).then((cachedResponse) => {
          if (cachedResponse) return cachedResponse;
          if (event.request.headers.get('accept') && event.request.headers.get('accept').includes('text/html')) {
            return caches.match('./index.html');
          }
        });
      })
  );
});
