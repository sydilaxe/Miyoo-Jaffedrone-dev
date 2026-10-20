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
- **HARM:** a light touch at a node, like a harmonic on a guitar or a flageolet on a violin. Touching at a half,
  a third, a quarter or a fifth of the string brings out the 2nd, 3rd, 4th or 5th harmonic, which blooms out
  of the note with a glassy tone.

Shared by all five strings (SOUND page, bottom rows):

- **COUPLE:** sympathetic resonance. The strings share a bridge, so each one hears the others, and an
  untouched string tuned to a harmonic starts to ring.
- **BODY:** five resonant modes under the strings. Choose a body type:
  - **WOOD:** a wooden box.
  - **GOURD:** a gourd, like a tanpura's.
  - **BRONZE:** rings inharmonically, like a gong.
  - **HARMONIC:** resonates on the overtones.

  BRONZE and HARMONIC follow the root note.
- **TONE:** the master lowpass.

## Wind

ENGINE (top of the SOUND page) switches every voice from strings to **WIND**. Everything else stays: the
five voices, their rings, the progressions, the sympathetic strings, the mix and the effects.

A wind voice is a bore: a waveguide closed by a loss filter, driven at the mouthpiece by an **EXCITER**:

| Exciter | Like | How it sounds |
|---|---|---|
| LIP | brass, conch, didgeridoo | the lips buzz near the note and let breath through as they open |
| REED | clarinet | a single vibrating reed on a cylindrical bore: hollow, odd harmonics |
| DOUBLE | oboe, shehnai, duduk | a stiffer double reed on a full bore: reedy, nasal, all harmonics |
| FLUTE | concert flute | an air jet blown across an edge |
| FREE | sheng, harmonium | a free reed sounding at its own pitch into a short pipe |
| RECORDER | recorder | a duct-guided jet: steady and sweet, with a chiff on each attack |
| BANSURI | Indian bamboo flute | a longer, breathier transverse jet |

**MATERIAL** sets the walls' losses, a little wall dispersion and the body's resonances:

- **WOOD:** warm and soft.
- **METAL:** bright, with a ringing body tuned to the root.
- **CERAMIC:** an earthen hollow, like an ocarina.
- **GLASS:** pure and sparkling, with high, long resonances.

In WIND the voice controls take their wind meanings:

| Strings | Wind | |
|---|---|---|
| BOW | BREATH | steady blowing pressure (0 = only tongued notes and the hand) |
| DECAY | AIR | breath noise |
| STIFF | EMBOUCH | lip tension, reed opening or jet length: the edge of the tone |
| POS | VIBRATO | breath vibrato, a little different on each voice |
| BUZZ | GROWL | a rasp in the breath |
| HARM | REG | the register: 1st to 5th |

The pluck ring becomes **TONGUE**: each pulse is a tongued attack, a push of breath with a chiff. The bow ring
becomes **BREATH**: it swells the blowing. Flutes, recorders and bansuris sound an octave higher than the
strings, as flutes do. Each voice is checked by the test harness: every exciter in every material plays in
tune within a few cents over three octaves. The lip voice listens to itself and lips its pitch onto the note,
as a player does.

## Undertones

SUB (MIX page) gives every voice, in both engines, undertones that ring beneath it. Every n-th cycle of the
voice strikes a resonance n times lower, so the undertone follows the voice's pitch and dynamics.

- **SUB:** how loud.
- **SUB DIV:** an octave below (OCT), a twelfth below (12TH), two octaves below (2 OCT), or the octave and the
  twelfth together (OCT+12).
- **SUB RES:** how long the undertones ring, from a short thump to a long resonant hum.

## Sympathetic strings

Up to 12 extra strings that are never played. Like a sitar's taraf, they ring only when the main strings
sound their pitches through the bridge, adding a halo around the drone. On the SOUND page:

- **SYMP:** how many (0 turns them off).
- **S TUNE:** how they are tuned. In the two chord modes they retune by themselves at every chord change.
  - **SCALE:** the degrees of the scale, rising.
  - **CHORD:** the notes of the current chord.
  - **CHORD+:** the chord plus the scale tone above each chord note (9ths, 11ths, 13ths).
  - **DRONE:** root and fifth, like a tanpura.
  - **FIFTHS:** a stack of fifths from the root.
