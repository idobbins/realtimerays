use serde_json::Value;
use std::collections::HashMap;
use std::f32;
use std::fs::{self, File};
use std::path::{Path, PathBuf};

pub const RTR_BVH_NODE_WORDS: usize = 8;
pub const RTR_TRIANGLE_WORDS: usize = 24;
pub const DEFAULT_BVH_CONFIG: BvhBuildConfig = BvhBuildConfig {
    max_leaf_size: 2,
    split: BvhSplit::BinnedSah,
    bins: 24,
};

const DEFAULT_KAYKIT_ROOT: &str =
    "/Users/idobbins/Downloads/KayKit_Medieval_Hexagon_Pack_1.0_SOURCE/Assets/gltf";
const LIGHT_EMISSION: Vec3 = Vec3 {
    x: 11.0,
    y: 8.6,
    z: 5.6,
};

pub struct PackedScene {
    pub words: Vec<u32>,
    pub node_count: u32,
    pub triangle_count: u32,
}

#[derive(Clone, Copy, Debug)]
pub enum BvhSplit {
    Median,
    BinnedSah,
}

#[derive(Clone, Copy, Debug)]
pub struct BvhBuildConfig {
    pub max_leaf_size: usize,
    pub split: BvhSplit,
    pub bins: usize,
}

#[derive(Clone)]
pub struct SceneData {
    triangles: Vec<Triangle>,
}

#[derive(Clone)]
pub struct BuiltBvh {
    pub nodes: Vec<BvhNode>,
    pub triangles: Vec<Triangle>,
}

#[derive(Clone, Copy, Default)]
pub struct Vec2 {
    pub x: f32,
    pub y: f32,
}

#[derive(Clone, Copy, Default)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

#[derive(Clone)]
pub struct Triangle {
    pub v0: Vec3,
    pub v1: Vec3,
    pub v2: Vec3,
    pub n0: Vec3,
    pub n1: Vec3,
    pub n2: Vec3,
    pub albedo: Vec3,
    pub emission: Vec3,
    pub bmin: Vec3,
    pub bmax: Vec3,
    pub centroid: Vec3,
}

#[derive(Clone, Copy)]
pub struct BvhNode {
    pub bmin: Vec3,
    pub bmax: Vec3,
    pub left_first: u32,
    pub right_or_count: u32,
}

struct Image {
    width: usize,
    height: usize,
    rgba: Vec<u8>,
}

struct Transform {
    position: Vec3,
    yaw: f32,
    scale: f32,
}

struct SceneBuilder {
    asset_root: PathBuf,
    image_cache: HashMap<PathBuf, Image>,
    triangles: Vec<Triangle>,
}

#[derive(Clone, Copy)]
struct Bin {
    count: usize,
    bmin: Vec3,
    bmax: Vec3,
}

impl Vec2 {
    pub fn new(x: f32, y: f32) -> Self {
        Self { x, y }
    }

    pub fn add(self, rhs: Self) -> Self {
        Self::new(self.x + rhs.x, self.y + rhs.y)
    }

    pub fn mul(self, rhs: f32) -> Self {
        Self::new(self.x * rhs, self.y * rhs)
    }
}

impl Vec3 {
    pub fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    pub fn splat(v: f32) -> Self {
        Self::new(v, v, v)
    }

    pub fn add(self, rhs: Self) -> Self {
        Self::new(self.x + rhs.x, self.y + rhs.y, self.z + rhs.z)
    }

    pub fn sub(self, rhs: Self) -> Self {
        Self::new(self.x - rhs.x, self.y - rhs.y, self.z - rhs.z)
    }

    pub fn mul(self, rhs: f32) -> Self {
        Self::new(self.x * rhs, self.y * rhs, self.z * rhs)
    }

    pub fn div(self, rhs: f32) -> Self {
        Self::new(self.x / rhs, self.y / rhs, self.z / rhs)
    }

    pub fn min(self, rhs: Self) -> Self {
        Self::new(self.x.min(rhs.x), self.y.min(rhs.y), self.z.min(rhs.z))
    }

    pub fn max(self, rhs: Self) -> Self {
        Self::new(self.x.max(rhs.x), self.y.max(rhs.y), self.z.max(rhs.z))
    }

