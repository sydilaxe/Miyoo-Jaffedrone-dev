/* AIR with one voice playing and four only listening (never bowed, struck or
 * blown): they sound only through what reaches them in the air. Normal play
 * otherwise (the player's rhythms, chord changes, the default effects); the
 * listeners keep their own notes in the scale. Each file: AIR off, then full.
 * usage: air_listen OUTDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
extern float air_k[3], air_send[3];
#define R 48000
#define SEC 16
static int16_t out[R * 2 * 40];
static int pos;
static void wav(const char *path){
  FILE *f=fopen(path,"wb"); int bytes=pos*4; int hdr[11]={0x46464952,36+bytes,0x45564157,0x20746d66,16,0x00020001,R,R*4,0x00100004,0x61746164,bytes};
  fwrite(hdr,4,11,f); fwrite(out,4,pos,f); fclose(f); pos=0;}
static double take(const int *v, float air){
  song_default();
  for(int s=0;s<NS;s++){ song.sp[s][SP_VOICE]=v[s];
    if(s){ song.sp[s][SP_BOW]=0; song.sp[s][SP_PON]=song.sp[s][SP_BON]=0; song.sp[s][SP_DECAY]=100; } }
  engine_air=air; engine_room=0; engine_init(R); engine_set_playing(1);
  for(int i=0;i<SEC;i++){ if(i==12) song.on[0]=0; engine_render(out+2*pos,R); pos+=R; }
  double e=0; for(int i=pos-3*R;i<pos;i++){ double x=out[2*i]; e+=x*x; } /* the listeners after the player stops */
  memset(out+2*pos,0,R*2); pos+=R/2;
  return __builtin_sqrt(e/(3*R));
}
static void one(const char *dir, const char *name, const int *v){
  char p[256]; double off=take(v,0), on=take(v,1);
  snprintf(p,sizeof p,"%s/%s_off-on.wav",dir,name); wav(p);
  printf("%s: after the player stops, off %.0f, on %.0f\n", name, off, on);
}
#define S(p) VOICE_PERC(PC_##p)
#define W(e) (1 + EX_##e)
int main(int argc,char**argv){
  air_k[0]=argc>2?atof(argv[2]):1; air_k[1]=2; air_k[2]=argc>4?atof(argv[4]):2; air_send[2]=argc>5?atof(argv[5]):0.3f; air_send[0]=argc>3?atof(argv[3]):1;
  const char *d=argv[1];
  { int v[NS]={W(FLUTE),S(GONG),S(BOWL),S(GLASS),0}; one(d,"E-flute-plays_gong-bowl-glass-string-listen",v); }
  { int v[NS]={0,S(GONG),S(GLASS),S(CYMBAL),0}; one(d,"F-string-plays_gong-glass-cymbal-string-listen",v); }
  { int v[NS]={W(REED),S(GENDER),S(BONANG),S(GONG),S(BOWL)}; one(d,"G-reed-plays_gamelan-bowl-listen",v); }
  return 0;
}
