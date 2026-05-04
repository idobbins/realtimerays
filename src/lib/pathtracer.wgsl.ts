// WGSL path tracer shader source. Kept as a TS string so Vite bundles it cleanly.
export const pathTracerWGSL = /* wgsl */ `
struct Uniforms {
  resolution : vec2<f32>,
  frame      : u32,
  sampleIdx  : u32,
  camPos     : vec3<f32>,
  _pad0      : f32,
  camForward : vec3<f32>,
  _pad1      : f32,
  camRight   : vec3<f32>,
  _pad2      : f32,
  camUp      : vec3<f32>,
  fov        : f32,
  cameraType : u32, // 0 = perspective, 1 = orthographic, 2 = thin lens
  orthoScale : f32,
  apertureRadius : f32,
  focusDistance : f32,
};

struct Sphere {
  center   : vec3<f32>,
  radius   : f32,
  albedo   : vec3<f32>,
  matType  : f32, // 0 = diffuse, 1 = metal, 2 = emissive, 3 = glass
  emission : vec3<f32>,
  fuzz     : f32,
};

@group(0) @binding(0) var<uniform> U : Uniforms;
@group(0) @binding(1) var<storage, read> spheres : array<Sphere>;
@group(0) @binding(2) var accumPrev : texture_storage_2d<rgba32float, read>;
@group(0) @binding(3) var accumNext : texture_storage_2d<rgba32float, write>;
@group(0) @binding(4) var outTex    : texture_storage_2d<rgba8unorm, write>;

// ---------- RNG ----------
fn pcg(state : ptr<function, u32>) -> u32 {
  let oldstate = *state;
  *state = oldstate * 747796405u + 2891336453u;
  let word = ((oldstate >> ((oldstate >> 28u) + 4u)) ^ oldstate) * 277803737u;
  return (word >> 22u) ^ word;
}
fn rand(state : ptr<function, u32>) -> f32 {
  return f32(pcg(state)) / 4294967295.0;
}
fn randInUnitSphere(state : ptr<function, u32>) -> vec3<f32> {
  let z = rand(state) * 2.0 - 1.0;
  let a = rand(state) * 6.2831853;
  let r = sqrt(max(0.0, 1.0 - z * z));
  return vec3<f32>(r * cos(a), r * sin(a), z);
}
fn randInUnitDisk(state : ptr<function, u32>) -> vec2<f32> {
  let a = rand(state) * 6.2831853;
  let r = sqrt(rand(state));
  return vec2<f32>(r * cos(a), r * sin(a));
}

// ---------- Ray / hit ----------
struct Hit {
  t       : f32,
  p       : vec3<f32>,
  n       : vec3<f32>,
  matType : f32,
  albedo  : vec3<f32>,
  emission: vec3<f32>,
  fuzz    : f32,
};

fn intersectSphere(ro : vec3<f32>, rd : vec3<f32>, s : Sphere, tMax : f32) -> f32 {
  let oc = ro - s.center;
  let b = dot(oc, rd);
  let c = dot(oc, oc) - s.radius * s.radius;
  let disc = b * b - c;
  if (disc < 0.0) { return -1.0; }
  let sq = sqrt(disc);
  var t = -b - sq;
  if (t < 0.001) { t = -b + sq; }
  if (t < 0.001 || t > tMax) { return -1.0; }
  return t;
}

fn intersectPlane(ro : vec3<f32>, rd : vec3<f32>, tMax : f32) -> f32 {
  // y = -1 ground plane
  let denom = rd.y;
  if (abs(denom) < 1e-4) { return -1.0; }
  let t = (-1.0 - ro.y) / denom;
  if (t < 0.001 || t > tMax) { return -1.0; }
  return t;
}

fn sceneHit(ro : vec3<f32>, rd : vec3<f32>) -> Hit {
  var h : Hit;
  h.t = 1e30;
  let n = arrayLength(&spheres);
  for (var i = 0u; i < n; i = i + 1u) {
    let s = spheres[i];
    let t = intersectSphere(ro, rd, s, h.t);
    if (t > 0.0) {
      h.t = t;
      h.p = ro + rd * t;
      h.n = normalize(h.p - s.center);
      h.matType = s.matType;
      h.albedo = s.albedo;
      h.emission = s.emission;
      h.fuzz = s.fuzz;
    }
  }
  let tp = intersectPlane(ro, rd, h.t);
  if (tp > 0.0) {
    h.t = tp;
    h.p = ro + rd * tp;
    h.n = vec3<f32>(0.0, 1.0, 0.0);
    // checkerboard
    let cx = floor(h.p.x * 0.5);
    let cz = floor(h.p.z * 0.5);
    let chk = (cx + cz) - 2.0 * floor((cx + cz) * 0.5);
    let col = mix(vec3<f32>(0.85), vec3<f32>(0.25), chk);
    h.matType = 0.0;
    h.albedo = col;
    h.emission = vec3<f32>(0.0);
    h.fuzz = 0.0;
  }
  return h;
}

fn skyColor(rd : vec3<f32>) -> vec3<f32> {
  let t = 0.5 * (rd.y + 1.0);
  let bottom = vec3<f32>(1.0, 1.0, 1.0);
  let top = vec3<f32>(0.4, 0.6, 1.0);
  let base = mix(bottom, top, t);
  // soft sun
  let sunDir = normalize(vec3<f32>(0.5, 0.8, 0.3));
  let s = pow(max(dot(rd, sunDir), 0.0), 256.0);
  return base + vec3<f32>(8.0, 6.0, 3.0) * s;
}

fn schlick(cos : f32, ior : f32) -> f32 {
  var r0 = (1.0 - ior) / (1.0 + ior);
  r0 = r0 * r0;
  return r0 + (1.0 - r0) * pow(1.0 - cos, 5.0);
}

fn trace(roIn : vec3<f32>, rdIn : vec3<f32>, rng : ptr<function, u32>) -> vec3<f32> {
  var ro = roIn;
  var rd = rdIn;
  var throughput = vec3<f32>(1.0);
  var radiance = vec3<f32>(0.0);
  let maxBounces = 4u;
  for (var b = 0u; b < maxBounces; b = b + 1u) {
    let h = sceneHit(ro, rd);
    if (h.t >= 1e29) {
      radiance = radiance + throughput * skyColor(rd);
      break;
    }
    radiance = radiance + throughput * h.emission;
    let mt = h.matType;
    if (mt < 0.5) {
      // diffuse (lambert)
      let dir = normalize(h.n + randInUnitSphere(rng));
      ro = h.p + h.n * 0.001;
      rd = dir;
      throughput = throughput * h.albedo;
    } else if (mt < 1.5) {
      // metal
      let refl = reflect(rd, h.n);
      let dir = normalize(refl + h.fuzz * randInUnitSphere(rng));
      if (dot(dir, h.n) <= 0.0) { break; }
      ro = h.p + h.n * 0.001;
      rd = dir;
      throughput = throughput * h.albedo;
    } else if (mt < 2.5) {
      // emissive — already added
      break;
    } else {
      // glass
      var n = h.n;
      var ior = 1.5;
      var cosI = dot(-rd, n);
      var eta = 1.0 / ior;
      if (cosI < 0.0) {
        n = -n;
        cosI = -cosI;
        eta = ior;
      }
      let k = 1.0 - eta * eta * (1.0 - cosI * cosI);
      var dir : vec3<f32>;
      if (k < 0.0 || rand(rng) < schlick(cosI, ior)) {
        dir = reflect(rd, n);
      } else {
        dir = eta * rd + (eta * cosI - sqrt(k)) * n;
      }
      ro = h.p + dir * 0.001;
      rd = normalize(dir);
      throughput = throughput * h.albedo;
    }
    // russian roulette
    if (b > 1u) {
      let p = max(throughput.r, max(throughput.g, throughput.b));
      if (rand(rng) > p) { break; }
      throughput = throughput / max(p, 0.001);
    }
  }
  return radiance;
}

struct CameraRay {
  ro : vec3<f32>,
  rd : vec3<f32>,
};

fn makeCameraRay(uv : vec2<f32>, aspect : f32, rng : ptr<function, u32>) -> CameraRay {
  let scale = tan(U.fov * 0.5);
  var ray : CameraRay;

  if (U.cameraType == 1u) {
    let originOffset =
      U.camRight * (uv.x * aspect * U.orthoScale) -
      U.camUp * (uv.y * U.orthoScale);
    ray.ro = U.camPos + originOffset;
    ray.rd = normalize(U.camForward);
    return ray;
  }

  let perspectiveDir = normalize(
    U.camForward +
    U.camRight * (uv.x * aspect * scale) -
    U.camUp    * (uv.y * scale)
  );

  if (U.cameraType == 2u && U.apertureRadius > 0.0) {
    let focusT = U.focusDistance / max(dot(perspectiveDir, U.camForward), 0.001);
    let focusPoint = U.camPos + perspectiveDir * focusT;
    let lens = randInUnitDisk(rng) * U.apertureRadius;
    let originOffset = U.camRight * lens.x + U.camUp * lens.y;
    ray.ro = U.camPos + originOffset;
    ray.rd = normalize(focusPoint - ray.ro);
    return ray;
  }

  ray.ro = U.camPos;
  ray.rd = perspectiveDir;
  return ray;
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
  let dims = vec2<u32>(u32(U.resolution.x), u32(U.resolution.y));
  if (gid.x >= dims.x || gid.y >= dims.y) { return; }

  var rng = gid.x * 1973u + gid.y * 9277u + U.frame * 26699u + 1u;
  pcg(&rng);

  let spp = 1u;
  var col = vec3<f32>(0.0);
  for (var s = 0u; s < spp; s = s + 1u) {
    let jx = rand(&rng);
    let jy = rand(&rng);
    let uv = (vec2<f32>(f32(gid.x) + jx, f32(gid.y) + jy) / U.resolution) * 2.0 - 1.0;
    let aspect = U.resolution.x / U.resolution.y;
    let ray = makeCameraRay(uv, aspect, &rng);
    col = col + trace(ray.ro, ray.rd, &rng);
  }
  col = col / f32(spp);

  let coord = vec2<i32>(i32(gid.x), i32(gid.y));
  var accum : vec4<f32>;
  if (U.sampleIdx == 0u) {
    accum = vec4<f32>(col, 1.0);
  } else {
    let prev = textureLoad(accumPrev, coord);
    let n = f32(U.sampleIdx);
    accum = vec4<f32>((prev.rgb * n + col) / (n + 1.0), 1.0);
  }
  textureStore(accumNext, coord, accum);

  // tone map (Reinhard) + gamma
  var c = accum.rgb;
  c = c / (c + vec3<f32>(1.0));
  c = pow(c, vec3<f32>(1.0 / 2.2));
  textureStore(outTex, coord, vec4<f32>(c, 1.0));
}
`;

export const blitWGSL = /* wgsl */ `
@group(0) @binding(0) var srcTex : texture_2d<f32>;
@group(0) @binding(1) var samp   : sampler;

struct VsOut {
  @builtin(position) pos : vec4<f32>,
  @location(0)       uv  : vec2<f32>,
};

@vertex
fn vs(@builtin(vertex_index) vi : u32) -> VsOut {
  // fullscreen triangle
  var p = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -3.0),
    vec2<f32>(-1.0,  1.0),
    vec2<f32>( 3.0,  1.0),
  );
  let xy = p[vi];
  var o : VsOut;
  o.pos = vec4<f32>(xy, 0.0, 1.0);
  o.uv = vec2<f32>((xy.x + 1.0) * 0.5, 1.0 - (xy.y + 1.0) * 0.5);
  return o;
}

@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
  return textureSample(srcTex, samp, in.uv);
}
`;
