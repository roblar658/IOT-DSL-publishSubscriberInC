/**
 * Web Audio Synthesizer for Industrial Fire Alarm Systems
 * Generates realistic EN 54 sirens, gas discharge hiss, water deluge,
 * door release clunks, and speech synthesis without external audio files.
 */

class FireAlarmAudio {
    constructor() {
        this.ctx = null;
        this.isMuted = false;
        this.activeSirenOsc = null;
        this.activeSirenGain = null;
        this.activeNoiseNode = null;
        this.isSirenPlaying = false;
    }

    _initContext() {
        if (!this.ctx) {
            const AudioContext = window.AudioContext || window.webkitAudioContext;
            this.ctx = new AudioContext();
        }
        if (this.ctx.state === 'suspended') {
            this.ctx.resume();
        }
    }

    toggleMute() {
        this.isMuted = !this.isMuted;
        if (this.isMuted) {
            this.stopAll();
        }
        return this.isMuted;
    }

    // Short UI click / packet transmit ping
    playPacketChirp() {
        if (this.isMuted) return;
        this._initContext();

        const osc = this.ctx.createOscillator();
        const gain = this.ctx.createGain();

        osc.type = 'sine';
        const now = this.ctx.currentTime;
        osc.frequency.setValueAtTime(880, now);
        osc.frequency.exponentialRampToValueAtTime(1760, now + 0.04);

        gain.gain.setValueAtTime(0.08, now);
        gain.gain.exponentialRampToValueAtTime(0.001, now + 0.05);

        osc.connect(gain);
        gain.connect(this.ctx.destination);

        osc.start(now);
        osc.stop(now + 0.05);
    }

    // Pre-alarm warning pulse (intermittent warning tone)
    playPreAlarmBeep() {
        if (this.isMuted) return;
        this._initContext();

        const now = this.ctx.currentTime;
        for (let i = 0; i < 2; i++) {
            const osc = this.ctx.createOscillator();
            const gain = this.ctx.createGain();
            const t = now + i * 0.18;

            osc.type = 'square';
            osc.frequency.setValueAtTime(950, t);

            gain.gain.setValueAtTime(0.12, t);
            gain.gain.setValueAtTime(0, t + 0.08);

            osc.connect(gain);
            gain.connect(this.ctx.destination);

            osc.start(t);
            osc.stop(t + 0.09);
        }
    }

    // Continuous EN 54 Slow Whoop Fire Siren (Sweeps 500 Hz to 1200 Hz over 3.5s)
    startSiren() {
        if (this.isMuted || this.isSirenPlaying) return;
        this._initContext();
        this.isSirenPlaying = true;

        const now = this.ctx.currentTime;
        const osc = this.ctx.createOscillator();
        const gain = this.ctx.createGain();

        osc.type = 'sawtooth';
        osc.frequency.setValueAtTime(500, now);

        // Modulate with periodic sweep
        const sweepPeriod = 3.5;
        const cycles = 20;
        for (let i = 0; i < cycles; i++) {
            const tStart = now + i * sweepPeriod;
            osc.frequency.setValueAtTime(500, tStart);
            osc.frequency.linearRampToValueAtTime(1200, tStart + sweepPeriod - 0.5);
            osc.frequency.setValueAtTime(500, tStart + sweepPeriod);
        }

        gain.gain.setValueAtTime(0.25, now);

        osc.connect(gain);
        gain.connect(this.ctx.destination);

        osc.start(now);
        this.activeSirenOsc = osc;
        this.activeSirenGain = gain;
    }

    stopSiren() {
        if (this.activeSirenOsc) {
            try {
                this.activeSirenGain.gain.setTargetAtTime(0, this.ctx.currentTime, 0.2);
                this.activeSirenOsc.stop(this.ctx.currentTime + 0.3);
            } catch (e) {
                // ignore
            }
            this.activeSirenOsc = null;
            this.activeSirenGain = null;
        }
        this.isSirenPlaying = false;
    }

