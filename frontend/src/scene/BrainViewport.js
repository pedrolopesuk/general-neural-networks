import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { getTheme } from '../theme.js';

export class BrainViewport {
  constructor(container, { onSelectNeuron, onHoverNeuron, onZoomTierChange }) {
    this.container = container;
    this.onSelectNeuron = onSelectNeuron;
    this.onHoverNeuron = onHoverNeuron;
    this.onZoomTierChange = onZoomTierChange;

    this.lens = 'activity';
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
    const theme = getTheme();
    const w = this.container.clientWidth || window.innerWidth;
    const h = this.container.clientHeight || window.innerHeight;

    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(theme.bg);

    this.camera = new THREE.PerspectiveCamera(50, w / h, 0.35, 3600);
    this.camera.position.set(0, 130, 395);

    this.renderer = new THREE.WebGLRenderer({ antialias: true });
    this.renderer.setSize(w, h);
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.container.appendChild(this.renderer.domElement);

    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.08;
    this.controls.minDistance = 4.5;
    this.controls.maxDistance = 980.0;

    this.somaGeo = new THREE.SphereGeometry(1, 16, 16);
    this.shellGeo = new THREE.RingGeometry(0.98, 1.0, 48);

    const ringGeo = new THREE.RingGeometry(1.35, 1.55, 32);
    const ringMat = new THREE.MeshBasicMaterial({
      color: new THREE.Color(theme.signal),
      side: THREE.DoubleSide
    });
    this.selectionRing = new THREE.Mesh(ringGeo, ringMat);
    this.selectionRing.visible = false;
    this.scene.add(this.selectionRing);

    this.graphGroup = new THREE.Group();
    this.scene.add(this.graphGroup);

    this.raycaster = new THREE.Raycaster();
    this.pointer = new THREE.Vector2(-99, -99);

    this.cDim = new THREE.Color(theme.textDim);
    this.cLine = new THREE.Color(theme.line);
    this.cSignal = new THREE.Color(theme.signal);
    this.cYoung = new THREE.Color(theme.young);
    this.cOld = new THREE.Color(theme.old);
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
        dom.style.cursor = 'default';
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
        if (this.onSelectNeuron) this.onSelectNeuron(id, e.clientX, e.clientY);
      } else {
        this.selectedId = null;
        if (this.onSelectNeuron) this.onSelectNeuron(null, 0, 0);
      }
    });

    window.addEventListener('resize', () => {
      const w = this.container.clientWidth || window.innerWidth;
      const h = this.container.clientHeight || window.innerHeight;
      this.camera.aspect = w / h;
      this.camera.updateProjectionMatrix();
      this.renderer.setSize(w, h);
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
    const theme = getTheme();
    while (this.graphGroup.children.length > 0) {
      const child = this.graphGroup.children.pop();
      if (child.geometry && child.geometry !== this.somaGeo && child.geometry !== this.shellGeo) {
        child.geometry.dispose();
      }
    }
    this.neuronVisuals.clear();
    this.holonShells.clear();

    for (const n of nodes) {
      const baseR = n.depth === 0 ? 3.2 : (n.depth === 1 ? 1.8 : 1.1);
      const mat = new THREE.MeshBasicMaterial({ color: new THREE.Color(theme.textDim) });
      const mesh = new THREE.Mesh(this.somaGeo, mat);
      mesh.position.set(n.x, n.y, n.z);
      mesh.scale.setScalar(baseR);
      mesh.userData = { id: n.id, baseR };

      this.graphGroup.add(mesh);
      this.neuronVisuals.set(n.id, { mesh, baseR });

      if (n.is_composite) {
        const shellRadius = n.depth === 0 ? 28.0 : 10.0;
        const sMat = new THREE.MeshBasicMaterial({
          color: new THREE.Color(theme.line),
          side: THREE.DoubleSide
        });
        const sMesh = new THREE.Mesh(this.shellGeo, sMat);
        sMesh.position.set(n.x, n.y, n.z);
        sMesh.scale.setScalar(shellRadius);
        this.graphGroup.add(sMesh);
        this.holonShells.set(n.id, { mesh: sMesh, shellRadius });
      }
    }

    const synCount = synapses.length;
    const linePos = new Float32Array(synCount * 6);
    const lineCol = new Float32Array(synCount * 6);
    this.synLineGeo = new THREE.BufferGeometry();
    this.synLineGeo.setAttribute('position', new THREE.BufferAttribute(linePos, 3));
    this.synLineGeo.setAttribute('color', new THREE.BufferAttribute(lineCol, 3));

    const lineMat = new THREE.LineBasicMaterial({
      vertexColors: true,
      transparent: true,
      opacity: 0.45
    });
    this.synLineMesh = new THREE.LineSegments(this.synLineGeo, lineMat);
    this.graphGroup.add(this.synLineMesh);
  }

  _computeColor(node) {
    const act = Math.max(0, Math.min(1, node.activation || 0));
    if (this.lens === 'activity') {
      if (act > 0.62) {
        return this.tmpColor.copy(this.cDim).lerp(this.cSignal, (act - 0.62) / 0.38);
      }
      return this.tmpColor.copy(this.cDim);
    }
    const mat = Math.max(0, Math.min(1, node.maturity ?? 0));
    return this.tmpColor.copy(this.cYoung).lerp(this.cOld, mat);
  }

  renderFrame() {
    if (this.cameraFlight) {
      this.controls.target.lerp(this.cameraFlight.lookAt, 0.12);
      this.camera.position.lerp(this.cameraFlight.pos, 0.12);
      if (this.camera.position.distanceTo(this.cameraFlight.pos) < 0.75) {
        this.cameraFlight = null;
      }
    }

    this.controls.update();

    const camDist = this.camera.position.distanceTo(this.controls.target);
    const tier = camDist > 235 ? 'macro' : (camDist > 75 ? 'cluster' : 'micro');
    if (tier !== this._lastTier) {
      this._lastTier = tier;
      if (this.onZoomTierChange) this.onZoomTierChange(tier, camDist);
    }

    for (const [id, vis] of this.neuronVisuals.entries()) {
      const n = this.nodeDataMap.get(id);
      if (!n) continue;
      vis.mesh.position.set(n.x, n.y, n.z);
      vis.mesh.material.color.copy(this._computeColor(n));

      const shell = this.holonShells.get(id);
      if (shell) {
        shell.mesh.position.copy(vis.mesh.position);
        shell.mesh.lookAt(this.camera.position);
      }
    }

    if (this.selectedId !== null && this.neuronVisuals.has(this.selectedId)) {
      const selVis = this.neuronVisuals.get(this.selectedId);
      this.selectionRing.visible = true;
      this.selectionRing.position.copy(selVis.mesh.position);
      this.selectionRing.lookAt(this.camera.position);
      this.selectionRing.scale.setScalar(selVis.baseR * 1.4);
    } else {
      this.selectionRing.visible = false;
    }

    if (this.synLineGeo) {
      const posArr = this.synLineGeo.attributes.position.array;
      const colArr = this.synLineGeo.attributes.color.array;
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

        const active = (srcNode.activation || 0) * Math.abs(s.weight) > 0.55;
        const c = active ? this.cSignal : this.cLine;
        colArr[i6]     = c.r; colArr[i6 + 1] = c.g; colArr[i6 + 2] = c.b;
        colArr[i6 + 3] = c.r; colArr[i6 + 4] = c.g; colArr[i6 + 5] = c.b;
      }
      this.synLineGeo.attributes.position.needsUpdate = true;
      this.synLineGeo.attributes.color.needsUpdate = true;
    }

    this.renderer.render(this.scene, this.camera);
  }
}
