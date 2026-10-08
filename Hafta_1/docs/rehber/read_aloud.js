/* Uses the browser's speech engine; no keys, server or uploaded document. */
(() => {
  'use strict';
  const roots = [...document.querySelectorAll('.step-panel, .chapter')];
  if (!roots.length) return;
  const isSteps = roots[0].classList.contains('step-panel');
  const unit = isSteps ? 'adım' : 'bölüm';
  const player = document.createElement('aside');
  player.id = 'listen-player';
  player.className = 'listen-player';
  player.setAttribute('aria-label', 'Türkçe sesli okuma');
  player.innerHTML = `
    <div class="listen-summary">
      <div class="listen-info"><span class="listen-title">SESİ AÇ · KODUN AKIŞINI DİNLE</span>
        <p id="listen-status" role="status">Sesler hazırlanıyor…</p></div>
      <div class="listen-actions">
        <button type="button" id="listen-prev" aria-label="Önceki okuma parçası" title="Önceki okuma parçası" disabled>‹</button>
        <button type="button" id="listen-play" disabled>▶ Dinle</button>
        <button type="button" id="listen-next" aria-label="Sonraki okuma parçası" title="Sonraki okuma parçası" disabled>›</button>
        <button type="button" id="listen-stop" disabled>■ Durdur</button>
        <button type="button" id="listen-settings-toggle" aria-expanded="false" aria-controls="listen-settings">Ayarlar</button>
      </div>
    </div>
    <div class="listen-track" role="progressbar" aria-label="Okuma ilerlemesi" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0"><span id="listen-progress"></span></div>
    <div id="listen-settings" class="listen-settings" hidden>
      <div class="listen-grid">
        <label>Ne okunsun?<select id="listen-scope"><option value="current">Bulunduğum ${unit}</option><option value="all">Bütün rehber · en baştan</option></select></label>
        <label>Türkçe ses<select id="listen-voice" aria-describedby="listen-voice-note"><option>Sesler yükleniyor…</option></select></label>
        <label>Okuma hızı<select id="listen-rate"><option value="0.75">0,75× · Yavaş</option><option value="0.95" selected>0,95× · Rahat</option><option value="1">1× · Normal</option><option value="1.15">1,15×</option><option value="1.3">1,3×</option><option value="1.5">1,5× · Hızlı</option></select></label>
      </div>
      <div class="listen-checks"><label><input type="checkbox" id="listen-code">Kod bloklarını da oku</label><label><input type="checkbox" id="listen-follow" checked>Okunan paragrafı ekranda takip et</label></div>
      <p class="listen-help" id="listen-voice-note"></p>
      <p class="listen-help">Dinle ile seçtiğin kapsamın başından başlar. Açıklamalar ve tablo satırları okunur. Duraklat ile kaldığın yerde beklet; sürdür veya oklarla ilerle. Adım/bölüm bağlantısına tıklamak okumayı durdurur. Ses ve hız değişikliği sonraki okuma parçasında uygulanır.</p>
      <p id="listen-caption" hidden></p>
      <p class="listen-help" id="listen-support" hidden>Türkçe ses görünmüyorsa dosyayı Chrome veya Edge’de aç. Windows’ta Ayarlar → Zaman ve dil → Konuşma → Ses ekle yolundan Türkçe ses ekleyip tarayıcıyı yeniden açabilirsin. <a href="https://support.microsoft.com/en-us/accessibility/windows/narrator/appendix-a-supported-languages-and-voices" target="_blank" rel="noreferrer">Microsoft ses kurulum rehberi ↗</a></p>
    </div>`;
  document.body.append(player);
  if (typeof ResizeObserver === 'function') new ResizeObserver(() => {
    document.documentElement.style.setProperty('--listen-space', (Math.ceil(player.getBoundingClientRect().height) + 32) + 'px');
  }).observe(player);
  const $ = id => document.getElementById('listen-' + id);
  const synth = window.speechSynthesis;
  const supported = !!synth && typeof window.SpeechSynthesisUtterance === 'function';
  let voices = [], chunks = [], index = 0, mode = 'idle', generation = 0;
  let activeUtterance = null, startTimer = 0, pendingTimer = 0, activeNode = null, chosenRoot = null;
  const status = text => { $('status').textContent = text; $('status').title = text; };
  const preferencesKey = 'rtos-reader-settings-v1';
  const loadSettings = () => {
    try {
      const saved = JSON.parse(localStorage.getItem(preferencesKey) || '{}');
      if ([...$('rate').options].some(o => o.value === saved.rate)) $('rate').value = saved.rate;
      if (typeof saved.code === 'boolean') $('code').checked = saved.code;
      if (typeof saved.follow === 'boolean') $('follow').checked = saved.follow;
      return saved;
    } catch (_) { return {}; }
  };
  const saved = loadSettings();
  function saveSettings() {
    try { localStorage.setItem(preferencesKey, JSON.stringify({rate: $('rate').value, voice: $('voice').value, code: $('code').checked, follow: $('follow').checked})); } catch (_) { /* file:// storage can be disabled. */ }
  }
  function refreshVoices() {
    const old = voices.length ? $('voice').value : saved.voice;
    voices = synth.getVoices().filter(v => /^tr(?:[-_]|$)/i.test(v.lang));
    voices.sort((a, b) => Number(b.localService) - Number(a.localService) || a.name.localeCompare(b.name, 'tr'));
    $('voice').replaceChildren();
    voices.forEach(v => {
      const option = document.createElement('option');
      option.value = v.voiceURI;
      option.textContent = v.name + (v.localService ? ' · cihazda' : ' · çevrimiçi');
      $('voice').append(option);
    });
    if (voices.some(v => v.voiceURI === old)) $('voice').value = old;
    else if (voices.length) $('voice').value = (voices.find(v => /Tolga/i.test(v.name) && v.localService) || voices[0]).voiceURI;
    if (!voices.length) {
      const option = document.createElement('option');
      option.textContent = 'Türkçe ses bulunamadı';
      $('voice').append(option);
    }
    $('voice').disabled = !voices.length;
    $('support').hidden = !!voices.length;
    updateVoiceNote(); updateButtons();
    if (mode === 'idle') status(voices.length ? 'Hazır · Bulunduğun ' + unit + 'ı dinleyebilirsin.' : 'Türkçe ses bulunamadı · Ayarlar’ı aç.');
  }
  function updateVoiceNote() {
    const voice = voices.find(v => v.voiceURI === $('voice').value);
    $('voice-note').textContent = !voice ? 'Bu tarayıcı henüz Türkçe ses sunmuyor. Ses listesi değiştiğinde otomatik yenilenir.' : voice.localService ? 'Seçili ses cihazında çalışır. Okumayı başlatmak için Dinle’ye bas.' : 'Seçili ses çevrimiçi çalışır; metin tarayıcının ses hizmetiyle işlenebilir ve internet gerektirebilir.';
  }
  function currentRoot() {
    if (isSteps && typeof current !== 'undefined') return roots.find(r => Number(r.dataset.step) === current) || roots[1];
    if (chosenRoot && !chosenRoot.hidden) return chosenRoot;
    const visible = roots.filter(r => !r.hidden);
    const readingLine = Math.min(240, innerHeight / 3);
    return visible.find(r => { const box = r.getBoundingClientRect(); return box.bottom > readingLine && box.top < innerHeight / 2; }) || visible[0] || roots[0];
  }
  function speechText(raw) {
    const digits = ['sıfır', 'bir', 'iki', 'üç', 'dört'];
    return raw.replace(/\bt([0-4])\b/g, (_, n) => 'te ' + digits[Number(n)])
      .replace(/\bRTOS\b/g, 'ar ti o es').replace(/\bFIFO\b/g, 'fayfo')
      .replace(/\bDMA\b/g, 'di em ey').replace(/\bISR\b/g, 'ay es ar')
      .replace(/\bCPU\b/g, 'si pi yu').replace(/\bUART\b/g, 'yu art')
      .replace(/\bRAM\b/g, 'ram').replace(/\bCRC\b/g, 'si ar si')
      .replace(/([a-z])([A-Z])/g, '$1 $2').replace(/_/g, ' ')
      .replace(/→/g, ', ardından ').replace(/↗/g, '')
      .replace(/\b(\d+)\s*(?:us|µs)\b/g, '$1 mikrosaniye')
      .replace(/\b(\d+)\s*ms\b/g, '$1 milisaniye').replace(/\s+/g, ' ').trim();
  }
  // Small utterances avoid handing an entire chapter to the browser at once.
  function splitText(text) {
    const sentences = typeof Intl.Segmenter === 'function'
      ? [...new Intl.Segmenter('tr', {granularity: 'sentence'}).segment(text)].map(x => x.segment)
      : text.match(/[^.!?]+(?:[.!?]+|$)/g) || [text];
    const result = [];
    for (const sentence of sentences) {
      let remainder = sentence.trim();
      while (remainder.length > 190) {
        let cut = remainder.lastIndexOf(' ', 190);
        if (cut < 50) cut = 190;
        result.push(remainder.slice(0, cut).trim());
        remainder = remainder.slice(cut).trim();
      }
      if (remainder) result.push(remainder);
    }
    return result;
  }
  function collect(selectedRoots) {
    const result = [];
    for (const root of selectedRoots) {
      const nodes = root.querySelectorAll('h2,h3,h4,p,li,tr,pre');
      for (const node of nodes) {
        if (node.closest('.local-toc,.step-footer,.chapter-tail,.frame-widget,.fifo-widget,.source-head,.source-link')) continue;
        if (node.tagName !== 'PRE' && node.closest('.source')) continue;
        if (node.closest('thead')) continue;
        if (node.matches('p') && node.closest('li,td,th')) continue;
        if (node.matches('li') && node.querySelector('li')) continue;
        let raw;
        if (node.matches('pre')) {
          if (!$('code').checked) continue;
          const code = node.cloneNode(true);
          code.querySelectorAll('.ln').forEach(n => n.remove());
          const lines = [...code.querySelectorAll('.line,.code-line')];
          raw = 'Kod bloğu. ' + (lines.length ? lines.map(n => n.textContent).join('. ') : code.textContent);
        } else if (node.matches('tr')) {
          const table = node.closest('table');
          const headers = [...table.querySelectorAll('thead th')].map(n => n.textContent.trim());
          raw = [...node.cells].map((cell, i) => (headers[i] ? headers[i] + ': ' : '') + cell.textContent.trim()).join('. ');
        } else raw = node.textContent;
        const text = speechText(raw);
        if (text) splitText(text).forEach(text => result.push({text, node, root}));
      }
    }
    return result;
  }
  function updateButtons() {
    $('play').disabled = !supported || !voices.length;
    $('play').textContent = mode === 'playing' ? '❚❚ Duraklat' : mode === 'paused' ? '▶ Sürdür' : '▶ Dinle';
    $('stop').disabled = mode === 'idle';
    $('prev').disabled = !chunks.length || index <= 0;
    $('next').disabled = !chunks.length || index >= chunks.length - 1;
  }
  function progress(done = false) {
    const percent = done ? 100 : chunks.length ? Math.floor(index / chunks.length * 100) : 0;
    $('progress').style.width = percent + '%';
    player.querySelector('[role="progressbar"]').setAttribute('aria-valuenow', String(percent));
  }
  function clearMark() { if (activeNode) activeNode.classList.remove('listen-active'); activeNode = null; }
  function cancelSpeech() {
    generation++;
    clearTimeout(startTimer); clearTimeout(pendingTimer);
    if (supported) { synth.cancel(); synth.resume(); }
    activeUtterance = null;
  }
  function stop(message = 'Durduruldu · Dinle ile seçili kapsamın başından başlar.') {
    cancelSpeech(); mode = 'idle'; chunks = []; index = 0;
    clearMark(); progress(); updateButtons(); status(message);
    $('caption').hidden = true;
  }
  function reveal(item) {
    clearMark(); activeNode = item.node; activeNode.classList.add('listen-active');
    if (!$('follow').checked) return;
    const details = item.node.closest('details');
    if (details) details.open = true;
    if (isSteps && typeof go === 'function' && Number(item.root.dataset.step) !== current) go(Number(item.root.dataset.step), false);
    else if (!isSteps && item.root.hidden) {
      const search = document.getElementById('search');
      search.value = ''; search.dispatchEvent(new Event('input', {bubbles: true}));
    }
    const box = item.node.getBoundingClientRect();
    const limit = player.getBoundingClientRect().top;
    if (box.top < 100 || box.bottom > limit - 20) {
      window.scrollTo({top: Math.max(0, window.scrollY + box.top - 120), behavior: 'instant'});
    }
  }
  function speakCurrent() {
    if (mode !== 'playing') return;
    if (index >= chunks.length) {
      clearMark(); progress(true); mode = 'idle'; chunks = []; index = 0;
      activeUtterance = null; updateButtons(); status('Okuma tamamlandı.'); return;
    }
    const item = chunks[index], token = generation;
    reveal(item); progress();
    $('caption').textContent = item.text; $('caption').hidden = false;
    const utterance = new SpeechSynthesisUtterance(item.text);
    utterance.lang = 'tr-TR';
    utterance.voice = voices.find(v => v.voiceURI === $('voice').value) || voices[0];
    utterance.rate = Number($('rate').value);
    activeUtterance = utterance;
    const label = item.root.querySelector('h2').textContent;
    status('Başlıyor · ' + label);
    utterance.onstart = () => {
      if (token !== generation) return;
      clearTimeout(startTimer);
      $('support').hidden = true;
      status((mode === 'paused' ? 'Duraklatıldı' : 'Okunuyor') + ' · ' + (index + 1) + '/' + chunks.length + ' · ' + label);
    };
    utterance.onend = () => {
      if (token !== generation) return;
      clearTimeout(startTimer); activeUtterance = null; index++; updateButtons();
      if (mode === 'playing') pendingTimer = setTimeout(speakCurrent, 60);
    };
    utterance.onerror = event => {
      if (token !== generation) return;
      stop('Ses başlatılamadı (' + (event.error || 'bilinmeyen hata') + '). Başka bir ses seçip Dinle’ye bas.');
      $('support').hidden = false;
    };
    startTimer = setTimeout(() => {
      if (token !== generation || mode !== 'playing') return;
      stop('Ses motoru yanıt vermedi. HTML’yi Chrome veya Edge’de açıp yeniden Dinle’ye bas.');
      $('support').hidden = false;
    }, 15000);
    try { synth.speak(utterance); } catch (_) { stop('Ses motoru kullanılamadı. HTML’yi Chrome veya Edge’de aç.'); }
    updateButtons();
  }
  $('play').onclick = () => {
    if (mode === 'playing') {
      mode = 'paused'; clearTimeout(startTimer); clearTimeout(pendingTimer); synth.pause();
      status('Duraklatıldı · Sürdür ile devam et.'); updateButtons(); return;
    }
    if (mode === 'paused') {
      mode = 'playing'; synth.resume();
      if (!activeUtterance) speakCurrent();
      else status('Okunuyor · ' + (index + 1) + '/' + chunks.length);
      updateButtons(); return;
    }
    if (!voices.length) return;
    cancelSpeech();
    chunks = collect($('scope').value === 'all' ? roots : [currentRoot()]); index = 0;
    if (!chunks.length) { status('Bu bölümde okunacak metin bulunamadı.'); return; }
    mode = 'playing'; speakCurrent();
  };
  function skip(delta) {
    if (!chunks.length) return;
    const wasPaused = mode === 'paused';
    cancelSpeech(); index = Math.max(0, Math.min(chunks.length - 1, index + delta));
    mode = wasPaused ? 'paused' : 'playing';
    if (!wasPaused) speakCurrent();
    else { reveal(chunks[index]); $('caption').textContent = chunks[index].text; progress(); status('Duraklatıldı · Yeni parça seçildi.'); updateButtons(); }
  }
  $('prev').onclick = () => skip(-1); $('next').onclick = () => skip(1);
  $('stop').onclick = () => stop();
  $('settings-toggle').onclick = () => {
    $('settings').hidden = !$('settings').hidden;
    $('settings-toggle').setAttribute('aria-expanded', String(!$('settings').hidden));
  };
  ['scope', 'code'].forEach(id => $(id).onchange = () => { stop('Kapsam güncellendi · Dinle ile başlat.'); saveSettings(); });
  $('rate').onchange = saveSettings;
  $('voice').onchange = () => { updateVoiceNote(); saveSettings(); };
  $('follow').onchange = saveSettings;
  // User navigation stops speech; programmatic following uses go() directly.
  document.addEventListener('click', event => {
    if (event.target.closest('#listen-player')) return;
    const link = event.target.closest('a[href^="#ch"]');
    if (link) chosenRoot = roots.find(r => '#' + r.id === link.hash) || null;
    if (mode === 'idle') return;
    if (event.target.closest('a[href^="#step-"],a[href^="#ch"],[data-go],.search-results button')) stop('Bölüm değişti · Dinle ile bu ' + unit + 'ın başından başlat.');
  }, true);
  ['wheel', 'touchmove'].forEach(name => window.addEventListener(name, () => { chosenRoot = null; }, {passive: true}));
  window.addEventListener('keydown', event => { if (['PageDown','PageUp','Home','End','ArrowDown','ArrowUp',' '].includes(event.key) && !event.target.closest('#listen-player')) chosenRoot = null; });
  if (!isSteps) document.querySelector('#search').addEventListener('input', event => { if (event.isTrusted && mode !== 'idle') stop('Arama değişti · Dinle ile görünen bölümü başlat.'); });
  window.addEventListener('hashchange', () => { if (mode !== 'idle') stop('Bölüm değişti · Dinle ile yeniden başlat.'); });
  window.addEventListener('pagehide', () => { if (supported) cancelSpeech(); });
  if (!supported) {
    status('Bu görüntüleyici sesli okumayı desteklemiyor · Chrome veya Edge’de aç.');
    $('voice-note').textContent = 'HTML dosyasını tam bir tarayıcıda açarak dinleyebilirsin.';
    $('support').hidden = false; updateButtons();
  } else {
    synth.addEventListener('voiceschanged', refreshVoices);
    refreshVoices();
    // Some engines enumerate installed voices shortly after page load.
    setTimeout(refreshVoices, 500); setTimeout(refreshVoices, 1800);
  }
})();
