/* ZFramework Android ARM64 host for Nintendo Switch. */
#include <switch.h>
#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "android_native_unity.h"
#include "config.h"
#include "ctr_video.h"
#include "debug.h"
#include "error.h"
#include "imports.h"
#include "jni_fake.h"
#include "libc_shim.h"
#include "so_util.h"
#include "util.h"
#include "unity_jni.h"
#include "zf_audio.h"
#include "zf_input.h"

#define SO_REGION_BYTES ((size_t)64*1024*1024)
static void *heap_so_base;
static size_t heap_so_limit;
void *g_mmap_arena_base;
size_t g_mmap_arena_size;
size_t g_mmap_big_align=MMAP_ARENA_ALIGN_36;
int g_overcommit;
u64 g_alias_base,g_alias_size;
so_module game_mod;
/* Retained for generic shim objects that expose optional Unity-oriented dlsym paths. */
so_module main_mod,unity_mod,il2cpp_mod;
extern void nx_net_init(void);

void __libnx_initheap(void){
  void *addr=NULL;size_t size=0,total=0,used=0;const size_t MB=1024*1024;
  u64 aslr_base=0,aslr_size=0;if(R_SUCCEEDED(svcGetInfo(&aslr_base,InfoType_AslrRegionAddress,CUR_PROCESS_HANDLE,0))&&R_SUCCEEDED(svcGetInfo(&aslr_size,InfoType_AslrRegionSize,CUR_PROCESS_HANDLE,0))&&(aslr_base>=(1ull<<36)||aslr_size>(1ull<<36)-aslr_base))g_mmap_big_align=MMAP_ARENA_ALIGN_39;
  if(envHasHeapOverride()){addr=envGetHeapOverrideAddr();size=envGetHeapOverrideSize();}
  else{svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0);svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0);if(total>used+2*MB)size=(total-used-2*MB)&~(size_t)0x1fffff;if(!size)size=512*MB;if(R_FAILED(svcSetHeapSize(&addr,size)))diagAbortWithResult(MAKERESULT(Module_Libnx,LibnxError_HeapAllocFailed));}
  size_t so_zone=SO_REGION_BYTES;if(so_zone>size/3)so_zone=size/3;
  size_t arena=(size>512*MB)?MMAP_ARENA_RESERVE:0;size_t align=g_mmap_big_align;
  if(arena&&size<so_zone+arena+192*MB+align)arena=0;
  size_t fake_size=size-so_zone-arena-align;
  extern char *fake_heap_start,*fake_heap_end;fake_heap_start=(char*)addr;fake_heap_end=(char*)addr+fake_size;
  heap_so_base=(void*)ALIGN_MEM((uintptr_t)addr+fake_size,0x1000);heap_so_limit=so_zone;
  if(arena){g_mmap_arena_base=(void*)ALIGN_MEM((uintptr_t)heap_so_base+so_zone,align);g_mmap_arena_size=arena;}
}

static void check_syscalls(void){
  if(!envIsSyscallHinted(0x77))fatal_error("svcMapProcessCodeMemory is unavailable.");
  if(!envIsSyscallHinted(0x78))fatal_error("svcUnmapProcessCodeMemory is unavailable.");
  if(!envIsSyscallHinted(0x73))fatal_error("svcSetProcessMemoryPermission is unavailable.");
  if(envGetOwnProcessHandle()==INVALID_HANDLE)fatal_error("The launcher did not provide an own-process handle. Start the NRO through title takeover.");
}

static int has_suffix(const char *text,const char *suffix){
  const size_t text_len=strlen(text),suffix_len=strlen(suffix);
  return text_len>=suffix_len&&!strcmp(text+text_len-suffix_len,suffix);
}

static const char *game_library_name(void){
  const char *slash=strrchr(GAME_LIBRARY,'/');
  return slash?slash+1:GAME_LIBRARY;
}

/* Accept the layouts produced by common APK extractors: the normal Android
 * lib/arm64-v8a path, a flattened .so, or a split-named subdirectory.  The
 * exact release size prevents an ARMv7/x86 library from being selected. */
