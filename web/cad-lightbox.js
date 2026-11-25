// 3D-Viewer fuer das Gehaeuse: Lightbox mit three.js (lokal unter vendor/three, Importmap in index.html).
// three.js wird erst beim Klick auf "3D ansehen" per dynamischem import() geladen.

// Drei Gehaeuse-Varianten (enclosure/export/<variante>/glb -> gehaeuse/3d/<variante>/): LiPo 503450, LiFePO4 14500 und nur USB-C (ohne Akku).
const COMMON = [
  { id: 'assembly', label: 'Baugruppe' },
  { id: 'body', label: 'Gehäuse' },
  { id: 'front_bezel', label: 'Frontrahmen' },
  { id: 'battery_door', label: 'Akkudeckel' },
];
const VARIANTS = [
  { id: 'lipo', label: 'LiPo 503450',
    models: [...COMMON, { id: 'vent_clip', label: 'Lüftungs-Clip' }, { id: 'assembly_exploded', label: 'Explosionsansicht' }] },
  { id: 'lifepo4', label: 'LiFePO4 14500',
    models: [...COMMON, { id: 'charger_lid', label: 'Modul-Deckel' }, { id: 'vent_clip', label: 'Lüftungs-Clip' },
             { id: 'assembly_exploded', label: 'Explosionsansicht' }] },
  { id: 'usb', label: 'Nur USB',
    models: [{ id: 'assembly', label: 'Baugruppe' }, { id: 'body', label: 'Gehäuse' }, { id: 'front_bezel', label: 'Frontrahmen' },
             { id: 'vent_clip', label: 'Lüftungs-Clip' }, { id: 'assembly_exploded', label: 'Explosionsansicht' }] },
];
let variant = VARIANTS[0];
const base = () => `gehaeuse/3d/${variant.id}/`;
const HINT_MOUSE = 'Ziehen: drehen · Scrollen: zoomen · Rechtsklick: verschieben';
const HINT_TOUCH = 'Ziehen: drehen · Zwei Finger: zoomen/verschieben';
const isTouch = () => window.matchMedia('(pointer: coarse)').matches;

let viewer = null; // DOM einmal aufgebaut, bei jedem Oeffnen neu gestartet
let gen = 0;       // Generationszaehler: jedes Oeffnen/Schliessen erhoeht ihn, veraltete awaits brechen ab
let threePromise = null;

function loadThree() {
  threePromise ||= Promise.all([
    import('three'),
    import('three/addons/controls/OrbitControls.js'),
    import('three/addons/loaders/GLTFLoader.js'),
    import('three/addons/environments/RoomEnvironment.js'),
  ]).then(([THREE, oc, gl, re]) => ({
    THREE, OrbitControls: oc.OrbitControls, GLTFLoader: gl.GLTFLoader, RoomEnvironment: re.RoomEnvironment,
  }));
  threePromise.catch(() => { threePromise = null; });
  return threePromise;
}

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text != null) e.textContent = text;
  return e;
}

function buildDom() {
  const dialog = el('dialog', 'cad-dialog');
  dialog.setAttribute('aria-label', '3D-Ansicht des Gehäuses');
  const stage = el('div', 'cad-stage');
  const close = el('button', 'cad-close', '×');
  close.type = 'button';
  close.setAttribute('aria-label', 'Schließen');
  const bar = el('div', 'cad-bar');
  const tabs = el('div', 'cad-tabs');
  tabs.setAttribute('aria-label', 'Modell wählen');
  const variants = el('div', 'cad-variants');
  variants.setAttribute('role', 'group');
  variants.setAttribute('aria-label', 'Variante wählen');
  const vbuttons = VARIANTS.map((vr) => {
    const b = el('button', 'cad-variant', vr.label);
    b.type = 'button';
    b.dataset.variant = vr.id;
    variants.append(b);
    return b;
  });
  const hint = el('div', 'cad-hint', HINT_MOUSE);
  const status = el('div', 'cad-status');
  status.setAttribute('role', 'status');
  bar.append(variants, tabs, hint);
  dialog.append(stage, close, status, bar);
  document.body.append(dialog);

  const d = { dialog, stage, close, tabs, buttons: [], vbuttons, status, hint };
  d.renderTabs = () => {
    tabs.replaceChildren();
    d.buttons = variant.models.map((m) => {
      const b = el('button', 'cad-tab');
      b.type = 'button';
      b.dataset.model = m.id;
      const img = el('img');
      img.alt = '';
      img.loading = 'lazy';
      img.src = `${base()}posters/${m.id}.png`;
      img.addEventListener('error', () => img.remove());
      b.append(img, el('span', null, m.label));
      tabs.append(b);
      return b;
    });
    vbuttons.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.variant === variant.id)));
    dialog.dataset.variant = variant.id;
  };
  d.renderTabs();
  return d;
}

