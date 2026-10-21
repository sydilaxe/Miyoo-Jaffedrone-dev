Jaffedrone test build for the Miyoo Mini Plus (OnionOS).

**Install:** unzip `Jaffedrone-miyoo.zip` at the root of the SD card (it contains `App/Jaffedrone/`),
then open **Apps → Jaffedrone**.

**New in 0.10**
- Struck voices: nine physically modelled bodies for any voice: GENDER, BONANG and GONG (gamelan, with
  ombak), BOWL, HANDPAN, STEELPAN, TONGUE, CYMBAL and DRUM. Struck by a mallet that bounces, or bowed by
  stick-slip friction until they sing. R2 + Y / X on VOICE jumps between strings, winds and struck voices.

**New in 0.9.1**
- Overblows and harmonics with both shoulders in either order: bowing with R1, add L1 (also in LATCH,
  where B held does the same).

**New in 0.9**
- Tap L1 to strike a string like a hammered dulcimer (a wind voice: a hard overblown attack); hold it
  briefly to pluck.
- LATCH: hold B for the next harmonic of a droning string; Y / X set each string's bow attack.

**New in 0.8.2**
- LATCH: a long RELEASE now dies away properly (the leveller no longer lifts a fading string back up).
- Richer projector beams: a fan of rays lit by the spectrum, with drifting dust and a glow where they land.

**New in 0.8.1**
- Fixes for clicks and dropouts: the audio thread runs at real-time priority, the screen always pauses
  between frames and draws the projection a row at a time (much lighter), and changing a voice fades the
  old one out instead of cutting it. The log shows the screen's frame time and the audio priority.

**New in 0.8**
- LATCH play mode (START + A): every string is silent until you pluck (L1) or bow (R1) it, then drones
  until you play it again, and fades over its own RELEASE (0.05 to 20 s; up and down on PLAY).

**New in 0.7**
- A new wordmark, after a drawing and the Jaffe house, and a start-up screen on the house's roofline.
- A projection behind the screens: the spectrum as siding boards with roof-valley tops, and binaural as
  projector beams trading left and right (START + Y: SOFT, BRIGHT, OFF).
- Balinese gong kebyar tunings BEGBEG, SEDANG and TIRUS, and binaural MODE OMBAK: the gamelan's paired
  beating, for both ears and the speaker.
- Faster DSP (about 15% less work), and the single reed no longer jumps to its upper register at the attack.

**New in 0.6**
- Each voice can be a string or one of seven winds (VOICE row on the SOUND page).
- Louder undertones, blown wind attacks, binaural from 0.1 to 40 Hz with journeys, even loudness.

If you hear crackles, set `JD_BUFFER=2048` in `App/Jaffedrone/launch.sh`. The log is written to
`App/Jaffedrone/jaffedrone_log.txt` (START + SELECT cycles OFF / BASIC / DETAIL).

The manual, `App/Jaffedrone/Jaffedrone-Manual.pdf`, shows every control on a drawing of the Miyoo.