static int find_game_library(const char *dir,int depth,char *found,size_t cap){
  if(depth>6)return 0;
  DIR *handle=opendir(dir);
  if(!handle)return 0;
  struct dirent *entry;
  while((entry=readdir(handle))!=NULL){
    const char *name=entry->d_name;
    if(!strcmp(name,".")||!strcmp(name,".."))continue;
    char path[768];
    const int n=!strcmp(dir,".")?snprintf(path,sizeof path,"%s",name):
                                  snprintf(path,sizeof path,"%s/%s",dir,name);
    if(n<0||n>=(int)sizeof path)continue;
    struct stat st;
    if(stat(path,&st)!=0)continue;
    if(S_ISREG(st.st_mode)&&!strcmp(name,game_library_name())&&
       (long long)st.st_size==GAME_LIBRARY_SIZE){
      if(snprintf(found,cap,"%s",path)>=(int)cap){closedir(handle);return 0;}
      closedir(handle);
      return 1;
    }
    if(S_ISDIR(st.st_mode)&&depth<6&&strcmp(name,"assets")&&
       strcmp(name,"res")&&strcmp(name,"META-INF")&&
       find_game_library(path,depth+1,found,cap)){
      closedir(handle);
      return 1;
    }
  }
  closedir(handle);
  return 0;
}

static void prepare_extracted_apk(void){
  struct stat st;
  if(stat(GAME_LIBRARY,&st)==0&&S_ISREG(st.st_mode))return;
  if(mkdir("lib",0777)!=0&&errno!=EEXIST)
    fatal_error("Could not create the extracted APK library directory.");
  if(mkdir("lib/arm64-v8a",0777)!=0&&errno!=EEXIST)
    fatal_error("Could not create the ARM64 library directory.");

  char source[768];
  if(!find_game_library(".",0,source,sizeof source)){
    debug_log("first-boot: %s was not found in the extracted files",game_library_name());
    return;
  }
  if(rename(source,GAME_LIBRARY)!=0)
    fatal_error("Could not move %s into lib/arm64-v8a (errno %d).",source,errno);
  debug_log("first-boot: moved %s to %s",source,GAME_LIBRARY);
}

/* Android plays these through a Java VideoView, but the Switch FFmpeg player
 * needs them after first-boot cleanup removes the rest of res/.  Preserve every
 * MP4 from res/raw in a compact stable directory before that cleanup runs. */
static void preserve_apk_movies(void){
  DIR *raw=opendir("res/raw");if(!raw)return;
  if(mkdir("movies",0777)!=0&&errno!=EEXIST){
    closedir(raw);fatal_error("Could not create the movie directory (errno %d).",errno);
  }
  struct dirent *entry;
  while((entry=readdir(raw))!=NULL){
    const char *name=entry->d_name;
    if(!strcmp(name,".")||!strcmp(name,"..")||!has_suffix(name,".mp4"))continue;
    char source[768],destination[768];
    if(snprintf(source,sizeof source,"res/raw/%s",name)>=(int)sizeof source||
       snprintf(destination,sizeof destination,"movies/%s",name)>=(int)sizeof destination)
      continue;
    struct stat st;if(stat(source,&st)!=0||!S_ISREG(st.st_mode))continue;
    if(access(destination,F_OK)==0)continue;
    if(rename(source,destination)==0)
      debug_log("first-boot: preserved movie %s (%llu bytes)",name,(unsigned long long)st.st_size);
    else
      debug_log("first-boot: could not preserve %s (errno %d)",source,errno);
  }
  closedir(raw);
}

typedef struct CleanupStats{
  unsigned files;
  unsigned dirs;
  uint64_t bytes;
}CleanupStats;

static void remove_file_counted(const char *path,CleanupStats *stats){
  struct stat st;
  if(stat(path,&st)!=0||S_ISDIR(st.st_mode))return;
  if(unlink(path)==0){
    stats->files++;
    if(st.st_size>0)stats->bytes+=(uint64_t)st.st_size;
  }else{
    debug_log("first-boot: could not remove %s (errno %d)",path,errno);
  }
}

static void remove_tree_counted(const char *path,CleanupStats *stats){
  DIR *dir=opendir(path);
  if(!dir){remove_file_counted(path,stats);return;}
  struct dirent *entry;
  while((entry=readdir(dir))!=NULL){
    if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
    char child[768];
    if(snprintf(child,sizeof child,"%s/%s",path,entry->d_name)>=(int)sizeof child)
      continue;
    struct stat st;
    if(stat(child,&st)==0&&S_ISDIR(st.st_mode))
      remove_tree_counted(child,stats);
    else
      remove_file_counted(child,stats);
  }
  closedir(dir);
  if(rmdir(path)==0)stats->dirs++;
}

