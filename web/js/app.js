/**
 * Main Application Controller for Fire Alarm 3D & PubSub DSL System
 * Pure C Backend Integration:
 * - DSL parsing & execution occurs in native C (pubsub_dsl.exe)
 * - Real-time updates delivered over Server-Sent Events (SSE)
 * - No DSL interpreter in JavaScript
 */

class CBackendClient {
    constructor() {
        this.publishers = new Map();
        this.subscribers = new Map();
        this.subscriptions = [];
        this.isRunning = false;
        this.isPaused = false;
        this.speed = 1.0;

        this.onLog = null;
        this.onPacket = null;
        this.onDeviceStateChange = null;
        this.onRoutingUpdate = null;
        this.onStep = null;
        this.onExecutionStateChange = null;

        this.eventSource = null;
        this._initEventSource();
    }

    _initEventSource() {
        if (this.eventSource) {
            try { this.eventSource.close(); } catch(e){}
        }

        this.eventSource = new EventSource('/api/events');

        this.eventSource.onopen = () => {
            this.log('INFO', 'C_BACKEND', 'Tilkoblet C Backend Server-Sent Events (SSE) strøm.');
            this.refreshRouting();
        };

        this.eventSource.onerror = () => {
            // Browser EventSource automatically attempts reconnection
        };

        this.eventSource.onmessage = (e) => {
            try {
                const msg = JSON.parse(e.data);
                this._handleServerEvent(msg);
            } catch (err) {
                console.error('Failed to parse SSE payload:', err, e.data);
            }
        };
    }

    _handleServerEvent(msg) {
        if (!msg) return;
        const d = (msg.data !== undefined && msg.data !== null && typeof msg.data === 'object') ? msg.data : msg;

        switch (msg.type) {
            case 'INIT':
            case 'CONNECTED':
                this.log('DEBUG', 'C_ENGINE', 'C Backend Runtime tilkoblet (x86_64 WinSock / POSIX).');
                this.refreshRouting();
                break;

            case 'LOG':
                if (this.onLog) {
                    this.onLog({
                        timestamp: d.timestamp || new Date().toISOString(),
                        level: d.level || 'INFO',
                        category: d.category || d.component || 'C_CORE',
                        message: d.message || ''
                    });
                }
                break;

            case 'PACKET': {
                const packet = {
                    seq: d.seq,
                    type: d.packet_type || d.type || 'SEND',
                    fromTag: d.from_tag,
                    toTag: d.to_tag,
                    payload: d.payload || '',
                    byteSize: d.byte_size || 96
                };
                if (this.onPacket) this.onPacket(packet);
                if (this.onDeviceStateChange) this.onDeviceStateChange(packet);
                break;
            }

            case 'ROUTING_UPDATE': {
                const targetMap = d.registry === 'PUBLISHER' ? this.publishers : this.subscribers;
                if (d.action === 'DELETE') {
                    targetMap.delete(d.tag);
                } else {
                    const isEmpty = d.endpoint === 'EMPTY' || !d.endpoint;
                    const parts = (d.endpoint || '').split(':');
                    targetMap.set(d.tag, {
                        tag: d.tag,
                        host: parts[0] || 'EMPTY',
                        port: parts[1] || '',
                        isEmpty
                    });
                }
                if (this.onRoutingUpdate) {
                    this.onRoutingUpdate(d.action, d.tag);
                }
                break;
            }

            case 'STEP':
                if (this.onStep) {
                    this.onStep({
                        type: d.stmt_type || d.type || 'STMT',
                        line: d.line || 1
                    }, d.index || 0, d.total || 1);
                }
                break;

            case 'STATUS':
                if (d.is_running !== undefined) {
                    this.isRunning = !!d.is_running;
                    this.isPaused = !!d.is_paused;
                } else if (d.state === 'RUNNING') {
                    this.isRunning = true;
                    this.isPaused = false;
                } else if (d.state === 'PAUSED') {
                    this.isRunning = true;
                    this.isPaused = true;
                } else {
                    this.isRunning = false;
                    this.isPaused = false;
                }
                if (this.onExecutionStateChange) {
                    this.onExecutionStateChange({
                        isRunning: this.isRunning,
                        isPaused: this.isPaused
                    });
                }
                if (!this.isRunning) {
                    this.refreshRouting();
                }
                break;

            case 'RESET':
                this.publishers.clear();
                this.subscribers.clear();
                this.subscriptions = [];
                this.isRunning = false;
                this.isPaused = false;
                if (this.onExecutionStateChange) {
                    this.onExecutionStateChange({ isRunning: false, isPaused: false });
                }
                this.refreshRouting();
                break;
        }
    }

