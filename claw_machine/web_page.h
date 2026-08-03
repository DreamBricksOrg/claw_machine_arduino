/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <Arduino.h>

/* The control panel served at "/". Kept in its own header so web_portal.cpp
 * stays readable.
 *
 * HOLD_REPEAT_MS below mirrors WEB_INPUT_REFRESH_MS and POLL_MS mirrors
 * WEB_STATUS_POLL_MS from config.h -- the JavaScript cannot read them. The
 * firmware expires a virtual input VIRTUAL_INPUT_TTL_MS (400 ms) after the last
 * press message, so HOLD_REPEAT_MS must stay comfortably below that. */
static const char kWebPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>Arcade StamPLC</title>
    <style>
        :root { --bg: #1a1a2e; --panel: #16213e; --accent: #e94560; --text: #fff; --btn: #0f3460; --btn-hover: #1f5f99; --gold: #f9a826; }
        body { background: var(--bg); color: var(--text); font-family: 'Segoe UI', Tahoma, sans-serif; text-align: center; margin: 0; padding: 15px; user-select: none; -webkit-user-select: none; touch-action: manipulation; }
        h2 { margin: 5px 0 15px 0; color: var(--accent); letter-spacing: 2px; text-transform: uppercase; }
        .status-box { background: var(--panel); padding: 15px; border-radius: 12px; font-size: 1.2rem; font-weight: bold; margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); border: 1px solid #2a3a5e; }
        #statusText { color: var(--gold); }
        #sdText { font-size: 0.8rem; font-weight: normal; display: block; margin-top: 6px; opacity: 0.7; }
        .controls-top { display: flex; gap: 10px; justify-content: center; margin-bottom: 20px; }
        .btn { padding: 15px; border: none; border-radius: 8px; font-size: 1rem; font-weight: bold; cursor: pointer; text-transform: uppercase; color: var(--text); flex: 1; transition: transform 0.1s; }
        .btn:active { transform: scale(0.95); }
        .btn-coin { background: var(--gold); color: #000; }
        .btn-freeplay { background: var(--btn); }
        .btn-start { background: #2ecc71; margin-bottom: 20px; width: 100%; padding: 18px; font-size: 1.2rem; }
        .dpad-container { margin: 0 auto 20px auto; width: 220px; height: 220px; display: grid; grid-template-columns: 1fr 1fr 1fr; grid-template-rows: 1fr 1fr 1fr; gap: 8px; }
        .dpad-btn { background: var(--btn); color: white; border: none; border-radius: 15px; font-size: 2rem; display: flex; align-items: center; justify-content: center; box-shadow: 0 6px 0 #0a2342; transition: all 0.05s; }
        .dpad-btn:active { transform: translateY(6px); box-shadow: 0 0 0 #0a2342; background: var(--btn-hover); }
        .empty { background: transparent; }
        .btn-action { background: var(--accent); width: 100%; max-width: 350px; padding: 25px; font-size: 1.5rem; box-shadow: 0 6px 0 #8b2939; border-radius: 15px; margin-bottom: 20px; }
        .btn-action:active { transform: translateY(6px); box-shadow: 0 0 0 #8b2939; }
        .time-box { background: var(--panel); padding: 10px; border-radius: 8px; font-size: 0.9rem; margin-bottom: 10px; border: 1px solid #2a3a5e; }
        .btn-sync { background: #4a69bd; padding: 10px; font-size: 0.9rem; width: 100%; margin-top: 10px; }
        .btn-download { background: #e67e22; padding: 10px; font-size: 0.9rem; width: 100%; margin-top: 10px; }
    </style>
</head>
<body>
    <h2>Arcade Control</h2>

    <div class="time-box">
        Hora do Celular: <span id="mobileTime">--:--:--</span>
        <button class="btn btn-sync" onclick="syncArcadeTime()">Sincronizar Relogio do Arcade</button>
    </div>

    <div class="time-box">
        Historico de Jogadas:
        <button class="btn btn-download" onclick="downloadCsv()">Baixar CSV (SD Card)</button>
    </div>

    <div class="status-box">
        Status: <span id="statusText">CARREGANDO...</span>
        <span id="sdText">--</span>
    </div>

    <div class="controls-top">
        <button class="btn btn-coin" onclick="sendCmd('coin')">Ficha</button>
        <button class="btn btn-freeplay" onclick="sendCmd('freeplay')">Freeplay</button>
    </div>

    <button class="btn btn-start" id="btn-start">LIBERAR JOGADA (START)</button>

    <div class="dpad-container">
        <div class="empty"></div>
        <button class="dpad-btn" id="dpad-up">&#9650;</button>
        <div class="empty"></div>
        <button class="dpad-btn" id="dpad-left">&#9664;</button>
        <div class="empty"></div>
        <button class="dpad-btn" id="dpad-right">&#9654;</button>
        <div class="empty"></div>
        <button class="dpad-btn" id="dpad-down">&#9660;</button>
        <div class="empty"></div>
    </div>

    <button class="btn btn-action" id="btn-claw">DESCER GARRA</button>

    <script>
        const HOLD_REPEAT_MS = 200;
        const POLL_MS = 500;

        const timers = {};
        const releasers = [];

        function sendCmd(cmd) { fetch('/cmd?action=' + cmd); }
        function setInput(pin, state) { fetch('/input?pin=' + pin + '&state=' + state); }

        function bindMomentary(id, pin) {
            const el = document.getElementById(id);

            const press = (e) => {
                if (e) e.preventDefault();
                if (timers[pin]) return;
                setInput(pin, 1);
                // Re-send while held. The firmware expires a virtual input 400ms
                // after the last message, so a dropped packet or a backgrounded
                // tab releases the control instead of latching it on.
                timers[pin] = setInterval(() => setInput(pin, 1), HOLD_REPEAT_MS);
            };

            const release = (e) => {
                if (e) e.preventDefault();
                if (!timers[pin]) return;
                clearInterval(timers[pin]);
                timers[pin] = null;
                setInput(pin, 0);
            };

            el.addEventListener('touchstart', press, { passive: false });
            el.addEventListener('touchend', release);
            el.addEventListener('touchcancel', release);
            el.addEventListener('mousedown', press);
            el.addEventListener('mouseup', release);
            el.addEventListener('mouseleave', release);
            el.addEventListener('pointercancel', release);

            releasers.push(release);
        }

        function releaseEverything() { releasers.forEach((r) => r()); }
        window.addEventListener('blur', releaseEverything);
        window.addEventListener('pagehide', releaseEverything);
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) releaseEverything();
        });

        bindMomentary('dpad-up', 0);
        bindMomentary('dpad-left', 1);
        bindMomentary('dpad-down', 2);
        bindMomentary('dpad-right', 3);
        bindMomentary('btn-claw', 4);
        bindMomentary('btn-start', 5);

        setInterval(() => {
            fetch('/status').then((r) => r.json()).then((data) => {
                document.getElementById('statusText').innerText = data.status;
                document.getElementById('sdText').innerText =
                    data.sd ? 'SD Card OK' : 'SD Card indisponivel';
            }).catch((e) => console.log(e));
        }, POLL_MS);

        function updateClockUI() {
            document.getElementById('mobileTime').innerText =
                new Date().toLocaleString('pt-BR');
        }
        setInterval(updateClockUI, 1000);
        updateClockUI();

        function syncArcadeTime() {
            const now = new Date();
            const q = 'y=' + now.getFullYear() +
                      '&m=' + (now.getMonth() + 1) +
                      '&d=' + now.getDate() +
                      '&h=' + now.getHours() +
                      '&min=' + now.getMinutes() +
                      '&s=' + now.getSeconds();

            fetch('/sync_time?' + q)
                .then((r) => {
                    if (!r.ok) throw new Error(r.status);
                    alert('Relogio do Arcade sincronizado!');
                })
                .catch(() => alert('Erro ao sincronizar.'));
        }

        function downloadCsv() {
            fetch('/download_csv').then((r) => {
                // The transfer blocks the firmware loop, so it is refused while
                // a game is in progress.
                if (r.status === 409) { alert('Aguarde a jogada terminar.'); return null; }
                if (r.status === 404) { alert('Nenhum historico no cartao SD.'); return null; }
                if (!r.ok) { alert('Erro ao baixar.'); return null; }
                return r.blob();
            }).then((blob) => {
                if (!blob) return;
                const url = URL.createObjectURL(blob);
                const a = document.createElement('a');
                a.href = url;
                a.download = 'historico.csv';
                a.click();
                URL.revokeObjectURL(url);
            }).catch(() => alert('Erro ao baixar.'));
        }
    </script>
</body>
</html>
)rawliteral";