    pub fn dot(self, rhs: Self) -> f32 {
        self.x * rhs.x + self.y * rhs.y + self.z * rhs.z
    }

    pub fn cross(self, rhs: Self) -> Self {
        Self::new(
            self.y * rhs.z - self.z * rhs.y,
            self.z * rhs.x - self.x * rhs.z,
            self.x * rhs.y - self.y * rhs.x,
        )
    }

    pub fn length(self) -> f32 {
        self.dot(self).sqrt()
    }

    pub fn normalize(self) -> Self {
        let len = self.length();
        if len > 0.000001 {
            self.div(len)
        } else {
            Self::new(0.0, 1.0, 0.0)
        }
    }

    pub fn component(self, axis: usize) -> f32 {
        match axis {
            0 => self.x,
            1 => self.y,
            _ => self.z,
        }
    }
}

impl Triangle {
    pub fn new(
        v0: Vec3,
        v1: Vec3,
        v2: Vec3,
        n0: Vec3,
        n1: Vec3,
        n2: Vec3,
        albedo: Vec3,
        emission: Vec3,
    ) -> Self {
        let bmin = v0.min(v1).min(v2);
        let bmax = v0.max(v1).max(v2);
        let centroid = v0.add(v1).add(v2).div(3.0);
        Self {
            v0,
            v1,
            v2,
            n0,
            n1,
            n2,
            albedo,
            emission,
            bmin,
            bmax,
            centroid,
        }
    }
}

impl SceneData {
    pub fn triangles(&self) -> &[Triangle] {
        &self.triangles
    }
}

impl Image {
    fn sample(&self, uv: Vec2) -> Vec3 {
        let mut u = uv.x.fract();
        let mut v = uv.y.fract();
        if u < 0.0 {
            u += 1.0;
        }
        if v < 0.0 {
            v += 1.0;
        }

        let x = ((u * (self.width as f32 - 1.0)).round() as usize).min(self.width - 1);
        let y = ((v * (self.height as f32 - 1.0)).round() as usize).min(self.height - 1);
        let i = (y * self.width + x) * 4;
        let r = srgb_to_linear(self.rgba[i]);
        let g = srgb_to_linear(self.rgba[i + 1]);
        let b = srgb_to_linear(self.rgba[i + 2]);
        Vec3::new(r, g, b)
    }
}

impl Transform {
    fn new(position: Vec3, yaw: f32, scale: f32) -> Self {
        Self {
            position,
            yaw,
            scale,
        }
    }

    fn point(&self, p: Vec3) -> Vec3 {
        let p = p.mul(self.scale);
        let c = self.yaw.cos();
        let s = self.yaw.sin();
        Vec3::new(p.x * c + p.z * s, p.y, -p.x * s + p.z * c).add(self.position)
    }

    fn normal(&self, n: Vec3) -> Vec3 {
        let c = self.yaw.cos();
        let s = self.yaw.sin();
        Vec3::new(n.x * c + n.z * s, n.y, -n.x * s + n.z * c).normalize()
    }
}

impl SceneBuilder {
    fn new(asset_root: PathBuf) -> Self {
        Self {
            asset_root,
            image_cache: HashMap::new(),
            triangles: Vec::new(),
        }
    }

    fn add_asset(&mut self, rel: &str, transform: Transform) {
        let path = self.asset_root.join(rel);
        if let Err(err) = self.load_gltf(&path, transform) {
            eprintln!("realtimerays: failed to load {}: {err}", path.display());
        }
    }

