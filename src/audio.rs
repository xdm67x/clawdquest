//! Sound: short square-wave beeps. Failure-tolerant: if audio cannot open,
//! every call is a silent no-op.

use std::sync::{Arc, Mutex};

use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};

const RATE: u32 = 48000;

/// One scheduled beep, like fx.js beep(f, d, t).
#[derive(Clone, Copy)]
struct Beep {
    f: f32,
    d: f32,
    t0: f32,
}

#[derive(Default)]
struct AudioState {
    on: bool,
    /// seconds of audio generated so far
    t: f64,
    q: Vec<Beep>,
}

pub struct Audio {
    state: Arc<Mutex<AudioState>>,
    _stream: Option<cpal::Stream>,
}

impl Audio {
    pub fn new() -> Self {
        let state = Arc::new(Mutex::new(AudioState::default()));
        let stream = open_stream(&state);
        if stream.is_some() {
            state.lock().unwrap().on = true;
        }
        Self {
            state,
            _stream: stream,
        }
    }

    /// Seconds of audio generated so far (the beep scheduling clock).
    fn beep(&self, f: f32, d: f32, t: f32) {
        let mut s = self.state.lock().unwrap();
        if !s.on || s.q.len() >= 64 {
            return;
        }
        let t0 = s.t as f32 + t;
        s.q.push(Beep { f, d, t0 });
    }

    pub fn sfx_hit(&self) {
        self.beep(180.0, 0.10, 0.0);
        self.beep(320.0, 0.12, 0.06);
    }

    pub fn sfx_win(&self) {
        const F: [f32; 4] = [523.0, 659.0, 784.0, 1046.0];
        for (i, &f) in F.iter().enumerate() {
            self.beep(f, 0.15, 0.09 * i as f32);
        }
    }

    pub fn sfx_add(&self) {
        self.beep(440.0, 0.06, 0.0);
    }

    pub fn sfx_pet(&self) {
        self.beep(660.0, 0.05, 0.0);
        self.beep(880.0, 0.06, 0.05);
    }
}

/// Open the default playback device as an F32 mono stream. The callback
/// synthesizes the scheduled beeps (square wave, exponential decay).
fn open_stream(state: &Arc<Mutex<AudioState>>) -> Option<cpal::Stream> {
    let host = cpal::default_host();
    let device = host.default_output_device()?;
    // Ask for F32 mono at 48k (CoreAudio's native format); if the device's
    // default config is not F32, just run silent rather than convert.
    let supported = device.default_output_config().ok()?;
    if supported.sample_format() != cpal::SampleFormat::F32 {
        return None;
    }
    let config = cpal::StreamConfig {
        channels: 1,
        sample_rate: RATE,
        buffer_size: cpal::BufferSize::Default,
    };
    let rate = config.sample_rate;
    let state2 = state.clone();
    // cpal 0.18: the typed build_output_stream hands the callback the f32
    // samples directly.
    let stream = device
        .build_output_stream(
            config,
            move |samples: &mut [f32], _: &cpal::OutputCallbackInfo| {
                let mut s = state2.lock().unwrap();
                let mut buf = [0.0f32; 4800];
                let n = samples.len().min(4800);
                // Synthesize mono into the scratch buffer, frame-indexed.
                for b in &s.q {
                    let start = b.t0 - s.t as f32; // seconds from buffer start
                    let mut i0 = (start * rate as f32) as i32;
                    let mut i1 = ((start + b.d) * rate as f32) as i32;
                    if i1 < 0 {
                        continue;
                    }
                    i0 = i0.max(0);
                    if i1 > n as i32 {
                        i1 = n as i32;
                    }
                    for i in i0..i1 {
                        let t = i as f64 / rate as f64 - start as f64;
                        if t < 0.0 || t > b.d as f64 {
                            continue;
                        }
                        let phase = (t * b.f as f64).fract();
                        let sq = if phase < 0.5 { 1.0 } else { -1.0 };
                        // Exponential decay 0.07 -> 0.001 across the beep.
                        let g = 0.07 * (0.001f64 / 0.07).powf(t / b.d as f64);
                        buf[i as usize] += sq as f32 * g as f32;
                    }
                }
                s.t += n as f64 / rate as f64;
                // Retire beeps that ended before the buffer start.
                let t = s.t as f32;
                s.q.retain(|b| b.t0 + b.d > t);
                samples.copy_from_slice(&buf[..samples.len()]);
            },
            |err| eprintln!("audio error: {err}"),
            None,
        )
        .ok()?;
    stream.play().ok()?;
    Some(stream)
}
