// KO-SEKAI webcam tracking: pose model, camera loop and move detection.
// Shared by the tracking test page (index.html) and the fight (fight.html).
import { PoseLandmarker, FilesetResolver } from "https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.14";

const WASM = "https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.14/wasm";
const MODEL_URL = (n) => `https://storage.googleapis.com/mediapipe-models/pose_landmarker/pose_landmarker_${n}/float16/1/pose_landmarker_${n}.task`;

export const MOVES = ["Left straight", "Right straight", "Left hook", "Right hook", "Left uppercut", "Right uppercut", "Block", "Evade left", "Evade right", "Duck"];
export const STANCES = ["Block", "Evade left", "Evade right", "Duck"];

/* Tuning knobs. Unless noted, distances are in shoulder widths and speeds in shoulder widths per second. */
export const TUNE = {
  strokeStart: 2.0,     // arm activity that opens a "stroke" (one burst of arm movement)
  strokePeak: 3.5,      // a real punch must reach at least this activity
  strokeEnd: 0.35,      // the stroke closes when activity falls below this share of its peak
  strokeMaxMs: 450,     // ... or after this long
  minScore: 0.55,       // a punch type needs at least this score (0 to 1); divided by sensitivity
  twoHand: 0.6,         // other hand moved this much the same way: two-handed move, not a punch
  refractoryMs: 120,    // after a punch, that arm waits this long before the next stroke
  evadeOn: 0.45, evadeOff: 0.3,   // sideways lean from the calibrated position (with hysteresis)
  duckOn: 0.45, duckOff: 0.3,     // drop from the calibrated position
  holdMs: 100,          // block must be held this long before it counts
  calMs: 800,           // calibration: hold still this long
  driftTau: 5           // seconds; slowly re-centres the calibration while you stand neutral
};

const clamp01 = (v) => Math.max(0, Math.min(1, v));
const dist = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
export const r3 = (v) => Math.round(v * 1000) / 1000;
const f2 = (v) => v.toFixed(2);

