/* AIR ensembles: players play for 14 s, then stop being played (no bow, breath or strikes) and everything rings on
 * for 5 s. Listeners are never bowed, struck or blown. Delay and reverb off.
 * Each file: AIR off, half, full. usage: air_requested OUTDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
extern float air_k[3], air_send[3];
#define R 48000
static int16_t out[R * 2 * 70];
static int pos;
static void wav(const char *path){
  FILE *f=fopen(path,"wb"); int bytes=pos*4; int hdr[11]={0x46464952,36+bytes,0x45564157,0x20746d66,16,0x00020001,R,R*4,0x00100004,0x61746164,bytes};
  fwrite(hdr,4,11,f); fwrite(out,4,pos,f); fclose(f); pos=0;}
static void take(const int *v, const int *play, float air){
  song_default();
  song.fx[FX_DMIX]=0; song.fx[FX_VSEND]=0;
  for(int s=0;s<NS;s++){ song.sp[s][SP_VOICE]=v[s];
    if(!play[s]){ song.sp[s][SP_BOW]=0; song.sp[s][SP_PON]=song.sp[s][SP_BON]=0; song.sp[s][SP_DECAY]=100; } }
  engine_air=air; engine_room=0; engine_init(R); engine_set_playing(1);
  for(int i=0;i<19;i++){ if(i==14) for(int s=0;s<NS;s++) if(play[s]){ song.sp[s][SP_BOW]=0; song.sp[s][SP_PON]=song.sp[s][SP_BON]=0; } engine_render(out+2*pos,R); pos+=R; }
  memset(out+2*pos,0,R*2); pos+=R/2;
}
static void ens(const char *dir, const char *name, const int *v, const int *play){
  char p[256]; take(v,play,0); take(v,play,0.5f); take(v,play,1);
  snprintf(p,sizeof p,"%s/%s_off-half-full.wav",dir,name); wav(p);
}
#define S(p) VOICE_PERC(PC_##p)
#define W(e) (1 + EX_##e)
int main(int argc,char**argv){
  air_k[0]=1; air_k[1]=2; air_k[2]=2; air_send[2]=0.3f;
  const char *d=argv[1];
  { int v[NS]={W(FLUTE),W(FLUTE),S(GONG),S(GONG),S(GONG)}, p[NS]={1,1,0,0,0}; ens(d,"1-two-flutes_three-gongs-listening",v,p); }
  { int v[NS]={S(GONG),S(GONG),S(GONG),S(GONG),S(GONG)}, p[NS]={1,1,1,1,1}; ens(d,"2-five-gongs-together",v,p); }
  { int v[NS]={W(FLUTE),W(FLUTE),W(FLUTE),W(FLUTE),S(GONG)}, p[NS]={1,1,1,1,0}; ens(d,"3-four-flutes-in-harmony_one-gong-listening",v,p); }
  { int v[NS]={W(FREE),W(FREE),S(BOWL),S(BOWL),S(BOWL)}, p[NS]={1,1,0,0,0}; ens(d,"4-two-free-reeds_three-bowls-listening",v,p); }
  return 0;
}
