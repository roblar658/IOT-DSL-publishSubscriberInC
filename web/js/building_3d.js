/**
 * Three.js 3D Building & Fire Protection Physical Simulation
 * Renders multi-floor facility, physical fire alarm devices, fire/smoke particles,
 * water sprinkler showers, gas flooding, door closure, and network packet pulses.
 */

class Building3D {
    constructor(containerElement, audioManager) {
        this.container = containerElement;
        this.audio = audioManager;

        // Floor offsets for exploded view
        this.floorSpacing = 3.6;
        this.explosionFactor = 0; // 0 = normal, 1 = max exploded
        this.selectedFloorFilter = 'ALL';
        this.xrayMode = false;

        // Device registry: tag -> { mesh, group, type, zone, pos, status, light, ... }
        this.devices = new Map();
        
        // Active visual effects
        this.particles = [];
        this.packetPulses = [];
        this.animatingDoors = [];
        this.spinningFans = [];
        this.strobeLights = [];
        this.activeFires = new Map(); // zone/tag -> { fireGroup, smokeGroup }

        // Event Animations (Shockwaves, Floating 3D Badges, Sparks, Halos)
        this.shockwaves = [];
        this.floatingBadges = [];
        this.impactSparks = [];
        this.activeHalos = new Map();
        this.shakeIntensity = 0;
        this.lastFrameTime = performance.now() * 0.001;
        this.onNewEvent = null; // Callback for UI event toast feed

        // Navigation & Movement State
        this.navMode = 'ORBIT'; // 'ORBIT' or 'WALK'
        this.walkSpeed = 7.5;
        this.walkYaw = -Math.PI * 0.72;
        this.walkPitch = -0.15;
        this.keysDown = { w: false, a: false, s: false, d: false, q: false, e: false, shift: false, space: false, c: false };
        this.isPointerDown = false;
        this.lastPointerX = 0;
        this.lastPointerY = 0;
        this.flyTo = null; // { startPos, endPos, startTarget, endTarget, progress, duration }

        // Elevator state
        this.elevatorCar = null;
        this.elevatorTargetY = 0;
        this.elevatorEmergencyLocked = false;

        this._initThree();
        this._buildArchitecture();
        this._buildConduitNetwork();
        this._buildSensorsAndActuators();
        this._setupInteraction();
        this._animate();
    }

