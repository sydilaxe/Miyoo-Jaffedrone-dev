# Jaffedrone

A five-string drone instrument for the Miyoo Mini Plus running OnionOS. It joins an organic string drone
to an algorithmic progression. You can play it as an instrument, or let it run as a slowly changing drone
for meditation.

## The strings

Each string is a Karplus-Strong loop with the Jaffe-Smith extensions:

- an allpass for fine tuning;
- a damping filter for brightness;
- a loop gain for decay.

A noise "bow" keeps the string ringing, held in check by the loop's own level. Per string:

- **FOLD:** a wavefolder on the string's output.
- **STIFF:** dispersion, for an inharmonic, bell-like tone.
- **POS:** the pick position.
- **BUZZ:** a jawari-like bridge. A strong note buzzes, and the buzz blooms and fades as the note decays, as
  on a tanpura or sitar.

Shared by all five strings (FX page, BODY):

- **COUPLE:** sympathetic resonance. The strings share a bridge, so each one hears the others, and an
  untouched string tuned to a harmonic starts to ring.
- **BODY:** five resonant modes under the strings. Choose a body type:
  - **WOOD:** a wooden box.
  - **GOURD:** a gourd, like a tanpura's.
  - **BRONZE:** rings inharmonically, like a gong.
  - **HARMONIC:** resonates on the overtones.

  BRONZE and HARMONIC follow the root note.

## Rhythm: two rings per string

Each string has two Euclidean rings (steps, pulses, rotation) that share one step length, from 1/16 to two bars:

- **The pluck ring** plucks the string on its pulses. The first pulse of each cycle is a little stronger.
- **The bow ring** swells the bow in and out on its pulses, so the string breathes. With the bow ring off, the
  bow plays continuously.

Each ring can be switched on and off. The rings are meant to be simple and slow, and each string keeps its own.
You can always pluck or bow a string by hand on top of the rings, and mute any string from the PLAY page.

## Tunings and progressions

`SCALE` (DRONE page, or R2 + left/right on PLAY) sets the tuning. Each string's `NOTE` counts degrees of the
scale above the root.

| Scale | Tuning | What SELECT and CHANGES do |
|---|---|---|
| EQUAL | 12-tone equal temperament | chords from the Dorian mode |
| JUST MAJOR, JUST MINOR | 5-limit just intonation | chords stacked from the scale |
| HARMONICS | the 8th to 15th overtones | the same; sevenths are pure 4:5:6:7 |
| PENTA JUST, SEPTIMAL | just pentatonics (5-limit, and 7-limit with 7/6 and 7/4) | the same |
| YAMAN, BHAIRAV, KAFI, BHUPALI, MALKAUNS | ragas in just (shruti) ratios | a tanpura below (Sa and Pa, or Ma for Malkauns); the upper strings wander a step or two at a time |
| SLENDRO, PELOG, SELISIR | Javanese and Balinese gamelan tunings | a gong tone, a kempyung pair and a beating unison pair (ombak) move in parallel |

How the chord progressions move:

- **Root motion** is mostly by fourths and fifths, sometimes by thirds and steps.
- **Chord types:** triads, sevenths and suspensions.
- **Going home:** the further the progression wanders, the likelier it returns to the tonic.
- **Voice leading:** each string moves to the nearest chord tone in its own register. Common tones stay, and
  the bass sometimes holds as a pedal.

Note names are shown in each scale's own language:

- Western scales: note names with the cents offset from equal temperament, for example `C#3-14`.
- Ragas: sargam (`Sa Re Ga MA Pa`). Lowercase is komal, `MA` is tivra, `'` is the octave above and `,` the
  octave below.
- Gamelan: `ji ro lu`… for Javanese and `ding dong deng dung dang` for Balinese.

Changing the scale moves each string to its nearest degree in the new tuning.

- **SELECT:** the next chord.
- **CHANGES:** chords move on their own every 1 to 16 bars (8 by default).

The strings are not re-plucked on a change. They keep ringing and slide to the new notes over the GLIDE time.

## Pages

- **PLAY:** the five strings, each with its pluck ring and bow ring drawn as polygons. The steps are the
  corners, and the pulses are joined into an inner shape that lights on each pluck and fills as the bow swells.
  The band at the top shows the scale, the current chord and the bars until the next change.
- **The quiet view** (tap L2 on PLAY): only the five strings and their rings, for meditation.
- **STRINGS:** the sound of each string.
- **DRONE:** voicing, scale, root, changes, tempo, drift, glide, cutoff, master and Wi-Fi sync.
- **MOD:** two LFOs.
- **FX:** delay, reverb, gate and body.

## Controls

**PLAY**

| Button | Action |
|---|---|
| Left / right | choose a string |
| Up / down | choose the pluck ring / the bow ring; press again to switch that ring on or off |
| X / Y | add / remove a pulse |
| A (hold) | bow the string by hand |
| B | pluck the string by hand |
| L2 + X / Y | more / fewer steps |
| L2 + left / right | rotate the pattern |
| L2 + up / down | step length longer / shorter |
| L2 + A | mute / unmute the string (it fades out and in; a red bar marks it) |
| L2 + B | a new simple pattern |
| tap L2 | the quiet view |
| R2 + left / right | previous / next scale |
| R2 + up / down | tone (cutoff) |
| R2 + X / Y | coupling more / less |
| R2 + A / B | body more / less |

**Everywhere**

| Button | Action |
|---|---|
| SELECT | next chord |
| START | start / stop the drone |
| L1 / R1 | previous / next page |
| MENU | exit |

**On the edit pages:** up/down chooses a parameter and Y/X adjusts it. On STRINGS, left/right chooses a
string, A turns it on or off, and B plucks it.

Songs from earlier versions load as they sounded:

- the scale is set to EQUAL;
- each old PLUCK setting becomes a one-pulse pluck ring with the same timing;
- COUPLE, BODY and CHANGES are off.

## Build

- `make` cross-compiles `dist/Jaffedrone/jaffedrone` with zig against Onion's own SDL 1.2.
- `make test` renders a WAV and screenshots into `build/test` on the host, and checks:
  - tuning, including just intervals;
  - glide;
  - loading old saves;
  - each tone shaper;
  - coupling and the bodies;
  - the rings;
  - each scale's progression;
  - the gate;
  - Wi-Fi sync;
  - DSP load.
- `make install SD=/path/to/card` copies the app into `App/`.