    async refreshRouting() {
        try {
            const resp = await fetch('/api/routing');
            if (!resp.ok) return;
            const data = await resp.json();
            this.publishers.clear();
            (data.publishers || []).forEach(p => {
                const parts = (p.endpoint || '').split(':');
                this.publishers.set(p.tag, {
                    tag: p.tag,
                    host: parts[0] || 'EMPTY',
                    port: parts[1] || '',
                    isEmpty: !!p.is_empty
                });
            });
            this.subscribers.clear();
            (data.subscribers || []).forEach(s => {
                const parts = (s.endpoint || '').split(':');
                this.subscribers.set(s.tag, {
                    tag: s.tag,
                    host: parts[0] || 'EMPTY',
                    port: parts[1] || '',
                    isEmpty: !!s.is_empty
                });
            });
            this.subscriptions = data.subscriptions || [];
            if (this.onRoutingUpdate) {
                this.onRoutingUpdate('REFRESH', null);
            }
        } catch (e) {
            console.error('Failed to refresh routing table:', e);
        }
    }

    async execute(source) {
        this.isRunning = true;
        this.isPaused = false;
        if (this.onExecutionStateChange) {
            this.onExecutionStateChange({ isRunning: true, isPaused: false });
        }
        try {
            const resp = await fetch('/api/run', {
                method: 'POST',
                headers: { 'Content-Type': 'text/plain' },
                body: source
            });
            const data = await resp.json();
            if (!data.ok && data.status !== 'OK') {
                this.log('ERROR', 'PARSER', `C Kompileringsfeil: ${data.error || 'Ukjent feil'}`);
                this.isRunning = false;
                if (this.onExecutionStateChange) {
                    this.onExecutionStateChange({ isRunning: false, isPaused: false });
                }
            }
        } catch (err) {
            this.log('ERROR', 'HTTP', `Kunne ikke sende script til C backend: ${err.message}`);
            this.isRunning = false;
            if (this.onExecutionStateChange) {
                this.onExecutionStateChange({ isRunning: false, isPaused: false });
            }
        }
    }

    async pause() {
        try { await fetch('/api/pause', { method: 'POST' }); } catch(e){}
    }

    async resume() {
        try { await fetch('/api/resume', { method: 'POST' }); } catch(e){}
    }

    async stop() {
        try { await fetch('/api/stop', { method: 'POST' }); } catch(e){}
        this.isRunning = false;
        this.isPaused = false;
        if (this.onExecutionStateChange) {
            this.onExecutionStateChange({ isRunning: false, isPaused: false });
        }
    }

    async reset() {
        try { await fetch('/api/reset', { method: 'POST' }); } catch(e){}
        this.publishers.clear();
        this.subscribers.clear();
        this.subscriptions = [];
        this.isRunning = false;
        this.isPaused = false;
        if (this.onExecutionStateChange) {
            this.onExecutionStateChange({ isRunning: false, isPaused: false });
        }
        await this.refreshRouting();
    }

    async triggerDirect(fromTag, toTag, message) {
        try {
            await fetch('/api/trigger', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    from_tag: fromTag,
                    to_tag: toTag,
                    message: message
                })
            });
        } catch (err) {
            this.log('ERROR', 'TRIGGER', `Trigger feilet: ${err.message}`);
        }
    }

    setSpeed(speedVal) {
        this.speed = speedVal;
    }

    log(level, category, message) {
        if (this.onLog) {
            const now = new Date();
            const timeStr = now.toTimeString().split(' ')[0] + '.' + String(now.getMilliseconds()).padStart(3, '0');
            this.onLog({
                timestamp: `${now.toISOString().slice(0, 10)} ${timeStr}`,
                level,
                category,
                message
            });
        }
    }
}