- **S OCT:** their octave, from one below the root to two above.
- **S LEVEL** and **S RING:** how loud they are, and how long they ring.

Each tuning repeats within two octaves. A second pass over the same pitches is detuned a few cents, so doubled
strings shimmer against each other.

## Rhythm: two rings per string

Each string has two Euclidean rings (steps, pulses, rotation) that share one step length, from 1/16 to two bars:

- **The pluck ring** plucks the string on its pulses. The first pulse of each cycle is a little stronger.
- **The bow ring** swells the bow in and out on its pulses, so the string breathes. With the bow ring off, the
  bow plays continuously.

Each ring can be switched on and off and has a **CHANCE**: how often each pulse actually plays. Below 100%
the patterns breathe instead of looping exactly. The rings are meant to be simple and slow, and each string
keeps its own.

Each string also has a **GATE** (MIX page): how far its sound follows its own bow ring. At 0 the bow ring
only shapes the bowing; at 100 it gates the string's level as well. This replaces the old master gate.
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

How the chord progressions move, and the COMPOSE controls that shape them:

- **Root motion** is mostly by fourths and fifths. **MOTION** sets the rest: low favours thirds, which keep two
  notes in common, so changes are calm; high favours steps and distant moves. For ragas it sets how far a
  voice may wander; for gamelan, how far the block moves.
- **Chord types:** triads, sevenths and suspensions. **COLOR** sets how often sevenths and suspensions
  appear; for gamelan, how widely the beating pair is tuned apart.
- **Going home:** the further the progression wanders, the likelier it returns to the tonic. **HOME** sets
  how strong that pull is; for ragas it draws voices back to Sa and Pa.
- **Voice leading:** each string moves to the nearest chord tone in its own register. Common tones stay, and
  the bass sometimes holds as a pedal.
- **EVOLVE:** the chance that a chord change also nudges one string's rhythm: a ring rotates, gains or loses
  a pulse or a step, or a bow ring starts or stops breathing.
- **CHANCE:** the chance that a scheduled change (CHANGES) actually happens, so the harmony sometimes stays
  longer.
- **RELATE:** how many of the notes now sounding the next chord must keep: FREE, 1 TONE or 2 TONES. With 1
  or 2, every change grows out of the one before. For ragas, RELATE holds that many upper voices still; for
  gamelan, the block moves by kempyung so one tone stays.

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
- **SOUND:** tone and sound shaping. The engine, exciter and material at the top, then each string's note,
  harmonic touch, bow, decay, brightness, fold,
  stiffness, pick position and buzz, then coupling, body, tone and the sympathetic strings.
- **COMPOSE:** the algorithmic side. On the left, the scale is drawn as a polygon, each degree at its true
  angle in the octave. The current chord is a shape inside it, the sympathetic strings are dots outside, and
  below are the chord, the chords before it and the bars until the next change. On the right: scale, root,
  voicing, changes, CHANCE, glide, tempo, MOTION, HOME, COLOR, EVOLVE, RELATE, drift, Wi-Fi sync and the
  two LFOs.
- **MIX:** each string's level, pan and gate with a meter, master, the sympathetic strings' level, the
  undertones (SUB, SUB DIV, SUB RES), then delay,
  reverb and binaural.

## Binaural

Binaural is built into the strings, not added as a separate tone. The sound of the strings is shifted a few
hertz down in the left ear and up in the right, so every overtone of every string, and the sympathetic
strings, beats between the ears at the same rate. Listen on headphones.

- **BEAT:** the beat rate, 0.5–12 Hz. The display names the band it falls in (DELTA below 4 Hz, THETA
  4–8 Hz, ALPHA above). OFF by default.
- **DEPTH:** how much of the shifted sound is used.
- **LOCK:** TEMPO puts the beat on the nearest power-of-two multiple of the tempo, so it pulses with the
  rhythm.

## Sound quality

- Each string reads its delay line with four-point Hermite interpolation. Strings keep their brightness on
  every pass instead of being dulled by linear interpolation, and decay evenly across the range.
- The wavefolder is anti-aliased: it averages the fold over each sample from its antiderivative.
- The reverb is an eight-line feedback delay network with damping in every line and slow modulation, for a
  dense tail that doesn't ring. TIME runs from 0.5 to 10 seconds.
- A 3 ms look-ahead leveller keeps peaks clean, and the output is rounded to 16 bits with triangular
  dither, so quiet tails fade smoothly.