/* ---------- stroke analysis (pure functions) ---------- */
const windowOf = (a, t0) => a.buf.filter((s) => s.t >= t0 - 50);
function motion(win) {
  let path = 0, maxStep = 0;
  for (let i = 1; i < win.length; i++) {
    const d = Math.hypot(win[i].ax - win[i - 1].ax, win[i].ay - win[i - 1].ay);
    path += d; if (d > maxStep) maxStep = d;
  }
  const a = win[0], b = win[win.length - 1];
  return { path, maxStep, jumped: maxStep > 0.45 && maxStep > 0.6 * path,
    wrist: Math.hypot(b.ax - a.ax, b.ay - a.ay), head: Math.hypot(b.hx - a.hx, b.hy - a.hy) };
}
// Three-frame average, so single-frame jitter does not look like movement.
const FIELDS = ["inn", "up", "elUp", "fx", "fy", "fl", "depth", "ang", "hs"];
const avgOf = (vals) => (vals.length ? vals.reduce((p, q) => p + q, 0) / vals.length : 0);
function smooth(win) {
  return win.map((_, i) => {
    const part = win.slice(Math.max(0, i - 1), i + 2), o = {};
    for (const k of FIELDS) o[k] = avgOf(k === "hs" ? part.map((s) => s.hs).filter((v) => v > 0) : part.map((s) => s[k]));
    return o;
  });
}
// Sideways and upward travel are measured from the least-extended point inside the stroke, so a wind-up
// (dropping the hand before an uppercut) does not hide the punch and pulling the arm back scores zero.
// Towards-the-camera signals are noisier, so they are measured from the average of the stroke's first frames.
function feats(win) {
  const sm = smooth(win), s1 = sm[sm.length - 1], first = sm.slice(0, 3);
  const start = (k) => avgOf(first.map((s) => s[k]).filter((v) => k !== "hs" || v > 0));
  let minIn = Infinity, minUp = Infinity, maxFl = 0;
  for (const s of sm) { minIn = Math.min(minIn, s.inn); minUp = Math.min(minUp, s.up); maxFl = Math.max(maxFl, s.fl); }
  const hs0 = start("hs"), fsum = s1.fx + Math.abs(s1.fy) + 1e-6;
  return {
    dIn: s1.inn - minIn, dUp: s1.up - minUp, dDepth: s1.depth - start("depth"), dAng: s1.ang - start("ang"),
    grow: s1.hs > 0 && hs0 > 0 ? s1.hs / hs0 : 1,
    flRatio: maxFl > 0 ? s1.fl / maxFl : 1,
    horiz: s1.fx / fsum, vert: Math.max(0, s1.fy) / fsum, elUp: s1.elUp
  };
}
function scores(f) {
  // straight: fist comes at the camera (it grows, gets nearer, the elbow opens, the forearm looks shorter)
  const ev = [clamp01(f.dDepth / 0.25), clamp01((f.grow - 1) / 0.35), clamp01((f.dAng - 10) / 50), clamp01((0.85 - f.flRatio) / 0.35)].sort((p, q) => q - p);
  let straight = (ev[0] + ev[1]) / 2;
  const flatArm = clamp01((f.flRatio - 0.6) / 0.3);   // forearm still looks full length: it swung sideways or upward
  straight = Math.max(0, straight - 0.4 * flatArm * Math.max(clamp01((f.dIn - 0.5) / 0.5), clamp01((f.dUp - 0.5) / 0.5)));
  // hook: wrist sweeps across the body with the forearm flat and the elbow raised
  const hookGeo = (clamp01((f.horiz - 0.4) / 0.35) + clamp01((f.flRatio - 0.5) / 0.3) + clamp01((f.elUp + 0.75) / 0.45)) / 3;
  const hook = clamp01((f.dIn - 0.15) / 0.6) * (0.35 + 0.65 * hookGeo);
  // uppercut: wrist rises with the forearm upright, mostly upward rather than across
  const dom = f.dUp / (f.dUp + Math.max(0, f.dIn) + 1e-6);
  const upGeo = (clamp01((f.vert - 0.45) / 0.35) + clamp01((f.flRatio - 0.45) / 0.3) + clamp01((dom - 0.45) / 0.3)) / 3;
  const uppercut = clamp01((f.dUp - 0.15) / 0.6) * (0.35 + 0.65 * upGeo);
  return { straight, hook, uppercut };
}
const mainAxis = (type, f) => type === "hook" ? f.dIn : type === "uppercut" ? f.dUp : Math.max(f.dDepth / 0.25, (f.grow - 1) / 0.35);

export function describe(r) {
  const sc = r.sc ? `  str ${f2(r.sc.straight)} hook ${f2(r.sc.hook)} upp ${f2(r.sc.uppercut)}` : "";
  return (r.move ? r.move.toUpperCase() : "no punch: " + r.why) + sc;
}

/* ---------- the detector: feed it one pose result per camera frame ---------- */
export class Detector {
  constructor() {
    this.arms = [
      { name: "Left", sh: 11, el: 13, wr: 15, sign: -1, color: "#29e6ff" },
      { name: "Right", sh: 12, el: 14, wr: 16, sign: 1, color: "#ff3fb4" }
    ];
    this.reset();
  }
  reset() {
    this.base = null; this.cal = null; this.wantCal = false; this.Ss = 0; this.lastT = 0;
    this.lastFireT = -1e9; this.coverSince = 0; this.stanceDbg = ""; this.seen = false;
    this.held = { Block: false, "Evade left": false, "Evade right": false, Duck: false };
    for (const a of this.arms) Object.assign(a, { buf: [], stroke: null, cool: 0, act: 0, last: "no strokes yet" });
  }
  calibrate() { this.wantCal = true; }
  get calProgress() { return this.cal ? clamp01((this.lastT - this.cal.t0) / TUNE.calMs) : null; }

