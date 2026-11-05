#include "lab_api.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <string>
// addition: resumable local art generation and progress. Attaching the monitor
// does not restart the worker, change normal mod switches or clear the Test Zoo.
LAB_EXPORT const char *lab_about(){return "Generate native map art at 4x with separate emission maps and resumable batches";}
namespace {
int job=-1;HANDLE attached=nullptr;
struct CloseMonitor {~CloseMonitor(){if(attached)CloseHandle(attached);}} closeMonitor;
const char *root="E:/zt1-shots/hd-lighting-batch-2026-11-03";
std::string status(){std::ifstream file(std::string(root)+"/full-work/status.json");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
double number(const std::string &s,const char *key){auto n=s.find(std::string("\"")+key+"\"");if(n==std::string::npos)return 0;n=s.find(':',n);return n==std::string::npos?0:std::strtod(s.c_str()+n+1,nullptr);}
std::string value(const std::string &s,const char *key){auto n=s.find(std::string("\"")+key+"\"");if(n==std::string::npos)return {};n=s.find(':',n);n=s.find('"',n);if(n==std::string::npos)return {};auto end=s.find('"',n+1);return end==std::string::npos?std::string{}:s.substr(n+1,end-n-1);}
void progress(Lab *lab){const auto s=status();if(s.empty())return;const auto stage=value(s,"stage"),prefix=value(s,"prefix");
  lab->watch("HD frames verified",number(s,"frames_done"),"of 1,030,934 surveyed frames; checkpointed after finishing");
  // measured: --hd-survey over this native inventory, 3 November 2026.
  lab->watch("Verified percent",100*number(s,"frames_done")/1030934,"verified frames divided by native survey frames");
  lab->watch("Emission maps",number(s,"layers_done"),"separate baked material layers");
  lab->watch("Completed batches",number(s,"chunks_done"),"durable frame checkpoints");
  lab->watch("Current chunk upscaled",number(s,"chunk_upscaled"),(stage+": "+prefix).c_str());
  lab->watch("Elapsed hours",(std::time(nullptr)-number(s,"started"))/3600,"current worker session; remaining duration varies with artwork size");
}
}
LAB_EXPORT void lab_start(Lab *lab){
  progress(lab);
  // addition: a new monitor can attach to the owned batch without replacing
  // its process or disturbing an active GPU chunk.
  std::ifstream lock(std::string(root)+"/full-work/running.lock");DWORD pid=0;lock>>pid;
  if(pid){attached=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(attached && WaitForSingleObject(attached,0)==WAIT_TIMEOUT){job=-2;lab->log("Monitoring the existing HD worker (PID %lu). The current chunk continues.",static_cast<unsigned long>(pid));return;}
    if(attached){CloseHandle(attached);attached=nullptr;}
  }
  lab->log("HD art and lighting batch. Progress: %s/full-work/status.json",root);
  lab->log("Colour and emission stay separate; sun, shadows and fluorescence remain dynamic. The preview mod stays off.");
  SECURITY_ATTRIBUTES security{sizeof security,nullptr,TRUE};
  HANDLE previous=GetStdHandle(STD_INPUT_HANDLE);HANDLE input=CreateFileA("NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
  SetStdHandle(STD_INPUT_HANDLE,input);
  job=lab->start("C:/Users/batty/AppData/Local/Programs/Python/Python312/python.exe",
    "-I -S -u B:/GitHub/zt1-engine/re/lab/hd_batch.py --engine E:/zt1-shots/hd-lighting-batch-2026-11-03/tool-v2/zt1-engine.exe --cwd B:/GitHub/zt1-engine/build-x64/Release --upscaler E:/zt1-shots/tools/realesrgan/realesrgan-ncnn-vulkan.exe --work E:/zt1-shots/hd-lighting-batch-2026-11-03/full-work --output E:/zt1-hd-packs/ZT1-HD-Lighting-Preview",
    "B:/GitHub/zt1-engine");
  SetStdHandle(STD_INPUT_HANDLE,previous);CloseHandle(input);
  lab->expect("HD batch worker started",job>=0);
}
LAB_EXPORT bool lab_step(Lab *lab,int step){
  if(step%30==0)progress(lab);
  if(job==-2){if(WaitForSingleObject(attached,0)==WAIT_TIMEOUT)return true;CloseHandle(attached);attached=nullptr;progress(lab);const auto s=status();
    lab->expect("HD generation completed",value(s,"state")=="complete");lab->log("Worker finished: %s. Read status.json for completion, a space stop or an error.",value(s,"state").c_str());return false;}
  if(job<0)return false;
  if(lab->running(job))return true;
  progress(lab);lab->expect("HD batch completed",lab->exitCode(job)==0);
  lab->log("Worker exited %d. Read status.json for completion, a space stop or an error. Normal mod switches are unchanged.",lab->exitCode(job));return false;
}
