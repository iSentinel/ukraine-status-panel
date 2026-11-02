#pragma once
#include "FreeRTOS.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cassert>
#include <chrono>
struct HostTask {std::thread thread;};
using TaskHandle_t=HostTask *;
namespace host_tasks {
inline std::mutex mutex;
inline std::condition_variable changed;
inline bool released=false,fail_create=false;
inline bool pause_requested=false,at_boundary=false;
inline std::vector<HostTask *> tasks;
inline void release() {
  {std::lock_guard<std::mutex> lock(mutex);released=true;}
  changed.notify_all();
}
/** Stop the host worker between iterations, without locking production state. */
inline void pause_at_boundary() {
  std::unique_lock<std::mutex> lock(mutex);pause_requested=true;
  assert(changed.wait_for(lock,std::chrono::seconds(2),[]{return at_boundary;}));
}
inline void resume() {
  {std::lock_guard<std::mutex> lock(mutex);pause_requested=false;}
  changed.notify_all();
}
inline void boundary() {
  std::unique_lock<std::mutex> lock(mutex);at_boundary=true;changed.notify_all();
  changed.wait(lock,[]{return !pause_requested;});at_boundary=false;
}

inline void join_all() {
  release();
  for(auto *task:tasks){task->thread.join();delete task;}
  tasks.clear();released=false;pause_requested=false;at_boundary=false;
}
}
namespace esphome {
inline void host_release_tasks() {host_tasks::release();}
}
inline BaseType_t xTaskCreatePinnedToCoreWithCaps(void (*entry)(void *),const char *,uint32_t stack,
                                                void *argument,unsigned priority,TaskHandle_t *output,int core,unsigned caps) {
  assert(stack==12288 && priority==1 && core==0 && caps==6);
  if(host_tasks::fail_create)return 0;
  auto *task=new HostTask;
  *output=task;
  task->thread=std::thread([=](){
    {std::unique_lock<std::mutex> lock(host_tasks::mutex);
     host_tasks::changed.wait(lock,[]{return host_tasks::released;});}
    entry(argument);
  });
  host_tasks::tasks.push_back(task);return pdPASS;
}
inline void vTaskDelay(uint32_t) {
  host_tasks::boundary();
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
inline void vTaskDeleteWithCaps(void *) {}