- Denormal numbers are flushed to zero in the audio thread, so long tails stay cheap on the ARM chip.

## Controls

**PLAY**

The rings use the same buttons as HI5's pattern cells and RadioScan's rhythms. The strings are played by hand
on the shoulder buttons.

| Button | Action |
|---|---|
| Left / right | choose a string |
| Up / down | choose the pluck ring / the bow ring |
| A | switch the ring on / off |
| B | fewer pulses, wrapping back to all |
| X | rotate the pattern |
| Y | a new simple pattern |
| L1 | pluck the selected string by hand (WIND: tongue a note) |
| R1 (hold) | bow the selected string by hand: it swells in and sings steadily for as long as R1 is held (WIND: blow) |
| hold L1, then R1 | bow with a harmonic touch: the next harmonic up sings (WIND: overblow, an octave up, a twelfth for REED) |
| L2 + up / down | more / fewer steps |
| L2 + left / right | step length (right = faster) |
| L2 + X | more pulses |
| L2 + Y | rotate back |
| L2 + A | mute / unmute the string (it fades out and in; a red bar marks it) |
| tap L2 | the quiet view |
| R2 + left / right | previous / next scale |
| R2 + up / down | tone (cutoff) |
| R2 + X / Y | the selected ring's chance more / less (10% steps) |
| R2 + A / B | body more / less |

**Everywhere**

| Button | Action |
|---|---|
| SELECT | next chord |
| START (tap) | start / stop the drone |
| START + L1 / R1 | previous / next page (on SOUND, COMPOSE and MIX, L1 / R1 alone also work) |
| MENU | exit |

**On SOUND, COMPOSE and MIX:** the d-pad moves to the nearest control in that direction, and Y / X lowers or
raises it. On a string's control, A mutes or unmutes that string and B plucks it. Elsewhere, B plucks the
string selected on PLAY. Hand playing only ever plays the selected string; the others keep to their rings.

Songs from earlier versions load as they sounded:

- the scale is set to EQUAL;
- each old PLUCK setting becomes a one-pulse pluck ring with the same timing;
- COUPLE, BODY, CHANGES, EVOLVE, RELATE, binaural and the sympathetic strings are off, and every pulse plays.
- The old master gate is gone, so songs that used it play ungated.

## Log

Jaffedrone keeps a log for tuning the engine, following how it is played, and spotting audio problems:
`jaffedrone_log.txt` in the app folder (it rotates to `jaffedrone_log.txt.1` past 2 MB). **START +
SELECT** cycles it OFF, BASIC and DETAIL; LOG shows in the header while it runs, and the choice is kept in
`jaffedrone.cfg`. It starts at BASIC.

- **Events:** every message shown, parameter changes (repeats while a button is held are merged into one
  line), page changes, chord changes with their scale, and the audio setup at start. DETAIL adds every press
  (with L2/R2 held) and hand playing: plucks, tongued notes, bowing or blowing, with how long.
- **Status:** every 10 s (every second in DETAIL), for example:
  `STAT load avg 4% peak 9% late 0 | peak 0.71 rms 0.18 clips 0 | leveller avg 3.1 min 1.9 | sync lead`

  - **load:** how much of each audio buffer's time rendering took.
  - **late:** callbacks that came after a gap of more than one and a half buffers. These are crackles.
  - **peak**, **rms** and **clips:** the output level, and samples near full scale.
  - **leveller:** how much the leveller lifts the sound.
- **Voices** (DETAIL): each voice's wanted and measured pitch in hertz and cents, its meter, its breath or
  bow, and the lip voice's tuning correction. The pitch is measured from the voice's own sound by the same
  method the test harness uses.
- **Hints:** plain suggestions when something looks wrong: a high load or late callbacks (raise `JD_BUFFER`,
  or use fewer sympathetic strings), clipping (lower MASTER), a leveller lifting at full (raise LEVEL or
  BREATH), or a voice off its note (try another REG, BREATH or EMBOUCH).
- **Summary** at exit: presses per button, time on each page, the five most adjusted parameters, and how
  much was played by hand.

The audio thread only fills counters; all writing happens in the main thread.

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
  - binaural beats, harmonic touch, chance, RELATE and the string gates;
  - Wi-Fi sync;
  - DSP load.
- `make install SD=/path/to/card` copies the app into `App/`.
