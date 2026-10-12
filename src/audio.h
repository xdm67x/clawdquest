// ---------------------------------------------------------------------------
// Sound: short square-wave beeps (port of the original WebAudio sfx).
// Failure-tolerant: if audio cannot open, every call is a silent no-op.
// ---------------------------------------------------------------------------
#pragma once

void audio_init(void);
void sfx_hit(void); // low thump on quest complete
void sfx_win(void); // rising arpeggio on level-up / board clear
void sfx_add(void); // blip on quest added