static int apk_extract_present(void){
  if(access("AndroidManifest.xml",F_OK)==0||access("resources.arsc",F_OK)==0||
     access("classes.dex",F_OK)==0||access("META-INF",F_OK)==0||
     access("res",F_OK)==0)return 1;
  DIR *root=opendir(".");
  if(!root)return 0;
  int found=0;
  struct dirent *entry;
  while((entry=readdir(root))!=NULL&&!found){
    const char *name=entry->d_name;
    found=(!strncmp(name,"classes",7)&&has_suffix(name,".dex"))||
          has_suffix(name,".apk")||has_suffix(name,".properties");
  }
  closedir(root);
  return found;
}

/* Keep loose ZeptoLab game assets and user-created files.  Only known Android
 * bytecode/resources, ad SDK payloads, package archives, and unused ABIs are
 * removed.  This is idempotent, so an interrupted first boot safely resumes. */
static void cleanup_apk_extract(void){
  static const char *const dirs[]={
    "META-INF","res","com","explorestack","google","kotlin","mozilla",
    "okhttp3","org","src","lib/armeabi-v7a","lib/armeabi_v7a",
    "lib/x86","lib/x86_64","lib/x86-64","assets/ad-viewer",
    "assets/audience_network","assets/bm_networks","assets/dexopt",
    "assets/iads","assets/html"
  };
  static const char *const files[]={
    "AndroidManifest.xml","resources.arsc","DebugProbesKt.bin",
    "androidsupportmultidexversion.txt","stamp-cert-sha256",
    "APKM_installer.url","info.json",
    "assets/audience_network.dex","assets/com.moloco.sdk.xenoss.sdkdevkit.mraid.js",
    "assets/favicon.ico","assets/fyb_iframe_endcard_tmpl.html",
    "assets/fyb_static_endcard_tmpl.html","assets/ia_js_load_monitor.txt",
    "assets/ia_mraid_bridge.txt","assets/mbridge_download_dialog_view.xml",
    "assets/mraid-bridge.js","assets/mraid.js","assets/rv_binddatas.xml",
    "assets/sdk_core.min.js","assets/tt_mime_type.pro",
    "assets/pp.htm","assets/pp_de.htm","assets/pp_es.htm","assets/pp_ru.htm",
    "assets/tc.htm","assets/tc_de.htm","assets/tc_es.htm","assets/tc_ru.htm"
  };
  CleanupStats stats={0};
  for(size_t i=0;i<sizeof dirs/sizeof dirs[0];++i){
    if(access(dirs[i],F_OK)==0)debug_log("first-boot: removing %s",dirs[i]);
    remove_tree_counted(dirs[i],&stats);
  }
  for(size_t i=0;i<sizeof files/sizeof files[0];++i)
    remove_file_counted(files[i],&stats);

  DIR *root=opendir(".");
  if(root){
    struct dirent *entry;
    while((entry=readdir(root))!=NULL){
      const char *name=entry->d_name;
      const int dex=!strncmp(name,"classes",7)&&has_suffix(name,".dex");
      const int metadata=has_suffix(name,".properties")||has_suffix(name,".proto");
      const int archive=has_suffix(name,".apk");
      const int flat_lib=has_suffix(name,".so");
      if(!dex&&!metadata&&!archive&&!flat_lib)continue;
      remove_file_counted(name,&stats);
    }
    closedir(root);
  }

  DIR *libs=opendir("lib/arm64-v8a");
  if(libs){
    struct dirent *entry;
    while((entry=readdir(libs))!=NULL){
      const char *name=entry->d_name;
      if(!strcmp(name,".")||!strcmp(name,"..")||!strcmp(name,game_library_name()))
        continue;
      char path[768];
      if(snprintf(path,sizeof path,"lib/arm64-v8a/%s",name)>=(int)sizeof path)
        continue;
      remove_tree_counted(path,&stats);
    }
    closedir(libs);
  }
  debug_log("first-boot: cleanup removed %u files, %u directories, %llu bytes",
            stats.files,stats.dirs,(unsigned long long)stats.bytes);
}

