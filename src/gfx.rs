//! The renderer: immediate-mode drawing on top of wgpu (Metal on macOS).
//!
//! Draw calls record vertices into per-pipeline CPU arenas plus a per-draw
//! uniform slot; `present` uploads both and replays the calls in the exact
//! order they were recorded (blending order matters for a transparent desktop
//! pet).

use std::num::NonZeroU64;
use std::sync::Arc;

use wgpu::{
    BindGroup, Buffer, BufferDescriptor, BufferUsages, CommandEncoderDescriptor, Device, Extent3d,
    Origin3d, Queue, RenderPipeline, RenderPipelineDescriptor, Surface, SurfaceConfiguration,
    TextureAspect, TextureDescriptor, TextureDimension, TextureFormat, TextureUsages,
    VertexAttribute, VertexBufferLayout,
};

use crate::math::{Mat4, Rect, Vec4, m4_ortho};

const UNIFORM_SLOT: u64 = 256; // also the dynamic-offset alignment
const UNIFORM_SLOTS: u64 = 240; // rounded-rect draws per frame
const UNIFORM_SIZE: u64 = UNIFORM_SLOT * UNIFORM_SLOTS;
const RR_UNIFORM_BYTES: u64 = 80;

/// Per-draw uniforms for the rounded-rect shader (its own dedicated layout —
/// the shared-union struct confused per-pipeline shader layouts).
#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
pub struct RrUniforms {
    pub rect: [f32; 4],
    pub params: [f32; 4], // r, bw, gaxis, _
    pub fill: [f32; 4],
    pub fill2: [f32; 4],
    pub border: [f32; 4],
}

/// A font glyph quad vertex: position (pixels), atlas uv, RGBA color.
#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
pub struct FontVert {
    pub pos: [f32; 2],
    pub uv: [f32; 2],
    pub col: [f32; 4],
}

/// Flat colored vertex: shadow/apple mesh, sparks, confetti quads.
#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
pub struct FlatVert {
    pub pos: [f32; 2],
    pub col: [f32; 4],
}

/// Sprite quad vertex: position (pixels, CPU-transformed) and atlas uv.
#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
pub struct SprVert {
    pub pos: [f32; 2],
    pub uv: [f32; 2],
}

/// Rounded-rect vertex: pixel-space position (the SDF stays in pixel space).
#[repr(C)]
#[derive(Clone, Copy, bytemuck::Pod, bytemuck::Zeroable)]
pub struct RrVert {
    pub pos: [f32; 2],
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Pipe {
    Sprite,
    Flat,
    Rr,
    Font,
}

struct DrawCmd {
    pipe: Pipe,
    first: u32, // first vertex in the pipe's arena
    count: u32,
    uniform: Option<u16>,      // rrect uniform slot; none for the others
    scissor: Option<[u32; 4]>, // pixel scissor at record time
}

/// CPU vertex arena + its GPU mirror (recreated on growth between frames).
struct Arena {
    cpu: Vec<u8>,
    cap: u64,
    gpu: Option<Buffer>,
}

impl Arena {
    fn new(initial: u64) -> Self {
        Self {
            cpu: Vec::new(),
            cap: initial,
            gpu: None,
        }
    }

    /// Append a vertex slice; returns (first vertex index, vertex count).
    fn push<V: bytemuck::Pod>(&mut self, verts: &[V], stride: u64) -> (u32, u32) {
        let first = (self.cpu.len() / stride as usize) as u32;
        self.cpu.extend_from_slice(bytemuck::cast_slice(verts));
        (first, verts.len() as u32)
    }

    fn buffer(&mut self, device: &Device, label: &str) -> &Buffer {
        let needed = self.cpu.len() as u64;
        if self.gpu.is_none() || self.cap < needed {
            while self.cap < needed {
                self.cap *= 2;
            }
            self.gpu = Some(device.create_buffer(&BufferDescriptor {
                label: Some(label),
                size: self.cap,
                usage: BufferUsages::VERTEX | BufferUsages::COPY_DST,
                mapped_at_creation: false,
            }));
        }
        self.gpu.as_ref().unwrap()
    }
}

pub struct Gfx {
    pub device: Device,
    pub queue: Queue,
    surface: Surface<'static>,
    config: SurfaceConfiguration,

