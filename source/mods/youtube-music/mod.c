/* Steam IL2CPP mod. No network media URLs or media bytes enter the game.
 * The host OS helper owns extraction; this callback reads bounded PCM chunks. */
#include "../../runtime/native/gameplay.h"
#include "core.h"
#include "unity.h"
#include "transport.h"
int _fltused=0;
static struct {
    BOOL (*read)(PTR,void *,DWORD,DWORD *,PTR),(*close)(PTR),(*move)(const char *,const char *,DWORD),(*mkdir)(const char *,PTR),(*size)(PTR,U64 *);
    DWORD (*module_path)(PTR,char *,DWORD);U64 (*clock)(void);
    char directory[1024],state[1100],temp[1100],command[1100];
    PTR world_field,frame,restoring,log,file;DWORD thread;
    SURoot world,go,source,clip,samples;
    YTimeline host;YGuest guest;U64 owner,local,epoch,last_poll,last_ping,last_command,last_apply;
    YU32 frames,loaded;unsigned role,volume;int last_frame,loaded_ok;
    char video[12];const char *status;
    short pcm[YM_CHUNK];
} music;
static void ym_event(const char *s){if(!music.log)return;char b[200],*p=cat(b,"{\"mod\":\"youtube-music\",\"event\":\"");p=cat(p,s);p=cat(p,"\"}\n");DWORD n=0;WriteFile(music.log,b,(DWORD)(p-b),&n,0);}
static PTR ym_open(const char *p){PTR f=CreateFileA(p,0x80000000,7,0,3,0x80,0);return f==(PTR)(U64)-1?0:f;}
static int ym_read(PTR f,void *p,DWORD n){DWORD got=0;return music.read(f,p,n,&got,0)&&got==n;}
static void ym_audio_drop(void){
    if(music.file)music.close(music.file);music.file=0;
    if(su_alive(su_get(music.source))){int ok=1;void *args[]={0};call(su.pause,su_get(music.source),0,&ok);call(su.set_clip,su_get(music.source),args,&ok);}
    ym_destroy(&music.clip);su_drop(&music.samples);music.frames=music.loaded=0;music.loaded_ok=0;ym_zero(music.video,12);
}
static void ym_reset(PTR world,U64 owner,U64 local,U64 now){
    ym_audio_drop();ym_destroy(&music.go);su_drop(&music.source);su_drop(&music.world);
    music.world=su_hold(world);music.owner=owner;music.local=local;music.role=owner&&owner==local;
    music.epoch=now^(local<<1)^(++music.last_command);if(!music.epoch)music.epoch=1;
    ym_begin(&music.host,music.epoch,now);ym_zero(&music.guest,sizeof(music.guest));music.guest.host=owner;
    music.status=music.role?"stopped":"waiting-for-host";music.last_ping=0;ym_event("session-reset");
}
static int ym_audio_source(PTR world){
    if(su_alive(su_get(music.source)))return 1;
    PTR klass=find_class("UnityEngine","GameObject");PTR go=klass?su.new_object(klass):0;music.go=su_hold(go);
    int ok=1;void *ctor[]={su.string("MoonLab YouTube Music")};call(su.go_ctor,su_get(music.go),ctor,&ok);
    PTR source_type=find_class("UnityEngine","AudioSource");PTR type=source_type?su.type_object(su.class_type(source_type)):0;
    void *add[]={type};PTR source=type?call(su.go_add,su_get(music.go),add,&ok):0;music.source=su_hold(source);
    if(!ok||!music.source)return 0;
    unsigned char no=0;float zero=0;void *flag[]={&no},*spatial[]={&zero};
    call(su.awake,su_get(music.source),flag,&ok);call(su.loop,su_get(music.source),flag,&ok);call(su.spatial,su_get(music.source),spatial,&ok);
    PTR go_transform=su_method("UnityEngine","GameObject","get_transform","");
    SURoot tr=su_hold(call(go_transform,su_get(music.go),0,&ok)),parent=su_hold(call(su.go_transform,world,0,&ok));
    void *args[]={su_get(parent),&no};if(!tr||!parent)ok=0;else call(su.tr_parent,su_get(tr),args,&ok);
    su_drop(&tr);su_drop(&parent);return ok;
}
static void ym_load(const YTimeline *t,PTR world){
    if(!ym_id(t->video)){if(music.clip||music.file)ym_audio_drop();music.status="stopped";return;}
    if(!ym_equal(music.video,t->video)){
        ym_audio_drop();char p[1100];char *end=cat(cat(p,music.directory),"\\cache\\");end=cat(end,t->video);cat(end,".pcm");
        PTR f=ym_open(p);if(!f){music.status="waiting-for-helper";return;}
        unsigned char header[32];U64 size=0;YU32 frames=music.size(f,&size)&&ym_read(f,header,32)?ym_pcm(header,size):0;
        YU32 duration=(YU32)((YU64)frames*1000/YM_RATE),delta=duration>t->duration?duration-t->duration:t->duration-duration;
        if(!frames||delta>50){music.close(f);music.status="cache-invalid";return;}
        if(!ym_audio_source(world)){music.close(f);music.status="audio-source-failed";return;}
        int length=(int)frames,channels=YM_CHANNELS,rate=YM_RATE,ok=1;unsigned char stream=0;
        void *args[]={su.string(t->video),&length,&channels,&rate,&stream};
        music.clip=su_hold(call(su.create,0,args,&ok));music.samples=su_hold(su.new_array(su.float_class,YM_CHUNK));
        if(!ok||!music.clip||!music.samples){music.close(f);ym_audio_drop();music.status="audio-create-failed";return;}
        music.file=f;music.frames=frames;ym_copy(music.video,t->video,12);ym_event("loading-pcm");
    }
    if(music.loaded_ok)return;
    /* At most 512 KiB of file I/O per callback; no whole-track synchronous read. */
    for(unsigned batch=0;batch<4&&music.loaded<music.frames;batch++){
        YU32 frames=music.frames-music.loaded;if(frames>YM_CHUNK/2)frames=YM_CHUNK/2;YU32 count=frames*2;
        if(!ym_read(music.file,music.pcm,count*2)){ym_audio_drop();music.status="cache-read-failed";return;}
        SURoot tail=0,array=music.samples;if(count!=YM_CHUNK){tail=su_hold(su.new_array(su.float_class,count));array=tail;}
        float *samples=su.array_address(su_get(array),4,0);if(!samples){su_drop(&tail);ym_audio_drop();music.status="audio-buffer-failed";return;}
        for(unsigned i=0;i<count;i++)samples[i]=(float)music.pcm[i]/32768.0f;
        int offset=(int)music.loaded,ok=1;void *args[]={su_get(array),&offset};PTR result=call(su.set_data,su_get(music.clip),args,&ok);
        int accepted=ok&&result&&*(unsigned char *)unbox(result);su_drop(&tail);
        if(!accepted){ym_audio_drop();music.status="audio-data-failed";return;}music.loaded+=frames;
    }
    music.status="loading";
    if(music.loaded==music.frames){music.close(music.file);music.file=0;su_drop(&music.samples);
        int ok=1;void *args[]={su_get(music.clip)};call(su.set_clip,su_get(music.source),args,&ok);
        music.loaded_ok=ok;music.last_apply=0;ym_event(ok?"audio-ready":"audio-bind-failed");
    }
}
static void ym_audio_tick(const YTimeline *t,int active,U64 now,PTR world){
    if(!active){if(su_alive(su_get(music.source))){int ok=1;call(su.pause,su_get(music.source),0,&ok);}music.status="waiting-for-host";return;}
    ym_load(t,world);if(!music.loaded_ok)return;
    int ok=1;YU32 pos=ym_position(t,now);int playing=t->playing&&pos<t->duration;
    if(now-music.last_apply<250)return;music.last_apply=now;
    float volume=(float)music.volume/100.0f;void *vol[]={&volume};call(su.volume,su_get(music.source),vol,&ok);
    int is_playing=boolean(su.is_playing,su_get(music.source),&ok);
    YU64 target=(YU64)pos*YM_RATE/1000;if(target>=music.frames)target=music.frames-1;
    float desired=(float)target/(float)YM_RATE,position=0;
    PTR boxed=call(su.get_time,su_get(music.source),0,&ok);
    if(ok&&boxed)su_copy(&position,unbox(boxed),sizeof(position));else ok=0;
    float drift=position>desired?position-desired:desired-position;
    if(!playing&&is_playing)call(su.pause,su_get(music.source),0,&ok);
    if(!is_playing||drift>.2f){void *seek[]={&desired};call(su.set_time,su_get(music.source),seek,&ok);}
    if(playing&&!is_playing)call(su.play,su_get(music.source),0,&ok);
    music.status=ok?(playing?"playing":"paused"):"audio-playback-failed";
}
static void ym_receive(U64 peer,const unsigned char *p,unsigned n,void *ctx){
    (void)ctx;U64 now=music.clock();
    if(music.role){unsigned char reply[YM_PACKET];if(ym_reply(&music.host,p,n,reply,now))st_send(peer,reply,YM_PACKET);}
    else ym_accept(&music.guest,peer,p,n,now);
}
static void ym_commands(YTimeline *t,U64 session,U64 now){
    PTR f=ym_open(music.command);if(!f)return;unsigned char p[YM_COMMAND];U64 size=0;
    int valid=music.size(f,&size)&&size==YM_COMMAND&&ym_read(f,p,YM_COMMAND);music.close(f);
    if(!valid||ym_r32(p)!=YM_COMMAND_MAGIC||ym_r32(p+4)!=1)return;
    U64 nonce=ym_r64(p+8);if(!nonce||nonce==music.last_command)return;
    music.last_command=nonce;
    if(ym_r64(p+24)!=session){ym_event("stale-command-rejected");return;}
    unsigned action=ym_r32(p+16),value=ym_r32(p+20);
    if(action==6&&value<=100){music.volume=value;return;}
    if(!music.role){ym_event("guest-command-rejected");return;}
    ym_event(ym_command(t,action,(const char *)p+32,value,now)?"command-applied":"command-rejected");
}
static void ym_status(const YTimeline *t,int active,U64 session,U64 now){
    char b[768],*p=cat(b,"{\"schema\":1,\"session\":");p=num(p,session);p=cat(p,",\"host\":");p=num(p,music.role);
    p=cat(p,",\"active\":");p=num(p,(unsigned)active);p=cat(p,",\"video\":\"");p=cat(p,ym_id(t->video)?t->video:"");
    p=cat(p,"\",\"position_ms\":");p=num(p,ym_position(t,now));p=cat(p,",\"duration_ms\":");p=num(p,t->duration);
    p=cat(p,",\"playing\":");p=num(p,t->playing&&ym_position(t,now)<t->duration);p=cat(p,",\"volume\":");p=num(p,music.volume);
    p=cat(p,",\"status\":\"");p=cat(p,music.status?music.status:"waiting");p=cat(p,"\",\"rtt_ms\":");p=num(p,music.guest.rtt);
    p=cat(p,",\"revision\":");p=num(p,t->revision);p=cat(p,",\"command\":");p=num(p,music.last_command);
    p=cat(p,",\"loaded_frames\":");p=num(p,music.loaded);p=cat(p,"}\n");
    PTR f=CreateFileA(music.temp,0x40000000,7,0,2,0x80,0);if(f==(PTR)(U64)-1)return;
    DWORD n=0;int ok=WriteFile(f,b,(DWORD)(p-b),&n,0)&&n==(DWORD)(p-b);music.close(f);
    if(ok)music.move(music.temp,music.state,1|8);
}
static int ym_before(int id,Context *c){
    (void)id;if(music.thread!=GetCurrentThreadId()&&music.thread)return 0;music.thread=GetCurrentThreadId();
    int ok=1,frame=integer(music.frame,0,&ok);if(!ok||frame==music.last_frame)return 0;music.last_frame=frame;
    PTR world=0;field_value((PTR)c->rcx,music.world_field,&world);if(!world||!su_alive(world))return 0;
    if(boolean(music.restoring,world,&ok)||!ok)return 0;
    U64 now=music.clock(),local=st_local_id(),owner=st_joined()?st_host_id():local;
    if(!local||!owner){ym_audio_tick(&music.host,0,now,world);return 0;}
    if(world!=su_get(music.world)||owner!=music.owner||local!=music.local)ym_reset(world,owner,local,now);
    st_receive(ym_receive,0);
    if(!music.role&&(!music.guest.pending||now-music.guest.pending>2000)&&now-music.last_ping>=500){
        unsigned char ping[YM_PACKET];ym_ping(ping,now);if(st_send(owner,ping,YM_PACKET))music.guest.pending=now;music.last_ping=now;
    }
    YTimeline *t=music.role?&music.host:&music.guest.timeline;int active=music.role||ym_fresh(&music.guest,now);
    /* Bridge generation is local to this world/authority. A queued GUI command
     * from the previous world cannot restart music in the next one. */
    if(now-music.last_poll>=250){music.last_poll=now;ym_commands(t,music.epoch,now);ym_status(t,active,music.epoch,now);}
    ym_audio_tick(t,active,now,world);return 0;
}
__declspec(dllexport) int MoonLab_ModStart(PTR domain,PTR game){
    PTR k=LoadLibraryA("kernel32.dll");if(!k)return 0;
#define YWIN(F,N,T) music.F=(T)GetProcAddress(k,N);if(!music.F)return 0
    YWIN(read,"ReadFile",BOOL(*)(PTR,void *,DWORD,DWORD *,PTR));YWIN(close,"CloseHandle",BOOL(*)(PTR));
    YWIN(move,"MoveFileExA",BOOL(*)(const char *,const char *,DWORD));YWIN(mkdir,"CreateDirectoryA",BOOL(*)(const char *,PTR));
    YWIN(size,"GetFileSizeEx",BOOL(*)(PTR,U64 *));YWIN(module_path,"GetModuleFileNameA",DWORD(*)(PTR,char *,DWORD));YWIN(clock,"GetTickCount64",U64(*)(void));
#undef YWIN
    char path[1024];DWORD n=music.module_path(0,path,900);if(!n||n>=900)return 0;
    while(n&&path[n-1]!='\\'&&path[n-1]!='/')n--;if(!n)return 0;path[n]=0;
    cat(cat(music.directory,path),"MoonLab");music.mkdir(music.directory,0);cat(cat(music.directory,path),"MoonLab\\music");music.mkdir(music.directory,0);
    cat(cat(music.state,music.directory),"\\state.json");cat(cat(music.temp,music.directory),"\\state.tmp");cat(cat(music.command,music.directory),"\\command.bin");
    cat(cat(path,music.directory),"\\events.jsonl");music.log=CreateFileA(path,0x40000000,7,0,2,0x80,0);if(music.log==(PTR)(U64)-1)music.log=0;
    if(!bind_runtime(domain,game)){ym_event("runtime-binding-failed");return 0;}
    if(!ym_unity(game)){ym_event("unity-binding-failed");ym_event(su.failed?su.failed:"unknown-binding");return 0;}
    if(!st_bind(game)){ym_event("transport-binding-failed");return 0;}
    PTR walker=find_class("MoonCleaners","MoonWalker");music.world_field=walker?get_field(walker,"world"):0;
    music.frame=su_method("UnityEngine","Time","get_frameCount","");music.restoring=su_method("MoonCleaners","MoonWorld","get_IsRestoringRendering","");
    if(!music.world_field||!music.frame||!music.restoring||!prepare_hook(method("MoonCleaners","MoonWalker","Update",0),100671733,0,game)){ym_event("hook-refused");return 0;}
    music.volume=70;music.last_frame=-1;before_hook=ym_before;
    if(!install_hooks()){ym_event("hook-failed");return 0;}ym_event("ready");return 1;
}