    fn load_gltf(&mut self, path: &Path, transform: Transform) -> Result<(), String> {
        let text = fs::read_to_string(path).map_err(|err| err.to_string())?;
        let gltf: Value = serde_json::from_str(&text).map_err(|err| err.to_string())?;
        let base_dir = path.parent().ok_or("asset has no parent directory")?;
        let buffers = load_buffers(&gltf, base_dir)?;
        let meshes = gltf
            .get("meshes")
            .and_then(Value::as_array)
            .ok_or("missing meshes")?;

        for mesh in meshes {
            let primitives = mesh
                .get("primitives")
                .and_then(Value::as_array)
                .ok_or("missing mesh primitives")?;
            for primitive in primitives {
                let attrs = primitive
                    .get("attributes")
                    .and_then(Value::as_object)
                    .ok_or("missing primitive attributes")?;
                let pos_accessor = value_usize(attrs.get("POSITION")).ok_or("missing POSITION")?;
                let normal_accessor = value_usize(attrs.get("NORMAL"));
                let uv_accessor = value_usize(attrs.get("TEXCOORD_0"));
                let index_accessor =
                    value_usize(primitive.get("indices")).ok_or("missing primitive indices")?;
                let positions = read_vec3s(&gltf, &buffers, pos_accessor)?;
                let normals = match normal_accessor {
                    Some(accessor) => read_vec3s(&gltf, &buffers, accessor)?,
                    None => vec![Vec3::default(); positions.len()],
                };
                let uvs = match uv_accessor {
                    Some(accessor) => read_vec2s(&gltf, &buffers, accessor)?,
                    None => vec![Vec2::default(); positions.len()],
                };
                let indices = read_indices(&gltf, &buffers, index_accessor)?;
                let material_index = value_usize(primitive.get("material")).unwrap_or(0);
                let image_path = material_image_path(&gltf, base_dir, material_index);

                for tri in indices.chunks_exact(3) {
                    let i0 = tri[0] as usize;
                    let i1 = tri[1] as usize;
                    let i2 = tri[2] as usize;
                    if i0 >= positions.len() || i1 >= positions.len() || i2 >= positions.len() {
                        continue;
                    }

                    let p0 = transform.point(positions[i0]);
                    let p1 = transform.point(positions[i1]);
                    let p2 = transform.point(positions[i2]);
                    let face_n = p1.sub(p0).cross(p2.sub(p0)).normalize();
                    let n0 = normal_at(&normals, i0, face_n, &transform);
                    let n1 = normal_at(&normals, i1, face_n, &transform);
                    let n2 = normal_at(&normals, i2, face_n, &transform);
                    let uv = uvs[i0].add(uvs[i1]).add(uvs[i2]).mul(1.0 / 3.0);
                    let albedo = image_path
                        .as_ref()
                        .and_then(|image_path| self.sample_image(image_path, uv).ok())
                        .unwrap_or_else(|| fallback_albedo(path));
                    self.triangles.push(Triangle::new(
                        p0,
                        p1,
                        p2,
                        n0,
                        n1,
                        n2,
                        albedo,
                        Vec3::splat(0.0),
                    ));
                }
            }
        }

        Ok(())
    }

    fn sample_image(&mut self, path: &Path, uv: Vec2) -> Result<Vec3, String> {
        if !self.image_cache.contains_key(path) {
            let image = load_png(path)?;
            self.image_cache.insert(path.to_path_buf(), image);
        }
        Ok(self.image_cache.get(path).unwrap().sample(uv))
    }

    fn add_light(&mut self) {
        let center = Vec3::new(0.0, 6.0, -2.8);
        let u = Vec3::new(2.8, 0.0, 0.0);
        let v = Vec3::new(0.0, 0.0, 2.0);
        let n = Vec3::new(0.0, -1.0, 0.0);
        let p0 = center.sub(u.mul(0.5)).sub(v.mul(0.5));
        let p1 = center.add(u.mul(0.5)).sub(v.mul(0.5));
        let p2 = center.add(u.mul(0.5)).add(v.mul(0.5));
        let p3 = center.sub(u.mul(0.5)).add(v.mul(0.5));
        self.triangles.push(Triangle::new(
            p0,
            p2,
            p1,
            n,
            n,
            n,
            Vec3::splat(1.0),
            LIGHT_EMISSION,
        ));
        self.triangles.push(Triangle::new(
            p0,
            p3,
            p2,
            n,
            n,
            n,
            Vec3::splat(1.0),
            LIGHT_EMISSION,
        ));
    }

    fn add_fallback_scene(&mut self) {
        let n = Vec3::new(0.0, 1.0, 0.0);
        let a = Vec3::new(0.45, 0.62, 0.36);
        self.triangles.push(Triangle::new(
            Vec3::new(-5.0, 0.0, -5.0),
            Vec3::new(5.0, 0.0, -5.0),
            Vec3::new(5.0, 0.0, 5.0),
            n,
            n,
            n,
            a,
            Vec3::splat(0.0),
        ));
        self.triangles.push(Triangle::new(
            Vec3::new(-5.0, 0.0, -5.0),
            Vec3::new(5.0, 0.0, 5.0),
            Vec3::new(-5.0, 0.0, 5.0),
            n,
            n,
            n,
            a,
            Vec3::splat(0.0),
        ));
        self.add_light();
    }
}