static uint64_t now_ns(void){struct timespec ts={0};clock_gettime(CLOCK_MONOTONIC,&ts);return(uint64_t)ts.tv_sec*1000000000ull+(uint64_t)ts.tv_nsec;}
static void validate_data(void){
  struct stat st;if(stat(GAME_LIBRARY,&st)!=0||!S_ISREG(st.st_mode))fatal_error("Missing %s. Extract both base.apk and split_config.arm64_v8a.apk into %s, merging their folders.",GAME_LIBRARY,GAME_HOME);
  if((long long)st.st_size!=GAME_LIBRARY_SIZE)fatal_error("Unsupported %s (%lld bytes; expected %lld).",GAME_LIBRARY,(long long)st.st_size,(long long)GAME_LIBRARY_SIZE);
#if ZF_RENDERER_API == 1
  const char *required="assets/640x960/startup/splash_logo.pb";
#else
  const char *required="assets/builtInShaders/shaders.json";
#endif
  if(stat(required,&st)!=0||!S_ISREG(st.st_mode))fatal_error("Missing extracted game asset: %s",required);
  debug_log("data: validated %s and %s",GAME_LIBRARY,required);
}
static void load_game(void){
  debug_log("loader: reading %s",GAME_LIBRARY);int rc=so_load(&game_mod,GAME_LIBRARY,heap_so_base,heap_so_limit);if(rc<0)fatal_error("Could not load %s (loader error %d).",GAME_LIBRARY,rc);
  snprintf(game_mod.name,sizeof game_mod.name,"%s",GAME_LIBRARY);debug_log("loader: image=%u bytes mapped=%u bytes at %p",(unsigned)game_mod.so_size,(unsigned)game_mod.load_size,game_mod.load_virtbase);
  resolve_imports(&game_mod);so_finalize(&game_mod);so_flush_caches(&game_mod);
  static uint8_t main_tls[BIONIC_TLS_SIZE] __attribute__((aligned(16)));install_bionic_tls(main_tls);
  so_execute_init_array(&game_mod);so_free_temp(&game_mod);debug_log("loader: relocations and constructors complete");
}
static void *require_export(const char *name){uintptr_t p=so_try_find_addr_rx(&game_mod,name);if(!p)fatal_error("Required game export is missing: %s",name);debug_log("loader: %s = %p",name,(void*)p);return(void*)p;}

typedef struct {EGLDisplay d;EGLSurface s;EGLContext c;} Gfx;

