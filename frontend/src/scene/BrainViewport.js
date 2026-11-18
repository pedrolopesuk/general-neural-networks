import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';

const HOLON_SHELL_VERT = `
  varying vec3 vNormalW;
  varying vec3 vViewDir;
  void main() {
    vec4 worldPos = modelMatrix * vec4(position, 1.0);
    vNormalW = normalize(mat3(modelMatrix) * normal);
    vViewDir = normalize(cameraPosition - worldPos.xyz);
    gl_Position = projectionMatrix * viewMatrix * worldPos;
  }
`;

const HOLON_SHELL_FRAG = `
  uniform vec3 uColor;
  uniform float uOpacity;
  uniform float uPulse;
  varying vec3 vNormalW;
  varying vec3 vViewDir;
  void main() {
    float fresnel = pow(1.0 - abs(dot(normalize(vNormalW), normalize(vViewDir))), 2.35);
    float rim = clamp(fresnel * (0.85 + 0.55 * uPulse), 0.0, 1.0);
    gl_FragColor = vec4(uColor * (1.0 + 0.6 * uPulse), rim * uOpacity);
  }
`;

function buildGlowTexture() {
  const canvas = document.createElement('canvas');
  canvas.width = 128;
  canvas.height = 128;
  const ctx = canvas.getContext('2d');
  const g = ctx.createRadialGradient(64, 64, 2, 64, 64, 64);
  g.addColorStop(0.0, 'rgba(255,255,255,1.0)');
  g.addColorStop(0.2, 'rgba(210,248,255,0.92)');
  g.addColorStop(0.5, 'rgba(40,175,255,0.32)');
  g.addColorStop(1.0, 'rgba(0,0,0,0.0)');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, 128, 128);
  return new THREE.CanvasTexture(canvas);
}

export class BrainViewport {
  constructor(container, { onSelectNeuron, onHoverNeuron, onZoomTierChange }) {
    this.container = container;
    this.onSelectNeuron = onSelectNeuron;
    this.onHoverNeuron = onHoverNeuron;
    this.onZoomTierChange = onZoomTierChange;

    this.lens = 'activity'; // 'activity' | 'age'
    this.selectedId = null;
    this.topologySignature = '';
    this.nodeDataMap = new Map();
    this.neuronVisuals = new Map();
    this.holonShells = new Map();
    this.synapsesData = [];
    this.cameraFlight = null;

    this._initScene();
    this._bindEvents();
  }

  _initScene() {
    const w = this.container.clientWidth || window.innerWidth;
    const h = this.container.clientHeight || window.innerHeight;

    this.scene = new THREE.Scene();
    this.scene.fog = new THREE.FogExp2(0x03060f, 0.00095);

    this.camera = new THREE.PerspectiveCamera(50, w / h, 0.35, 3600);
    this.camera.position.set(0, 130, 395);

    this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    this.renderer.setSize(w, h);
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.18;
    this.container.appendChild(this.renderer.domElement);

    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.065;
    this.controls.minDistance = 4.5;
    this.controls.maxDistance = 980.0;

    this.composer = new EffectComposer(this.renderer);
    this.composer.addPass(new RenderPass(this.scene, this.camera));
    this.bloomPass = new UnrealBloomPass(new THREE.Vector2(w, h), 1.15, 0.5, 0.17);
    this.composer.addPass(this.bloomPass);

    this.glowTex = buildGlowTexture();
    this.somaGeo = new THREE.SphereGeometry(1, 22, 22);
    this.shellGeo = new THREE.SphereGeometry(1, 30, 22);

    // Selection Reticle Ring
    const ringGeo = new THREE.RingGeometry(1.55, 1.82, 40);
    const ringMat = new THREE.MeshBasicMaterial({
      color: 0x00ff95,
      side: THREE.DoubleSide,
      transparent: true,
      opacity: 0.92,
      blending: THREE.AdditiveBlending,
      depthWrite: false
    });
    this.selectionRing = new THREE.Mesh(ringGeo, ringMat);
    this.selectionRing.visible = false;
    this.scene.add(this.selectionRing);

    // Ambient Deep-Space Neural Dust
    const dustCount = 1100;
    const dustPos = new Float32Array(dustCount * 3);
    for (let i = 0; i < dustCount; i++) {
      dustPos[i * 3]     = (Math.random() - 0.5) * 1800;
      dustPos[i * 3 + 1] = (Math.random() - 0.5) * 1300;
      dustPos[i * 3 + 2] = (Math.random() - 0.5) * 1800;
    }
    const dustGeo = new THREE.BufferGeometry();
    dustGeo.setAttribute('position', new THREE.BufferAttribute(dustPos, 3));
    const dustMat = new THREE.PointsMaterial({
      color: 0x3a62a8,
      size: 1.5,
      transparent: true,
      opacity: 0.36
    });
    this.dustPoints = new THREE.Points(dustGeo, dustMat);
    this.scene.add(this.dustPoints);

    this.graphGroup = new THREE.Group();
    this.scene.add(this.graphGroup);

    this.raycaster = new THREE.Raycaster();
    this.pointer = new THREE.Vector2(-99, -99);

    this.cRest = new THREE.Color(0x0a1736);
    this.cCyan = new THREE.Color(0x00e5ff);
    this.cHotWhite = new THREE.Color(1.65, 1.72, 1.8);
    this.cEmerald = new THREE.Color(0x00ff95);
    this.cPurple = new THREE.Color(0x9d4edd);
    this.tmpColor = new THREE.Color();
  }