pub fn load_scene_data() -> SceneData {
    let asset_root = std::env::var_os("RTR_KAYKIT_GLTF_ROOT")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(DEFAULT_KAYKIT_ROOT));
    let mut builder = SceneBuilder::new(asset_root);

    add_hex_world(&mut builder);
    builder.add_light();

    if builder.triangles.is_empty() {
        builder.add_fallback_scene();
    }

    SceneData {
        triangles: builder.triangles,
    }
}

pub fn build_packed_scene() -> PackedScene {
    build_packed_scene_with_config(DEFAULT_BVH_CONFIG)
}

pub fn build_packed_scene_with_config(config: BvhBuildConfig) -> PackedScene {
    let scene = load_scene_data();
    let triangle_count = scene.triangles.len();
    let bvh = build_bvh(&scene.triangles, config);
    let mut words = Vec::with_capacity(
        bvh.nodes.len() * RTR_BVH_NODE_WORDS + bvh.triangles.len() * RTR_TRIANGLE_WORDS,
    );
    for node in &bvh.nodes {
        push_vec3(&mut words, node.bmin);
        words.push(node.left_first);
        push_vec3(&mut words, node.bmax);
        words.push(node.right_or_count);
    }
    for triangle in &bvh.triangles {
        let e1 = triangle.v1.sub(triangle.v0);
        let e2 = triangle.v2.sub(triangle.v0);
        push_vec3(&mut words, triangle.v0);
        push_vec3(&mut words, e1);
        push_vec3(&mut words, e2);
        push_vec3(&mut words, triangle.n0);
        push_vec3(&mut words, triangle.n1);
        push_vec3(&mut words, triangle.n2);
        push_vec3(&mut words, triangle.albedo);
        push_vec3(&mut words, triangle.emission);
    }

    eprintln!(
        "realtimerays: packed KayKit scene: {} triangles, {} BVH nodes, {} KiB ({:?})",
        triangle_count,
        bvh.nodes.len(),
        words.len() * 4 / 1024,
        config
    );

    PackedScene {
        words,
        node_count: bvh.nodes.len() as u32,
        triangle_count: bvh.triangles.len() as u32,
    }
}

fn add_hex_world(builder: &mut SceneBuilder) {
    let radius: i32 = 4;
    for q in -radius..=radius {
        for r in -radius..=radius {
            let s = -q - r;
            let d = q.abs().max(r.abs()).max(s.abs());
            if d > radius {
                continue;
            }
            let pos = axial_to_world(q, r);
            let tile = if d == radius {
                "tiles/base/hex_water.gltf"
            } else if q == 0 || r == 0 {
                "tiles/roads/hex_road_A.gltf"
            } else {
                "tiles/base/hex_grass.gltf"
            };
            builder.add_asset(tile, Transform::new(pos, road_yaw(q, r), 1.0));
        }
    }

    let building_scale = 0.95;
    let buildings = [
        ("buildings/red/building_castle_red.gltf", 0, 0, 0.0, 0.86),
        (
            "buildings/red/building_townhall_red.gltf",
            -2,
            1,
            0.7,
            building_scale,
        ),
        (
            "buildings/red/building_blacksmith_red.gltf",
            2,
            -1,
            -0.4,
            building_scale,
        ),
        (
            "buildings/red/building_market_red.gltf",
            -1,
            -2,
            2.6,
            building_scale,
        ),
        (
            "buildings/red/building_home_A_red.gltf",
            2,
            1,
            -2.1,
            building_scale,
        ),
        ("buildings/red/building_well_red.gltf", -1, 1, 0.0, 0.95),
        (
            "buildings/neutral/fence_wood_straight.gltf",
            -2,
            0,
            1.57,
            1.0,
        ),
        (
            "buildings/neutral/fence_wood_straight.gltf",
            1,
            -2,
            0.0,
            1.0,
        ),
    ];
    for (asset, q, r, yaw, scale) in buildings {
        builder.add_asset(asset, Transform::new(axial_to_world(q, r), yaw, scale));
    }

    let trees = [
        (-3, 2, 0.1),
        (-3, 3, 1.0),
        (-2, 3, 2.3),
        (1, 3, 0.5),
        (3, -3, 2.7),
        (3, -1, 1.7),
        (-3, 0, 0.8),
        (0, -3, 2.0),
    ];
    for (q, r, yaw) in trees {
        builder.add_asset(
            "decoration/nature/trees_A_medium.gltf",
            Transform::new(
                axial_to_world(q, r).add(Vec3::new(0.15, 0.0, -0.1)),
                yaw,
                1.0,
            ),
        );
    }

    let props = [
        ("decoration/props/barrel.gltf", -1, 0, 0.1, 1.0),
        ("decoration/props/crate_A_big.gltf", -1, 0, 0.8, 1.0),
        ("decoration/props/flag_red.gltf", 1, 0, -0.3, 1.0),
        ("units/red/unit_red_full.gltf", 0, 1, 2.7, 1.2),
        ("units/red/horse_red_full.gltf", 1, -1, -0.6, 1.1),
    ];
    for (asset, q, r, yaw, scale) in props {
        let pos = axial_to_world(q, r).add(Vec3::new(0.35, 0.0, 0.25));
        builder.add_asset(asset, Transform::new(pos, yaw, scale));
    }
}

