/* Engine-independent host timeline and authenticated ping/snapshot protocol.
 * All timestamps are monotonic milliseconds in the local process's domain.
 * Only echoed outstanding requests cross clock domains; wall clocks never do. */
#ifndef MOON_YOUTUBE_CORE_H
#define MOON_YOUTUBE_CORE_H
typedef unsigned int YU32;
typedef unsigned long long YU64;
#define YM_MAX_MS 600000u
#define YM_PACKET 64u
#define YM_COMMAND 48u
#define YM_MAGIC 0x3154594du
#define YM_COMMAND_MAGIC 0x3143594du
#define YM_STALE_MS 3000u
typedef struct {
    YU64 session, sequence, revision, anchor;
    YU32 position, duration, playing;
    char video[12];
} YTimeline;
typedef struct {
    YTimeline timeline;
    YU64 host, pending, received, sequence;
    YU32 rtt;
    int fresh;
} YGuest;
static inline void ym_zero(void *p,unsigned n){unsigned char *s=p;while(n--)*s++=0;}
static inline void ym_copy(void *p,const void *q,unsigned n){unsigned char *d=p;const unsigned char *s=q;while(n--)*d++=*s++;}
static inline YU32 ym_r32(const unsigned char *p){return (YU32)p[0]|(YU32)p[1]<<8|(YU32)p[2]<<16|(YU32)p[3]<<24;}
static inline YU64 ym_r64(const unsigned char *p){return (YU64)ym_r32(p)|(YU64)ym_r32(p+4)<<32;}
static inline void ym_w32(unsigned char *p,YU32 n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i));}
static inline void ym_w64(unsigned char *p,YU64 n){ym_w32(p,(YU32)n);ym_w32(p+4,(YU32)(n>>32));}
static inline int ym_id(const char *s){
    for(unsigned i=0;i<11;i++){char c=s[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return 0;}
    return s[11]==0;
}
static inline int ym_equal(const char *a,const char *b){for(unsigned i=0;i<12;i++)if(a[i]!=b[i])return 0;return 1;}
static inline YU32 ym_position(const YTimeline *t,YU64 now){
    YU64 p=t->position;if(t->playing&&now>=t->anchor)p+=now-t->anchor;
    return (YU32)(p>t->duration?t->duration:p);
}
static inline void ym_begin(YTimeline *t,YU64 epoch,YU64 now){ym_zero(t,sizeof(*t));t->session=epoch?epoch:1;t->anchor=now;}
/* Actions: play/replace, pause, resume, seek, stop. Volume is strictly local. */
static inline int ym_command(YTimeline *t,unsigned action,const char *id,YU32 value,YU64 now){
    if(action<1||action>5)return 0;
    if(action==1&&(!id||!ym_id(id)||!value||value>YM_MAX_MS))return 0;
    if(action!=1&&action!=5&&!ym_id(t->video))return 0;
    if(action==4&&value>t->duration)return 0;
    t->position=ym_position(t,now);t->anchor=now;t->revision++;
    if(action==1){ym_copy(t->video,id,12);t->duration=value;t->position=0;t->playing=1;}
    if(action==2)t->playing=0;
    if(action==3){if(t->position>=t->duration)t->position=0;t->playing=1;}
    if(action==4)t->position=value;
    if(action==5){ym_zero(t->video,12);t->playing=0;t->duration=t->position=0;}
    return 1;
}
static inline void ym_ping(unsigned char *p,YU64 stamp){ym_zero(p,YM_PACKET);ym_w32(p,YM_MAGIC);ym_w32(p+4,1);ym_w64(p+24,stamp);}
static inline int ym_reply(YTimeline *t,const unsigned char *ping,unsigned n,unsigned char *p,YU64 now){
    if(n!=YM_PACKET||ym_r32(ping)!=YM_MAGIC||ym_r32(ping+4)!=1||!ym_r64(ping+24))return 0;
    ym_zero(p,YM_PACKET);ym_w32(p,YM_MAGIC);ym_w32(p+4,2);ym_w64(p+8,t->session);
    ym_w64(p+16,++t->sequence);ym_w64(p+24,ym_r64(ping+24));ym_w64(p+32,t->revision);
    YU32 position=ym_position(t,now);ym_w32(p+40,position);ym_w32(p+44,t->duration);
    ym_w32(p+48,t->playing&&position<t->duration);ym_copy(p+52,t->video,12);return 1;
}
static inline int ym_accept(YGuest *g,YU64 sender,const unsigned char *p,unsigned n,YU64 now){
    if(sender!=g->host||!sender||n!=YM_PACKET||ym_r32(p)!=YM_MAGIC||ym_r32(p+4)!=2)return 0;
    YU64 echo=ym_r64(p+24),epoch=ym_r64(p+8),seq=ym_r64(p+16),rev=ym_r64(p+32);
    YU32 pos=ym_r32(p+40),dur=ym_r32(p+44),playing=ym_r32(p+48);
    if(!epoch||!seq||!g->pending||echo!=g->pending||now<echo||now-echo>2000||dur>YM_MAX_MS||pos>dur||playing>1)return 0;
    if(dur?(!ym_id((const char *)p+52)):((p[52]!=0)||pos||playing))return 0;
    if(g->fresh&&epoch==g->timeline.session&&(seq<=g->sequence||rev<g->timeline.revision))return 0;
    g->rtt=(YU32)(now-echo);g->pending=0;g->received=now;g->sequence=seq;g->fresh=1;
    g->timeline=(YTimeline){epoch,seq,rev,now,pos,dur,playing,{0}};
    if(playing){YU64 ahead=(YU64)pos+g->rtt/2;g->timeline.position=(YU32)(ahead>dur?dur:ahead);}
    ym_copy(g->timeline.video,p+52,12);return 1;
}
static inline int ym_fresh(const YGuest *g,YU64 now){return g->fresh&&now>=g->received&&now-g->received<=YM_STALE_MS;}
/* PCM contract: 32-byte header followed by interleaved signed 16-bit LE stereo.
 * The helper publishes by rename only after validating the complete output. */
#define YM_RATE 44100u
#define YM_CHANNELS 2u
#define YM_CHUNK 65536u
static inline YU32 ym_pcm(const unsigned char *h,YU64 size){
    static const unsigned char magic[8]={'M','Y','P','C','M','0','1',0};
    for(unsigned i=0;i<8;i++)if(h[i]!=magic[i])return 0;
    YU32 frames=ym_r32(h+16),bytes=ym_r32(h+20);
    for(unsigned i=24;i<32;i++)if(h[i])return 0;
    if(ym_r32(h+8)!=YM_RATE||ym_r32(h+12)!=YM_CHANNELS||!frames||frames>YM_RATE*600u||bytes!=(YU64)frames*4||size!=(YU64)bytes+32)return 0;
    return frames;
}
#endif