  _bindEvents() {
    const dom = this.renderer.domElement;

    dom.addEventListener('pointermove', (e) => {
      const rect = dom.getBoundingClientRect();
      this.pointer.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
      this.pointer.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;

      this.raycaster.setFromCamera(this.pointer, this.camera);
      const meshes = Array.from(this.neuronVisuals.values()).map(v => v.mesh);
      const hits = this.raycaster.intersectObjects(meshes, false);
      if (hits.length > 0) {
        const id = hits[0].object.userData.id;
        const n = this.nodeDataMap.get(id);
        dom.style.cursor = 'pointer';
        if (this.onHoverNeuron) this.onHoverNeuron(n, e.clientX, e.clientY);
      } else {
        dom.style.cursor = 'grab';
        if (this.onHoverNeuron) this.onHoverNeuron(null, 0, 0);
      }
    });

    let downPos = { x: 0, y: 0 };
    dom.addEventListener('pointerdown', (e) => {
      downPos = { x: e.clientX, y: e.clientY };
    });

    dom.addEventListener('pointerup', (e) => {
      if (Math.hypot(e.clientX - downPos.x, e.clientY - downPos.y) > 5) return;
      this.raycaster.setFromCamera(this.pointer, this.camera);
      const meshes = Array.from(this.neuronVisuals.values()).map(v => v.mesh);
      const hits = this.raycaster.intersectObjects(meshes, false);
      if (hits.length > 0) {
        const id = hits[0].object.userData.id;
        this.selectedId = id;
        if (this.onSelectNeuron) this.onSelectNeuron(id);
      }
    });

    dom.addEventListener('dblclick', () => {
      this.raycaster.setFromCamera(this.pointer, this.camera);
      const meshes = Array.from(this.neuronVisuals.values()).map(v => v.mesh);
      const hits = this.raycaster.intersectObjects(meshes, false);
      if (hits.length > 0) {
        const id = hits[0].object.userData.id;
        this.selectedId = id;
        if (this.onSelectNeuron) this.onSelectNeuron(id);
        this.flyToNeuron(id);
      }
    });

    window.addEventListener('resize', () => {
      const w = this.container.clientWidth || window.innerWidth;
      const h = this.container.clientHeight || window.innerHeight;
      this.camera.aspect = w / h;
      this.camera.updateProjectionMatrix();
      this.renderer.setSize(w, h);
      this.composer.setSize(w, h);
    });
  }

  setLens(mode) {
    this.lens = mode;
  }

  selectAndFlyTo(id) {
    this.selectedId = id;
    this.flyToNeuron(id);
  }