fn axial_to_world(q: i32, r: i32) -> Vec3 {
    let q = q as f32;
    let r = r as f32;
    Vec3::new(1.5 * q, 0.0, 1.732_050_8 * (r + q * 0.5))
}

fn road_yaw(q: i32, r: i32) -> f32 {
    if q == 0 && r != 0 {
        1.047_197_6
    } else {
        0.0
    }
}

pub fn build_bvh(triangles: &[Triangle], config: BvhBuildConfig) -> BuiltBvh {
    let mut nodes = Vec::new();
    let mut ordered = Vec::with_capacity(triangles.len());
    let mut indices: Vec<usize> = (0..triangles.len()).collect();
    build_bvh_node(triangles, &mut indices, &mut nodes, &mut ordered, config);
    BuiltBvh {
        nodes,
        triangles: ordered,
    }
}

fn build_bvh_node(
    triangles: &[Triangle],
    indices: &mut [usize],
    nodes: &mut Vec<BvhNode>,
    ordered: &mut Vec<Triangle>,
    config: BvhBuildConfig,
) -> u32 {
    let node_index = nodes.len() as u32;
    let (bmin, bmax, cmin, cmax) = bounds_for(triangles, indices);
    nodes.push(BvhNode {
        bmin,
        bmax,
        left_first: 0,
        right_or_count: 0,
    });

    if indices.len() <= config.max_leaf_size.max(1) {
        let first = ordered.len() as u32;
        for index in indices.iter() {
            ordered.push(triangles[*index].clone());
        }
        nodes[node_index as usize].left_first = first;
        nodes[node_index as usize].right_or_count = 0x8000_0000u32 | indices.len() as u32;
        return node_index;
    }

    let mid = match config.split {
        BvhSplit::Median => split_median(triangles, indices, cmin, cmax),
        BvhSplit::BinnedSah => split_binned_sah(triangles, indices, cmin, cmax, config.bins)
            .unwrap_or_else(|| split_median(triangles, indices, cmin, cmax)),
    };
    let (left_indices, right_indices) = indices.split_at_mut(mid);
    let left = build_bvh_node(triangles, left_indices, nodes, ordered, config);
    let right = build_bvh_node(triangles, right_indices, nodes, ordered, config);
    nodes[node_index as usize].left_first = left;
    nodes[node_index as usize].right_or_count = right;
    node_index
}

fn split_median(triangles: &[Triangle], indices: &mut [usize], cmin: Vec3, cmax: Vec3) -> usize {
    let axis = largest_axis(cmax.sub(cmin));
    indices.sort_by(|a, b| {
        triangles[*a]
            .centroid
            .component(axis)
            .total_cmp(&triangles[*b].centroid.component(axis))
    });
    indices.len() / 2
}

