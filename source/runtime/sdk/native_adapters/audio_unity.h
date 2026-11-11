/* Shared first-party IL2CPP audio reflection/GC boundary. Each consumer compiles
 * its own state. Persistent managed references use pointer-sized roots. */
#ifndef MOON_NATIVE_AUDIO_UNITY_H
#define MOON_NATIVE_AUDIO_UNITY_H
typedef U64 SURoot;
static struct {
    PTR (*methods)(PTR,PTR *),(*param)(PTR,DWORD),(*class_type)(PTR),(*type_object)(PTR);
    const char *(*name)(PTR);DWORD (*arity)(PTR);char *(*type_name)(PTR);void (*free)(PTR);
    PTR (*new_object)(PTR),(*new_array)(PTR,U64),(*string)(const char *),(*target)(SURoot);
    U64 (*array_length)(PTR);SURoot (*root)(PTR,BOOL);void (*unroot)(SURoot);
    void (*static_field)(PTR,void *);int (*field_offset)(PTR);PTR (*array_address)(PTR,int,U64);
    PTR byte_class,intptr_class,float_class;
    PTR go_ctor,go_add,obj_alive,obj_destroy,go_transform,tr_parent;
    PTR create,set_data,set_clip,set_time,get_time,play,pause,is_playing,volume,awake,spatial,loop;
    const char *failed;
    int ready;
} su;
static inline int su_eq(const char *a,const char *b){if(!a||!b)return a==b;while(*a&&*a==*b){a++;b++;}return *a==*b;}
static inline void su_copy(void *d,const void *s,U64 n){unsigned char *a=d;const unsigned char *b=s;while(n--)*a++=*b++;}
static inline void su_zero(void *d,U64 n){unsigned char *a=d;while(n--)*a++=0;}
static inline PTR su_method(const char *ns,const char *type,const char *name,const char *sig){
    PTR k=find_class(ns,type),it=0,m,found=0;if(!k)return 0;
    while((m=su.methods(k,&it))){
        if(!su_eq(su.name(m),name))continue;
        const char *s=sig;DWORD count=su.arity(m);int match=1;
        for(DWORD i=0;i<count;i++){
            char *text=su.type_name(su.param(m,i));if(!text){match=0;break;}const char *t=text;
            while(*t&&*s&&*s!=';'&&*t==*s){t++;s++;}
            if(*t||(*s&&*s!=';'))match=0;su.free(text);
            if(*s==';')s++;else if(i+1<count)match=0;if(!match)break;
        }
        if(*s)match=0;if(match){if(found)return 0;found=m;}
    }return found;
}
static inline SURoot su_hold(PTR p){return p?su.root(p,0):0;}
static inline PTR su_get(SURoot r){return r?su.target(r):0;}
static inline void su_drop(SURoot *r){if(*r)su.unroot(*r);*r=0;}
static inline PTR ym_array(PTR a,int stride,U64 i){return a&&i<su.array_length(a)?(unsigned char *)a+32+(U64)stride*i:0;}
static inline int su_alive(PTR p){int ok=1;void *args[]={p};PTR v=p?call(su.obj_alive,0,args,&ok):0;return ok&&v&&*(unsigned char *)unbox(v);}
static inline void ym_destroy(SURoot *root){PTR p=su_get(*root);if(su_alive(p)){int ok=1;void *args[]={p};call(su.obj_destroy,0,args,&ok);}su_drop(root);}
static inline int ym_unity(PTR game){
#define YE(F,N,T) su.F=(T)GetProcAddress(game,"il2cpp_" N);if(!su.F){su.failed="il2cpp_" N;return 0;}
    YE(methods,"class_get_methods",PTR(*)(PTR,PTR *));YE(param,"method_get_param",PTR(*)(PTR,DWORD));
    YE(name,"method_get_name",const char *(*)(PTR));YE(arity,"method_get_param_count",DWORD(*)(PTR));
    YE(type_name,"type_get_name",char *(*)(PTR));YE(free,"free",void(*)(PTR));
    YE(class_type,"class_get_type",PTR(*)(PTR));YE(type_object,"type_get_object",PTR(*)(PTR));
    YE(new_object,"object_new",PTR(*)(PTR));YE(new_array,"array_new",PTR(*)(PTR,U64));
    YE(array_length,"array_length",U64(*)(PTR));YE(string,"string_new",PTR(*)(const char *));
    YE(root,"gchandle_new",SURoot(*)(PTR,BOOL));YE(target,"gchandle_get_target",PTR(*)(SURoot));YE(unroot,"gchandle_free",void(*)(SURoot));
    YE(static_field,"field_static_get_value",void(*)(PTR,void *));YE(field_offset,"field_get_offset",int(*)(PTR));
#undef YE
    /* Same source-pinned length producer used by the established native mods. */
    const unsigned char *p=(const unsigned char *)(PTR)su.array_length;if(p[0]!=0xe9){su.failed="array-length-thunk";return 0;}
    int d=0;su_copy(&d,p+1,4);p+=5+d;if(p[0]!=0x8b||p[1]!=0x41||p[2]!=0x18||p[3]!=0xc3){su.failed="array-length-layout";return 0;}
    su.array_address=ym_array;
#define YB(F,T,N,S) su.F=su_method("UnityEngine",T,N,S);if(!su.F){su.failed=T "::" N;return 0;}
    YB(go_ctor,"GameObject",".ctor","System.String");YB(go_add,"GameObject","AddComponent","System.Type");
    YB(go_transform,"Component","get_transform","");YB(tr_parent,"Transform","SetParent","UnityEngine.Transform;System.Boolean");
    YB(obj_alive,"Object","op_Implicit","UnityEngine.Object");YB(obj_destroy,"Object","Destroy","UnityEngine.Object");
    YB(create,"AudioClip","Create","System.String;System.Int32;System.Int32;System.Int32;System.Boolean");
    YB(set_data,"AudioClip","SetData","System.Single[];System.Int32");YB(set_clip,"AudioSource","set_clip","UnityEngine.AudioClip");
    /* timeSamples is stripped from the pinned player; its seconds API remains. */
    YB(set_time,"AudioSource","set_time","System.Single");YB(get_time,"AudioSource","get_time","");
    YB(play,"AudioSource","Play","");YB(pause,"AudioSource","Pause","");YB(is_playing,"AudioSource","get_isPlaying","");
    YB(volume,"AudioSource","set_volume","System.Single");YB(awake,"AudioSource","set_playOnAwake","System.Boolean");
    YB(spatial,"AudioSource","set_spatialBlend","System.Single");YB(loop,"AudioSource","set_loop","System.Boolean");
#undef YB
    su.byte_class=find_class("System","Byte");su.intptr_class=find_class("System","IntPtr");su.float_class=find_class("System","Single");
    su.ready=su.byte_class&&su.intptr_class&&su.float_class;if(!su.ready)su.failed="primitive-classes";return su.ready;
}
#endif