  flyToNeuron(id) {
    const n = this.nodeDataMap.get(id);
    if (!n) return;
    const target = new THREE.Vector3(n.x, n.y, n.z);
    const dist = n.depth === 0 ? (n.is_composite ? 62 : 110) : (n.depth === 1 ? 26 : 14);
    this.flyToTarget(target, dist);
  }

  flyToTarget(targetVec, distance) {
    const dir = this.camera.position.clone().sub(this.controls.target);
    if (dir.lengthSq() < 1e-4) dir.set(0, 0.3, 1);
    dir.normalize();
    this.cameraFlight = {
      lookAt: targetVec.clone(),
      pos: targetVec.clone().add(dir.multiplyScalar(distance))
    };
  }

  ingestState(state) {
    if (!state || !state.flat_nodes) return;

    this.nodeDataMap.clear();
    for (const n of state.flat_nodes) {
      this.nodeDataMap.set(n.id, n);
    }
    this.synapsesData = state.synapses || [];

    const sig = `${state.flat_nodes.length}:${this.synapsesData.length}:${state.flat_nodes.filter(n => n.is_composite).length}`;
    if (sig !== this.topologySignature) {
      this.topologySignature = sig;
      this._rebuildTopology(state.flat_nodes, this.synapsesData);
    }
  }

  _rebuildTopology(nodes, synapses) {
    while (this.graphGroup.children.length > 0) {
      const child = this.graphGroup.children.pop();
      if (child.geometry && child.geometry !== this.somaGeo && child.geometry !== this.shellGeo) {
        child.geometry.dispose();
      }
    }
    this.neuronVisuals.clear();
    this.holonShells.clear();

    // 1. Neuron Soma Cores + Volumetric Glow Sprites + GLSL Holon Shells
    for (const n of nodes) {
      const baseR = n.depth === 0
        ? (n.role === 'TerminalReadout' ? 5.8 : (n.role === 'RootSensory' ? 4.8 : 5.2))
        : (n.depth === 1 ? 2.35 : (n.depth === 2 ? 1.25 : 0.78));

      const mat = new THREE.MeshBasicMaterial({
        color: 0x00e5ff,
        transparent: true,
        opacity: 0.95
      });
      const mesh = new THREE.Mesh(this.somaGeo, mat);
      mesh.position.set(n.x, n.y, n.z);
      mesh.scale.setScalar(baseR);
      mesh.userData = { id: n.id, baseR };

      const spMat = new THREE.SpriteMaterial({
        map: this.glowTex,
        color: 0x00e5ff,
        transparent: true,
        opacity: 0.72,
        blending: THREE.AdditiveBlending,
        depthWrite: false
      });
      const sprite = new THREE.Sprite(spMat);
      sprite.scale.setScalar(4.3);
      mesh.add(sprite);

      this.graphGroup.add(mesh);
      this.neuronVisuals.set(n.id, { mesh, sprite, baseR });

      if (n.is_composite) {
        const shellRadius = n.depth === 0 ? 29.0 : (n.depth === 1 ? 10.6 : 4.2);
        const shellMat = new THREE.ShaderMaterial({
          vertexShader: HOLON_SHELL_VERT,
          fragmentShader: HOLON_SHELL_FRAG,
          uniforms: {
            uColor: { value: new THREE.Color(0x38bdf8) },
            uOpacity: { value: 0.42 },
            uPulse: { value: 0.2 }
          },
          transparent: true,
          side: THREE.FrontSide,
          blending: THREE.AdditiveBlending,
          depthWrite: false
        });
        const sMesh = new THREE.Mesh(this.shellGeo, shellMat);
        sMesh.position.set(n.x, n.y, n.z);
        sMesh.scale.setScalar(shellRadius);
        this.graphGroup.add(sMesh);
        this.holonShells.set(n.id, { mesh: sMesh, mat: shellMat, shellRadius });
      }
    }

    // 2. Synaptic Filament Segments
    const synCount = synapses.length;
    const linePos = new Float32Array(synCount * 6);
    const lineCol = new Float32Array(synCount * 6);
    this.synLineGeo = new THREE.BufferGeometry();
    this.synLineGeo.setAttribute('position', new THREE.BufferAttribute(linePos, 3));
    this.synLineGeo.setAttribute('color', new THREE.BufferAttribute(lineCol, 3));

    const lineMat = new THREE.LineBasicMaterial({
      vertexColors: true,
      transparent: true,
      opacity: 0.38,
      blending: THREE.AdditiveBlending,
      depthWrite: false
    });
    this.synLineMesh = new THREE.LineSegments(this.synLineGeo, lineMat);
    this.graphGroup.add(this.synLineMesh);

    // 3. Dual-Packet Action Potential Sparks per Synapse
    const sparkCount = synCount * 2;
    const sparkPos = new Float32Array(sparkCount * 3);
    const sparkCol = new Float32Array(sparkCount * 3);
    this.sparkGeo = new THREE.BufferGeometry();
    this.sparkGeo.setAttribute('position', new THREE.BufferAttribute(sparkPos, 3));
    this.sparkGeo.setAttribute('color', new THREE.BufferAttribute(sparkCol, 3));

    const sparkMat = new THREE.PointsMaterial({
      size: 4.5,
      map: this.glowTex,
      vertexColors: true,
      transparent: true,
      opacity: 0.94,
      blending: THREE.AdditiveBlending,
      depthWrite: false
    });
    this.sparkMesh = new THREE.Points(this.sparkGeo, sparkMat);
    this.graphGroup.add(this.sparkMesh);
  }