  /** res: PoseLandmarker result; t: frame time in ms; w, h: video size; sens: punch sensitivity (1 = normal).
   *  Returns { ok, events: [{name, arm, sc, info}], judged: [stroke verdicts], calibratedNow }. Held stances are in this.held. */
  process(res, t, w, h, sens = 1) {
    const out = { ok: false, events: [], judged: [], calibratedNow: false };
    const L = res && res.landmarks && res.landmarks[0], Wd = res && res.worldLandmarks && res.worldLandmarks[0];
    this.seen = !!(L && Wd);
    if (!this.seen) {
      for (const a of this.arms) { a.buf.length = 0; a.stroke = null; }
      for (const k of STANCES) this.held[k] = false;
      this.coverSince = 0; this.Ss = 0; this.lastT = 0;
      return out;
    }
    const ar = w / h;
    const P = (i) => ({ x: L[i].x * ar, y: L[i].y });          // image-height units, aspect corrected
    const sL = P(11), sR = P(12), nose = P(0);
    const S = dist(sL, sR);
    if (S < 0.04) return out;
    out.ok = true;
    const dt = this.lastT ? Math.min(0.2, (t - this.lastT) / 1000) : 0;
    this.lastT = t;
    // Shoulder width shrinks and grows as the torso turns during punches, so use a slow average as the yardstick.
    this.Ss = this.Ss && dt ? this.Ss + (S - this.Ss) * (1 - Math.exp(-dt / 0.5)) : S;
    const mid = { x: (sL.x + sR.x) / 2, y: (sL.y + sR.y) / 2 };

    if ((!this.base && !this.cal) || this.wantCal) { this.cal = { t0: t, samples: [] }; this.wantCal = false; }
    if (this.cal && this.calStep(t, { nose, mid, S, wL: P(15), wR: P(16) })) out.calibratedNow = true;

    const results = [];
    for (const a of this.arms) {
      const r = this.strokeStep(a, this.armSample(a, t, P, L, Wd, mid, nose), t, sens);
      if (r) results.push(r);
    }
    const hits = results.filter((r) => r.move);
    if (hits.length > 1) {
      hits.sort((p, q) => q.sc[q.move] - p.sc[p.move]);
      hits[1].move = ""; hits[1].why = "other hand won";
    }
    for (const r of results) {
      const noise = r.why === "too slow" || r.why === "too short";   // jitter, not worth showing
      if (noise) continue;
      r.arm.last = describe(r);
      out.judged.push({ arm: r.arm.name, move: r.move, why: r.why, sc: r.sc });
      if (r.move) {
        r.arm.cool = t + TUNE.refractoryMs; this.lastFireT = t;
        out.events.push({ name: `${r.arm.name} ${r.move}`, arm: r.arm.name, type: r.move, sc: r.sc });
      }
    }

    if (this.base && !this.cal) this.stances(t, P, nose, mid, out);

    // While standing neutral, let the calibration follow slow drift (stepping a little, leaning back).
    const neutral = this.base && !this.cal && !this.arms.some((a) => a.stroke) && !STANCES.some((k) => this.held[k]) && t - this.lastFireT > 600;
    if (neutral && dt) {
      const b = this.base, al = 1 - Math.exp(-dt / TUNE.driftTau);
      b.nose.x += (nose.x - b.nose.x) * al; b.nose.y += (nose.y - b.nose.y) * al;
      b.mid.x += (mid.x - b.mid.x) * al; b.mid.y += (mid.y - b.mid.y) * al;
      b.S += (S - b.S) * al;
    }
    return out;
  }

  // Calibration averages a still guard and restarts if you move. Returns true when it completes.
  calStep(t, snap) {
    const cal = this.cal, first = cal.samples[0];
    if (first && (dist(snap.nose, first.nose) / first.S > 0.12 || dist(snap.wL, first.wL) / first.S > 0.3 || dist(snap.wR, first.wR) / first.S > 0.3)) {
      cal.samples = []; cal.t0 = t;
    }
    cal.samples.push(snap);
    if (t - cal.t0 < TUNE.calMs) return false;
    const n = cal.samples.length, avg = (f) => cal.samples.reduce((s, x) => s + f(x), 0) / n;
    this.base = {
      nose: { x: avg((s) => s.nose.x), y: avg((s) => s.nose.y) },
      mid: { x: avg((s) => s.mid.x), y: avg((s) => s.mid.y) },
      S: avg((s) => s.S),
      wristSep: avg((s) => dist(s.wL, s.wR) / s.S),
      wristBelowNose: avg((s) => ((s.wL.y + s.wR.y) / 2 - s.nose.y) / s.S)
    };
    this.cal = null;
    return true;
  }