static void gfx_init(Gfx *g){
  memset(g,0,sizeof *g);
  debug_log("egl: getting default display");
  g->d=eglGetDisplay(EGL_DEFAULT_DISPLAY);
  debug_log("egl: display=%p",(void*)g->d);
  if(g->d==EGL_NO_DISPLAY)fatal_error("eglGetDisplay failed (0x%x).",eglGetError());

  EGLint major=0,minor=0;
  debug_log("egl: initializing display");
  if(!eglInitialize(g->d,&major,&minor)){
    const EGLint error=eglGetError();eglTerminate(g->d);g->d=EGL_NO_DISPLAY;
    fatal_error("eglInitialize failed (0x%x).",error);
  }
  debug_log("egl: initialized EGL %d.%d",major,minor);

  if(!eglBindAPI(EGL_OPENGL_ES_API)){
    const EGLint error=eglGetError();eglTerminate(g->d);g->d=EGL_NO_DISPLAY;
    fatal_error("eglBindAPI failed (0x%x).",error);
  }
  const EGLint attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig ec;EGLint n=0;
  debug_log("egl: choosing GLES2 window config");
  if(!eglChooseConfig(g->d,attrs,&ec,1,&n)||n<1){
    const EGLint error=eglGetError();eglTerminate(g->d);g->d=EGL_NO_DISPLAY;
    fatal_error("No compatible GLES2 EGL config (0x%x).",error);
  }

  debug_log("egl: preparing portrait NWindow %ux%u",android_native_width(),android_native_height());
  EGLNativeWindowType window=(EGLNativeWindowType)android_native_window();
  debug_log("egl: creating window surface from %p",(void*)window);
  g->s=eglCreateWindowSurface(g->d,ec,window,NULL);
  if(g->s==EGL_NO_SURFACE){
    const EGLint error=eglGetError();eglTerminate(g->d);g->d=EGL_NO_DISPLAY;
    fatal_error("eglCreateWindowSurface failed (0x%x).",error);
  }
  debug_log("egl: surface=%p",(void*)g->s);

  const EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
  debug_log("egl: creating GLES2 context");
  g->c=eglCreateContext(g->d,ec,EGL_NO_CONTEXT,ca);
  if(g->c==EGL_NO_CONTEXT){
    const EGLint error=eglGetError();eglDestroySurface(g->d,g->s);eglTerminate(g->d);
    g->s=EGL_NO_SURFACE;g->d=EGL_NO_DISPLAY;
    fatal_error("eglCreateContext failed (0x%x).",error);
  }
  debug_log("egl: context=%p; making current",(void*)g->c);
  if(!eglMakeCurrent(g->d,g->s,g->s,g->c)){
    const EGLint error=eglGetError();eglDestroyContext(g->d,g->c);
    eglDestroySurface(g->d,g->s);eglTerminate(g->d);
    g->c=EGL_NO_CONTEXT;g->s=EGL_NO_SURFACE;g->d=EGL_NO_DISPLAY;
    fatal_error("eglMakeCurrent failed (0x%x).",error);
  }
  if(!eglSwapInterval(g->d,1))debug_log("egl: swap interval failed (0x%x)",eglGetError());
  debug_log("egl: GLES2 portrait surface ready %ux%u",android_native_width(),android_native_height());
}
static void gfx_shutdown(Gfx *g){if(g->d&&g->d!=EGL_NO_DISPLAY){eglMakeCurrent(g->d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(g->c&&g->c!=EGL_NO_CONTEXT)eglDestroyContext(g->d,g->c);if(g->s&&g->s!=EGL_NO_SURFACE)eglDestroySurface(g->d,g->s);eglTerminate(g->d);}}

int main(int argc,char **argv){(void)argc;(void)argv;
  startup_status_begin("Preparing " GAME_TITLE);
  if(chdir(GAME_HOME)!=0)fatal_error("Could not open %s. Put the NRO and extracted APK files in that folder.",GAME_HOME);
  debug_init();debug_log("boot: application start");check_syscalls();
  int cfg=read_config(GAME_HOME "/" CONFIG_NAME);if(cfg!=0)write_config(GAME_HOME "/" CONFIG_NAME);if(config.portrait!=1&&config.portrait!=2)config.portrait=DEFAULT_PORTRAIT;
  const int first_boot=apk_extract_present();
  if(first_boot)startup_status_update("Preparing extracted APK (first boot)...");
  prepare_extracted_apk();
  preserve_apk_movies();
  startup_status_update("Validating extracted APK...");validate_data();
  if(first_boot)startup_status_update("Removing unused Android files (first boot)...");
  cleanup_apk_extract();
  android_native_update_mode();screen_width=(int)android_native_width();screen_height=(int)android_native_height();
  startup_status_update("Starting platform services...");nx_net_init();SDL_SetMainReady();if(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER)<0)fatal_error("SDL_Init failed: %s",SDL_GetError());zf_audio_init();ctr_video_init();jni_init();
  startup_status_update("Loading the Android ARM64 engine...");
  debug_log("display: releasing startup console before installing Bionic TLS");
  startup_status_end();
  debug_log("display: startup console released");
  load_game();
  typedef int(*onload_fn)(void*,void*);onload_fn onload=(onload_fn)require_export("JNI_OnLoad");int jv=onload(fake_vm,NULL);debug_log("jni: JNI_OnLoad returned 0x%x",jv);if(jv!=0x00010006&&jv!=0x00010004)fatal_error("JNI_OnLoad failed (0x%x).",jv);
  Gfx gfx;gfx_init(&gfx);void *renderer=jni_make_object(
#if ZF_RENDERER_API == 1
    "com/zeptolab/zframework/ZRenderer"
#else
    "com/zf/ZRenderer"
#endif
  );void *manager=jni_make_object(
#if ZF_RENDERER_API == 1
    "com/zeptolab/zframework/ZJNIManager"
#else
    "com/zf/ZJNIManager"
#endif
  );
  typedef void(*v0)(void*,void*);typedef void(*vtouch)(void*,void*,float,float,int,int);typedef uint8_t(*vbutton)(void*,void*);
  vtouch pass_touch;vbutton back;v0 pause_fn,resume_fn,destroy_fn,playback_finished_fn=NULL;
#if ZF_RENDERER_API == 1
  typedef void(*vinit)(void*,void*,void*);typedef void(*vresize)(void*,void*,int,int);typedef void(*vtick)(void*,void*,float);
  vinit init=(vinit)require_export("Java_com_zeptolab_zframework_ZRenderer_nativeInit");v0 surface=(v0)require_export("Java_com_zeptolab_zframework_ZRenderer_nativeSurfaceCreated");vresize resize=(vresize)require_export("Java_com_zeptolab_zframework_ZRenderer_nativeResize");vtick tick=(vtick)require_export("Java_com_zeptolab_zframework_ZRenderer_nativeTick");v0 render=(v0)require_export("Java_com_zeptolab_zframework_ZRenderer_nativeRender");
  pass_touch=(vtouch)require_export("Java_com_zeptolab_zframework_ZRenderer_nativePassTouch");back=(vbutton)require_export("Java_com_zeptolab_zframework_ZRenderer_nativeBackPressed");pause_fn=(v0)require_export("Java_com_zeptolab_zframework_ZRenderer_nativePause");resume_fn=(v0)require_export("Java_com_zeptolab_zframework_ZRenderer_nativeResume");destroy_fn=(v0)require_export("Java_com_zeptolab_zframework_ZRenderer_nativeDestroy");playback_finished_fn=(v0)require_export("Java_com_zeptolab_zframework_ZRenderer_nativePlaybackFinished");
  debug_log("lifecycle: nativeInit");init(fake_env,renderer,manager);surface(fake_env,renderer);resize(fake_env,renderer,(int)android_native_width(),(int)android_native_height());resume_fn(fake_env,renderer);
#else
  typedef void(*vcreate)(void*,void*,void*,void*);typedef void(*vchanged)(void*,void*,int64_t,int64_t);typedef void(*vdraw)(void*,void*,int64_t);
  vcreate create=(vcreate)require_export("Java_com_zf_ZRenderer_nativeViewCreated");v0 surface=(v0)require_export("Java_com_zf_ZRenderer_nativeSurfaceCreated");vchanged changed=(vchanged)require_export("Java_com_zf_ZRenderer_nativeSurfaceChanged");vdraw draw=(vdraw)require_export("Java_com_zf_ZRenderer_nativeDrawFrame");
  pass_touch=(vtouch)require_export("Java_com_zf_ZRenderer_nativePassTouch");back=(vbutton)require_export("Java_com_zf_ZRenderer_nativeBackPressed");pause_fn=(v0)require_export("Java_com_zf_ZRenderer_nativeOnPause");resume_fn=(v0)require_export("Java_com_zf_ZRenderer_nativeOnResume");destroy_fn=(v0)require_export("Java_com_zf_ZRenderer_nativeOnDestroy");
  debug_log("lifecycle: nativeViewCreated");create(fake_env,renderer,manager,jni_make_object("android/content/res/AssetManager"));surface(fake_env,renderer);changed(fake_env,renderer,(int64_t)android_native_width(),(int64_t)android_native_height());resume_fn(fake_env,renderer);
#endif
  zf_input_init();
#if ZF_RENDERER_API == 1
  uint64_t last=now_ns();
#endif
  while(appletMainLoop()&&!jni_quit_requested&&zf_input_update(pass_touch,back,fake_env,renderer)){
    pthread_compat_reap();
    ctr_video_update();
    jni_process_offline_services();
    if(playback_finished_fn&&ctr_video_take_completion()){
      debug_log("jni/video: delivering nativePlaybackFinished on renderer thread");
      playback_finished_fn(fake_env,renderer);
    }
    uint64_t now=now_ns();
#if ZF_RENDERER_API == 1
    float dt=(float)(now-last)/1000000000.0f;if(dt<=0||dt>0.25f)dt=1.0f/60.0f;tick(fake_env,renderer,dt);render(fake_env,renderer);last=now;
#else
    draw(fake_env,renderer,(int64_t)(now/1000000ull));
#endif
    ctr_video_render();
    if(!ctr_video_is_active())zf_input_draw_cursor();
    if(!eglSwapBuffers(gfx.d,gfx.s)){debug_log("egl: swap failed 0x%x",eglGetError());break;}
  }
  pause_fn(fake_env,renderer);ctr_video_shutdown();destroy_fn(fake_env,renderer);pthread_compat_reap();unity_jni_flush();zf_audio_shutdown();gfx_shutdown(&gfx);SDL_Quit();socketExit();debug_shutdown();
  extern void NX_NORETURN __libnx_exit(int rc);__libnx_exit(0);return 0;
}
