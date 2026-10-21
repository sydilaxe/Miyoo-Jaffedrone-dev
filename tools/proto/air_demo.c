#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
extern float air_k[3];
#define R 48000
#define SEC 7
static int16_t out[R * 2 * 40];
static int pos;
static void gap(void){ memset(out+2*pos,0,R*2); pos+=R/2; }
static void wav(const char *path){
  FILE *f=fopen(path,"wb"); int bytes=pos*4; int hdr[11]={0x46464952,36+bytes,0x45564157,0x20746d66,16,0x00020001,R,R*4,0x00100004,0x61746164,bytes};
  fwrite(hdr,4,11,f); fwrite(out,4,pos,f); fclose(f); pos=0;}
typedef void (*Setup)(void);
static void run(Setup su, float air, float room, int secs, int mute0_at){
  su(); engine_air=air; engine_room=room; engine_init(R); engine_set_playing(1);
  engine_render(out+2*pos,R); /* settle: rendered over, then the take */
  for(int i=0;i<secs;i++){ if(i==mute0_at) song.on[0]=0; engine_render(out+2*pos,R); pos+=R; }
  gap();
}
static void two_strings(void){ song_default(); for(int s=0;s<NS;s++){song.on[s]=s<2; song.sp[s][SP_VOICE]=0; song.sp[s][SP_NOTE]=song.sp[0][SP_NOTE]; song.sp[s][SP_PAN]=s?20:-20;} song.sp[1][SP_FINE]+=6; song.gp[GP_CHANGES]=0; song.fx[FX_SYMP]=0; }
static void two_reeds(void){ two_strings(); song.sp[0][SP_VOICE]=song.sp[1][SP_VOICE]=1+EX_REED; }
static void flute_gong(void){ song_default(); for(int s=0;s<NS;s++) song.on[s]=0; song.on[0]=song.on[1]=1; song.sp[0][SP_VOICE]=1+EX_FLUTE; song.sp[1][SP_VOICE]=VOICE_PERC(PC_GONG); song.sp[1][SP_BOW]=0; song.sp[1][SP_DECAY]=100; song.sp[1][SP_PON]=song.sp[1][SP_BON]=0; song.gp[GP_CHANGES]=0; song.fx[FX_SYMP]=0; }
static void ensemble(void){ song_default(); song.sp[0][SP_VOICE]=0; song.sp[1][SP_VOICE]=1+EX_FLUTE; song.sp[2][SP_VOICE]=VOICE_PERC(PC_BOWL); song.sp[3][SP_VOICE]=1+EX_REED; song.sp[4][SP_VOICE]=VOICE_PERC(PC_GONG); }
int main(int argc,char**argv){
  const char *d=argv[1]; char p[256];
  air_k[0]=atof(argv[2]); air_k[1]=atof(argv[3]); air_k[2]=atof(argv[4]);
  run(two_strings,0,0,SEC,-1); run(two_strings,0.5f,0,SEC,-1); run(two_strings,1,0,SEC,-1); snprintf(p,256,"%s/1-two-strings_off-half-full.wav",d); wav(p);
  run(two_reeds,0,0,SEC,-1); run(two_reeds,0.5f,0,SEC,-1); run(two_reeds,1,0,SEC,-1); snprintf(p,256,"%s/2-two-reeds_off-half-full.wav",d); wav(p);
  run(flute_gong,0,0,10,6); run(flute_gong,1,0,10,6); snprintf(p,256,"%s/3-flute-and-silent-gong_off-on.wav",d); wav(p);
  run(ensemble,0,0,10,-1); run(ensemble,0.6f,0.5f,10,-1); run(ensemble,1,1,10,-1); snprintf(p,256,"%s/4-ensemble_off-medium-full.wav",d); wav(p);
  return 0;
}
