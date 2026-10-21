/* AIR pairings: an instrument playing beside struck or string bodies that are
 * never struck or bowed, tuned to its note (a second gong a fifth up); it stops after 6 s and the bodies ring on alone.
 * Each file: AIR off, then on. usage: air_pairs OUTDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
extern float air_k[3], air_send[3];
#define R 48000
static int16_t out[R * 2 * 30];
static int pos;
static void wav(const char *path){
  FILE *f=fopen(path,"wb"); int bytes=pos*4; int hdr[11]={0x46464952,36+bytes,0x45564157,0x20746d66,16,0x00020001,R,R*4,0x00100004,0x61746164,bytes};
  fwrite(hdr,4,11,f); fwrite(out,4,pos,f); fclose(f); pos=0;}
/* voice 0 plays; the listed bodies listen (no strikes, no bow) at the given pans */
static void take(int player, const int *bodies, const int *pans, int nb, float air){
  song_default(); for(int s=0;s<NS;s++) song.on[s]=0;
  song.on[0]=1; song.sp[0][SP_VOICE]=player; song.sp[0][SP_PAN]=0;
  for(int b=0;b<nb;b++){ int s=b+1; song.on[s]=1; song.sp[s][SP_VOICE]=bodies[b]; song.sp[s][SP_PAN]=pans[b];
    song.sp[s][SP_NOTE]=song.sp[0][SP_NOTE]+(b==1?7:0); song.sp[s][SP_BOW]=0; song.sp[s][SP_DECAY]=100; song.sp[s][SP_PON]=song.sp[s][SP_BON]=0; }
  song.gp[GP_CHANGES]=0; song.fx[FX_SYMP]=0;
  engine_air=air; engine_room=0; engine_init(R); engine_set_playing(1);
  for(int i=0;i<10;i++){ if(i==6) song.on[0]=0; engine_render(out+2*pos,R); pos+=R; }
  memset(out+2*pos,0,R*2); pos+=R/2;
}
static void pair(const char *dir, const char *name, int player, const int *bodies, const int *pans, int nb){
  char p[256]; take(player,bodies,pans,nb,0); take(player,bodies,pans,nb,1);
  snprintf(p,sizeof p,"%s/%s_off-on.wav",dir,name); wav(p);
}
int main(int argc,char**argv){
  air_k[0]=argc>2?atof(argv[2]):1; air_k[1]=argc>3?atof(argv[3]):4; air_k[2]=argc>4?atof(argv[4]):2; air_send[2]=argc>5?atof(argv[5]):1;
  const char *d=argv[1];
  { int b[1]={VOICE_PERC(PC_BOWL)}, p[1]={15}; pair(d,"5-flute-and-singing-bowl",1+EX_FLUTE,b,p,1); }
  { int b[1]={VOICE_PERC(PC_CYMBAL)}, p[1]={15}; pair(d,"6-reed-and-cymbal",1+EX_REED,b,p,1); }
  { int b[1]={VOICE_PERC(PC_GLASS)}, p[1]={15}; pair(d,"7-string-and-glass",0,b,p,1); }
  { int b[2]={VOICE_PERC(PC_GONG),VOICE_PERC(PC_GONG)}, p[2]={10,50}; pair(d,"8-flute-and-two-gongs-near-and-far",1+EX_FLUTE,b,p,2); }
  return 0;
}