document.addEventListener('DOMContentLoaded', () => {
    // 1. Initialize Audio Synthesizer
    const audio = new FireAlarmAudio();

    // 2. Initialize 3D Building
    const viewportContainer = document.getElementById('viewport-3d');
    const building = new Building3D(viewportContainer, audio);

    // 3. Initialize Pure C Backend Client (No JavaScript DSL parser!)
    const dslEngine = new CBackendClient();

    // DOM Elements
    const dslEditor = document.getElementById('dsl-editor');
    const scenarioSelect = document.getElementById('scenario-select');
    const btnRun = document.getElementById('btn-run');
    const btnPause = document.getElementById('btn-pause');
    const btnStop = document.getElementById('btn-stop');
    const btnReset = document.getElementById('btn-reset');
    const speedSlider = document.getElementById('speed-slider');
    const speedLabel = document.getElementById('speed-label');
    const explodedSlider = document.getElementById('exploded-slider');
    const consoleOutput = document.getElementById('console-output');
    const btnClearConsole = document.getElementById('btn-clear-console');
    const alarmBanner = document.getElementById('alarm-status-banner');
    const alarmTitle = document.getElementById('alarm-title');
    const alarmDesc = document.getElementById('alarm-desc');
    const btnMute = document.getElementById('btn-mute');
    const deviceTableBody = document.getElementById('device-table-body');
    const routingTableBody = document.getElementById('routing-table-body');
    const deviceCountBadge = document.getElementById('device-count-badge');
    const currentStepText = document.getElementById('current-step-text');
    const progressBar = document.getElementById('execution-progress-bar');

    // -------------------------------------------------------------
    // SCENARIO LOADING
    // -------------------------------------------------------------
    function loadScenario(key) {
        const scen = FIRE_ALARM_SCENARIOS[key];
        if (scen) {
            dslEditor.value = scen.source;
            document.getElementById('scenario-desc').textContent = scen.description;
            updateEditorLineNumbers();
        }
    }

    scenarioSelect.addEventListener('change', (e) => {
        loadScenario(e.target.value);
    });

    // Populate scenario selector
    scenarioSelect.innerHTML = '';
    Object.keys(FIRE_ALARM_SCENARIOS).forEach((key) => {
        const opt = document.createElement('option');
        opt.value = key;
        opt.textContent = FIRE_ALARM_SCENARIOS[key].name;
        scenarioSelect.appendChild(opt);
    });
    loadScenario('complex_en54');

    // Line numbers in editor
    function updateEditorLineNumbers() {
        const lineCount = (dslEditor.value.match(/\n/g) || []).length + 1;
        const lineNumbersEl = document.getElementById('editor-line-numbers');
        if (lineNumbersEl) {
            lineNumbersEl.innerHTML = Array.from({ length: lineCount }, (_, i) => i + 1).join('<br>');
        }
    }
    dslEditor.addEventListener('input', updateEditorLineNumbers);
    dslEditor.addEventListener('scroll', () => {
        const lineNumbersEl = document.getElementById('editor-line-numbers');
        if (lineNumbersEl) {
            lineNumbersEl.scrollTop = dslEditor.scrollTop;
        }
    });

    // -------------------------------------------------------------
    // C BACKEND ENGINE HOOKS & UI SYNC
    // -------------------------------------------------------------

    dslEngine.onLog = (entry) => {
        const line = document.createElement('div');
        line.className = `log-line log-${entry.level.toLowerCase()}`;

        let catBadge = '';
        if (entry.category) {
            catBadge = `<span class="log-cat log-cat-${entry.category.toLowerCase()}">[${entry.category}]</span> `;
        }

        const timeOnly = entry.timestamp.includes(' ') ? entry.timestamp.split(' ')[1] : entry.timestamp;
        line.innerHTML = `<span class="log-time">[${timeOnly}]</span> <span class="log-lvl">${entry.level.padEnd(7)}</span> ${catBadge}<span class="log-msg">${escapeHtml(entry.message)}</span>`;
        consoleOutput.appendChild(line);

        if (document.getElementById('autoscroll-toggle').checked) {
            consoleOutput.scrollTop = consoleOutput.scrollHeight;
        }
    };

    dslEngine.onPacket = (packet) => {
        building.spawnPacketPulse(packet);
        updatePacketStats(packet);
    };

    dslEngine.onDeviceStateChange = (evt) => {
        building.handleDeviceStateChange(evt);
        updateDeviceTable();
        evaluateSystemAlarmStatus(evt);
    };

    dslEngine.onRoutingUpdate = (action, tag) => {
        updateRoutingTable();
        updateDeviceTable();
    };

    dslEngine.onStep = (stmt, index, total) => {
        const pct = Math.round(((index + 1) / total) * 100);
        if (progressBar) progressBar.style.width = `${pct}%`;
        if (currentStepText) currentStepText.textContent = `C Runtime Steg ${index + 1}/${total}: ${stmt.type} (linje ${stmt.line})`;
        highlightEditorLine(stmt.line);
    };

    dslEngine.onExecutionStateChange = ({ isRunning, isPaused }) => {
        btnRun.disabled = isRunning && !isPaused;
        btnPause.disabled = !isRunning;
        btnStop.disabled = !isRunning;
        btnPause.innerHTML = isPaused ? '<i class="fas fa-play"></i> Fortsett' : '<i class="fas fa-pause"></i> Pause';
        btnRun.innerHTML = (isRunning && !isPaused) ? '<i class="fas fa-spinner fa-spin"></i> C Kjører...' : '<i class="fas fa-play"></i> Kjør DSL (i C)';
    };

    function highlightEditorLine(lineNum) {
        const lineHeight = 19;
        dslEditor.scrollTop = (lineNum - 4) * lineHeight;
    }

    // -------------------------------------------------------------
    // CONTROLS
    // -------------------------------------------------------------

    btnRun.addEventListener('click', async () => {
        if (dslEngine.isPaused) {
            dslEngine.resume();
            return;
        }
        const source = dslEditor.value;
        await dslEngine.execute(source);
    });

    btnPause.addEventListener('click', () => {
        if (dslEngine.isPaused) {
            dslEngine.resume();
        } else {
            dslEngine.pause();
        }
    });

    btnStop.addEventListener('click', () => {
        dslEngine.stop();
        if (progressBar) progressBar.style.width = '0%';
        if (currentStepText) currentStepText.textContent = 'Eksekvering stoppet';
    });

    btnReset.addEventListener('click', () => {
        dslEngine.stop();
        dslEngine.reset();
        building.resetAllEffects();
        if (progressBar) progressBar.style.width = '0%';
        if (currentStepText) currentStepText.textContent = 'Klar til kjøring';
        resetAlarmBanner();
        updateDeviceTable();
        updateRoutingTable();
        consoleOutput.innerHTML = '';
        dslEngine.log('INFO', 'SYSTEM', 'C Runtime og 3D-visning tilbakestilt til NORMAL TILSTAND');
    });

    speedSlider.addEventListener('input', (e) => {
        const val = parseFloat(e.target.value);
        dslEngine.setSpeed(val);
        speedLabel.textContent = `${val.toFixed(1)}x`;
    });

    explodedSlider.addEventListener('input', (e) => {
        const factor = parseFloat(e.target.value);
        building.setExplosion(factor);
        document.getElementById('exploded-label').textContent = `${Math.round(factor * 100)}%`;
    });

    btnClearConsole.addEventListener('click', () => {
        consoleOutput.innerHTML = '';
    });

    btnMute.addEventListener('click', () => {
        const muted = audio.toggleMute();
        btnMute.innerHTML = muted ? '<i class="fas fa-volume-mute"></i> Lyd: AV' : '<i class="fas fa-volume-up"></i> Lyd: PÅ';
        btnMute.classList.toggle('btn-muted', muted);
    });

    // -------------------------------------------------------------
    // NAVIGATION & MOVEMENT CONTROLS (ORBIT VS WALK MODE)
    // -------------------------------------------------------------

    const btnModeOrbit = document.getElementById('btn-mode-orbit');
    const btnModeWalk = document.getElementById('btn-mode-walk');
    const walkHud = document.getElementById('walk-hud');

    function setNavigationMode(mode) {
        if (btnModeOrbit && btnModeWalk) {
            btnModeOrbit.classList.toggle('active', mode === 'ORBIT');
            btnModeWalk.classList.toggle('active', mode === 'WALK');
        }
        if (walkHud) {
            walkHud.classList.toggle('hidden', mode !== 'WALK');
        }
        building.setNavigationMode(mode);
        dslEngine.log('INFO', 'NAV', mode === 'WALK' ? 
            'Gå-Modus aktivert: Bruk W/A/S/D for å gå, Q/E for opp/ned, Shift for å løpe, og dra musen for å se deg rundt.' : 
            '3D Orbit-Modus aktivert: Roter, pan og zoom fritt rundt bygget.');
    }

    if (btnModeOrbit) btnModeOrbit.addEventListener('click', () => setNavigationMode('ORBIT'));
    if (btnModeWalk) btnModeWalk.addEventListener('click', () => setNavigationMode('WALK'));

    // On-screen touch / mouse D-Pad bindings for navigation
    function bindDpadButton(btnId, keyName) {
        const btn = document.getElementById(btnId);
        if (!btn) return;
        const press = (e) => {
            e.preventDefault();
            building.keysDown[keyName] = true;
        };
        const release = (e) => {
            e.preventDefault();
            building.keysDown[keyName] = false;
        };
        btn.addEventListener('pointerdown', press);
        btn.addEventListener('pointerup', release);
        btn.addEventListener('pointerleave', release);
        btn.addEventListener('pointercancel', release);
    }

    bindDpadButton('dpad-up', 'w');
    bindDpadButton('dpad-down', 's');
    bindDpadButton('dpad-left', 'a');
    bindDpadButton('dpad-right', 'd');
    bindDpadButton('dpad-elev-up', 'e');
    bindDpadButton('dpad-elev-down', 'q');

    // -------------------------------------------------------------
    // LIVE EVENT FEED TOAST NOTIFICATIONS
    // -------------------------------------------------------------

    const liveEventFeed = document.getElementById('live-event-feed');
    function showEventToast(title, desc, type = 'alarm') {
        if (!liveEventFeed) return;
        const toast = document.createElement('div');
        toast.className = `event-toast toast-${type}`;

        let iconHtml = '⚠️';
        if (type === 'alarm') iconHtml = '<i class="fas fa-triangle-exclamation" style="color: #ef4444;"></i>';
        else if (type === 'packet') iconHtml = '<i class="fas fa-satellite-dish" style="color: #38bdf8;"></i>';
        else if (type === 'routing') iconHtml = '<i class="fas fa-network-wired" style="color: #c084fc;"></i>';

        toast.innerHTML = `
            <span style="font-size: 1rem; display: flex; align-items: center;">${iconHtml}</span>
            <div style="display: flex; flex-direction: column; gap: 1px;">
                <span style="font-weight: 700; color: #fff;">${escapeHtml(title)}</span>
                <span style="font-size: 0.7rem; color: #cbd5e1;">${escapeHtml(desc)}</span>
            </div>
        `;

        liveEventFeed.appendChild(toast);

        // Keep maximum 4 concurrent toasts
        while (liveEventFeed.children.length > 4) {
            liveEventFeed.removeChild(liveEventFeed.firstChild);
        }

        setTimeout(() => {
            toast.classList.add('fade-out');
            setTimeout(() => {
                if (toast.parentNode === liveEventFeed) {
                    liveEventFeed.removeChild(toast);
                }
            }, 500);
        }, 4000);
    }

    building.onNewEvent = (data) => {
        showEventToast(data.title || 'HENDELSE', data.desc || '', data.type || 'alarm');
    };

    // -------------------------------------------------------------
    // FLOOR & CAMERA PRESETS
    // -------------------------------------------------------------

    document.querySelectorAll('.btn-floor').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.btn-floor').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            const floor = btn.dataset.floor;
            building.setFloorFilter(floor);
        });
    });

    function selectFloorFilter(floor) {
        document.querySelectorAll('.btn-floor').forEach(b => {
            b.classList.toggle('active', b.dataset.floor === floor);
        });
        building.setFloorFilter(floor);
    }

    document.querySelectorAll('.btn-cam').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.btn-cam').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            const cam = btn.dataset.cam;

            if (cam === 'SERVER_ROOM') {
                selectFloorFilter('BASEMENT');
            } else if (cam === 'KITCHEN') {
                selectFloorFilter('FLOOR2');
            } else if (cam === 'CENTRAL_PANEL') {
                selectFloorFilter('FLOOR1');
            } else if (cam === 'ROOF_VENT') {
                selectFloorFilter('ROOF');
            } else if (cam === 'OVERVIEW') {
                selectFloorFilter('ALL');
            }
            building.setCameraPreset(cam);
        });
    });

    const xrayToggle = document.getElementById('xray-toggle');
    if (xrayToggle) {
        xrayToggle.addEventListener('change', (e) => {
            building.setXrayMode(e.target.checked);
        });
    }

    // -------------------------------------------------------------
    // QUICK EVENT TRIGGER BUTTONS (Simulating physical actions via C runtime)
    // -------------------------------------------------------------

    document.getElementById('btn-quick-smoke').addEventListener('click', () => {
        triggerSimulatedDslEvent('ROYK_KONTOR', 'SENTRAL_KONTROLL', 'PRE_ALARM: Optisk røykdeteksjon kontor E1');
    });

    document.getElementById('btn-quick-breakglass').addEventListener('click', () => {
        triggerSimulatedDslEvent('MANUELL_MELDER_E1', 'SENTRAL_KONTROLL', 'MANUAL_PULL_ALARM: Manuell melder E1 utløst');
    });

    document.getElementById('btn-quick-kitchen').addEventListener('click', () => {
        triggerSimulatedDslEvent('VARME_KJOKKEN', 'SPRINKLER_KJOKKEN', 'HEAT_ALARM_88C: Frityrbrann oppstått, utløser sprinkler');
    });

    document.getElementById('btn-quick-server').addEventListener('click', () => {
        triggerSimulatedDslEvent('VESDA_SERVER', 'INERGEN_GASS_SERVER', 'VESDA_ALARM_STAGE2: Ulmebrann serverrack 3, gassutslipp aktivert');
    });

    async function triggerSimulatedDslEvent(fromTag, toTag, message) {
        dslEngine.log('WARN', 'NODE_EVENT', `[MANUELL TEST-TRIGGER] Simulert sensor-hendelse for '${fromTag}' til C Backend`);
        await dslEngine.triggerDirect(fromTag, toTag, message);
    }

    // Handle 3D device click
    building.onDeviceClick = (tag, dev) => {
        const modalTitle = document.getElementById('modal-device-tag');
        const modalType = document.getElementById('modal-device-type');
        const modalZone = document.getElementById('modal-device-zone');
        const modalStatus = document.getElementById('modal-device-status');
        const modalIp = document.getElementById('modal-device-ip');
        const btnTrigger = document.getElementById('modal-btn-trigger');

        if (modalTitle) modalTitle.textContent = tag;
        if (modalType) modalType.textContent = dev.type;
        if (modalZone) modalZone.textContent = dev.zone;
        if (modalStatus) modalStatus.textContent = dev.status;

        const pub = dslEngine.publishers.get(tag);
        const sub = dslEngine.subscribers.get(tag);
        const ep = pub ? `${pub.host}:${pub.port}` : (sub ? `${sub.host}:${sub.port}` : '10.0.x.x');
        if (modalIp) modalIp.textContent = ep;

        if (btnTrigger) {
            btnTrigger.onclick = () => {
                triggerSimulatedDslEvent(tag, 'SENTRAL_KONTROLL', `MANUAL_TRIGGER: Hendelse fra 3D-modell [${tag}]`);
                closeModal();
            };
        }

        openModal();
    };

    function openModal() {
        const modal = document.getElementById('device-modal');
        if (modal) modal.classList.add('visible');
    }
    function closeModal() {
        const modal = document.getElementById('device-modal');
        if (modal) modal.classList.remove('visible');
    }
    const modalClose = document.getElementById('modal-close');
    if (modalClose) modalClose.addEventListener('click', closeModal);

    // -------------------------------------------------------------
    // ALARM BANNER & HUD STATUS
    // -------------------------------------------------------------

    function evaluateSystemAlarmStatus(evt) {
        if (!alarmBanner) return;
        const vignette = document.getElementById('alarm-vignette');

        if (evt.payload && (evt.payload.includes('DISCHARGE') || evt.payload.includes('INERGEN'))) {
            alarmBanner.className = 'status-banner banner-gas';
            alarmTitle.textContent = '⚠️ GASS-SLUKKING AKTIVERT (INERGEN N2/Ar/CO2)';
            alarmDesc.textContent = 'Serverrom i kjeller forseglet. 30s nedtelling til oksygenfortrengning. Adgang strengt forbudt!';
            if (vignette) vignette.className = 'alarm-vignette active-gas';
        } else if (evt.payload && evt.payload.includes('GENERAL_EVACUATION')) {
            alarmBanner.className = 'status-banner banner-evac';
            alarmTitle.textContent = '🚨 GENERELL EVAKUERING AKTIV (EN 54)';
            alarmDesc.textContent = 'Full alarm utløst i alle etasjer. Talevarsling, sirener og røykluker operative. Heiser låst i 1. etasje.';
            if (vignette) vignette.className = 'alarm-vignette active';
        } else if (evt.payload && evt.payload.includes('SPRINKLER')) {
            alarmBanner.className = 'status-banner banner-sprinkler';
            alarmTitle.textContent = '💦 SPRINKLER UTØST PÅ STORKJØKKEN (SONE 2)';
            alarmDesc.textContent = 'Vanntilførsel aktivert over kokeøy. Kjøkkenvifte stanset og branndør lukket.';
            if (vignette) vignette.className = 'alarm-vignette active';
        } else if (evt.payload && (evt.payload.includes('PRE_ALARM') || evt.payload.includes('STAGE_1') || evt.payload.includes('ALARM'))) {
            alarmBanner.className = 'status-banner banner-prealarm';
            alarmTitle.textContent = '⚠️ PRE-ALARM: RØYKUTVIKLING REGISTRERT';
            alarmDesc.textContent = 'Verifiseringsfase aktiv. Magnetlåser på branndører frigjort i berørt seksjon.';
            if (vignette) vignette.className = 'alarm-vignette active';
        }
    }

    function resetAlarmBanner() {
        if (alarmBanner) {
            alarmBanner.className = 'status-banner banner-normal';
            alarmTitle.textContent = '✅ BYGG OVERVÅKET - NORMAL DRIFT (C BACKEND)';
            alarmDesc.textContent = 'Alle 4 soner, 12 sensorer og 13 aktuatorer operative over C PubSub DSL-nettverket.';
        }
        const vignette = document.getElementById('alarm-vignette');
        if (vignette) vignette.className = 'alarm-vignette';
    }

    // -------------------------------------------------------------
    // TABLES: DEVICE LIST & ROUTING MATRIX
    // -------------------------------------------------------------

    function updateDeviceTable() {
        if (!deviceTableBody) return;
        deviceTableBody.innerHTML = '';

        let count = 0;
        building.devices.forEach((dev, tag) => {
            count++;
            const tr = document.createElement('tr');
            
            let statusBadge = '<span class="badge badge-normal">OK / NORMAL</span>';
            if (dev.status === 'ALARM_ACTIVE') {
                statusBadge = '<span class="badge badge-alarm blink">BRANNALARM</span>';
            } else if (dev.status === 'ACTIVATED' || dev.isActive) {
                statusBadge = '<span class="badge badge-active">AKTIVERT</span>';
            } else if (dev.status === 'MAGNET_RELEASED_CLOSED') {
                statusBadge = '<span class="badge badge-closed">LUKKET</span>';
            }

            const pub = dslEngine.publishers.get(tag);
            const sub = dslEngine.subscribers.get(tag);
            const ep = pub ? (pub.isEmpty ? 'EMPTY' : `${pub.host}:${pub.port}`) :
                       (sub ? (sub.isEmpty ? 'EMPTY' : `${sub.host}:${sub.port}`) : 'Internt');

            tr.innerHTML = `
                <td><strong>${tag}</strong></td>
                <td><small>${dev.type}</small></td>
                <td>${dev.zone}</td>
                <td><code>${ep}</code></td>
                <td>${statusBadge}</td>
            `;

            tr.addEventListener('click', () => {
                building.onDeviceClick(tag, dev);
            });

            deviceTableBody.appendChild(tr);
        });

        if (deviceCountBadge) deviceCountBadge.textContent = `${count} enheter`;
    }

    function updateRoutingTable() {
        if (!routingTableBody) return;
        routingTableBody.innerHTML = '';

        // Publisher rows
        dslEngine.publishers.forEach((pub, tag) => {
            const tr = document.createElement('tr');
            const ep = pub.isEmpty ? '<span class="text-muted">EMPTY</span>' : `${pub.host}:${pub.port}`;
            const state = pub.isEmpty ? '<span class="badge badge-empty">EMPTY</span>' : '<span class="badge badge-normal">ACTIVE</span>';
            tr.innerHTML = `
                <td><span class="badge badge-pub">PUB</span></td>
                <td><strong>${tag}</strong></td>
                <td><code>${ep}</code></td>
                <td>${state}</td>
            `;
            routingTableBody.appendChild(tr);
        });

        // Subscriber rows
        dslEngine.subscribers.forEach((sub, tag) => {
            const tr = document.createElement('tr');
            const ep = sub.isEmpty ? '<span class="text-muted">EMPTY</span>' : `${sub.host}:${sub.port}`;
            const state = sub.isEmpty ? '<span class="badge badge-empty">EMPTY</span>' : '<span class="badge badge-normal">ACTIVE</span>';
            tr.innerHTML = `
                <td><span class="badge badge-sub">SUB</span></td>
                <td><strong>${tag}</strong></td>
                <td><code>${ep}</code></td>
                <td>${state}</td>
            `;
            routingTableBody.appendChild(tr);
        });
    }

    let totalPackets = 0;
    let totalBytes = 0;
    function updatePacketStats(packet) {
        totalPackets++;
        totalBytes += packet.byteSize || 120;
        const pCountEl = document.getElementById('stat-packet-count');
        const bCountEl = document.getElementById('stat-bytes-count');
        if (pCountEl) pCountEl.textContent = totalPackets;
        if (bCountEl) bCountEl.textContent = `${(totalBytes / 1024).toFixed(1)} KB`;
    }

    function escapeHtml(str) {
        return str.replace(/[&<>'"]/g, 
            tag => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', "'": '&#39;', '"': '&quot;' }[tag] || tag)
        );
    }

    // Tabs navigation
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
            btn.classList.add('active');
            const targetPane = document.getElementById(btn.dataset.tab);
            if (targetPane) targetPane.classList.add('active');
        });
    });

    // Initial setup
    updateDeviceTable();
    updateRoutingTable();
    resetAlarmBanner();
    dslEngine.log('INFO', 'SYSTEM', 'C Backend Web Server grensesnitt initialisert.');
});