  _computeColor(node) {
    const act = Math.max(0, Math.min(1, node.activation || 0));
    if (this.lens === 'activity') {
      if (act < 0.48) {
        this.tmpColor.copy(this.cRest).lerp(this.cCyan, act / 0.48);
      } else {
        this.tmpColor.copy(this.cCyan).lerp(this.cHotWhite, (act - 0.48) / 0.52);
      }
    } else {
      const mat = Math.max(0, Math.min(1, node.maturity ?? 0));
      this.tmpColor.copy(this.cEmerald).lerp(this.cPurple, mat);
      this.tmpColor.multiplyScalar(0.55 + 0.95 * act);
    }
    return this.tmpColor;
  }

  renderFrame(timeSec) {
    if (this.cameraFlight) {
      this.controls.target.lerp(this.cameraFlight.lookAt, 0.085);
      this.camera.position.lerp(this.cameraFlight.pos, 0.085);
      if (this.camera.position.distanceTo(this.cameraFlight.pos) < 0.75) {
        this.cameraFlight = null;
      }
    }

    this.controls.update();
    this.dustPoints.rotation.y = timeSec * 0.012;

    const camDist = this.camera.position.distanceTo(this.controls.target);
    const tier = camDist > 235 ? 'macro' : (camDist > 75 ? 'cluster' : 'micro');
    if (tier !== this._lastTier) {
      this._lastTier = tier;
      if (this.onZoomTierChange) this.onZoomTierChange(tier, camDist);
    }

    // Smoothly interpolate neuron 3D positions & heatmap colors
    for (const [id, vis] of this.neuronVisuals.entries()) {
      const n = this.nodeDataMap.get(id);
      if (!n) continue;

      vis.mesh.position.x += (n.x - vis.mesh.position.x) * 0.18;
      vis.mesh.position.y += (n.y - vis.mesh.position.y) * 0.18;
      vis.mesh.position.z += (n.z - vis.mesh.position.z) * 0.18;

      const col = this._computeColor(n);
      vis.mesh.material.color.copy(col);

      let lodFade = 1.0;
      if (camDist > 260 && n.depth >= 2) lodFade = 0.2;
      else if (camDist < 68 && n.depth === 0 && n.is_composite) lodFade = 0.32;

      vis.mesh.material.opacity = 0.92 * lodFade;
      const act = n.activation || 0;
      vis.mesh.scale.setScalar(vis.baseR * (1.0 + 0.34 * act));

      vis.sprite.material.color.copy(col);
      vis.sprite.material.opacity = (0.22 + 0.75 * act) * lodFade;
      vis.sprite.scale.setScalar(3.1 + 3.5 * act);

      const shell = this.holonShells.get(id);
      if (shell) {
        shell.mesh.position.copy(vis.mesh.position);
        const distToShell = this.camera.position.distanceTo(shell.mesh.position);
        // Dissolve shell smoothly when camera flies inside it
        const insideFade = Math.min(1.0, Math.max(0.08, (distToShell - shell.shellRadius * 0.65) / (shell.shellRadius * 1.4)));
        shell.mat.uniforms.uOpacity.value = 0.36 * insideFade;
        shell.mat.uniforms.uPulse.value = act;
        shell.mat.uniforms.uColor.value.setHex(this.lens === 'age' ? 0xa855f7 : 0x38bdf8);
      }
    }

    // Update Selection Reticle Ring
    if (this.selectedId !== null && this.neuronVisuals.has(this.selectedId)) {
      const selVis = this.neuronVisuals.get(this.selectedId);
      this.selectionRing.visible = true;
      this.selectionRing.position.copy(selVis.mesh.position);
      this.selectionRing.lookAt(this.camera.position);
      const pulse = 1.0 + 0.12 * Math.sin(timeSec * 5.5);
      this.selectionRing.scale.setScalar(selVis.baseR * 1.35 * pulse);
    } else {
      this.selectionRing.visible = false;
    }

    // Update Synapses & Action-Potential Sparks using smoothed mesh positions
    if (this.synLineGeo && this.sparkGeo) {
      const posArr = this.synLineGeo.attributes.position.array;
      const colArr = this.synLineGeo.attributes.color.array;
      const sPos = this.sparkGeo.attributes.position.array;
      const sCol = this.sparkGeo.attributes.color.array;

      const syns = this.synapsesData;
      for (let i = 0; i < syns.length; i++) {
        const s = syns[i];
        const srcVis = this.neuronVisuals.get(s.source);
        const dstVis = this.neuronVisuals.get(s.target);
        const srcNode = this.nodeDataMap.get(s.source);
        if (!srcVis || !dstVis || !srcNode) continue;

        const sp = srcVis.mesh.position;
        const dp = dstVis.mesh.position;

        const i6 = i * 6;
        posArr[i6]     = sp.x; posArr[i6 + 1] = sp.y; posArr[i6 + 2] = sp.z;
        posArr[i6 + 3] = dp.x; posArr[i6 + 4] = dp.y; posArr[i6 + 5] = dp.z;

        const flux = Math.min(1.0, (srcNode.activation || 0) * Math.min(1.6, Math.abs(s.weight)));
        const isExc = s.weight >= 0;

        const r = isExc ? (0.06 + 0.94 * flux) : (0.42 + 0.58 * flux);
        const g = isExc ? (0.38 + 0.62 * flux) : 0.08;
        const b = isExc ? (0.62 + 0.38 * flux) : 0.38;

        colArr[i6]     = r * 0.45; colArr[i6 + 1] = g * 0.45; colArr[i6 + 2] = b * 0.45;
        colArr[i6 + 3] = r;        colArr[i6 + 4] = g;        colArr[i6 + 5] = b;

        const speed = 0.65 + 1.85 * flux;
        for (let p = 0; p < 2; p++) {
          const idx3 = (i * 2 + p) * 3;
          const phase = (timeSec * speed + i * 0.173 + p * 0.5) % 1.0;
          sPos[idx3]     = sp.x + (dp.x - sp.x) * phase;
          sPos[idx3 + 1] = sp.y + (dp.y - sp.y) * phase;
          sPos[idx3 + 2] = sp.z + (dp.z - sp.z) * phase;

          const glow = flux > 0.1 ? flux * 1.15 : 0.0;
          sCol[idx3]     = glow;
          sCol[idx3 + 1] = isExc ? glow : glow * 0.25;
          sCol[idx3 + 2] = isExc ? glow : glow * 0.55;
        }
      }

      this.synLineGeo.attributes.position.needsUpdate = true;
      this.synLineGeo.attributes.color.needsUpdate = true;
      this.sparkGeo.attributes.position.needsUpdate = true;
      this.sparkGeo.attributes.color.needsUpdate = true;
    }

    this.composer.render();
  }
}