  armSample(a, t, P, L, Wd, mid, head) {
    const Ss = this.Ss;
    const el = P(a.el), wr = P(a.wr), fi = P(a.wr + 4), pk = P(a.wr + 2);
    // elbow angle in 3D: about 50 degrees in guard, 150+ when the arm is straight
    const E = Wd[a.el], A = Wd[a.sh], B = Wd[a.wr];
    const u = { x: A.x - E.x, y: A.y - E.y, z: A.z - E.z }, w = { x: B.x - E.x, y: B.y - E.y, z: B.z - E.z };
    const cos = (u.x * w.x + u.y * w.y + u.z * w.z) / (Math.hypot(u.x, u.y, u.z) * Math.hypot(w.x, w.y, w.z) || 1);
    const ang = Math.acos(Math.max(-1, Math.min(1, cos))) * 180 / Math.PI;
    // apparent fist size: grows when the fist comes towards the camera
    const handOk = (L[a.wr + 4].visibility ?? 1) > 0.5 && (L[a.wr + 2].visibility ?? 1) > 0.5;
    return {
      t,
      inn: (wr.x - mid.x) * a.sign / Ss,     // sideways position, grows as the wrist moves across the body
      up: (mid.y - wr.y) / Ss,               // height above the shoulder line
      elUp: (mid.y - el.y) / Ss,
      fx: Math.abs(wr.x - el.x) / Ss, fy: (el.y - wr.y) / Ss, fl: dist(wr, el) / Ss,   // forearm as seen on screen
      depth: A.z - B.z,                      // metres, positive = fist in front of the shoulder
      ang, hs: handOk ? (dist(wr, fi) + dist(wr, pk)) / 2 / Ss : 0,
      ax: wr.x / Ss, ay: wr.y / Ss,          // wrist on screen, for spotting tracking jumps
      hx: head.x / Ss, hy: head.y / Ss,      // head on screen, for spotting whole-body moves
      vis: Math.min(L[a.wr].visibility ?? 1, L[a.el].visibility ?? 1)
    };
  }

  strokeStep(a, s, t, k) {
    a.buf.push(s);
    while (a.buf.length && a.buf[0].t < t - 700) a.buf.shift();
    let ref = null;
    for (let i = a.buf.length - 2; i >= 0; i--) if (a.buf[i].t <= t - 45) { ref = a.buf[i]; break; }
    if (!ref) return null;
    const dt = (t - ref.t) / 1000;
    const growRate = s.hs > 0 && ref.hs > 0 ? (s.hs / ref.hs - 1) / dt : 0;
    a.act = Math.hypot(s.inn - ref.inn, s.up - ref.up) / dt + Math.max(0, (s.depth - ref.depth) / dt) / 0.5
      + Math.max(0, growRate) + Math.max(0, (s.ang - ref.ang) / dt) / 250;
    if (!a.stroke) {
      if (t >= a.cool && a.act > TUNE.strokeStart / k) a.stroke = { t0: ref.t, peak: a.act };
      return null;
    }
    const st = a.stroke;
    st.peak = Math.max(st.peak, a.act);
    const dur = t - st.t0;
    if (!((a.act < TUNE.strokeEnd * st.peak && dur >= 80) || dur > TUNE.strokeMaxMs)) return null;
    a.stroke = null;
    return this.judge(a, st, t, k);
  }

