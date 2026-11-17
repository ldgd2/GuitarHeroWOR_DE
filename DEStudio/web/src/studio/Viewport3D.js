// Vista 3D reutilizable del estudio (three.js): escena, camara de edicion con orbita, gizmo de mover/girar/escalar,
// eleccion con clic y una vista pequena de la camara activa (la que vera el juego). Sin estado del proyecto: quien la
// usa crea los objetos y escucha los callbacks.
//
//   const v = new Viewport3D(canvas, { onPick(obj|null), onGizmoStart(obj), onGizmo(obj), onGizmoEnd(obj) })
//   v.scene.add(...); v.attach(obj); v.setProgram(camera); v.look = 'edit' | 'program'; v.dispose()
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { TransformControls } from 'three/addons/controls/TransformControls.js';

export class Viewport3D {
  constructor(canvas, cb = {}) {
    this.canvas = canvas; this.cb = cb;
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
    this.renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x07080a);
    this.editorOnly = new THREE.Group(); this.scene.add(this.editorOnly);
    const grid = new THREE.GridHelper(4000, 40, 0x2a3440, 0x161b22);
    grid.position.y = -400; this.editorOnly.add(grid);
    this.editCam = new THREE.PerspectiveCamera(50, 16 / 9, 1, 50000);
    this.editCam.position.set(900, 500, 1500);
    this.orbit = new OrbitControls(this.editCam, canvas);
    this.orbit.enableDamping = true; this.orbit.dampingFactor = 0.15;
    this.tc = new TransformControls(this.editCam, canvas);
    this.tc.setSize(0.9);
    this.editorOnly.add(this.tc);
    this.tc.addEventListener('dragging-changed', e => {
      this.orbit.enabled = !e.value;
      if (e.value) cb.onGizmoStart?.(this.tc.object); else cb.onGizmoEnd?.(this.tc.object);
    });
    this.tc.addEventListener('objectChange', () => cb.onGizmo?.(this.tc.object));
    this.program = null;            // camara del proyecto que se ve en la vista pequena / a pantalla completa
    this.look = 'edit';
    this.inset = true;
    this.pickables = [];
    this._ray = new THREE.Raycaster();
    this._down = null;
    canvas.addEventListener('pointerdown', this._pd = e => { this._down = [e.clientX, e.clientY]; });
    canvas.addEventListener('pointerup', this._pu = e => {
      if (!this._down || Math.hypot(e.clientX - this._down[0], e.clientY - this._down[1]) > 4 || this.tc.dragging) return;
      this._down = null;
      cb.onPick?.(this.pick(e), e);
    });
    this._ro = new ResizeObserver(() => this.resize());
    this._ro.observe(canvas.parentElement);
    this.resize();
    this._raf = requestAnimationFrame(this._loop = () => { this.render(); this._raf = requestAnimationFrame(this._loop); });
  }

  resize() {
    const r = this.canvas.parentElement.getBoundingClientRect();
    const w = Math.max(10, r.width), h = Math.max(10, r.height);
    this.renderer.setSize(w, h, false);
    this.canvas.style.width = w + 'px'; this.canvas.style.height = h + 'px';
    this.editCam.aspect = w / h; this.editCam.updateProjectionMatrix();
  }

  /** objeto bajo el raton (de los registrados en pickables) */
  pick(e) {
    const r = this.canvas.getBoundingClientRect();
    const cam = this.look === 'program' && this.program ? this.program : this.editCam;
    const p = new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
    if (cam === this.program) {   // la vista de programa va en 16:9 centrada
      const vp = this._fit(r.width, r.height);
      p.x = ((e.clientX - r.left - vp.x) / vp.w) * 2 - 1; p.y = -((e.clientY - r.top - (r.height - vp.y - vp.h)) / vp.h) * 2 + 1;
    }
    this._ray.layers.enableAll();   // los iconos de camara van en otra capa de three.js
    this._ray.setFromCamera(p, cam);
    const hit = this._ray.intersectObjects(this.pickables.filter(o => o.visible), false)[0];
    return hit ? hit.object.userData.owner || hit.object : null;
  }

  attach(obj) { if (obj) this.tc.attach(obj); else this.tc.detach(); }
  setMode(m) { this.tc.setMode(m); }      // translate | rotate | scale
  setSpace(s) { this.tc.setSpace(s); }    // world | local

  /** encuadra la camara de edicion sobre un punto */
  frame(target, dist = 1600) {
    const dir = this.editCam.position.clone().sub(this.orbit.target).normalize();
    this.orbit.target.copy(target);
    this.editCam.position.copy(target).add(dir.multiplyScalar(dist));
  }

  _fit(w, h) {   // rectangulo 16:9 dentro de w x h
    let vw = w, vh = w * 9 / 16;
    if (vh > h) { vh = h; vw = h * 16 / 9; }
    return { x: (w - vw) / 2, y: (h - vh) / 2, w: vw, h: vh };
  }

  render() {
    this.orbit.update();
    this.cb.onFrame?.();
    const R = this.renderer, size = R.getSize(new THREE.Vector2()), W = size.x, H = size.y;
    R.setScissorTest(false);
    R.setViewport(0, 0, W, H);
    R.setClearColor(0x050607); R.clear();
    if (this.look === 'program' && this.program) {
      const vp = this._fit(W, H);
      this.editorOnly.visible = false; this.program.aspect = 16 / 9; this.program.updateProjectionMatrix();
      R.setScissorTest(true); R.setScissor(vp.x, vp.y, vp.w, vp.h); R.setViewport(vp.x, vp.y, vp.w, vp.h);
      R.render(this.scene, this.program);
      this.editorOnly.visible = true;
      return;
    }
    R.render(this.scene, this.editCam);
    if (this.inset && this.program) {
      const iw = Math.round(Math.min(360, W * 0.3)), ih = Math.round(iw * 9 / 16), m = 12;
      this.editorOnly.visible = false; this.program.aspect = 16 / 9; this.program.updateProjectionMatrix();
      R.setScissorTest(true);
      R.setScissor(W - iw - m - 2, m - 2, iw + 4, ih + 4); R.setViewport(W - iw - m - 2, m - 2, iw + 4, ih + 4);
      R.setClearColor(0xe3a043); R.clear();
      R.setScissor(W - iw - m, m, iw, ih); R.setViewport(W - iw - m, m, iw, ih);
      R.setClearColor(0x000000); R.clear();
      R.render(this.scene, this.program);
      this.editorOnly.visible = true;
      R.setScissorTest(false);
    }
  }

  dispose() {
    cancelAnimationFrame(this._raf);
    this._ro.disconnect();
    this.canvas.removeEventListener('pointerdown', this._pd);
    this.canvas.removeEventListener('pointerup', this._pu);
    this.tc.dispose(); this.orbit.dispose(); this.renderer.dispose();
  }
}