function disposeObject(root) {
  root.traverse((o) => {
    if (o.geometry) o.geometry.dispose();
    if (o.material) {
      for (const m of Array.isArray(o.material) ? o.material : [o.material]) {
        for (const v of Object.values(m)) if (v && v.isTexture) v.dispose();
        m.dispose();
      }
    }
  });
}

async function open(initial, variantId) {
  variant = VARIANTS.find((x) => x.id === variantId) || variant;
  if (!viewer) {
    viewer = buildDom();
    viewer.close.addEventListener('click', () => viewer.dialog.close());
  }
  const v = viewer;
  const { dialog, stage, status } = v;
  if (dialog.open) return;
  v.renderTabs();
  const my = ++gen;
  const stale = () => my !== gen;
  let cleanup = null; // wird nach dem Aufbau der Szene gesetzt
  dialog.showModal();
  document.documentElement.classList.add('cad-open');
  dialog.dataset.ready = '';
  v.hint.textContent = isTouch() ? HINT_TOUCH : HINT_MOUSE;
  status.textContent = 'Lade 3D-Viewer …';
  status.className = 'cad-status';
  // einziger Close-Handler (Esc, X, close()), sofort registriert: ungueltig machen + aufraeumen
  dialog.addEventListener('close', () => {
    gen++;
    document.documentElement.classList.remove('cad-open');
    dialog.dataset.ready = '';
    if (cleanup) cleanup();
  }, { once: true });

  let lib;
  try {
    lib = await loadThree();
  } catch (e) {
    if (!stale()) fail('3D-Viewer konnte nicht geladen werden.');
    return;
  }
  if (stale()) return;
  const { THREE, OrbitControls, GLTFLoader, RoomEnvironment } = lib;

  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, powerPreference: 'high-performance' });
  } catch (e) {
    fail('WebGL ist in diesem Browser nicht verfügbar – die 3D-Ansicht kann nicht angezeigt werden.');
    return;
  }
  renderer.domElement.setAttribute('aria-label', '3D-Ansicht des Gehäuses, mit der Maus oder per Touch drehbar');
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  stage.append(renderer.domElement);

  const scene = new THREE.Scene();
  const pmrem = new THREE.PMREMGenerator(renderer);
  const envTex = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
  scene.environment = envTex;
  scene.environmentIntensity = 0.7;
  scene.add(new THREE.HemisphereLight(0xffffff, 0x8a8f98, 0.35));
  const sun = new THREE.DirectionalLight(0xffffff, 1.3);
  scene.add(sun, sun.target);
  const camera = new THREE.PerspectiveCamera(35, 1, 0.001, 10);
  const controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;
  controls.screenSpacePanning = true;
  controls.touches = { ONE: THREE.TOUCH.ROTATE, TWO: THREE.TOUCH.DOLLY_PAN };

  const mq = window.matchMedia('(prefers-color-scheme: dark)');
  const applyTheme = () => { scene.background = new THREE.Color(mq.matches ? 0x141920 : 0xe9ecf0); };
  applyTheme();
  mq.addEventListener('change', applyTheme);

  const loader = new GLTFLoader();
  let model = null, token = 0, raf = 0, disposed = false, current = null;

  function resize() {
    const w = Math.max(stage.clientWidth, 1), h = Math.max(stage.clientHeight, 1);
    renderer.setSize(w, h, false);
    renderer.domElement.style.width = w + 'px';
    renderer.domElement.style.height = h + 'px';
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  const ro = new ResizeObserver(resize);
  ro.observe(stage);
  resize();

  function fit(obj, id) {
    const box = new THREE.Box3().setFromObject(obj);
    const sphere = box.getBoundingSphere(new THREE.Sphere());
    const vfov = THREE.MathUtils.degToRad(camera.fov);
    const hfov = 2 * Math.atan(Math.tan(vfov / 2) * camera.aspect);
    const dist = (sphere.radius / Math.sin(Math.min(vfov, hfov) / 2)) * 1.05;
    // Vorderseite (+Z) zum Betrachter, leicht von rechts oben
    // Explosionsansicht: seitlich schraeg, damit alle Teile (auch hinten) sichtbar sind
    const dir = (id === 'assembly_exploded' ? new THREE.Vector3(1.5, 0.45, 0.55) : new THREE.Vector3(0.45, 0.38, 1)).normalize();
    camera.position.copy(sphere.center).addScaledVector(dir, dist);
    camera.near = dist / 100;
    camera.far = dist * 20;
    camera.updateProjectionMatrix();
    controls.target.copy(sphere.center);
    controls.minDistance = sphere.radius * 0.4;
    controls.maxDistance = dist * 4;
    controls.update();
    sun.position.copy(sphere.center).add(new THREE.Vector3(1.2, 1.6, 2).multiplyScalar(sphere.radius * 3));
    sun.target.position.copy(sphere.center);
  }

  async function show(id) {
    const my = ++token;
    current = id;
    v.buttons.forEach((b) => b.setAttribute('aria-pressed', String(b.dataset.model === id)));
    dialog.dataset.ready = '';
    status.className = 'cad-status';
    status.textContent = 'Modell wird geladen …';
    if (model) { scene.remove(model); disposeObject(model); model = null; }
    try {
      const gltf = await loader.loadAsync(`${base()}${id}.glb`);
      if (my !== token || disposed || stale()) { disposeObject(gltf.scene); return; }
      model = gltf.scene;
      scene.add(model);
      fit(model, id);
      status.textContent = '';
      status.className = 'cad-status';
      renderer.render(scene, camera);
      dialog.dataset.ready = id;
    } catch (e) {
      if (my !== token || disposed) return;
      status.textContent = 'Das Modell konnte nicht geladen werden.';
      status.className = 'cad-status cad-error';
    }
  }

  const onTab = (e) => {
    const b = e.target.closest('.cad-tab');
    if (b && b.dataset.model !== current) show(b.dataset.model);
  };
  v.tabs.addEventListener('click', onTab);
  const onVariant = (e) => {
    const b = e.target.closest('.cad-variant');
    if (!b || b.dataset.variant === variant.id) return;
    variant = VARIANTS.find((x) => x.id === b.dataset.variant);
    v.renderTabs();
    show(variant.models.some((m) => m.id === current) ? current : 'assembly');
  };
  v.vbuttons.forEach((b) => b.addEventListener('click', onVariant));

  function loop() {
    if (disposed) return;
    raf = requestAnimationFrame(loop);
    controls.update();
    renderer.render(scene, camera);
  }
  loop();

  cleanup = function teardown() {
    disposed = true;
    cancelAnimationFrame(raf);
    ro.disconnect();
    mq.removeEventListener('change', applyTheme);
    v.tabs.removeEventListener('click', onTab);
    v.vbuttons.forEach((b) => b.removeEventListener('click', onVariant));
    controls.dispose();
    if (model) disposeObject(model);
    envTex.dispose();
    pmrem.dispose();
    renderer.dispose();
    renderer.forceContextLoss();
    renderer.domElement.remove();
  };

  show(initial);

  function fail(msg) {
    status.textContent = msg;
    status.className = 'cad-status cad-error';
  }
}

document.addEventListener('click', (e) => {
  const b = e.target.closest('[data-cad-open]');
  if (b) { e.preventDefault(); open(b.dataset.cadOpen || 'assembly', b.dataset.cadVariant); }
});