    _initThree() {
        const width = this.container.clientWidth || window.innerWidth;
        const height = this.container.clientHeight || window.innerHeight;

        this.scene = new THREE.Scene();
        this.scene.background = new THREE.Color(0x0a0d14);
        this.scene.fog = new THREE.FogExp2(0x0a0d14, 0.015);

        this.camera = new THREE.PerspectiveCamera(45, width / height, 0.5, 300);
        this.camera.position.set(24, 22, 28);

        this.renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false });
        this.renderer.setSize(width, height);
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
        this.container.appendChild(this.renderer.domElement);

        if (typeof THREE.OrbitControls !== 'undefined') {
            this.controls = new THREE.OrbitControls(this.camera, this.renderer.domElement);
            this.controls.enableDamping = true;
            this.controls.dampingFactor = 0.05;
            this.controls.maxPolarAngle = Math.PI / 2 + 0.05; // don't go too far below ground
            this.controls.minDistance = 6;
            this.controls.maxDistance = 120;
            this.controls.target.set(0, 4, 0);
        }

        // Lighting
        const ambientLight = new THREE.AmbientLight(0x2a364f, 0.9);
        this.scene.add(ambientLight);

        const dirLight = new THREE.DirectionalLight(0xdde8ff, 1.2);
        dirLight.position.set(20, 40, 25);
        dirLight.castShadow = true;
        dirLight.shadow.mapSize.width = 2048;
        dirLight.shadow.mapSize.height = 2048;
        dirLight.shadow.camera.near = 5;
        dirLight.shadow.camera.far = 100;
        dirLight.shadow.camera.left = -25;
        dirLight.shadow.camera.right = 25;
        dirLight.shadow.camera.top = 25;
        dirLight.shadow.camera.bottom = -25;
        this.scene.add(dirLight);

        const blueFill = new THREE.DirectionalLight(0x183b6b, 0.6);
        blueFill.position.set(-20, 10, -20);
        this.scene.add(blueFill);

        // Ground grid
        const grid = new THREE.GridHelper(80, 40, 0x1e3a5f, 0x0f2035);
        grid.position.y = -3.51;
        this.scene.add(grid);

        window.addEventListener('resize', () => this._onResize());
    }

    _onResize() {
        const width = this.container.clientWidth;
        const height = this.container.clientHeight;
        this.camera.aspect = width / height;
        this.camera.updateProjectionMatrix();
        this.renderer.setSize(width, height);
    }

    _buildArchitecture() {
        this.floors = {
            basement: new THREE.Group(),
            floor1: new THREE.Group(),
            floor2: new THREE.Group(),
            roof: new THREE.Group()
        };

        this.floorY = {
            basement: -3.5,
            floor1: 0,
            floor2: 3.5,
            roof: 7.0
        };

        // Shared materials
        this.materials = {
            slabConcrete: new THREE.MeshStandardMaterial({
                color: 0x111928,
                roughness: 0.5,
                metalness: 0.3,
                transparent: true,
                opacity: 0.75
            }),
            slabFloor1: new THREE.MeshStandardMaterial({
                color: 0x152238,
                roughness: 0.4,
                metalness: 0.4,
                transparent: true,
                opacity: 0.65
            }),
            slabKitchen: new THREE.MeshStandardMaterial({
                color: 0x1a2b42,
                roughness: 0.4,
                metalness: 0.4,
                transparent: true,
                opacity: 0.65
            }),
            slabRoof: new THREE.MeshPhysicalMaterial({
                color: 0x253d60,
                roughness: 0.2,
                metalness: 0.1,
                transparent: true,
                opacity: 0.22,
                transmission: 0.7,
                ior: 1.4
            }),
            wallOpaque: new THREE.MeshStandardMaterial({
                color: 0x141d2e,
                roughness: 0.9,
                metalness: 0.1,
                transparent: true,
                opacity: 0.55
            }),
            glassWall: new THREE.MeshPhysicalMaterial({
                color: 0x5aa8f0,
                transparent: true,
                opacity: 0.28,
                roughness: 0.1,
                metalness: 0.1,
                transmission: 0.7,
                ior: 1.5
            }),
            serverRack: new THREE.MeshStandardMaterial({
                color: 0x0c121e,
                metalness: 0.8,
                roughness: 0.4
            }),
            kitchenMetal: new THREE.MeshStandardMaterial({
                color: 0x8a9ba8,
                metalness: 0.85,
                roughness: 0.25
            }),
            fireRed: new THREE.MeshStandardMaterial({
                color: 0xd92525,
                roughness: 0.3,
                metalness: 0.5
            }),
            steelDoor: new THREE.MeshStandardMaterial({
                color: 0xb53030,
                roughness: 0.4,
                metalness: 0.7
            }),
            warningYellow: new THREE.MeshStandardMaterial({
                color: 0xe6a800,
                roughness: 0.4,
                metalness: 0.2
            }),
            conduit: new THREE.MeshStandardMaterial({
                color: 0xc82323,
                emissive: 0x330000,
                roughness: 0.3,
                metalness: 0.8
            })
        };

        // 1. BASEMENT (Sone 0: Serverrom, Inergen-anlegg, teknisk rom)
        const bBase = this.floorY.basement;
        this.floors.basement.position.y = bBase;
        
        // Floor slab
        const bSlab = new THREE.Mesh(new THREE.BoxGeometry(22, 0.4, 16), this.materials.slabConcrete);
        bSlab.position.y = -0.2;
        bSlab.receiveShadow = true;
        this.floors.basement.add(bSlab);

        // Server Room glass perimeter (X: -9 to 0, Z: -7 to 1)
        const srvWall1 = new THREE.Mesh(new THREE.BoxGeometry(9, 3.2, 0.1), this.materials.glassWall);
        srvWall1.position.set(-4.5, 1.6, 1);
        const srvWall2 = new THREE.Mesh(new THREE.BoxGeometry(0.1, 3.2, 8), this.materials.glassWall);
        srvWall2.position.set(0, 1.6, -3);
        this.floors.basement.add(srvWall1, srvWall2);

        // Server Racks with blinking LEDs
        this.serverLeds = [];
        for (let r = 0; r < 4; r++) {
            const rack = new THREE.Mesh(new THREE.BoxGeometry(1.4, 2.5, 0.9), this.materials.serverRack);
            rack.position.set(-7.5 + r * 2.2, 1.25, -2.5);
            rack.castShadow = true;
            this.floors.basement.add(rack);

            // Blinking green/cyan server LED strip
            const ledGeo = new THREE.PlaneGeometry(1.2, 0.08);
            const ledMat = new THREE.MeshBasicMaterial({ color: 0x00ffcc });
            const ledMesh = new THREE.Mesh(ledGeo, ledMat);
            ledMesh.position.set(-7.5 + r * 2.2, 2.1, -2.04);
            this.floors.basement.add(ledMesh);
            this.serverLeds.push(ledMat);
        }

        // Inergen gas cylinder rack (X: 3 to 8, Z: -5 to -2)
        const gasRackGroup = new THREE.Group();
        for (let row = 0; row < 2; row++) {
            for (let col = 0; col < 4; col++) {
                const cyl = new THREE.Mesh(
                    new THREE.CylinderGeometry(0.22, 0.22, 2.2, 16),
                    this.materials.fireRed
                );
                cyl.position.set(3.5 + col * 0.65, 1.1, -4 + row * 0.7);
                cyl.castShadow = true;
                gasRackGroup.add(cyl);

                // Chrome valve cap
                const cap = new THREE.Mesh(
                    new THREE.CylinderGeometry(0.08, 0.12, 0.2, 12),
                    this.materials.kitchenMetal
                );
                cap.position.set(3.5 + col * 0.65, 2.25, -4 + row * 0.7);
                gasRackGroup.add(cap);
            }
        }
        // Inergen manifold pipe
        const pipe = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.06, 3.2, 12), this.materials.kitchenMetal);
        pipe.rotation.z = Math.PI / 2;
        pipe.position.set(4.8, 2.35, -3.65);
        gasRackGroup.add(pipe);
        this.floors.basement.add(gasRackGroup);

        // Technical Room equipment (Boiler/Pump)
        const boiler = new THREE.Mesh(new THREE.CylinderGeometry(1.1, 1.1, 2.2, 24), this.materials.kitchenMetal);
        boiler.position.set(6.5, 1.1, 3.5);
        this.floors.basement.add(boiler);

        // Interior basement light (Cyan glow for server room)
        const bLight = new THREE.PointLight(0x00e5ff, 1.4, 20);
        bLight.position.set(-4, 2.4, -2.5);
        this.floors.basement.add(bLight);

        // 2. FLOOR 1 (Sone 1: Resepsjon, Kontor, Branndører, Hovedtrapp)
        this.floors.floor1.position.y = this.floorY.floor1;

        const f1Slab = new THREE.Mesh(new THREE.BoxGeometry(22, 0.35, 16), this.materials.slabFloor1);
        f1Slab.position.y = -0.175;
        f1Slab.receiveShadow = true;
        this.floors.floor1.add(f1Slab);

        // Interior Floor 1 light (Office daylight)
        const f1Light = new THREE.PointLight(0xe8f0ff, 1.2, 22);
        f1Light.position.set(0, 2.5, 0);
        this.floors.floor1.add(f1Light);

        // Corridor and office division walls
        const f1WallCorr = new THREE.Mesh(new THREE.BoxGeometry(0.15, 3.2, 10), this.materials.wallOpaque);
        f1WallCorr.position.set(-1, 1.6, -2);
        const f1WallOffice = new THREE.Mesh(new THREE.BoxGeometry(8, 3.2, 0.15), this.materials.wallOpaque);
        f1WallOffice.position.set(-5, 1.6, -1);
        this.floors.floor1.add(f1WallCorr, f1WallOffice);

        // Reception counter
        const desk = new THREE.Mesh(new THREE.BoxGeometry(3.5, 1.1, 1.4), this.materials.slabFloor1);
        desk.position.set(4, 0.55, 3.5);
        this.floors.floor1.add(desk);

        // Fire Alarm Master Panel (SENTRAL_KONTROLL) in security office / reception wall
        const panelBox = new THREE.Mesh(new THREE.BoxGeometry(1.2, 1.6, 0.25), this.materials.fireRed);
        panelBox.position.set(0.15, 1.7, 4.5);
        const panelScreen = new THREE.Mesh(new THREE.PlaneGeometry(0.9, 0.7), new THREE.MeshBasicMaterial({ color: 0x00ff88 }));
        panelScreen.position.set(0.28, 1.9, 4.5);
        panelScreen.rotation.y = Math.PI / 2;
        this.panelScreenMat = panelScreen.material;
        this.floors.floor1.add(panelBox, panelScreen);

        // Double Fire Doors (BRANNDOR_E1) - Hinged for animation!
        this.doorE1 = this._createHingedDoor(-1, 1.4, 3.2, 1.4, 2.6, true);
        this.floors.floor1.add(this.doorE1.group);

        // 3. FLOOR 2 (Sone 2: Kantine, Storkjøkken, Møtesenter)
        this.floors.floor2.position.y = this.floorY.floor2;

        const f2Slab = new THREE.Mesh(new THREE.BoxGeometry(22, 0.35, 16), this.materials.slabKitchen);
        f2Slab.position.y = -0.175;
        f2Slab.receiveShadow = true;
        this.floors.floor2.add(f2Slab);

        // Commercial Kitchen Island (X: -9 to -2, Z: -6 to -1)
        const kitchenRange = new THREE.Mesh(new THREE.BoxGeometry(5.5, 0.95, 2.0), this.materials.kitchenMetal);
        kitchenRange.position.set(-5.5, 0.475, -3.5);
        
        // Stainless kitchen canopy / hood above range
        const hood = new THREE.Mesh(new THREE.BoxGeometry(5.8, 0.6, 2.4), this.materials.kitchenMetal);
        hood.position.set(-5.5, 2.8, -3.5);
        this.floors.floor2.add(kitchenRange, hood);

        // Conference Table in Sone 2 (X: 3 to 8, Z: -4 to 1)
        const confTable = new THREE.Mesh(new THREE.BoxGeometry(4.8, 0.75, 2.2), this.materials.slabKitchen);
        confTable.position.set(5.5, 0.375, -2);
        this.floors.floor2.add(confTable);

        // Fire Door E2
        this.doorE2 = this._createHingedDoor(-1, 1.4, 3.2, 1.4, 2.6, true);
        this.floors.floor2.add(this.doorE2.group);

        // 4. ROOF & HVAC (Sone 3: Røykluker, HVAC avtrekk, antenne)
        this.floors.roof.position.y = this.floorY.roof;

        const rSlab = new THREE.Mesh(new THREE.BoxGeometry(22, 0.35, 16), this.materials.slabRoof);
        rSlab.position.y = -0.175;
        this.floors.roof.add(rSlab);

        // Glass skylight grid frame on roof
        const skylightFrame = new THREE.Mesh(
            new THREE.BoxGeometry(14, 0.05, 10),
            new THREE.MeshBasicMaterial({ color: 0x38bdf8, wireframe: true })
        );
        skylightFrame.position.set(0, 0.05, 0);
        this.floors.roof.add(skylightFrame);

        // Parapet walls around roof
        const parapetMat = this.materials.wallOpaque;
        const parN = new THREE.Mesh(new THREE.BoxGeometry(22.2, 0.9, 0.3), parapetMat);
        parN.position.set(0, 0.45, -8);
        const parS = new THREE.Mesh(new THREE.BoxGeometry(22.2, 0.9, 0.3), parapetMat);
        parS.position.set(0, 0.45, 8);
        const parW = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.9, 16), parapetMat);
        parW.position.set(-11, 0.45, 0);
        const parE = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.9, 16), parapetMat);
        parE.position.set(11, 0.45, 0);
        this.floors.roof.add(parN, parS, parW, parE);

        // HVAC Mechanical Air Handler Unit (HVAC_VENTILASJON_STOPP)
        const hvacUnit = new THREE.Mesh(new THREE.BoxGeometry(4.5, 2.0, 3.5), this.materials.kitchenMetal);
        hvacUnit.position.set(-5, 1.0, 3.5);
        this.floors.roof.add(hvacUnit);

        // Smoke Exhaust Roof Hatch & Damper Fan (ROYKAVTREKK_TAK)
        const hatchGroup = new THREE.Group();
        hatchGroup.position.set(-5, 0.05, -3.5);

        // Hatch curb
        const curb = new THREE.Mesh(new THREE.BoxGeometry(3.0, 0.5, 3.0), this.materials.kitchenMetal);
        curb.position.y = 0.25;
        hatchGroup.add(curb);

        // Damper lid (opens on alarm)
        const lid = new THREE.Mesh(new THREE.BoxGeometry(3.1, 0.1, 3.1), this.materials.fireRed);
        lid.position.set(0, 0.55, 0);
        this.roofHatchLid = lid;
        hatchGroup.add(lid);

        // Extraction Fan with 4 blades
        const fanGroup = new THREE.Group();
        fanGroup.position.set(0, 0.35, 0);
        for (let b = 0; b < 4; b++) {
            const blade = new THREE.Mesh(new THREE.BoxGeometry(0.35, 0.05, 1.1), this.materials.warningYellow);
            blade.rotation.y = (b * Math.PI) / 2;
            blade.position.set(
                Math.sin((b * Math.PI) / 2) * 0.45,
                0,
                Math.cos((b * Math.PI) / 2) * 0.45
            );
            fanGroup.add(blade);
        }
        hatchGroup.add(fanGroup);
        this.spinningFans.push({ group: fanGroup, speed: 0, active: false });
        this.floors.roof.add(hatchGroup);

        // 5. ELEVATOR SHAFT (Connecting all floors)
        const shaftGroup = new THREE.Group();
        shaftGroup.position.set(0, 2.0, 0);

        // Glass shaft enclosure
        const shaftGeo = new THREE.BoxGeometry(2.4, 15, 2.4);
        const shaftMesh = new THREE.Mesh(shaftGeo, this.materials.glassWall);
        shaftGroup.add(shaftMesh);

        // Elevator Cab
        const cabGeo = new THREE.BoxGeometry(2.0, 2.5, 2.0);
        this.elevatorCar = new THREE.Mesh(cabGeo, this.materials.kitchenMetal);
        this.elevatorCar.position.y = this.floorY.floor1 + 1.25;
        shaftGroup.add(this.elevatorCar);

        this.scene.add(shaftGroup);

        // Add all floor groups to scene
        this.scene.add(this.floors.basement);
        this.scene.add(this.floors.floor1);
        this.scene.add(this.floors.floor2);
        this.scene.add(this.floors.roof);
    }

    _createHingedDoor(x, y, z, width, height, isOpening) {
        const group = new THREE.Group();
        group.position.set(x, y, z);

        // Frame
        const frameMat = this.materials.wallOpaque;
        const frameL = new THREE.Mesh(new THREE.BoxGeometry(0.1, height, 0.1), frameMat);
        frameL.position.set(-width / 2, 0, 0);
        const frameR = new THREE.Mesh(new THREE.BoxGeometry(0.1, height, 0.1), frameMat);
        frameR.position.set(width / 2, 0, 0);
        const frameT = new THREE.Mesh(new THREE.BoxGeometry(width, 0.1, 0.1), frameMat);
        frameT.position.set(0, height / 2, 0);
        group.add(frameL, frameR, frameT);

        // Hinged door leaf (hinge at left frame)
        const hinge = new THREE.Group();
        hinge.position.set(-width / 2, 0, 0);

        const leaf = new THREE.Mesh(
            new THREE.BoxGeometry(width, height - 0.05, 0.08),
            this.materials.steelDoor
        );
        // Center of leaf is offset from hinge by width/2
        leaf.position.set(width / 2, 0, 0);
        hinge.add(leaf);

        // Glass vision panel in door
        const vision = new THREE.Mesh(
            new THREE.BoxGeometry(0.3, 0.7, 0.1),
            this.materials.glassWall
        );
        vision.position.set(width * 0.65, 0.2, 0);
        hinge.add(vision);

        // Normal state: held OPEN at 85 degrees by magnetic door holder!
        hinge.rotation.y = isOpening ? Math.PI * 0.45 : 0;
        group.add(hinge);

        return {
            group,
            hinge,
            targetAngle: Math.PI * 0.45,
            isClosed: false
        };
    }

    _buildConduitNetwork() {
        // Red glowing conduit network running along the building core
        this.conduitPath = [
            new THREE.Vector3(0.15, 1.7, 4.5), // Sentral panel
            new THREE.Vector3(0.15, 3.1, 4.5), // Up to ceiling
            new THREE.Vector3(0.15, 3.1, 0),   // Central riser
            new THREE.Vector3(0.15, -0.4, 0),  // Down to basement
            new THREE.Vector3(0.15, 6.6, 0),   // Up to 2nd floor
            new THREE.Vector3(0.15, 10.1, 0)   // Up to roof
        ];
    }

    _createTagBadge(text, color = '#38bdf8') {
        const canvas = document.createElement('canvas');
        canvas.width = 256;
        canvas.height = 64;
        const ctx = canvas.getContext('2d');
        ctx.fillStyle = 'rgba(8, 14, 26, 0.88)';
        ctx.strokeStyle = color;
        ctx.lineWidth = 3;
        if (ctx.roundRect) {
            ctx.roundRect(4, 4, 248, 56, 10);
        } else {
            ctx.rect(4, 4, 248, 56);
        }
        ctx.fill();
        ctx.stroke();
        ctx.font = 'bold 20px monospace';
        ctx.fillStyle = color;
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(text, 128, 32);

        const tex = new THREE.CanvasTexture(canvas);
        const mat = new THREE.SpriteMaterial({ map: tex, depthTest: false, transparent: true });
        const sprite = new THREE.Sprite(mat);
        sprite.scale.set(1.5, 0.38, 1.0);
        return sprite;
    }

    _buildSensorsAndActuators() {
        // Helper to create optical smoke detector puck
        const createSmokeDetector = (tag, x, y, z, floorGroup, zoneName) => {
            const group = new THREE.Group();
            group.position.set(x, y, z);

            // Base mounting ring
            const base = new THREE.Mesh(
                new THREE.CylinderGeometry(0.32, 0.38, 0.1, 24),
                new THREE.MeshStandardMaterial({ color: 0xeeeeee, roughness: 0.3 })
            );
            // Optical sampling chamber
            const chamber = new THREE.Mesh(
                new THREE.CylinderGeometry(0.24, 0.3, 0.12, 24),
                new THREE.MeshStandardMaterial({ color: 0x222222, roughness: 0.8 })
            );
            chamber.position.y = -0.07;

            // Tricolor status LED
            const led = new THREE.Mesh(
                new THREE.SphereGeometry(0.06, 12, 12),
                new THREE.MeshBasicMaterial({ color: 0x00ff44 })
            );
            led.position.set(0.16, -0.09, 0);

            // Floating 3D Tag Label
            const badge = this._createTagBadge(tag, '#38bdf8');
            badge.position.set(0, 0.5, 0);

            group.add(base, chamber, led, badge);
            floorGroup.add(group);

            const dev = {
                tag,
                group,
                ledMat: led.material,
                type: 'SMOKE_DETECTOR',
                zone: zoneName,
                worldPos: new THREE.Vector3(),
                status: 'NORMAL',
                isTriggered: false
            };
            this.devices.set(tag, dev);
            return dev;
        };

        // Helper to create thermal / heat detector
        const createHeatDetector = (tag, x, y, z, floorGroup, zoneName) => {
            const group = new THREE.Group();
            group.position.set(x, y, z);

            const base = new THREE.Mesh(
                new THREE.CylinderGeometry(0.25, 0.3, 0.08, 24),
                new THREE.MeshStandardMaterial({ color: 0xf5f5f5, roughness: 0.2 })
            );
            // Thermal probe tip
            const probe = new THREE.Mesh(
                new THREE.ConeGeometry(0.14, 0.16, 16),
                this.materials.kitchenMetal
            );
            probe.position.y = -0.1;

            const led = new THREE.Mesh(
                new THREE.SphereGeometry(0.04, 12, 12),
                new THREE.MeshBasicMaterial({ color: 0x00ff44 })
            );
            led.position.set(0.12, -0.06, 0);

            // Floating 3D Tag Label
            const badge = this._createTagBadge(tag, '#f59e0b');
            badge.position.set(0, 0.45, 0);

            group.add(base, probe, led, badge);
            floorGroup.add(group);

            const dev = {
                tag,
                group,
                ledMat: led.material,
                type: 'HEAT_DETECTOR',
                zone: zoneName,
                worldPos: new THREE.Vector3(),
                status: 'NORMAL',
                isTriggered: false
            };
            this.devices.set(tag, dev);
            return dev;
        };

        // Helper to create manual call point (break glass)
        const createManualCallPoint = (tag, x, y, z, floorGroup, zoneName, rotY = 0) => {
            const group = new THREE.Group();
            group.position.set(x, y, z);
            group.rotation.y = rotY;

            // Red box
            const box = new THREE.Mesh(
                new THREE.BoxGeometry(0.5, 0.55, 0.15),
                this.materials.fireRed
            );
            // White frangible glass center
            const glass = new THREE.Mesh(
                new THREE.PlaneGeometry(0.28, 0.28),
                new THREE.MeshBasicMaterial({ color: 0xffffff })
            );
            glass.position.z = 0.08;

            // Status LED
            const led = new THREE.Mesh(
                new THREE.SphereGeometry(0.04, 8, 8),
                new THREE.MeshBasicMaterial({ color: 0x00ff44 })
            );
            led.position.set(0, 0.18, 0.08);

            // Floating 3D Tag Label
            const badge = this._createTagBadge(tag, '#ef4444');
            badge.position.set(0, 0.55, 0);

            group.add(box, glass, led, badge);
            floorGroup.add(group);

            const dev = {
                tag,
                group,
                ledMat: led.material,
                type: 'MANUAL_CALL_POINT',
                zone: zoneName,
                worldPos: new THREE.Vector3(),
                status: 'NORMAL',
                isTriggered: false
            };
            this.devices.set(tag, dev);
            return dev;
        };

        // Helper to create Wall Siren / Strobe
        const createSirenStrobe = (tag, x, y, z, floorGroup, zoneName, rotY = 0) => {
            const group = new THREE.Group();
            group.position.set(x, y, z);
            group.rotation.y = rotY;

            // Red horn box
            const box = new THREE.Mesh(new THREE.BoxGeometry(0.45, 0.65, 0.18), this.materials.fireRed);
            // Clear strobe lens
            const lens = new THREE.Mesh(
                new THREE.CylinderGeometry(0.14, 0.16, 0.16, 16),
                new THREE.MeshPhysicalMaterial({ color: 0xffffff, transmission: 0.9, opacity: 0.9, roughness: 0.1 })
            );
            lens.position.set(0, 0.18, 0.11);
            lens.rotation.x = Math.PI / 2;

            // Strobe PointLight
            const pLight = new THREE.PointLight(0xff1100, 0, 14);
            pLight.position.set(0, 0.18, 0.45);
            group.add(pLight);

            // Floating 3D Tag Label
            const badge = this._createTagBadge(tag, '#f43f5e');
            badge.position.set(0, 0.65, 0);

            group.add(box, lens, badge);
            floorGroup.add(group);

            const dev = {
                tag,
                group,
                light: pLight,
                type: 'SIREN_STROBE',
                zone: zoneName,
                worldPos: new THREE.Vector3(),
                status: 'IDLE',
                isActive: false
            };
            this.devices.set(tag, dev);
            this.strobeLights.push(dev);
            return dev;
        };

        // Helper to create Sprinkler Head
        const createSprinkler = (tag, x, y, z, floorGroup, zoneName) => {
            const group = new THREE.Group();
            group.position.set(x, y, z);

            const body = new THREE.Mesh(new THREE.CylinderGeometry(0.1, 0.1, 0.22, 12), this.materials.kitchenMetal);
            const deflector = new THREE.Mesh(new THREE.CylinderGeometry(0.22, 0.03, 0.05, 16), this.materials.kitchenMetal);
            deflector.position.y = -0.14;

            // Red glass bulb
            const bulb = new THREE.Mesh(new THREE.CylinderGeometry(0.04, 0.04, 0.12, 8), this.materials.fireRed);
            bulb.position.y = -0.06;

            const badge = this._createTagBadge(tag, '#0ea5e9');
            badge.position.set(0, 0.45, 0);

            group.add(body, deflector, bulb, badge);
            floorGroup.add(group);

            const dev = {
                tag,
                group,
                type: 'SPRINKLER',
                zone: zoneName,
                worldPos: new THREE.Vector3(),
                status: 'IDLE',
                isActive: false
            };
            this.devices.set(tag, dev);
            return dev;
        };

        // Helper to create Inergen Gas Nozzle
        const createGasNozzle = (tag, x, y, z, floorGroup, zoneName) => {
            const group = new THREE.Group();
            group.position.set(x, y, z);

            const body = new THREE.Mesh(new THREE.CylinderGeometry(0.12, 0.18, 0.28, 12), this.materials.kitchenMetal);
            const badge = this._createTagBadge(tag, '#06b6d4');
            badge.position.set(0, 0.45, 0);

            group.add(body, badge);
            floorGroup.add(group);

            const dev = {
                tag,
                group,
                type: 'GAS_NOZZLE',
                zone: zoneName,
                worldPos: new THREE.Vector3(),
                status: 'IDLE',
                isActive: false
            };
            this.devices.set(tag, dev);
            return dev;
        };

        // -------------------------------------------------------------
        // INSTANTIATE PHYSICAL DEVICES FROM DSL TAGS
        // -------------------------------------------------------------

        // SONE 0 (Kjeller)
        createSmokeDetector('VESDA_SERVER', -4.5, 3.1, -2.5, this.floors.basement, 'Sone 0 Serverrom');
        createHeatDetector('VARME_TEKNISK', 6.5, 3.1, 3.5, this.floors.basement, 'Sone 0 Teknisk rom');
        createSirenStrobe('SIRENE_SERVER', -0.2, 2.2, -1.0, this.floors.basement, 'Sone 0 Serverrom', -Math.PI / 2);
        createGasNozzle('INERGEN_GASS_SERVER', -4.5, 3.1, -2.5, this.floors.basement, 'Sone 0 Serverrom');

        // SONE 1 (1. Etasje)
        createSmokeDetector('ROYK_RESEPSJON', 4.0, 3.1, 2.5, this.floors.floor1, 'Sone 1 Resepsjon');
        createSmokeDetector('ROYK_KONTOR', -5.0, 3.1, -4.0, this.floors.floor1, 'Sone 1 Kontor');
        createManualCallPoint('MANUELL_MELDER_E1', -0.9, 1.4, 2.4, this.floors.floor1, 'Sone 1 Hovedtrapp', Math.PI / 2);
        createSirenStrobe('SIRENE_E1', -0.9, 2.5, -2.0, this.floors.floor1, 'Sone 1 Korridor', Math.PI / 2);
        createSirenStrobe('BLITZ_VARSLING', 4.0, 2.5, 6.9, this.floors.floor1, 'Sone 1 Resepsjon', Math.PI);

        // Virtual/Actuator devices mapped to building objects
        this.devices.set('SENTRAL_KONTROLL', {
            tag: 'SENTRAL_KONTROLL',
            group: this.floors.floor1,
            type: 'PANEL',
            zone: 'Sentral Sikkerhet',
            worldPos: new THREE.Vector3(0.15, 1.7, 4.5),
            status: 'ONLINE'
        });

        this.devices.set('BRANNDOR_E1', {
            tag: 'BRANNDOR_E1',
            group: this.doorE1.group,
            doorObj: this.doorE1,
            type: 'FIRE_DOOR',
            zone: 'Sone 1 Trapp',
            worldPos: new THREE.Vector3(-1, 1.4, 3.2),
            status: 'OPEN_HELD'
        });

        this.devices.set('TALEVARSLING_BYGG', {
            tag: 'TALEVARSLING_BYGG',
            group: this.floors.floor1,
            type: 'PA_SYSTEM',
            zone: 'Hele bygget',
            worldPos: new THREE.Vector3(0, 3.1, 0),
            status: 'STANDBY'
        });

        // SONE 2 (2. Etasje)
        createHeatDetector('VARME_KJOKKEN', -5.5, 3.1, -3.5, this.floors.floor2, 'Sone 2 Storkjøkken');
        createSmokeDetector('GASS_KJOKKEN', -3.0, 1.0, -3.5, this.floors.floor2, 'Sone 2 Gass-sikring');
        createSmokeDetector('ROYK_MOTE', 5.5, 3.1, -2.0, this.floors.floor2, 'Sone 2 Møtesenter');
        createManualCallPoint('MANUELL_MELDER_E2', -0.9, 1.4, 2.4, this.floors.floor2, 'Sone 2 Trapp E2', Math.PI / 2);
        createSirenStrobe('SIRENE_E2', -0.9, 2.5, -2.0, this.floors.floor2, 'Sone 2 Spisesal', Math.PI / 2);
        createSprinkler('SPRINKLER_KJOKKEN', -5.5, 3.1, -3.5, this.floors.floor2, 'Sone 2 Storkjøkken');

        this.devices.set('BRANNDOR_E2', {
            tag: 'BRANNDOR_E2',
            group: this.doorE2.group,
            doorObj: this.doorE2,
            type: 'FIRE_DOOR',
            zone: 'Sone 2 Trapp',
            worldPos: new THREE.Vector3(-1, 1.4 + 3.5, 3.2),
            status: 'OPEN_HELD'
        });

        // SONE 3 (Tak & Felles)
        this.devices.set('ROYKAVTREKK_TAK', {
            tag: 'ROYKAVTREKK_TAK',
            group: this.floors.roof,
            type: 'ROOF_DAMPER',
            zone: 'Tak Røykventilasjon',
            worldPos: new THREE.Vector3(-5, 7.5, -3.5),
            status: 'CLOSED'
        });

        this.devices.set('HVAC_VENTILASJON_STOPP', {
            tag: 'HVAC_VENTILASJON_STOPP',
            group: this.floors.roof,
            type: 'HVAC_CONTROL',
            zone: 'Tak Aggregat',
            worldPos: new THREE.Vector3(-5, 8.0, 3.5),
            status: 'RUNNING'
        });

        this.devices.set('HEIS_NODRETUR', {
            tag: 'HEIS_NODRETUR',
            group: this.elevatorCar,
            type: 'ELEVATOR',
            zone: 'Heissjakt',
            worldPos: new THREE.Vector3(0, 1.5, 0),
            status: 'OPERATIONAL'
        });

        this.devices.set('BRANNVESEN_110', {
            tag: 'BRANNVESEN_110',
            group: this.floors.floor1,
            type: 'FIRE_BRIGADE_LINK',
            zone: 'Nødsentral 110',
            worldPos: new THREE.Vector3(12, 10, 0),
            status: 'CONNECTED'
        });

        // Compute world positions for raycasting and packet animation
        this.devices.forEach((dev) => {
            if (dev.group) {
                dev.group.updateMatrixWorld(true);
                dev.worldPos.setFromMatrixPosition(dev.group.matrixWorld);
            }
        });
    }

    _setupInteraction() {
        this.raycaster = new THREE.Raycaster();
        this.mouse = new THREE.Vector2();

        // Keyboard navigation for Walk Mode
        window.addEventListener('keydown', (e) => {
            if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) return;
            const key = e.code;
            if (key === 'KeyW' || key === 'ArrowUp') this.keysDown.w = true;
            if (key === 'KeyS' || key === 'ArrowDown') this.keysDown.s = true;
            if (key === 'KeyA' || key === 'ArrowLeft') this.keysDown.a = true;
            if (key === 'KeyD' || key === 'ArrowRight') this.keysDown.d = true;
            if (key === 'KeyQ' || key === 'KeyC') this.keysDown.q = true;
            if (key === 'KeyE' || key === 'Space') this.keysDown.e = true;
            if (key === 'ShiftLeft' || key === 'ShiftRight') this.keysDown.shift = true;
        });

        window.addEventListener('keyup', (e) => {
            const key = e.code;
            if (key === 'KeyW' || key === 'ArrowUp') this.keysDown.w = false;
            if (key === 'KeyS' || key === 'ArrowDown') this.keysDown.s = false;
            if (key === 'KeyA' || key === 'ArrowLeft') this.keysDown.a = false;
            if (key === 'KeyD' || key === 'ArrowRight') this.keysDown.d = false;
            if (key === 'KeyQ' || key === 'KeyC') this.keysDown.q = false;
            if (key === 'KeyE' || key === 'Space') this.keysDown.e = false;
            if (key === 'ShiftLeft' || key === 'ShiftRight') this.keysDown.shift = false;
        });

        // Pointer look in Walk Mode & click raycasting
        this.renderer.domElement.addEventListener('pointerdown', (e) => {
            this.isPointerDown = true;
            this.lastPointerX = e.clientX;
            this.lastPointerY = e.clientY;

            const rect = this.renderer.domElement.getBoundingClientRect();
            this.mouse.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
            this.mouse.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;

            this.raycaster.setFromCamera(this.mouse, this.camera);
            const clickableObjects = [];
            this.devices.forEach((dev) => {
                if (dev.group) {
                    dev.group.traverse((child) => {
                        if (child.isMesh) {
                            child.userData.deviceTag = dev.tag;
                            clickableObjects.push(child);
                        }
                    });
                }
            });

            const intersects = this.raycaster.intersectObjects(clickableObjects, false);
            if (intersects.length > 0) {
                const tag = intersects[0].object.userData.deviceTag;
                if (tag && this.onDeviceClick) {
                    this.onDeviceClick(tag, this.devices.get(tag));
                }
            }
        });

        window.addEventListener('pointermove', (e) => {
            if (!this.isPointerDown) return;
            if (this.navMode === 'WALK') {
                const dx = e.clientX - this.lastPointerX;
                const dy = e.clientY - this.lastPointerY;
                this.walkYaw -= dx * 0.0035;
                this.walkPitch -= dy * 0.0035;
                this.walkPitch = THREE.MathUtils.clamp(this.walkPitch, -1.35, 1.35);
                this.lastPointerX = e.clientX;
                this.lastPointerY = e.clientY;
            }
        });

        window.addEventListener('pointerup', () => {
            this.isPointerDown = false;
        });

        // Double-click to fly smoothly to clicked point
        this.renderer.domElement.addEventListener('dblclick', (e) => {
            const rect = this.renderer.domElement.getBoundingClientRect();
            this.mouse.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
            this.mouse.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
            this.raycaster.setFromCamera(this.mouse, this.camera);
            const hits = this.raycaster.intersectObjects(this.scene.children, true);
            if (hits.length > 0) {
                const point = hits[0].point;
                const currentDir = this.camera.position.clone().sub(point).normalize();
                const targetPos = point.clone().add(currentDir.multiplyScalar(8.0));
                this.flyCameraTo(targetPos, point, 0.85);
            }
        });
    }

    // Exploded view slider (separates floors vertically)
    setExplosion(factor) {
        this.explosionFactor = THREE.MathUtils.clamp(factor, 0, 1);
        const gap = this.explosionFactor * 5.0;

        this.floors.basement.position.y = this.floorY.basement - gap * 0.8;
        this.floors.floor1.position.y = this.floorY.floor1;
        this.floors.floor2.position.y = this.floorY.floor2 + gap * 0.9;
        this.floors.roof.position.y = this.floorY.roof + gap * 1.8;

        this.devices.forEach((dev) => {
            if (dev.group) {
                dev.group.updateMatrixWorld(true);
                dev.worldPos.setFromMatrixPosition(dev.group.matrixWorld);
            }
        });
    }

    // Floor isolation filter ('ALL', 'BASEMENT', 'FLOOR1', 'FLOOR2', 'ROOF')
    setFloorFilter(filter) {
        this.selectedFloorFilter = filter;
        const setVisible = (group, vis) => {
            group.visible = vis;
        };

        if (filter === 'ALL') {
            setVisible(this.floors.basement, true);
            setVisible(this.floors.floor1, true);
            setVisible(this.floors.floor2, true);
            setVisible(this.floors.roof, true);
        } else if (filter === 'BASEMENT') {
            setVisible(this.floors.basement, true);
            setVisible(this.floors.floor1, false);
            setVisible(this.floors.floor2, false);
            setVisible(this.floors.roof, false);
        } else if (filter === 'FLOOR1') {
            setVisible(this.floors.basement, false);
            setVisible(this.floors.floor1, true);
            setVisible(this.floors.floor2, false);
            setVisible(this.floors.roof, false);
        } else if (filter === 'FLOOR2') {
            setVisible(this.floors.basement, false);
            setVisible(this.floors.floor1, false);
            setVisible(this.floors.floor2, true);
            setVisible(this.floors.roof, false);
        } else if (filter === 'ROOF') {
            setVisible(this.floors.basement, false);
            setVisible(this.floors.floor1, false);
            setVisible(this.floors.floor2, false);
            setVisible(this.floors.roof, true);
        }
    }

    // X-Ray / Wireframe Mode
    setXrayMode(enabled) {
        this.xrayMode = enabled;
        this.materials.wallOpaque.opacity = enabled ? 0.25 : 0.85;
        this.materials.slabConcrete.wireframe = enabled;
        this.materials.slabFloor1.wireframe = enabled;
        this.materials.slabKitchen.wireframe = enabled;
    }

    // Navigation mode switcher ('ORBIT' or 'WALK')
    setNavigationMode(mode) {
        this.navMode = mode;
        const walkHud = document.getElementById('walk-hud');
        if (mode === 'WALK') {
            if (this.controls) this.controls.enabled = false;
            if (walkHud) walkHud.classList.remove('hidden');
            // If camera is high up or outside, bring it into 1st floor corridor
            if (this.camera.position.length() > 30 || this.camera.position.y > 8) {
                this.flyCameraTo(new THREE.Vector3(2, 1.7, 8), new THREE.Vector3(0, 1.7, 0), 0.8);
                this.walkYaw = -Math.PI * 0.72;
                this.walkPitch = -0.1;
            }
        } else {
            if (this.controls) {
                this.controls.enabled = true;
                const dir = new THREE.Vector3();
                this.camera.getWorldDirection(dir);
                this.controls.target.copy(this.camera.position).add(dir.multiplyScalar(12));
                this.controls.update();
            }
            if (walkHud) walkHud.classList.add('hidden');
        }
    }

    // Smooth cinematic camera flight to target position & lookAt
    flyCameraTo(targetPos, targetLookAt, duration = 0.85) {
        const startTarget = (this.controls && this.controls.target) ? 
            this.controls.target.clone() : 
            this.camera.position.clone().add(new THREE.Vector3(0, 0, -5));

        this.flyTo = {
            startPos: this.camera.position.clone(),
            endPos: targetPos.clone(),
            startTarget: startTarget,
            endTarget: targetLookAt.clone(),
            progress: 0,
            duration: Math.max(0.3, duration || 0.85)
        };
    }

    // Camera preset views with smooth cinematic fly-to
    setCameraPreset(preset) {
        const targetPos = new THREE.Vector3();
        const targetLook = new THREE.Vector3();

        switch (preset) {
            case 'OVERVIEW':
                targetPos.set(24, 22, 28);
                targetLook.set(0, 4, 0);
                break;
            case 'SERVER_ROOM':
                targetPos.set(-6, 0.8, 6.5);
                targetLook.set(-4.5, -2, -2.5);
                break;
            case 'KITCHEN':
                targetPos.set(-4, 7.5, 6.0);
                targetLook.set(-5.5, 4, -3.5);
                break;
            case 'CENTRAL_PANEL':
                targetPos.set(4, 2.5, 6.5);
                targetLook.set(0.15, 1.7, 4.5);
                break;
            case 'ROOF_VENT':
                targetPos.set(-8, 13.5, -4.5);
                targetLook.set(-5, 7.5, -3.5);
                break;
        }

        this.flyCameraTo(targetPos, targetLook, 0.85);
    }

    // -------------------------------------------------------------
    // EVENT HANDLERS TRIGGERED BY DSL RUNTIME
    // -------------------------------------------------------------

    // -------------------------------------------------------------
    // EVENT HANDLERS TRIGGERED BY DSL RUNTIME
    // -------------------------------------------------------------

    handleDeviceStateChange(evt) {
        const { fromTag, toTag, type, payload } = evt;
        const fromDev = this.devices.get(fromTag);
        const toDev = this.devices.get(toTag);

        // Update publisher device
        if (fromDev) {
            fromDev.status = 'ALARM_ACTIVE';
            if (fromDev.ledMat) {
                fromDev.ledMat.color.setHex(0xff0022); // red alert LED
            }

            // Expanding shockwave beacon from alarmed detector
            const pulseColor = fromDev.type === 'HEAT_DETECTOR' ? 0xff6600 : 0xff1133;
            this.triggerShockwave(fromDev.worldPos, pulseColor, 4.5);
            this.addDeviceHalo(fromTag, fromDev.worldPos, pulseColor);

            // Floating 3D holographic badge
            const labelText = fromDev.type === 'HEAT_DETECTOR' ? 'VARMEALARM 88°C' :
                             (fromDev.type === 'SMOKE_DETECTOR' ? 'OPTISK RØYKALARM' :
                             (fromDev.type === 'MANUAL_CALL_POINT' ? 'MANUELL MELDER UTØST' : 'BRANNDETEKSJON'));
            const icon = fromDev.type === 'HEAT_DETECTOR' ? '🔥' :
                         (fromDev.type === 'MANUAL_CALL_POINT' ? '🚨' : '⚠️');
            this.triggerFloatingBadge(fromDev.worldPos, `${fromTag}: ${labelText}`, icon, '#ef4444');
            this.triggerCameraShake(0.25);

            if (this.onNewEvent) {
                this.onNewEvent({
                    title: `${icon} ${fromTag}`,
                    desc: labelText,
                    type: 'alarm'
                });
            }

            // Start smoke/fire particles at publisher location if not already started
            if (fromDev.type === 'SMOKE_DETECTOR' || fromDev.type === 'HEAT_DETECTOR') {
                this.startFireParticles(fromTag, fromDev.worldPos);
            }
        }

        // Update subscriber device
        if (toDev) {
            toDev.status = 'ACTIVATED';

            // 1. Sirens / Strobes
            if (toDev.type === 'SIREN_STROBE') {
                toDev.isActive = true;
                this.triggerShockwave(toDev.worldPos, 0xff0044, 3.0);
                this.triggerFloatingBadge(toDev.worldPos, `${toTag}: SIRENE & BLITZ AKTIV`, '📢', '#ef4444');
                if (this.audio) this.audio.startSiren();
                if (this.onNewEvent) {
                    this.onNewEvent({
                        title: `📢 ${toTag}`,
                        desc: 'Sirene & optisk blitz aktivert (EN 54-3)',
                        type: 'alarm'
                    });
                }
            }

            // 2. Fire Doors (BRANNDOR_E1, BRANNDOR_E2)
            if (toDev.type === 'FIRE_DOOR' && toDev.doorObj) {
                toDev.doorObj.targetAngle = 0; // swing closed to 0 deg
                toDev.doorObj.isClosed = true;
                toDev.status = 'MAGNET_RELEASED_CLOSED';
                this.triggerShockwave(toDev.worldPos, 0xf59e0b, 2.2);
                this.triggerFloatingBadge(toDev.worldPos, `${toTag}: MAGNET SLUPPET - DØR LUKKES`, '🚪', '#f59e0b');
                if (this.audio) this.audio.playDoorClunk();
                if (this.onNewEvent) {
                    this.onNewEvent({
                        title: `🚪 ${toTag}`,
                        desc: 'Holde-magnet frigjort, dør lukkes automatisk',
                        type: 'routing'
                    });
                }
            }

            // 3. Sprinkler
            if (toDev.type === 'SPRINKLER') {
                toDev.isActive = true;
                this.triggerShockwave(toDev.worldPos, 0x00d0ff, 4.2);
                this.triggerFloatingBadge(toDev.worldPos, `${toTag}: VANNTÅKE SPRINKLER AKTIVERT`, '💦', '#00d0ff');
                this.startSprinklerShower(toDev.worldPos);
                if (this.audio) this.audio.playSprinklerShower(4.0);
                if (this.onNewEvent) {
                    this.onNewEvent({
                        title: `💦 ${toTag}`,
                        desc: 'Sprinklerdyse utløst, vanntåke aktivert over kokeøy',
                        type: 'alarm'
                    });
                }
            }

            // 4. Inergen Gas Nozzle
            if (toDev.type === 'GAS_NOZZLE') {
                toDev.isActive = true;
                this.triggerShockwave(toDev.worldPos, 0x00ffff, 5.0);
                this.triggerFloatingBadge(toDev.worldPos, `${toTag}: INERGEN GASS-UTBLÅSNING`, '💨', '#06b6d4');
                this.triggerCameraShake(0.35);
                this.startInergenGasCloud(toDev.worldPos);
                if (this.audio) this.audio.playGasDischarge(3.5);
                if (this.onNewEvent) {
                    this.onNewEvent({
                        title: `⚠️ ${toTag}`,
                        desc: 'Inergen gass-slukking utblåst i serverrom',
                        type: 'alarm'
                    });
                }
            }

            // 5. Roof Hatch & Smoke Fan (ROYKAVTREKK_TAK)
            if (toDev.tag === 'ROYKAVTREKK_TAK') {
                if (this.roofHatchLid) {
                    this.roofHatchLid.rotation.x = -Math.PI * 0.35; // open lid
                    this.roofHatchLid.position.y = 1.2;
                }
                this.spinningFans.forEach(f => {
                    f.active = true;
                    f.speed = 18.0;
                });
                toDev.status = 'EXTRACTING_SMOKE';
                this.triggerFloatingBadge(toDev.worldPos, `${toTag}: RØYKLUKER ÅPNET & AVTREKKSVIFTE`, '🌀', '#38bdf8');
                if (this.onNewEvent) {
                    this.onNewEvent({
                        title: `🌀 ${toTag}`,
                        desc: 'Røykluker åpnet, avtrekksvifte starter',
                        type: 'routing'
                    });
                }
            }

            // 6. HVAC Shutdown
            if (toDev.tag === 'HVAC_VENTILASJON_STOPP') {
                toDev.status = 'EMERGENCY_SHUTDOWN';
                this.triggerFloatingBadge(toDev.worldPos, `${toTag}: NØDSTOPP VENTILASJON`, '🛑', '#f59e0b');
                if (this.onNewEvent) {
                    this.onNewEvent({
                        title: `🛑 ${toTag}`,
                        desc: 'Ventilasjonsaggregat nødavstengt mot røyksmitte',
                        type: 'routing'
                    });
                }
            }

            // 7. Elevator Recall
            if (toDev.tag === 'HEIS_NODRETUR') {
                this.elevatorEmergencyLocked = true;
                this.elevatorTargetY = this.floorY.floor1 + 1.25; // ground floor
                toDev.status = 'LOCKED_AT_GROUND';
                this.triggerFloatingBadge(toDev.worldPos, `${toTag}: HEIS RETUR TIL 1. ETASJE`, '🛗', '#f59e0b');
                if (this.onNewEvent) {
                    this.onNewEvent({
                        title: `🛗 ${toTag}`,
                        desc: 'Heis returnert og parkert med åpne dører i 1. etasje',
                        type: 'routing'
                    });
                }
            }

            // 8. PA Voice Evacuation
            if (toDev.tag === 'TALEVARSLING_BYGG') {
                toDev.status = 'VOICE_BROADCAST_ACTIVE';
                this.triggerFloatingBadge(toDev.worldPos, `${toTag}: TALEVARSLING AKTIVERT`, '🔊', '#ef4444');
                if (this.audio) {
                    this.audio.speakEvacuation();
                }
                if (this.onNewEvent) {
                    this.onNewEvent({
                        title: `🔊 ${toTag}`,
                        desc: 'Automatisert talevarsling (EN 54-16) sender evakueringsmelding',
                        type: 'alarm'
                    });
                }
            }
        }

        // Check if Central Panel mimic display should turn red
        if (this.panelScreenMat) {
            this.panelScreenMat.color.setHex(0xff1122);
        }
    }

    // Expanding glowing neon ring shockwave on ceiling/floor
    triggerShockwave(pos, colorHex = 0xff0044, maxRadius = 4.5) {
        const geo = new THREE.RingGeometry(0.15, 0.35, 32);
        const mat = new THREE.MeshBasicMaterial({
            color: colorHex,
            transparent: true,
            opacity: 0.9,
            side: THREE.DoubleSide
        });
        const mesh = new THREE.Mesh(geo, mat);
        mesh.position.copy(pos);
        mesh.rotation.x = -Math.PI / 2;
        mesh.position.y += 0.05;
        this.scene.add(mesh);

        this.shockwaves.push({
            mesh,
            radius: 0.2,
            maxRadius,
            opacity: 0.9,
            speed: 4.5 + Math.random() * 1.5
        });
    }

    // Floating 3D holographic badge sprite that pops up above device
    triggerFloatingBadge(pos, text, icon = '🚨', colorHex = '#ef4444') {
        const canvas = document.createElement('canvas');
        canvas.width = 512;
        canvas.height = 128;
        const ctx = canvas.getContext('2d');

        // Rounded badge background with neon border
        ctx.fillStyle = 'rgba(15, 23, 42, 0.90)';
        ctx.strokeStyle = colorHex;
        ctx.lineWidth = 6;
        ctx.beginPath();
        ctx.roundRect(8, 8, 496, 112, 24);
        ctx.fill();
        ctx.stroke();

        // Neon accent indicator
        ctx.fillStyle = colorHex;
        ctx.beginPath();
        ctx.roundRect(8, 8, 22, 112, [24, 0, 0, 24]);
        ctx.fill();

        // Text & icon
        ctx.font = 'bold 32px -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif';
        ctx.fillStyle = '#ffffff';
        ctx.fillText(`${icon}  ${text}`, 46, 70);

        const texture = new THREE.CanvasTexture(canvas);
        const mat = new THREE.SpriteMaterial({
            map: texture,
            transparent: true,
            depthTest: false
        });
        const sprite = new THREE.Sprite(mat);
        sprite.position.set(pos.x, pos.y + 0.9, pos.z);
        sprite.scale.set(3.2, 0.8, 1);
        this.scene.add(sprite);

        this.floatingBadges.push({
            sprite,
            vy: 0.008,
            life: 0,
            maxLife: 4.5,
            initialScale: new THREE.Vector3(3.2, 0.8, 1)
        });
    }

    // Impact spark burst on packet delivery
    triggerImpactSparks(pos, colorHex = 0x00f0ff, count = 10) {
        const group = new THREE.Group();
        group.position.copy(pos);
        this.scene.add(group);

        const sparks = [];
        for (let i = 0; i < count; i++) {
            const sGeo = new THREE.SphereGeometry(0.05, 6, 6);
            const sMat = new THREE.MeshBasicMaterial({ color: colorHex, transparent: true, opacity: 1.0 });
            const sMesh = new THREE.Mesh(sGeo, sMat);
            group.add(sMesh);

            sparks.push({
                mesh: sMesh,
                vx: (Math.random() - 0.5) * 0.16,
                vy: Math.random() * 0.14 + 0.02,
                vz: (Math.random() - 0.5) * 0.16,
                life: 1.0
            });
        }

        this.impactSparks.push({ group, sparks });
    }

    // Camera shake impulse
    triggerCameraShake(intensity = 0.3) {
        this.shakeIntensity = Math.max(this.shakeIntensity, intensity);
    }

    // Device pulsing beacon aura
    addDeviceHalo(tag, pos, colorHex = 0xff0033) {
        if (this.activeHalos.has(tag)) return;
        const geo = new THREE.RingGeometry(0.35, 0.55, 32);
        const mat = new THREE.MeshBasicMaterial({ color: colorHex, transparent: true, opacity: 0.8, side: THREE.DoubleSide });
        const mesh = new THREE.Mesh(geo, mat);
        mesh.rotation.x = -Math.PI / 2;
        mesh.position.copy(pos);
        mesh.position.y -= 0.05;
        this.scene.add(mesh);
        this.activeHalos.set(tag, { mesh, baseScale: 1.0 });
    }

    // Animate glowing network packet traveling along 3D space
    spawnPacketPulse(packet) {
        const fromDev = this.devices.get(packet.fromTag);
        const toDev = this.devices.get(packet.toTag);

        const p1 = fromDev ? fromDev.worldPos.clone() : new THREE.Vector3(0, 2, 0);
        const p2 = toDev ? toDev.worldPos.clone() : new THREE.Vector3(0.15, 1.7, 4.5);

        // Control waypoint through central riser
        const midY = (p1.y + p2.y) / 2 + 1.2;
        const waypoint = new THREE.Vector3(0.15, midY, 0.5);

        const curve = new THREE.QuadraticBezierCurve3(p1, waypoint, p2);

        const colorHex = packet.type === 'BROADCAST' ? 0xff0055 : (packet.type === 'MULTICAST' ? 0xffaa00 : 0x00f0ff);

        // Glowing packet projectile
        const sphereGeo = new THREE.SphereGeometry(0.2, 12, 12);
        const sphereMat = new THREE.MeshBasicMaterial({ color: colorHex });
        const pulseMesh = new THREE.Mesh(sphereGeo, sphereMat);
        pulseMesh.position.copy(p1);
        this.scene.add(pulseMesh);

        // Trailing comet particles
        const trails = [];
        for (let i = 0; i < 4; i++) {
            const tGeo = new THREE.SphereGeometry(0.12 - i * 0.02, 8, 8);
            const tMat = new THREE.MeshBasicMaterial({
                color: colorHex,
                transparent: true,
                opacity: 0.65 - i * 0.14
            });
            const tMesh = new THREE.Mesh(tGeo, tMat);
            tMesh.position.copy(p1);
            this.scene.add(tMesh);
            trails.push(tMesh);
        }

        this.packetPulses.push({
            mesh: pulseMesh,
            trails,
            colorHex,
            curve,
            progress: 0,
            speed: 2.2,
            toPos: p2,
            toDev: toDev
        });

        if (this.onNewEvent) {
            this.onNewEvent({
                title: `📡 Pakke: ${packet.fromTag} ➔ ${packet.toTag}`,
                desc: packet.payload ? (packet.payload.length > 36 ? packet.payload.slice(0, 36) + '...' : packet.payload) : 'PubSub pakke sendt',
                type: 'packet'
            });
        }

        if (this.audio) {
            this.audio.playPacketChirp();
        }
    }

    // -------------------------------------------------------------
    // PARTICLE SYSTEMS (Smoke, Fire, Sprinkler Water, Inergen Gas)
    // -------------------------------------------------------------

    startFireParticles(tag, pos) {
        if (this.activeFires.has(tag)) return;

        const fireGroup = new THREE.Group();
        fireGroup.position.copy(pos);
        // Position on floor below ceiling detector
        fireGroup.position.y -= 2.6;
        this.scene.add(fireGroup);

        const fireParticles = [];
        const count = 35;

        for (let i = 0; i < count; i++) {
            const pGeo = new THREE.SphereGeometry(0.15 + Math.random() * 0.15, 8, 8);
            const pMat = new THREE.MeshBasicMaterial({
                color: Math.random() > 0.4 ? 0xff4500 : 0xffcc00,
                transparent: true,
                opacity: 0.9
            });
            const pMesh = new THREE.Mesh(pGeo, pMat);
            pMesh.position.set(
                (Math.random() - 0.5) * 1.0,
                Math.random() * 0.5,
                (Math.random() - 0.5) * 1.0
            );
            fireGroup.add(pMesh);

            fireParticles.push({
                mesh: pMesh,
                vy: 0.04 + Math.random() * 0.05,
                vx: (Math.random() - 0.5) * 0.015,
                vz: (Math.random() - 0.5) * 0.015,
                maxLife: 0.8 + Math.random() * 0.5,
                life: Math.random() * 0.5
            });
        }

        // Billowing Smoke
        const smokeParticles = [];
        const smokeCount = 45;
        for (let i = 0; i < smokeCount; i++) {
            const sGeo = new THREE.SphereGeometry(0.3 + Math.random() * 0.35, 8, 8);
            const sMat = new THREE.MeshBasicMaterial({
                color: 0x333b45,
                transparent: true,
                opacity: 0.45
            });
            const sMesh = new THREE.Mesh(sGeo, sMat);
            sMesh.position.set(
                (Math.random() - 0.5) * 1.4,
                0.8 + Math.random() * 1.0,
                (Math.random() - 0.5) * 1.4
            );
            fireGroup.add(sMesh);

            smokeParticles.push({
                mesh: sMesh,
                vy: 0.025 + Math.random() * 0.035,
                vx: (Math.random() - 0.5) * 0.02,
                vz: (Math.random() - 0.5) * 0.02,
                maxLife: 2.2,
                life: Math.random() * 2.0
            });
        }

        this.activeFires.set(tag, {
            group: fireGroup,
            fireParticles,
            smokeParticles
        });
    }

    startSprinklerShower(pos) {
        const count = 75;
        const drops = [];
        const group = new THREE.Group();
        group.position.copy(pos);
        this.scene.add(group);

        for (let i = 0; i < count; i++) {
            const dGeo = new THREE.SphereGeometry(0.04, 6, 6);
            const dMat = new THREE.MeshBasicMaterial({ color: 0x66ccff, transparent: true, opacity: 0.75 });
            const mesh = new THREE.Mesh(dGeo, dMat);
            group.add(mesh);

            drops.push({
                mesh,
                vx: (Math.random() - 0.5) * 0.15,
                vz: (Math.random() - 0.5) * 0.15,
                vy: -0.15 - Math.random() * 0.1,
                life: Math.random()
            });
        }

        this.particles.push({
            type: 'SPRINKLER',
            group,
            drops,
            active: true
        });
    }

    startInergenGasCloud(pos) {
        const count = 50;
        const mist = [];
        const group = new THREE.Group();
        group.position.copy(pos);
        this.scene.add(group);

        for (let i = 0; i < count; i++) {
            const mGeo = new THREE.SphereGeometry(0.4 + Math.random() * 0.4, 8, 8);
            const mMat = new THREE.MeshBasicMaterial({ color: 0x88eeff, transparent: true, opacity: 0.35 });
            const mesh = new THREE.Mesh(mGeo, mMat);
            group.add(mesh);

            mist.push({
                mesh,
                vx: (Math.random() - 0.5) * 0.07,
                vz: (Math.random() - 0.5) * 0.07,
                vy: -0.02 - Math.random() * 0.04,
                life: Math.random() * 2.5
            });
        }

        this.particles.push({
            type: 'GAS_CLOUD',
            group,
            mist,
            active: true
        });
    }

    resetAllEffects() {
        // Clear active fires
        this.activeFires.forEach(f => {
            this.scene.remove(f.group);
        });
        this.activeFires.clear();

        // Clear particles
        this.particles.forEach(p => {
            this.scene.remove(p.group);
        });
        this.particles = [];

        // Clear shockwaves
        this.shockwaves.forEach(sw => this.scene.remove(sw.mesh));
        this.shockwaves = [];

        // Clear floating 3D badges
        this.floatingBadges.forEach(fb => this.scene.remove(fb.sprite));
        this.floatingBadges = [];

        // Clear impact sparks
        this.impactSparks.forEach(is => this.scene.remove(is.group));
        this.impactSparks = [];

        // Clear device halos
        this.activeHalos.forEach(h => this.scene.remove(h.mesh));
        this.activeHalos.clear();

        // Clear packet pulses and trails
        this.packetPulses.forEach(p => {
            this.scene.remove(p.mesh);
            if (p.trails) p.trails.forEach(t => this.scene.remove(t));
        });
        this.packetPulses = [];

        this.shakeIntensity = 0;

        // Reset doors to open (held by magnet)
        if (this.doorE1) {
            this.doorE1.targetAngle = Math.PI * 0.45;
            this.doorE1.isClosed = false;
        }
        if (this.doorE2) {
            this.doorE2.targetAngle = Math.PI * 0.45;
            this.doorE2.isClosed = false;
        }

        // Reset roof hatch
        if (this.roofHatchLid) {
            this.roofHatchLid.rotation.x = 0;
            this.roofHatchLid.position.y = 0.55;
        }
        this.spinningFans.forEach(f => {
            f.active = false;
            f.speed = 0;
        });

        // Reset panel display to green
        if (this.panelScreenMat) {
            this.panelScreenMat.color.setHex(0x00ff88);
        }

        // Reset elevator
        this.elevatorEmergencyLocked = false;

        // Reset device LEDs & lights
        this.devices.forEach(dev => {
            dev.status = 'NORMAL';
            if (dev.ledMat) dev.ledMat.color.setHex(0x00ff44);
            if (dev.light) dev.light.intensity = 0;
            dev.isActive = false;
            dev.isTriggered = false;
        });

        // Reset screen vignette
        const vig = document.getElementById('alarm-vignette');
        if (vig) vig.className = 'alarm-vignette';

        // Clear live event feed
        const feed = document.getElementById('live-event-feed');
        if (feed) feed.innerHTML = '';

        // Stop audio sirens
        if (this.audio) {
            this.audio.stopAll();
        }
    }

    // -------------------------------------------------------------
    // ANIMATION RENDER LOOP
    // -------------------------------------------------------------

    _animate() {
        requestAnimationFrame(() => this._animate());

        const now = performance.now() * 0.001;
        const dt = Math.min(now - this.lastFrameTime, 0.1);
        this.lastFrameTime = now;

        // 0. Smooth Camera Fly-To Interpolation
        if (this.flyTo) {
            this.flyTo.progress += dt / this.flyTo.duration;
            const t = THREE.MathUtils.smoothstep(this.flyTo.progress, 0, 1);
            this.camera.position.lerpVectors(this.flyTo.startPos, this.flyTo.endPos, t);
            if (this.controls) {
                this.controls.target.lerpVectors(this.flyTo.startTarget, this.flyTo.endTarget, t);
                this.controls.update();
            } else {
                const lookPoint = new THREE.Vector3().lerpVectors(this.flyTo.startTarget, this.flyTo.endTarget, t);
                this.camera.lookAt(lookPoint);
            }
            if (this.flyTo.progress >= 1) {
                this.flyTo = null;
            }
        }

        // 0b. Walk Mode Movement (WASD + Keyboard/D-pad)
        if (this.navMode === 'WALK' && !this.flyTo) {
            const speed = (this.keysDown.shift ? this.walkSpeed * 2.2 : this.walkSpeed) * dt;
            const forward = new THREE.Vector3(-Math.sin(this.walkYaw), 0, -Math.cos(this.walkYaw));
            const right = new THREE.Vector3(Math.cos(this.walkYaw), 0, -Math.sin(this.walkYaw));
            const move = new THREE.Vector3();

            if (this.keysDown.w) move.add(forward);
            if (this.keysDown.s) move.sub(forward);
            if (this.keysDown.a) move.sub(right);
            if (this.keysDown.d) move.add(right);

            if (move.lengthSq() > 0) {
                move.normalize().multiplyScalar(speed);
                this.camera.position.add(move);
            }

            if (this.keysDown.e) this.camera.position.y += speed;
            if (this.keysDown.q) this.camera.position.y -= speed;

            // Clamping within building bounds
            this.camera.position.y = THREE.MathUtils.clamp(this.camera.position.y, -3.2, 18.0);
            this.camera.position.x = THREE.MathUtils.clamp(this.camera.position.x, -28.0, 28.0);
            this.camera.position.z = THREE.MathUtils.clamp(this.camera.position.z, -28.0, 28.0);

            // Orient camera from yaw & pitch
            const targetLook = new THREE.Vector3(
                this.camera.position.x - Math.sin(this.walkYaw) * Math.cos(this.walkPitch),
                this.camera.position.y + Math.sin(this.walkPitch),
                this.camera.position.z - Math.cos(this.walkYaw) * Math.cos(this.walkPitch)
            );
            this.camera.lookAt(targetLook);
        } else if (this.controls && !this.flyTo) {
            this.controls.update();
        }

        // 0c. Camera Shake Impulse
        if (this.shakeIntensity > 0.005) {
            const shake = (Math.random() - 0.5) * this.shakeIntensity;
            this.camera.position.x += shake;
            this.camera.position.y += shake * 0.7;
            this.shakeIntensity *= 0.88;
        }

        // 1. Blinking server LEDs
        for (let i = 0; i < this.serverLeds.length; i++) {
            const blink = Math.sin(now * 8 + i * 1.5) > 0.2;
            this.serverLeds[i].color.setHex(blink ? 0x00ffcc : 0x004433);
        }

        // 2. Animate Hinged Doors closing smoothly
        [this.doorE1, this.doorE2].forEach(door => {
            if (door && door.hinge) {
                door.hinge.rotation.y = THREE.MathUtils.lerp(
                    door.hinge.rotation.y,
                    door.targetAngle,
                    0.05
                );
            }
        });

        // 3. Strobe Light Flashes
        this.strobeLights.forEach(s => {
            if (s.isActive && s.light) {
                const strobe = Math.sin(now * 16) > 0.4;
                s.light.intensity = strobe ? 16 : 0;
            }
        });

        // 4. Spinning Roof Fans
        this.spinningFans.forEach(f => {
            if (f.active) {
                f.group.rotation.y += f.speed * 0.016;
            }
        });

        // 5. Elevator Car Animation
        if (this.elevatorCar) {
            if (!this.elevatorEmergencyLocked) {
                const targetY = Math.sin(now * 0.4) * 3.5 + 2.0;
                this.elevatorCar.position.y = THREE.MathUtils.lerp(this.elevatorCar.position.y, targetY, 0.02);
            } else {
                this.elevatorCar.position.y = THREE.MathUtils.lerp(this.elevatorCar.position.y, this.elevatorTargetY, 0.04);
            }
        }

        // 6. Packet Pulses with Comet Trails & Terminal Impact Sparks
        for (let i = this.packetPulses.length - 1; i >= 0; i--) {
            const p = this.packetPulses[i];
            p.progress += 0.022 * p.speed;

            if (p.progress >= 1.0) {
                if (p.toPos) {
                    this.triggerImpactSparks(p.toPos, p.colorHex || 0x00f0ff, 10);
                }
                if (p.toDev && p.toDev.light) {
                    p.toDev.light.intensity = 8.0;
                    setTimeout(() => { if (p.toDev && p.toDev.light) p.toDev.light.intensity = 0; }, 200);
                }
                this.scene.remove(p.mesh);
                if (p.trails) {
                    p.trails.forEach(t => this.scene.remove(t));
                }
                this.packetPulses.splice(i, 1);
            } else {
                const currentPos = p.curve.getPoint(p.progress);
                p.mesh.position.copy(currentPos);
                const pulseScale = 1.0 + Math.sin(p.progress * Math.PI) * 0.35;
                p.mesh.scale.set(pulseScale, pulseScale, pulseScale);

                if (p.trails) {
                    p.trails.forEach((t, tIdx) => {
                        const trailProg = Math.max(0, p.progress - (tIdx + 1) * 0.035);
                        t.position.copy(p.curve.getPoint(trailProg));
                    });
                }
            }
        }

        // 7. Fire & Smoke Particles Update
        this.activeFires.forEach(f => {
            f.fireParticles.forEach(fp => {
                fp.life += 0.025;
                if (fp.life > fp.maxLife) {
                    fp.life = 0;
                    fp.mesh.position.set((Math.random() - 0.5) * 0.8, 0, (Math.random() - 0.5) * 0.8);
                } else {
                    fp.mesh.position.y += fp.vy;
                    fp.mesh.position.x += fp.vx;
                    fp.mesh.position.z += fp.vz;
                    const scale = 1.0 - (fp.life / fp.maxLife);
                    fp.mesh.scale.set(scale, scale, scale);
                }
            });

            f.smokeParticles.forEach(sp => {
                sp.life += 0.02;
                if (sp.life > sp.maxLife) {
                    sp.life = 0;
                    sp.mesh.position.set((Math.random() - 0.5) * 1.2, 0.8, (Math.random() - 0.5) * 1.2);
                } else {
                    sp.mesh.position.y += sp.vy;
                    sp.mesh.position.x += sp.vx;
                    sp.mesh.position.z += sp.vz;
                    const scale = 1.0 + (sp.life * 0.8);
                    sp.mesh.scale.set(scale, scale, scale);
                }
            });
        });

        // 8. Sprinkler Water & Gas Mist Update
        this.particles.forEach(p => {
            if (p.type === 'SPRINKLER') {
                p.drops.forEach(d => {
                    d.mesh.position.x += d.vx;
                    d.mesh.position.z += d.vz;
                    d.mesh.position.y += d.vy;
                    if (d.mesh.position.y < -3.0) {
                        d.mesh.position.set(0, 0, 0);
                    }
                });
            } else if (p.type === 'GAS_CLOUD') {
                p.mist.forEach(m => {
                    m.mesh.position.x += m.vx;
                    m.mesh.position.z += m.vz;
                    m.mesh.position.y += m.vy;
                    if (m.mesh.position.y < -3.0) {
                        m.mesh.position.set(0, 0, 0);
                    }
                });
            }
        });

        // 9. Shockwaves Update (Expanding Rings)
        for (let i = this.shockwaves.length - 1; i >= 0; i--) {
            const sw = this.shockwaves[i];
            sw.radius += dt * sw.speed;
            sw.mesh.scale.set(sw.radius, sw.radius, sw.radius);
            sw.opacity = Math.max(0, 0.9 * (1.0 - sw.radius / sw.maxRadius));
            sw.mesh.material.opacity = sw.opacity;
            if (sw.radius >= sw.maxRadius || sw.opacity <= 0.02) {
                this.scene.remove(sw.mesh);
                this.shockwaves.splice(i, 1);
            }
        }

        // 10. Floating Holographic 3D Badges
        for (let i = this.floatingBadges.length - 1; i >= 0; i--) {
            const fb = this.floatingBadges[i];
            fb.life += dt;
            fb.sprite.position.y += fb.vy;
            const scaleFactor = fb.life < 0.25 ? (fb.life / 0.25) * 1.15 : 1.0;
            fb.sprite.scale.copy(fb.initialScale).multiplyScalar(scaleFactor);
            if (fb.life > fb.maxLife - 1.0) {
                fb.sprite.material.opacity = Math.max(0, fb.maxLife - fb.life);
            }
            if (fb.life >= fb.maxLife) {
                this.scene.remove(fb.sprite);
                this.floatingBadges.splice(i, 1);
            }
        }

        // 11. Impact Sparks Particles
        for (let i = this.impactSparks.length - 1; i >= 0; i--) {
            const is = this.impactSparks[i];
            let anyAlive = false;
            is.sparks.forEach(s => {
                s.life -= dt * 2.2;
                if (s.life > 0) {
                    anyAlive = true;
                    s.mesh.position.x += s.vx;
                    s.mesh.position.y += s.vy;
                    s.mesh.position.z += s.vz;
                    s.vy -= 0.008; // gravity
                    s.mesh.material.opacity = s.life;
                } else {
                    s.mesh.visible = false;
                }
            });
            if (!anyAlive) {
                this.scene.remove(is.group);
                this.impactSparks.splice(i, 1);
            }
        }

        // 12. Alarmed Device Beacon Halos
        this.activeHalos.forEach(h => {
            const pulse = 1.0 + Math.sin(now * 5.0) * 0.25;
            h.mesh.scale.set(pulse, pulse, pulse);
            h.mesh.material.opacity = 0.5 + Math.sin(now * 5.0) * 0.35;
        });

        this.renderer.render(this.scene, this.camera);
    }
}

if (typeof window !== 'undefined') {
    window.Building3D = Building3D;
}
