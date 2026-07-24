// Offline A/B + f64-count harness for house atoms. Compile with -DATOM=<Header> -DCLASS=<name>
// and (optional) -DSTEREO. Feeds a fixed noise+impulse test signal, writes a wav, prints RMS.
// Purpose: prove a hybrid-float conversion is TONE-IDENTICAL and measure the f64-op drop.
#include <od/config.h>
#include ATOM_HEADER
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdlib>
namespace od { ConfigData globalConfig; }

int main(int argc, char** argv) {
  int sr = 48000, fr = 128; od::globalConfig.sampleRate = sr; od::globalConfig.frameLength = fr;
  house::CLASS op;
#ifdef SETPARAMS
  for (size_t k=0;k<op.mParams.size();k++){ char e[8]; snprintf(e,8,"P%zu",k); const char* v=getenv(e); if(v) op.mParams[k]->hardSet((float)atof(v)); }
#endif
  // deterministic test signal: filtered noise + periodic impulses (excites filters + transients)
  int N = sr * 2; std::vector<float> outacc; double rms = 0;
  uint32_t s = 0x12345u;
  auto nz = [&](){ s ^= s<<13; s^=s>>17; s^=s<<5; return (float)(int32_t)s * 4.6566129e-10f; };
#ifdef STEREO
  float *iL=op.mInL.buffer(), *iR=op.mInR.buffer(), *oL=op.mOutL.buffer(), *oR=op.mOutR.buffer();
#else
  float *ip=op.INBUF.buffer(), *ob=op.OUTBUF.buffer();
#endif
  int nb = N/fr;
  for (int b=0;b<nb;b++){
    for(int i=0;i<fr;i++){
      int n=b*fr+i;
      float x = nz()*0.3f + ((n%4000==0)?0.8f:0.0f);
#ifdef STEREO
      iL[i]=x; iR[i]=x*0.7f;
#else
      ip[i]=x;
#endif
    }
    op.process();
    for(int i=0;i<fr;i++){
#ifdef STEREO
      float y=oL[i];
#else
      float y=ob[i];
#endif
      outacc.push_back(y); rms += (double)y*y;
    }
  }
  fprintf(stderr, "RMS=%.8f  n=%zu\n", sqrt(rms/outacc.size()), outacc.size());
  if (argc>1){ FILE*f=fopen(argv[1],"wb"); uint32_t nn=outacc.size(),db=nn*2,riff=36+db,br=sr*2,s1=16; uint16_t ch=1,fmt=1,bits=16,al=2;
    fwrite("RIFF",1,4,f);fwrite(&riff,4,1,f);fwrite("WAVE",1,4,f);fwrite("fmt ",1,4,f);fwrite(&s1,4,1,f);
    fwrite(&fmt,2,1,f);fwrite(&ch,2,1,f);fwrite(&sr,4,1,f);fwrite(&br,4,1,f);fwrite(&al,2,1,f);fwrite(&bits,2,1,f);
    fwrite("data",1,4,f);fwrite(&db,4,1,f);
    for(float x:outacc){int v=(int)(x*32767);if(v>32767)v=32767;if(v<-32768)v=-32768;int16_t o=v;fwrite(&o,2,1,f);} fclose(f);}
  return 0;
}