  judge(a, st, t, k) {
    const win = windowOf(a, st.t0), out = { arm: a, move: "", why: "", sc: null };
    if (win.length < 3) return { ...out, why: "too short" };
    if (st.peak < TUNE.strokePeak / k) return { ...out, why: "too slow" };
    const m = motion(win);
    if (m.jumped) return { ...out, why: "tracking jump" };
    if (Math.min(...win.slice(-3).map((s) => s.vis)) < 0.45) return { ...out, why: "hand hidden" };
    if (m.head > 0.3 && m.head > 0.5 * m.wrist) return { ...out, why: "body moved, not the arm" };
    const f = feats(win), sc = scores(f);
    out.sc = sc;
    const best = Object.keys(sc).reduce((p, q) => (sc[q] > sc[p] ? q : p));
    if (sc[best] < Math.max(0.25, Math.min(0.9, TUNE.minScore / k))) return { ...out, why: "unclear" };
    // Raising both hands into a cover-up, or the whole upper body moving, shows up on both arms at once.
    const o = this.arms.find((x) => x !== a), ow = windowOf(o, st.t0);
    if (ow.length >= 2) {
      const om = motion(ow);
      const smoothMove = !om.jumped && ow[ow.length - 1].vis > 0.5;
      const mine = mainAxis(best, f), theirs = mainAxis(best, feats(ow));
      if (smoothMove && mine > 0 && theirs >= TUNE.twoHand * mine) { o.stroke = null; o.cool = t + 150; return { ...out, why: "both hands moved" }; }
    }
    return { ...out, move: best };
  }

  stances(t, P, nose, mid, out) {
    const held = this.held, base = this.base;
    const wL = P(15), wR = P(16), B = base.S, S = this.Ss;
    const lean = 0.6 * (nose.x - base.nose.x) / B + 0.4 * (mid.x - base.mid.x) / B;   // + = the player's own left
    const drop = 0.6 * (nose.y - base.nose.y) / B + 0.4 * (mid.y - base.mid.y) / B;
    const on = held.Block;
    const nearFace = (w) => Math.abs(w.x - nose.x) / S < (on ? 0.8 : 0.65) && (w.y - nose.y) / S < (on ? 0.4 : 0.2);
    const sep = dist(wL, wR) / S;
    const raised = base.wristBelowNose - ((wL.y + wR.y) / 2 - nose.y) / S;
    const cover = nearFace(wL) && nearFace(wR) && sep < (on ? 0.95 : Math.min(0.8, base.wristSep * 0.8)) && raised > (on ? 0.05 : 0.15);
    if (cover) { if (!this.coverSince) this.coverSince = t; } else this.coverSince = 0;
    const state = {
      Block: cover && (on || t - this.coverSince >= TUNE.holdMs),
      "Evade left": lean > (held["Evade left"] ? TUNE.evadeOff : TUNE.evadeOn),
      "Evade right": -lean > (held["Evade right"] ? TUNE.evadeOff : TUNE.evadeOn),
      Duck: drop > (held.Duck ? TUNE.duckOff : TUNE.duckOn)
    };
    for (const name of STANCES) {
      if (state[name] && !held[name]) { this.lastFireT = t; out.events.push({ name, info: { lean: r3(lean), drop: r3(drop), sep: r3(sep), raised: r3(raised) } }); }
      held[name] = state[name];
    }
    this.stanceDbg = `Lean ${f2(lean)} · drop ${f2(drop)} · gloves apart ${f2(sep)} · raised ${f2(raised)}`;
  }
}

/* ---------- drawing ---------- */
/** Draws the tracked skeleton onto a 2D context of size w x h (unmirrored; mirror the canvas with CSS). */
export function drawPose(g, res, w, h, arms) {
  const L = res && res.landmarks && res.landmarks[0];
  if (!L) return;
  const px = (i) => ({ x: L[i].x * w, y: L[i].y * h });
  const s = w / 640;
  g.lineWidth = 4 * s; g.lineCap = "round";
  const seg = (a, b, color) => { const p = px(a), q = px(b); g.strokeStyle = color; g.beginPath(); g.moveTo(p.x, p.y); g.lineTo(q.x, q.y); g.stroke(); };
  seg(11, 12, "#efe6ff"); seg(11, 23, "#a895cf"); seg(12, 24, "#a895cf"); seg(23, 24, "#a895cf");
  for (const a of arms) { seg(a.sh, a.el, a.color); seg(a.el, a.wr, a.color); }
  const dot = (i, color, r) => { const p = px(i); g.fillStyle = color; g.beginPath(); g.arc(p.x, p.y, r * s, 0, Math.PI * 2); g.fill(); };
  for (const a of arms) {
    dot(a.wr, a.color, 12);
    if (a.stroke) { const p = px(a.wr); g.strokeStyle = "#fff"; g.lineWidth = 3 * s; g.beginPath(); g.arc(p.x, p.y, 17 * s, 0, Math.PI * 2); g.stroke(); }
  }
  dot(0, "#ffe14d", 8);
}