fn split_binned_sah(
    triangles: &[Triangle],
    indices: &mut [usize],
    cmin: Vec3,
    cmax: Vec3,
    bins: usize,
) -> Option<usize> {
    let bin_count = bins.clamp(4, 32);
    let mut best_axis = 0usize;
    let mut best_split = 0usize;
    let mut best_cost = f32::INFINITY;
    let extent = cmax.sub(cmin);

    for axis in 0..3 {
        let axis_extent = extent.component(axis);
        if axis_extent <= 0.000001 {
            continue;
        }

        let mut bins = vec![
            Bin {
                count: 0,
                bmin: Vec3::splat(f32::INFINITY),
                bmax: Vec3::splat(f32::NEG_INFINITY),
            };
            bin_count
        ];
        for index in indices.iter() {
            let tri = &triangles[*index];
            let mut bin = ((tri.centroid.component(axis) - cmin.component(axis)) / axis_extent
                * bin_count as f32) as usize;
            if bin >= bin_count {
                bin = bin_count - 1;
            }
            bins[bin].count += 1;
            bins[bin].bmin = bins[bin].bmin.min(tri.bmin);
            bins[bin].bmax = bins[bin].bmax.max(tri.bmax);
        }

        let mut left_count = vec![0usize; bin_count - 1];
        let mut left_area = vec![0.0f32; bin_count - 1];
        let mut right_count = vec![0usize; bin_count - 1];
        let mut right_area = vec![0.0f32; bin_count - 1];

        let mut count = 0usize;
        let mut bmin = Vec3::splat(f32::INFINITY);
        let mut bmax = Vec3::splat(f32::NEG_INFINITY);
        for i in 0..bin_count - 1 {
            if bins[i].count > 0 {
                bmin = bmin.min(bins[i].bmin);
                bmax = bmax.max(bins[i].bmax);
            }
            count += bins[i].count;
            left_count[i] = count;
            left_area[i] = surface_area(bmin, bmax);
        }

        count = 0;
        bmin = Vec3::splat(f32::INFINITY);
        bmax = Vec3::splat(f32::NEG_INFINITY);
        for i in (1..bin_count).rev() {
            if bins[i].count > 0 {
                bmin = bmin.min(bins[i].bmin);
                bmax = bmax.max(bins[i].bmax);
            }
            count += bins[i].count;
            right_count[i - 1] = count;
            right_area[i - 1] = surface_area(bmin, bmax);
        }

        for split in 0..bin_count - 1 {
            if left_count[split] == 0 || right_count[split] == 0 {
                continue;
            }
            let cost = left_area[split] * left_count[split] as f32
                + right_area[split] * right_count[split] as f32;
            if cost < best_cost {
                best_cost = cost;
                best_axis = axis;
                best_split = split;
            }
        }
    }

    if !best_cost.is_finite() {
        return None;
    }

    let axis_extent = extent.component(best_axis);
    let split_pos =
        cmin.component(best_axis) + axis_extent * (best_split + 1) as f32 / bin_count as f32;
    let mut left = 0usize;
    let mut right = indices.len();
    while left < right {
        let index = indices[left];
        if triangles[index].centroid.component(best_axis) < split_pos {
            left += 1;
        } else {
            right -= 1;
            indices.swap(left, right);
        }
    }

    if left == 0 || left == indices.len() {
        None
    } else {
        Some(left)
    }
}

fn largest_axis(v: Vec3) -> usize {
    if v.x >= v.y && v.x >= v.z {
        0
    } else if v.y >= v.z {
        1
    } else {
        2
    }
}

fn surface_area(bmin: Vec3, bmax: Vec3) -> f32 {
    let d = bmax.sub(bmin);
    if d.x < 0.0 || d.y < 0.0 || d.z < 0.0 {
        0.0
    } else {
        2.0 * (d.x * d.y + d.y * d.z + d.z * d.x)
    }
}

fn bounds_for(triangles: &[Triangle], indices: &[usize]) -> (Vec3, Vec3, Vec3, Vec3) {
    let mut bmin = Vec3::splat(f32::INFINITY);
    let mut bmax = Vec3::splat(f32::NEG_INFINITY);
    let mut cmin = Vec3::splat(f32::INFINITY);
    let mut cmax = Vec3::splat(f32::NEG_INFINITY);
    for index in indices {
        let triangle = &triangles[*index];
        bmin = bmin.min(triangle.bmin);
        bmax = bmax.max(triangle.bmax);
        cmin = cmin.min(triangle.centroid);
        cmax = cmax.max(triangle.centroid);
    }
    let pad = Vec3::splat(0.0001);
    (bmin.sub(pad), bmax.add(pad), cmin, cmax)
}

