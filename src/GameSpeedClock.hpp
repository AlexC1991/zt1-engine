#ifndef GAME_SPEED_CLOCK_HPP
#define GAME_SPEED_CLOCK_HPP

#include <algorithm>
#include <chrono>
#include <cmath>

// ours: bounded fast-forward timing. Normal 1x retains its original variable
// update. Accelerated movement uses at most 1/60s per update; camera/input get
// real time once per frame. This backend is currently selected by dev speed.
class GameSpeedClock {
public:
  static constexpr double tickSeconds=1.0/60,backlogLimit=2.0;
  static constexpr int stepLimit=64;
  struct Frame { int steps=0;double simulated=0;bool limited=false; };
  double pending()const{return debt;}
  double discarded()const{return dropped;}
  void clearPending(){debt=0;}
  void reset(){debt=dropped=0;lastSpeed=1;}

  // callback(simulation seconds, real control seconds) returns false when a
  // modal or event paused the game inside a tick. No catch-up on its dismissal.
  template<class Update>Frame advance(double realSeconds,float requested,bool paused,Update update) {
    Frame frame;
    if(!std::isfinite(realSeconds) || realSeconds<0)realSeconds=0;
    const float speed=std::isfinite(requested)?std::clamp(requested,.1f,20.f):1.f;
    if(speed!=lastSpeed){clearPending();lastSpeed=speed;}
    if(paused){clearPending();update(0,float(std::min(realSeconds,.25)));return frame;}
    if(speed<=1) {
      clearPending();const float dt=float(realSeconds*speed);
      update(dt,float(realSeconds));frame.steps=dt>0;frame.simulated=dt;return frame;
    }
    // A blocked window must not jump minutes on resuming. Keep backlog bounded
    // and report discarded game seconds to diagnostics rather than hiding it.
    const double accepted=std::min(realSeconds,.25);
    dropped+=(realSeconds-accepted)*speed;debt+=accepted*speed;
    if(debt>backlogLimit){dropped+=debt-backlogLimit;debt=backlogLimit;frame.limited=true;}
    const auto start=std::chrono::steady_clock::now();
    while(debt+1e-9>=tickSeconds && frame.steps<stepLimit) {
      const float controls=frame.steps==0?float(accepted):0.f;
      debt=std::max(0.,debt-tickSeconds);++frame.steps;frame.simulated+=float(tickSeconds);
      if(!update(float(tickSeconds),controls)){clearPending();return frame;}
      if(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>=.008)break;
    }
    if(!frame.steps)update(0,float(accepted));
    frame.limited |= debt+1e-9>=tickSeconds;
    return frame;
  }
private:
  double debt=0,dropped=0;
  float lastSpeed=1;
};

#endif