    // Fire door magnetic latch release & slam
    playDoorClunk() {
        if (this.isMuted) return;
        this._initContext();

        const now = this.ctx.currentTime;
        const osc = this.ctx.createOscillator();
        const gain = this.ctx.createGain();

        osc.type = 'triangle';
        osc.frequency.setValueAtTime(140, now);
        osc.frequency.exponentialRampToValueAtTime(40, now + 0.25);

        gain.gain.setValueAtTime(0.3, now);
        gain.gain.exponentialRampToValueAtTime(0.001, now + 0.3);

        osc.connect(gain);
        gain.connect(this.ctx.destination);

        osc.start(now);
        osc.stop(now + 0.3);
    }

    // High pressure gas discharge hiss (Inergen / CO2)
    playGasDischarge(durationSec = 2.5) {
        if (this.isMuted) return;
        this._initContext();

        const bufferSize = this.ctx.sampleRate * durationSec;
        const buffer = this.ctx.createBuffer(1, bufferSize, this.ctx.sampleRate);
        const data = buffer.getChannelData(0);

        // White noise
        for (let i = 0; i < bufferSize; i++) {
            data[i] = Math.random() * 2 - 1;
        }

        const noise = this.ctx.createBufferSource();
        noise.buffer = buffer;

        // Bandpass filter to sound like high-pressure gas nozzle
        const filter = this.ctx.createBiquadFilter();
        filter.type = 'bandpass';
        filter.frequency.value = 2400;
        filter.Q.value = 1.2;

        const gain = this.ctx.createGain();
        const now = this.ctx.currentTime;
        gain.gain.setValueAtTime(0.01, now);
        gain.gain.linearRampToValueAtTime(0.35, now + 0.3);
        gain.gain.exponentialRampToValueAtTime(0.01, now + durationSec);

        noise.connect(filter);
        filter.connect(gain);
        gain.connect(this.ctx.destination);

        noise.start(now);
    }

    // Sprinkler water spray deluge
    playSprinklerShower(durationSec = 3.0) {
        if (this.isMuted) return;
        this._initContext();

        const bufferSize = this.ctx.sampleRate * durationSec;
        const buffer = this.ctx.createBuffer(1, bufferSize, this.ctx.sampleRate);
        const data = buffer.getChannelData(0);

        for (let i = 0; i < bufferSize; i++) {
            data[i] = Math.random() * 2 - 1;
        }

        const noise = this.ctx.createBufferSource();
        noise.buffer = buffer;

        const filter = this.ctx.createBiquadFilter();
        filter.type = 'lowpass';
        filter.frequency.value = 1100;

        const gain = this.ctx.createGain();
        const now = this.ctx.currentTime;
        gain.gain.setValueAtTime(0.01, now);
        gain.gain.linearRampToValueAtTime(0.28, now + 0.4);
        gain.gain.exponentialRampToValueAtTime(0.01, now + durationSec);

        noise.connect(filter);
        filter.connect(gain);
        gain.connect(this.ctx.destination);

        noise.start(now);
    }

    // Voice announcement using browser SpeechSynthesis API
    speakEvacuation(text = "Oppmerksomhet: Brannalarm aktivert. Vennligst forlat bygningen umiddelbart via nærmeste nødutgang.") {
        if (this.isMuted) return;
        if ('speechSynthesis' in window) {
            window.speechSynthesis.cancel();
            const utterance = new SpeechSynthesisUtterance(text);
            utterance.rate = 0.95;
            utterance.pitch = 1.0;
            // Prefer Norwegian or English
            const voices = window.speechSynthesis.getVoices();
            const norwegianVoice = voices.find(v => v.lang.startsWith('nb') || v.lang.startsWith('no'));
            if (norwegianVoice) {
                utterance.voice = norwegianVoice;
            }
            window.speechSynthesis.speak(utterance);
        }
    }

    stopAll() {
        this.stopSiren();
        if ('speechSynthesis' in window) {
            window.speechSynthesis.cancel();
        }
    }
}

if (typeof window !== 'undefined') {
    window.FireAlarmAudio = FireAlarmAudio;
}