fn load_buffers(gltf: &Value, base_dir: &Path) -> Result<Vec<Vec<u8>>, String> {
    let buffers = gltf
        .get("buffers")
        .and_then(Value::as_array)
        .ok_or("missing buffers")?;
    let mut data = Vec::with_capacity(buffers.len());
    for buffer in buffers {
        let uri = buffer
            .get("uri")
            .and_then(Value::as_str)
            .ok_or("buffer without uri")?;
        data.push(fs::read(base_dir.join(uri)).map_err(|err| err.to_string())?);
    }
    Ok(data)
}

fn read_vec3s(
    gltf: &Value,
    buffers: &[Vec<u8>],
    accessor_index: usize,
) -> Result<Vec<Vec3>, String> {
    read_float_vectors(gltf, buffers, accessor_index, 3).map(|values| {
        values
            .chunks_exact(3)
            .map(|v| Vec3::new(v[0], v[1], v[2]))
            .collect()
    })
}

fn read_vec2s(
    gltf: &Value,
    buffers: &[Vec<u8>],
    accessor_index: usize,
) -> Result<Vec<Vec2>, String> {
    read_float_vectors(gltf, buffers, accessor_index, 2).map(|values| {
        values
            .chunks_exact(2)
            .map(|v| Vec2::new(v[0], v[1]))
            .collect()
    })
}

fn read_float_vectors(
    gltf: &Value,
    buffers: &[Vec<u8>],
    accessor_index: usize,
    component_count: usize,
) -> Result<Vec<f32>, String> {
    let (accessor, view, data, base_offset, stride) = accessor_data(gltf, buffers, accessor_index)?;
    if value_u64(accessor.get("componentType")) != Some(5126) {
        return Err("expected float accessor".to_string());
    }
    let count = value_usize(accessor.get("count")).ok_or("accessor missing count")?;
    let stride = stride.unwrap_or(component_count * 4);
    let mut out = Vec::with_capacity(count * component_count);
    let view_length = value_usize(view.get("byteLength")).unwrap_or(data.len() - base_offset);
    for i in 0..count {
        let offset = base_offset + i * stride;
        if offset + component_count * 4 > data.len()
            || offset + component_count * 4 > base_offset + view_length
        {
            return Err("accessor out of bounds".to_string());
        }
        for c in 0..component_count {
            out.push(read_f32(data, offset + c * 4));
        }
    }
    Ok(out)
}

fn read_indices(
    gltf: &Value,
    buffers: &[Vec<u8>],
    accessor_index: usize,
) -> Result<Vec<u32>, String> {
    let (accessor, view, data, base_offset, stride) = accessor_data(gltf, buffers, accessor_index)?;
    let component_type = value_u64(accessor.get("componentType")).ok_or("index component type")?;
    let count = value_usize(accessor.get("count")).ok_or("index count")?;
    let component_size = match component_type {
        5121 => 1,
        5123 => 2,
        5125 => 4,
        _ => return Err("unsupported index component type".to_string()),
    };
    let stride = stride.unwrap_or(component_size);
    let view_length = value_usize(view.get("byteLength")).unwrap_or(data.len() - base_offset);
    let mut out = Vec::with_capacity(count);
    for i in 0..count {
        let offset = base_offset + i * stride;
        if offset + component_size > data.len()
            || offset + component_size > base_offset + view_length
        {
            return Err("index accessor out of bounds".to_string());
        }
        let value = match component_type {
            5121 => data[offset] as u32,
            5123 => u16::from_le_bytes([data[offset], data[offset + 1]]) as u32,
            _ => u32::from_le_bytes([
                data[offset],
                data[offset + 1],
                data[offset + 2],
                data[offset + 3],
            ]),
        };
        out.push(value);
    }
    Ok(out)
}

