/* AIR with all five voices in play, as normally played (rhythms, chord
 * changes, the default effects): each file is AIR off, medium, full.
 * usage: air_ensembles OUTDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
extern float air_k[3], air_send[3];
#define R 48000
#define SEC 15
static int16_t out[R * 2 * 50];
static int pos, peak, clips;
static void wav(const char *path){
  FILE *f=fopen(path,"wb"); int bytes=pos*4; int hdr[11]={0x46464952,36+bytes,0x45564157,0x20746d66,16,0x00020001,R,R*4,0x00100004,0x61746164,bytes};
  fwrite(hdr,4,11,f); fwrite(out,4,pos,f); fclose(f); pos=0;}
static void take(const int *v, float air){
  song_default();
  for(int s=0;s<NS;s++) song.sp[s][SP_VOICE]=v[s];
  engine_air=air; engine_room=0; engine_init(R); engine_set_playing(1);
  for(int i=0;i<SEC;i++){ engine_render(out+2*pos,R);
    for(int k=0;k<2*R;k++){ int a=abs(out[2*pos+k]); if(a>peak) peak=a; clips+=a>32000; } pos+=R; }
  memset(out+2*pos,0,R*2); pos+=R/2;
}
static void ens(const char *dir, const char *name, const int *v){
  char p[256]; peak=clips=0;
  take(v,0); take(v,0.5f); take(v,1);
  snprintf(p,sizeof p,"%s/%s_off-medium-full.wav",dir,name); wav(p);
  printf("%s: peak %d, samples near full scale %d\n", name, peak, clips);
}
#define S(p) VOICE_PERC(PC_##p)
#define W(e) (1 + EX_##e)
int main(int argc,char**argv){
  air_k[0]=argc>2?atof(argv[2]):1; air_k[1]=2; air_k[2]=2; air_send[2]=0.3f; air_send[0]=argc>3?atof(argv[3]):1;
  const char *d=argv[1];
  { int v[NS]={W(FLUTE),S(BOWL),0,S(GLASS),S(GONG)}; ens(d,"A-flute-bowl-string-glass-gong",v); }
  { int v[NS]={S(GENDER),S(BONANG),W(FLUTE),W(REED),S(GONG)}; ens(d,"B-gamelan-gender-bonang-flute-reed-gong",v); }
  { int v[NS]={0,0,0,0,0}; ens(d,"C-five-strings",v); }
  { int v[NS]={W(FLUTE),S(GONG),S(BOWL),0,S(GONG)}; ens(d,"D-flute-two-gongs-bowl-string",v); }
  return 0;
}
