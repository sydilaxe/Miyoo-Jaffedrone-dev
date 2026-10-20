Jaffedrone test build for the Miyoo Mini Plus (OnionOS).

**Install:** unzip `Jaffedrone-miyoo.zip` at the root of the SD card (it contains `App/Jaffedrone/`),
then open **Apps → Jaffedrone**.

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

See the README for the controls.
