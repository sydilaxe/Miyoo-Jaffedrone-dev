Jaffedrone test build for the Miyoo Mini Plus (OnionOS).

**Install:** unzip `Jaffedrone-miyoo.zip` at the root of the SD card (it contains `App/Jaffedrone/`),
then open **Apps → Jaffedrone**.

**New in 0.6**
- Each voice can be a string or one of seven winds (VOICE row on the SOUND page).
- Louder undertones (SUB), saturated a little so they carry on the speaker.
- Tongued wind notes sound blown: an air stop, a push of breath and noise, a lip buzz.
- Binaural beats from 0.1 to 40 Hz, with 10- and 15-minute journeys down to theta and delta.
- Even loudness across settings: a slow leveller and a peak limiter.

If you hear crackles, set `JD_BUFFER=2048` in `App/Jaffedrone/launch.sh`. The log is written to
`App/Jaffedrone/jaffedrone_log.txt` (START + SELECT cycles OFF / BASIC / DETAIL).

See the README for the controls.