fn accessor_data<'a>(
    gltf: &'a Value,
    buffers: &'a [Vec<u8>],
    accessor_index: usize,
) -> Result<(&'a Value, &'a Value, &'a [u8], usize, Option<usize>), String> {
    let accessors = gltf
        .get("accessors")
        .and_then(Value::as_array)
        .ok_or("missing accessors")?;
    let buffer_views = gltf
        .get("bufferViews")
        .and_then(Value::as_array)
        .ok_or("missing bufferViews")?;
    let accessor = accessors
        .get(accessor_index)
        .ok_or("accessor index out of range")?;
    let view_index =
        value_usize(accessor.get("bufferView")).ok_or("accessor missing bufferView")?;
    let view = buffer_views
        .get(view_index)
        .ok_or("bufferView index out of range")?;
    let buffer_index = value_usize(view.get("buffer")).ok_or("bufferView missing buffer")?;
    let data = buffers
        .get(buffer_index)
        .ok_or("buffer index out of range")?
        .as_slice();
    let base_offset = value_usize(view.get("byteOffset")).unwrap_or(0)
        + value_usize(accessor.get("byteOffset")).unwrap_or(0);
    let stride = value_usize(view.get("byteStride"));
    Ok((accessor, view, data, base_offset, stride))
}

fn material_image_path(gltf: &Value, base_dir: &Path, material_index: usize) -> Option<PathBuf> {
    let materials = gltf.get("materials")?.as_array()?;
    let textures = gltf.get("textures")?.as_array()?;
    let images = gltf.get("images")?.as_array()?;
    let material = materials.get(material_index)?;
    let texture_index = material
        .get("pbrMetallicRoughness")?
        .get("baseColorTexture")?
        .get("index")
        .and_then(|value| value.as_u64().map(|v| v as usize))?;
    let source = textures
        .get(texture_index)?
        .get("source")
        .and_then(|value| value.as_u64().map(|v| v as usize))?;
    let uri = images.get(source)?.get("uri")?.as_str()?;
    Some(base_dir.join(uri))
}

fn load_png(path: &Path) -> Result<Image, String> {
    let file = File::open(path).map_err(|err| err.to_string())?;
    let decoder = png::Decoder::new(file);
    let mut reader = decoder.read_info().map_err(|err| err.to_string())?;
    let mut buf = vec![0; reader.output_buffer_size()];
    let info = reader.next_frame(&mut buf).map_err(|err| err.to_string())?;
    let data = &buf[..info.buffer_size()];
    let width = info.width as usize;
    let height = info.height as usize;
    let mut rgba = Vec::with_capacity(width * height * 4);
    match info.color_type {
        png::ColorType::Rgba => rgba.extend_from_slice(data),
        png::ColorType::Rgb => {
            for rgb in data.chunks_exact(3) {
                rgba.extend_from_slice(&[rgb[0], rgb[1], rgb[2], 255]);
            }
        }
        _ => return Err("unsupported PNG color type".to_string()),
    }
    Ok(Image {
        width,
        height,
        rgba,
    })
}

fn normal_at(normals: &[Vec3], index: usize, fallback: Vec3, transform: &Transform) -> Vec3 {
    normals
        .get(index)
        .copied()
        .filter(|n| n.dot(*n) > 0.000001)
        .map(|n| transform.normal(n))
        .unwrap_or(fallback)
}

fn fallback_albedo(path: &Path) -> Vec3 {
    let text = path.to_string_lossy();
    if text.contains("water") || text.contains("river") {
        Vec3::new(0.10, 0.28, 0.55)
    } else if text.contains("tree") || text.contains("grass") {
        Vec3::new(0.24, 0.48, 0.20)
    } else if text.contains("red") {
        Vec3::new(0.62, 0.17, 0.12)
    } else {
        Vec3::new(0.55, 0.45, 0.34)
    }
}

fn read_f32(data: &[u8], offset: usize) -> f32 {
    f32::from_le_bytes([
        data[offset],
        data[offset + 1],
        data[offset + 2],
        data[offset + 3],
    ])
}

fn value_usize(value: Option<&Value>) -> Option<usize> {
    value?.as_u64().map(|v| v as usize)
}

fn value_u64(value: Option<&Value>) -> Option<u64> {
    value?.as_u64()
}

fn srgb_to_linear(v: u8) -> f32 {
    let v = v as f32 / 255.0;
    if v <= 0.04045 {
        v / 12.92
    } else {
        ((v + 0.055) / 1.055).powf(2.4)
    }
}

fn push_vec3(words: &mut Vec<u32>, v: Vec3) {
    words.push(v.x.to_bits());
    words.push(v.y.to_bits());
    words.push(v.z.to_bits());
}