    globals_buf: Buffer,
    uniform_buf: Buffer,
    uniform_bg: BindGroup,

    // (pipe, pipeline, group 0)
    pipelines: Vec<(Pipe, RenderPipeline, BindGroup)>,

    font_arena: Arena,
    flat_arena: Arena,
    spr_arena: Arena,
    rr_arena: Arena,

    draws: Vec<DrawCmd>,
    uniforms: Vec<RrUniforms>,
    cur_scissor: Option<[u32; 4]>,

    // frame geometry: logical (points) and pixel sizes
    lw: f32,
    lh: f32,
    pw: u32,
    ph: u32,
}

impl Gfx {
    pub fn new(
        instance: wgpu::Instance,
        window: Arc<winit::window::Window>,
        sprite_rgba: (&[u8], u32, u32),
        font_rgba: (&[u8], u32, u32),
    ) -> Self {
        let surface = instance
            .create_surface(window.clone())
            .expect("create wgpu surface");
        let adapter = pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions {
            power_preference: wgpu::PowerPreference::None,
            compatible_surface: Some(&surface),
            force_fallback_adapter: false,
            ..Default::default()
        }))
        .expect("request graphics adapter");
        let (device, queue) =
            pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor::default()))
                .expect("request graphics device");

        let caps = surface.get_capabilities(&adapter);
        // Keep values raw (like the GL framebuffer): pick a non-srgb format.
        let format = [TextureFormat::Bgra8Unorm, TextureFormat::Rgba8Unorm]
            .into_iter()
            .find(|f| caps.formats.contains(f))
            .unwrap_or(caps.formats[0]);
        // The desktop shows through: the window server treats the framebuffer
        // as premultiplied — exactly what these pipelines produce. Metal
        // historically advertised PostMultiplied for this and is migrating to
        // PreMultiplied; take whichever the surface reports.
        let alpha_mode = if caps
            .alpha_modes
            .contains(&wgpu::CompositeAlphaMode::PreMultiplied)
        {
            wgpu::CompositeAlphaMode::PreMultiplied
        } else if caps
            .alpha_modes
            .contains(&wgpu::CompositeAlphaMode::PostMultiplied)
        {
            wgpu::CompositeAlphaMode::PostMultiplied
        } else {
            wgpu::CompositeAlphaMode::Auto
        };
        let size = window.inner_size();
        let config = SurfaceConfiguration {
            usage: TextureUsages::RENDER_ATTACHMENT,
            format,
            width: size.width.max(1),
            height: size.height.max(1),
            present_mode: wgpu::PresentMode::Fifo, // vsync, like SwapInterval(1)
            alpha_mode,
            view_formats: vec![],
            desired_maximum_frame_latency: 2,
            color_space: wgpu::SurfaceColorSpace::Auto,
        };
        surface.configure(&device, &config);

        let globals_buf = device.create_buffer(&BufferDescriptor {
            label: Some("globals"),
            size: 64,
            usage: BufferUsages::UNIFORM | BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });
        let uniform_buf = device.create_buffer(&BufferDescriptor {
            label: Some("draw-uniforms"),
            size: UNIFORM_SIZE,
            usage: BufferUsages::UNIFORM | BufferUsages::COPY_DST,
            mapped_at_creation: false,
        });

        // --- bind group layouts -------------------------------------------------
        let globals_entry = |binding: u32| wgpu::BindGroupLayoutEntry {
            binding,
            visibility: wgpu::ShaderStages::VERTEX | wgpu::ShaderStages::FRAGMENT,
            ty: wgpu::BindingType::Buffer {
                ty: wgpu::BufferBindingType::Uniform,
                has_dynamic_offset: false,
                min_binding_size: NonZeroU64::new(64),
            },
            count: None,
        };
        let plain_bgl = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("plain-bgl"),
            entries: &[globals_entry(0)],
        });
        let tex_bgl = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("tex-bgl"),
            entries: &[
                globals_entry(0),
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
            ],
        });
        let u_bgl = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("rr-uniform-bgl"),
            entries: &[wgpu::BindGroupLayoutEntry {
                binding: 0,
                visibility: wgpu::ShaderStages::VERTEX | wgpu::ShaderStages::FRAGMENT,
                ty: wgpu::BindingType::Buffer {
                    ty: wgpu::BufferBindingType::Uniform,
                    has_dynamic_offset: true,
                    min_binding_size: NonZeroU64::new(RR_UNIFORM_BYTES),
                },
                count: None,
            }],
        });
        let uniform_bg = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("draw-uniform-bg"),
            layout: &u_bgl,
            entries: &[wgpu::BindGroupEntry {
                binding: 0,
                // Bind one uniform-sized window at offset 0 — dynamic offsets
                // slide it through the buffer (binding the whole buffer would
                // leave "the maximum the binding can be offset is 0 bytes").
                resource: wgpu::BindingResource::Buffer(wgpu::BufferBinding {
                    buffer: &uniform_buf,
                    offset: 0,
                    size: Some(NonZeroU64::new(RR_UNIFORM_BYTES).unwrap()),
                }),
            }],
        });

        // --- textures ------------------------------------------------------------
        let sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("linear-clamp"),
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            ..Default::default() // clamp-to-edge is the default
        });
        let spr_view =
            Self::make_texture(&device, &queue, sprite_rgba).create_view(&Default::default());
        let font_view =
            Self::make_texture(&device, &queue, font_rgba).create_view(&Default::default());
        let globals_binding = wgpu::BindGroupEntry {
            binding: 0,
            resource: globals_buf.as_entire_binding(),
        };
        let plain_bg = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("plain-bg"),
            layout: &plain_bgl,
            entries: std::slice::from_ref(&globals_binding),
        });
        let spr_bg = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("sprite-bg"),
            layout: &tex_bgl,
            entries: &[
                globals_binding.clone(),
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&spr_view),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::Sampler(&sampler),
                },
            ],
        });
        let font_bg = device.create_bind_group(&wgpu::BindGroupDescriptor {
            label: Some("font-bg"),
            layout: &tex_bgl,
            entries: &[
                globals_binding,
                wgpu::BindGroupEntry {
                    binding: 1,
                    resource: wgpu::BindingResource::TextureView(&font_view),
                },
                wgpu::BindGroupEntry {
                    binding: 2,
                    resource: wgpu::BindingResource::Sampler(&sampler),
                },
            ],
        });

        // --- pipelines -------------------------------------------------------------
        //  * straight-alpha sources (flat/font): color = src.a over,
        //    alpha accumulates as coverage
        //  * premultiplied sources (sprite/rrect): One / OneMinusSrcAlpha
        let over = wgpu::BlendState {
            color: wgpu::BlendComponent {
                src_factor: wgpu::BlendFactor::SrcAlpha,
                dst_factor: wgpu::BlendFactor::OneMinusSrcAlpha,
                operation: wgpu::BlendOperation::Add,
            },
            alpha: wgpu::BlendComponent {
                src_factor: wgpu::BlendFactor::One,
                dst_factor: wgpu::BlendFactor::OneMinusSrcAlpha,
                operation: wgpu::BlendOperation::Add,
            },
        };
        let premult_over = wgpu::BlendState {
            color: wgpu::BlendComponent {
                src_factor: wgpu::BlendFactor::One,
                dst_factor: wgpu::BlendFactor::OneMinusSrcAlpha,
                operation: wgpu::BlendOperation::Add,
            },
            alpha: wgpu::BlendComponent {
                src_factor: wgpu::BlendFactor::One,
                dst_factor: wgpu::BlendFactor::OneMinusSrcAlpha,
                operation: wgpu::BlendOperation::Add,
            },
        };
        let target = |blend: wgpu::BlendState| wgpu::ColorTargetState {
            format,
            blend: Some(blend),
            write_mask: wgpu::ColorWrites::ALL,
        };

        let spr_sm = device.create_shader_module(wgpu::include_wgsl!("shaders/sprite.wgsl"));
        let flat_sm = device.create_shader_module(wgpu::include_wgsl!("shaders/flat.wgsl"));
        let rr_sm = device.create_shader_module(wgpu::include_wgsl!("shaders/rrect.wgsl"));
        let font_sm = device.create_shader_module(wgpu::include_wgsl!("shaders/font.wgsl"));

        let spr_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("sprite-layout"),
            bind_group_layouts: &[Some(&tex_bgl)],
            immediate_size: 0,
        });
        let flat_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("flat-layout"),
            bind_group_layouts: &[Some(&plain_bgl)],
            immediate_size: 0,
        });
        let rr_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("rrect-layout"),
            bind_group_layouts: &[Some(&plain_bgl), Some(&u_bgl)],
            immediate_size: 0,
        });
        let font_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("font-layout"),
            bind_group_layouts: &[Some(&tex_bgl)],
            immediate_size: 0,
        });

        let make_pipe = |label: &str,
                         layout: &wgpu::PipelineLayout,
                         module: &wgpu::ShaderModule,
                         blend: wgpu::BlendState,
                         buffers: &[Option<VertexBufferLayout>]|
         -> RenderPipeline {
            device.create_render_pipeline(&RenderPipelineDescriptor {
                label: Some(label),
                layout: Some(layout),
                vertex: wgpu::VertexState {
                    module,
                    entry_point: Some("vs"),
                    buffers,
                    compilation_options: Default::default(),
                },
                primitive: wgpu::PrimitiveState {
                    topology: wgpu::PrimitiveTopology::TriangleList,
                    ..Default::default()
                },
                depth_stencil: None,
                multisample: wgpu::MultisampleState::default(),
                fragment: Some(wgpu::FragmentState {
                    module,
                    entry_point: Some("fs"),
                    targets: &[Some(target(blend))],
                    compilation_options: Default::default(),
                }),
                multiview_mask: None,
                cache: None,
            })
        };

        // Vertex attribute layouts (module-level statics: wgpu 30's
        // VertexBufferLayout borrows its attribute slice).
        static SPR_ATTRS: [VertexAttribute; 2] = [
            VertexAttribute {
                format: wgpu::VertexFormat::Float32x2,
                shader_location: 0,
                offset: 0,
            },
            VertexAttribute {
                format: wgpu::VertexFormat::Float32x2,
                shader_location: 1,
                offset: 8,
            },
        ];
        static FLAT_ATTRS: [VertexAttribute; 2] = [
            VertexAttribute {
                format: wgpu::VertexFormat::Float32x2,
                shader_location: 0,
                offset: 0,
            },
            VertexAttribute {
                format: wgpu::VertexFormat::Float32x4,
                shader_location: 1,
                offset: 8,
            },
        ];
        static RR_ATTRS: [VertexAttribute; 1] = [VertexAttribute {
            format: wgpu::VertexFormat::Float32x2,
            shader_location: 0,
            offset: 0,
        }];
        static FONT_ATTRS: [VertexAttribute; 3] = [
            VertexAttribute {
                format: wgpu::VertexFormat::Float32x2,
                shader_location: 0,
                offset: 0,
            },
            VertexAttribute {
                format: wgpu::VertexFormat::Float32x2,
                shader_location: 1,
                offset: 8,
            },
            VertexAttribute {
                format: wgpu::VertexFormat::Float32x4,
                shader_location: 2,
                offset: 16,
            },
        ];
        let vb = |stride: u64, attrs: &'static [VertexAttribute]| VertexBufferLayout {
            array_stride: stride,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: attrs,
        };

        let pipelines = vec![
            (
                Pipe::Sprite,
                make_pipe(
                    "sprite",
                    &spr_layout,
                    &spr_sm,
                    premult_over,
                    &[Some(vb(16, &SPR_ATTRS))],
                ),
                spr_bg,
            ),
            (
                Pipe::Flat,
                make_pipe(
                    "flat",
                    &flat_layout,
                    &flat_sm,
                    over,
                    &[Some(vb(24, &FLAT_ATTRS))],
                ),
                plain_bg.clone(),
            ),
            (
                Pipe::Rr,
                make_pipe(
                    "rrect",
                    &rr_layout,
                    &rr_sm,
                    premult_over,
                    &[Some(vb(8, &RR_ATTRS))],
                ),
                plain_bg,
            ),
            (
                Pipe::Font,
                make_pipe(
                    "font",
                    &font_layout,
                    &font_sm,
                    over,
                    &[Some(vb(32, &FONT_ATTRS))],
                ),
                font_bg,
            ),
        ];

        Self {
            device,
            queue,
            surface,
            config,
            globals_buf,
            uniform_buf,
            uniform_bg,
            pipelines,
            font_arena: Arena::new(256 * 1024),
            flat_arena: Arena::new(256 * 1024),
            spr_arena: Arena::new(4 * 1024),
            rr_arena: Arena::new(64 * 1024),
            draws: Vec::with_capacity(256),
            uniforms: Vec::with_capacity(64),
            cur_scissor: None,
            lw: 1.0,
            lh: 1.0,
            pw: 1,
            ph: 1,
        }
    }

    fn make_texture(
        device: &Device,
        queue: &Queue,
        (rgba, w, h): (&[u8], u32, u32),
    ) -> wgpu::Texture {
        assert_eq!(rgba.len(), (w * h * 4) as usize, "atlas size mismatch");
        let tex = device.create_texture(&TextureDescriptor {
            label: Some("atlas"),
            size: Extent3d {
                width: w,
                height: h,
                depth_or_array_layers: 1,
            },
            mip_level_count: 1,
            sample_count: 1,
            dimension: TextureDimension::D2,
            format: TextureFormat::Rgba8Unorm,
            usage: TextureUsages::TEXTURE_BINDING | TextureUsages::COPY_DST,
            view_formats: &[],
        });
        queue.write_texture(
            wgpu::TexelCopyTextureInfo {
                texture: &tex,
                mip_level: 0,
                origin: Origin3d::ZERO,
                aspect: TextureAspect::All,
            },
            rgba,
            wgpu::TexelCopyBufferLayout {
                offset: 0,
                bytes_per_row: Some(w * 4),
                rows_per_image: Some(h),
            },
            Extent3d {
                width: w,
                height: h,
                depth_or_array_layers: 1,
            },
        );
        tex
    }

    /// Logical points per framebuffer pixel (0.5 on a 2x Retina display).
    pub fn pts_per_px(&self) -> f32 {
        self.lw / self.pw.max(1) as f32
    }

    /// The current logical (point) size of the window.
    pub fn logical_size(&self) -> (f32, f32) {
        (self.lw, self.lh)
    }

    /// Reconfigure the surface if the pixel size changed.
    pub fn resize(&mut self, w: u32, h: u32) {
        if w == 0 || h == 0 {
            return;
        }
        if self.config.width != w || self.config.height != h {
            self.config.width = w;
            self.config.height = h;
            self.surface.configure(&self.device, &self.config);
        }
    }

    /// Start a frame: resets arenas and records the ortho projection.
    pub fn begin(&mut self, logical_w: f32, logical_h: f32, pixel_w: u32, pixel_h: u32) {
        self.lw = logical_w.max(1.0);
        self.lh = logical_h.max(1.0);
        self.pw = pixel_w.max(1);
        self.ph = pixel_h.max(1);
        self.resize(pixel_w, pixel_h);
        self.font_arena.cpu.clear();
        self.flat_arena.cpu.clear();
        self.spr_arena.cpu.clear();
        self.rr_arena.cpu.clear();
        self.draws.clear();
        self.uniforms.clear();
        self.cur_scissor = None;
        let ortho = m4_ortho(self.lw, self.lh);
        self.queue
            .write_buffer(&self.globals_buf, 0, bytemuck::bytes_of(&ortho));
    }

    fn uniform_slot(&mut self, u: RrUniforms) -> u16 {
        let slot = self.uniforms.len() as u16;
        assert!((slot as u64) < UNIFORM_SLOTS, "uniform slots exhausted");
        self.uniforms.push(u);
        slot
    }

    fn scissor_px(&self, r: Rect) -> [u32; 4] {
        // wgpu scissors are top-based physical pixels — no GL-style y flip.
        let scale = self.pw as f32 / self.lw;
        [
            (r.x * scale).max(0.0) as u32,
            (r.y * scale).max(0.0) as u32,
            (r.w * scale).max(1.0) as u32,
            (r.h * scale).max(1.0) as u32,
        ]
    }

    /// Clip subsequent draws to a logical-point rect (None = whole window).
    pub fn set_scissor(&mut self, r: Option<Rect>) {
        self.cur_scissor = r.map(|r| self.scissor_px(r));
    }

    /// The sprite: a unit quad transformed by `model` (on the CPU — the same
    /// approach as the font glyphs), uv-remapped into the atlas cell.
    pub fn draw_sprite(&mut self, model: Mat4, uv: [f32; 4]) {
        let corners = [(0.0f32, 0.0f32), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)];
        let mut px = [[0.0f32; 2]; 4];
        let mut uvs = [[0.0f32; 2]; 4];
        for i in 0..4 {
            let (x, y) = corners[i];
            px[i] = crate::math::transform2(&model, x, y);
            uvs[i] = [uv[0] + x * (uv[2] - uv[0]), uv[1] + y * (uv[3] - uv[1])];
        }
        let q = [
            SprVert {
                pos: px[0],
                uv: uvs[0],
            },
            SprVert {
                pos: px[1],
                uv: uvs[1],
            },
            SprVert {
                pos: px[2],
                uv: uvs[2],
            },
            SprVert {
                pos: px[0],
                uv: uvs[0],
            },
            SprVert {
                pos: px[2],
                uv: uvs[2],
            },
            SprVert {
                pos: px[3],
                uv: uvs[3],
            },
        ];
        let (first, count) = self.spr_arena.push(&q, 16);
        self.draws.push(DrawCmd {
            pipe: Pipe::Sprite,
            first,
            count,
            uniform: None,
            scissor: self.cur_scissor,
        });
    }

    /// Flat colored triangles (shadow, apple, sparks, confetti quads),
    /// transformed by `model` on the CPU.
    pub fn draw_flat(&mut self, verts: &[FlatVert], model: Mat4) {
        if verts.is_empty() {
            return;
        }
        let transformed: Vec<FlatVert> = verts
            .iter()
            .map(|v| FlatVert {
                pos: crate::math::transform2(&model, v.pos[0], v.pos[1]),
                col: v.col,
            })
            .collect();
        let (first, count) = self.flat_arena.push(&transformed, 24);
        self.draws.push(DrawCmd {
            pipe: Pipe::Flat,
            first,
            count,
            uniform: None,
            scissor: self.cur_scissor,
        });
    }

    /// Rounded rect: gradient from fill to fill2 when gaxis != 0, plus border.
    #[allow(clippy::too_many_arguments)]
    pub fn draw_rrect(
        &mut self,
        x: f32,
        y: f32,
        w: f32,
        h: f32,
        r: f32,
        fill: Vec4,
        fill2: Vec4,
        gaxis: f32,
        border: Vec4,
        bw: f32,
    ) {
        let pad = 2.0;
        let quad = [
            [x - pad, y - pad],
            [x + w + pad, y - pad],
            [x + w + pad, y + h + pad],
            [x - pad, y - pad],
            [x + w + pad, y + h + pad],
            [x - pad, y + h + pad],
        ];
        let verts: [RrVert; 6] = std::array::from_fn(|i| RrVert { pos: quad[i] });
        let (first, count) = self.rr_arena.push(&verts, 8);
        let u = RrUniforms {
            rect: [x, y, w, h],
            params: [r, bw, gaxis, 0.0],
            fill: [fill.r, fill.g, fill.b, fill.a],
            fill2: [fill2.r, fill2.g, fill2.b, fill2.a],
            border: [border.r, border.g, border.b, border.a],
        };
        let slot = self.uniform_slot(u);
        self.draws.push(DrawCmd {
            pipe: Pipe::Rr,
            first,
            count,
            uniform: Some(slot),
            scissor: self.cur_scissor,
        });
    }

    /// Atlas glyphs (vertices built by font::Font).
    pub fn draw_font(&mut self, verts: &[FontVert]) {
        if verts.is_empty() {
            return;
        }
        let (first, count) = self.font_arena.push(verts, 32);
        self.draws.push(DrawCmd {
            pipe: Pipe::Font,
            first,
            count,
            uniform: None,
            scissor: self.cur_scissor,
        });
    }

    /// Upload everything, replay the recorded draws in order, present.
    pub fn present(&mut self) {
        // wgpu 29: get_current_texture returns an enum covering every outcome
        // instead of Result<SurfaceTexture, SurfaceError>.
        let frame = match self.surface.get_current_texture() {
            wgpu::CurrentSurfaceTexture::Success(f) => f,
            wgpu::CurrentSurfaceTexture::Outdated
            | wgpu::CurrentSurfaceTexture::Lost
            | wgpu::CurrentSurfaceTexture::Suboptimal(_) => {
                self.resize(self.pw, self.ph);
                return;
            }
            // Timeout | Occluded | Validation: skip the frame.
            _ => return,
        };
        let view = frame
            .texture
            .create_view(&wgpu::TextureViewDescriptor::default());

        for (arena, label) in [
            (&mut self.font_arena, "font-verts"),
            (&mut self.flat_arena, "flat-verts"),
            (&mut self.spr_arena, "sprite-verts"),
            (&mut self.rr_arena, "rrect-verts"),
        ] {
            if !arena.cpu.is_empty() {
                // Clone the handle out of the mutable borrow so the CPU side
                // can be read in the same statement.
                let buf = arena.buffer(&self.device, label).clone();
                self.queue.write_buffer(&buf, 0, &arena.cpu);
            }
        }
        for (i, u) in self.uniforms.iter().enumerate() {
            self.queue.write_buffer(
                &self.uniform_buf,
                i as u64 * UNIFORM_SLOT,
                bytemuck::bytes_of(u),
            );
        }

        let mut enc = self
            .device
            .create_command_encoder(&CommandEncoderDescriptor {
                label: Some("frame"),
            });
        {
            let mut pass = enc.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some("pet"),
                color_attachments: &[Some(wgpu::RenderPassColorAttachment {
                    view: &view,
                    resolve_target: None,
                    ops: wgpu::Operations {
                        // Transparent desktop pet: clear to (0,0,0,0).
                        load: wgpu::LoadOp::Clear(wgpu::Color::TRANSPARENT),
                        store: wgpu::StoreOp::Store,
                    },
                    depth_slice: None,
                })],
                depth_stencil_attachment: None,
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            });

            let mut cur_pipe: Option<Pipe> = None;
            let mut cur_scissor: Option<[u32; 4]> = None;
            let full = [0u32, 0, self.config.width, self.config.height];
            for d in &self.draws {
                if cur_pipe != Some(d.pipe) {
                    for (pipe, pipeline, group0) in &self.pipelines {
                        if *pipe == d.pipe {
                            pass.set_pipeline(pipeline);
                            pass.set_bind_group(0, group0, &[]);
                            break;
                        }
                    }
                    cur_pipe = Some(d.pipe);
                }
                if let Some(slot) = d.uniform {
                    pass.set_bind_group(1, &self.uniform_bg, &[slot as u32 * 256]);
                }
                let want = d.scissor.unwrap_or(full);
                if cur_scissor != Some(want) {
                    // Clamp into the target (a mid-frame resize can leave the
                    // recorded scissor stale); a zero-size rect is valid and
                    // simply draws nothing.
                    let sx = want[0].min(full[2]);
                    let sy = want[1].min(full[3]);
                    let sw = want[2].min(full[2].saturating_sub(sx));
                    let sh = want[3].min(full[3].saturating_sub(sy));
                    pass.set_scissor_rect(sx, sy, sw, sh);
                    cur_scissor = Some(want);
                }
                let (buf, stride): (&Buffer, u64) = match d.pipe {
                    Pipe::Font => (self.font_arena.gpu.as_ref().unwrap(), 32),
                    Pipe::Flat => (self.flat_arena.gpu.as_ref().unwrap(), 24),
                    Pipe::Sprite => (self.spr_arena.gpu.as_ref().unwrap(), 16),
                    Pipe::Rr => (self.rr_arena.gpu.as_ref().unwrap(), 8),
                };
                pass.set_vertex_buffer(
                    0,
                    buf.slice(d.first as u64 * stride..(d.first + d.count) as u64 * stride),
                );
                pass.draw(0..d.count, 0..1);
            }
        }

        self.queue.submit(Some(enc.finish()));
        // wgpu 30: presenting moved from SurfaceTexture to Queue.
        self.queue.present(frame);
    }
}
