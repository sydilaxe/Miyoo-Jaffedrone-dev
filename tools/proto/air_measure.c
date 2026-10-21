#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "engine.h"
extern float air_k[3];
#define R 48000
static int16_t pcm[R * 2 * 8];
static void wav(const char *path, int16_t *d, int frames){
  FILE *f=fopen(path,"wb"); int bytes=frames*4; int hdr[11]={0x46464952,36+bytes,0x45564157,0x20746d66,16,0x00020001,R,R*4,0x00100004,0x61746164,bytes};
  fwrite(hdr,4,11,f); fwrite(d,4,frames,f); fclose(f);}
/* envelope wobble: std/mean of 20 ms rms over a span */
static double wobble(int16_t *d, int from, int n){
  double e[400]; int m=0; for(int i=from;i+960<=from+n && m<400;i+=960){double s=0;for(int k=0;k<960;k++){double x=d[2*(i+k)]+d[2*(i+k)+1];s+=x*x;} e[m++]=sqrt(s/960);}
  double mu=0; for(int i=0;i<m;i++) mu+=e[i]; mu/=m; double v=0; for(int i=0;i<m;i++) v+=(e[i]-mu)*(e[i]-mu); return sqrt(v/m)/(mu+1e-9);}
static double rms(int16_t *d,int from,int n){double s=0;for(int i=from;i<from+n;i++){double x=d[2*i];s+=x*x;}return sqrt(s/n);}
int main(int argc,char**argv){
  const char *out = argc>1?argv[1]:".";
  if(argc>2) air_k[0]=atof(argv[2]); if(argc>3) air_k[1]=atof(argv[3]); if(argc>4) air_k[2]=atof(argv[4]);
  char p[256];
  /* 1: two free reeds, 7 cents apart, near each other */
  for(int a=0;a<3;a++){
    float air = a==0?0:a==1?0.5f:1.f;
    song_default(); for(int s=0;s<NS;s++){song.on[s]=s<2; song.sp[s][SP_VOICE]=getenv("VV")?atoi(getenv("VV")):1+EX_FREE; song.sp[s][SP_NOTE]=song.sp[0][SP_NOTE];}
    song.sp[1][SP_FINE]=song.sp[0][SP_FINE]+(getenv("DC")?atoi(getenv("DC")):7); song.gp[GP_CHANGES]=0; song.fx[FX_DMIX]=song.fx[FX_VSEND]=0; song.fx[FX_SYMP]=0;
    engine_air=air; engine_room=0; engine_init(R); engine_set_playing(1);
    engine_render(pcm,R*2); engine_render(pcm,R*6);
    printf("reeds 7c apart, air %.1f: beat wobble %.3f rms %.0f\n", air, wobble(pcm,R,R*4), rms(pcm,R,R*4));
    snprintf(p,sizeof p,"%s/air_reeds_%d.wav",out,a); wav(p,pcm,R*6);
  }
  /* 2: a gong, never struck or bowed, beside a flute */
  for(int a=0;a<2;a++){
    song_default(); for(int s=0;s<NS;s++) song.on[s]=0;
    song.on[0]=song.on[1]=1; song.sp[0][SP_VOICE]=1+EX_FLUTE; song.sp[1][SP_VOICE]=VOICE_PERC(PC_GONG); song.sp[1][SP_BOW]=0; song.sp[1][SP_DECAY]=100; song.sp[1][SP_PON]=song.sp[1][SP_BON]=0; song.fx[FX_DMIX]=song.fx[FX_VSEND]=0;
    song.gp[GP_CHANGES]=0; song.fx[FX_SYMP]=0;
    engine_air=a; engine_room=0; engine_init(R); engine_set_playing(1);
    engine_render(pcm,R*8);
    /* the gong alone: mute the flute and hear what rings */
    song.on[0]=0; engine_render(pcm,R*4);
    printf("gong beside a flute, air %d: what rings on after the flute stops %.0f\n", a, rms(pcm,R/2,R));
    snprintf(p,sizeof p,"%s/air_gong_%d.wav",out,a); wav(p,pcm,R*4);
  }
  /* 3: worst cases: finite, bounded */
  static const int mixes[3][NS]={{VOICE_PERC(PC_CYMBAL),VOICE_PERC(PC_CYMBAL),VOICE_PERC(PC_CYMBAL),VOICE_PERC(PC_CYMBAL),VOICE_PERC(PC_CYMBAL)},
    {VOICE_PERC(PC_CYMBAL),VOICE_PERC(PC_CYMBAL),1+EX_FLUTE,1+EX_FLUTE,1+EX_FREE},{0,0,0,0,0}};
  for(int m=0;m<3;m++) for(int a=0;a<2;a++){
    song_default(); for(int q=0;q<NFX;q++) song.fx[q]=fparam_def(q)->max;
    static const int top[]={SP_BOW,SP_DECAY,SP_BUZZ,SP_FOLD,SP_STIFF,SP_BRIGHT};
    for(int s=0;s<NS;s++){song.on[s]=1; song.sp[s][SP_VOICE]=mixes[m][s]; for(int k=0;k<6;k++) song.sp[s][top[k]]=sparam_def(top[k])->max;}
    engine_air=a; engine_room=a; engine_init(R); engine_set_playing(1);
    clock_t c0=clock(); double pk=0; int bad=0;
    for(int i=0;i<20;i++){ engine_render(pcm,R); for(int k=0;k<R*2;k++){ if(abs(pcm[k])>pk) pk=abs(pcm[k]); } }
    double cpu=(double)(clock()-c0)/CLOCKS_PER_SEC/20*100;
    printf("worst mix %d, air+room %d: cpu %.2f%% peak %.0f rms(last s) %.0f\n", m, a, cpu, pk, rms(pcm,0,R));
    (void)bad;
  }
  return 0;
}