/* ---------- camera + model loop ---------- */
let fileset = null;
const hasFrameCallback = typeof HTMLVideoElement !== "undefined" && "requestVideoFrameCallback" in HTMLVideoElement.prototype;

/** Runs the pose model once per camera frame and calls onResult(result, frameTimeMs). */
export class PoseCamera {
  constructor(video, onResult) {
    this.video = video; this.onResult = onResult;
    this.landmarker = null; this.modelName = ""; this.stream = null; this.running = false;
    this.loopId = 0; this.lastTs = 0; this.lastVideoTime = -1;
    this.fps = 0; this.modelMs = 0; this.frames = 0; this.fpsAt = 0;
  }
  async setModel(name) {
    if (this.landmarker && this.modelName === name) return;
    if (!fileset) fileset = await FilesetResolver.forVisionTasks(WASM);
    const make = (delegate) => PoseLandmarker.createFromOptions(fileset, { baseOptions: { modelAssetPath: MODEL_URL(name), delegate }, runningMode: "VIDEO", numPoses: 1 });
    let lm;
    try { lm = await make("GPU"); } catch (e) { lm = await make("CPU"); }
    const old = this.landmarker;
    this.landmarker = lm; this.modelName = name;
    if (old) old.close();
  }
  async start() {
    this.stream = await navigator.mediaDevices.getUserMedia({ video: { width: { ideal: 640 }, height: { ideal: 480 }, frameRate: { ideal: 60 } }, audio: false });
    this.video.srcObject = this.stream; await this.video.play();
    this.running = true; this.lastVideoTime = -1; this.frames = 0; this.fpsAt = performance.now();
    const id = ++this.loopId;
    if (hasFrameCallback) this.videoLoop(id); else this.rafLoop(id);
  }
  stop() {
    this.running = false; this.loopId += 1;
    if (this.stream) this.stream.getTracks().forEach((t) => t.stop());
    this.stream = null; this.video.srcObject = null; this.fps = 0;
  }
  get width() { return this.video.videoWidth || 640; }
  get height() { return this.video.videoHeight || 480; }
  detect(t) {
    if (!this.landmarker) return;
    if (t <= this.lastTs) t = this.lastTs + 1;               // the model needs strictly increasing timestamps
    this.lastTs = t;
    const t0 = performance.now();
    let res = null, err = null;
    try { res = this.landmarker.detectForVideo(this.video, t); } catch (e) { err = e; }
    this.modelMs = performance.now() - t0;
    this.frames += 1;
    const now = performance.now();
    if (now - this.fpsAt >= 1000) { this.fps = Math.round(this.frames * 1000 / (now - this.fpsAt)); this.frames = 0; this.fpsAt = now; }
    this.onResult(res, t, err);
  }
  // One model run per camera frame, timed by the frame itself, so speeds are measured accurately.
  videoLoop(id) {
    this.video.requestVideoFrameCallback((now, meta) => {
      if (!this.running || id !== this.loopId) return;
      const cap = meta && meta.captureTime;
      this.detect(cap && Math.abs(cap - now) < 1000 ? cap : now);
      this.videoLoop(id);
    });
  }
  rafLoop(id) {
    if (!this.running || id !== this.loopId) return;
    const v = this.video;
    if (v.readyState >= 2 && v.currentTime !== this.lastVideoTime) { this.lastVideoTime = v.currentTime; this.detect(performance.now()); }
    requestAnimationFrame(() => this.rafLoop(id));
  }
}
