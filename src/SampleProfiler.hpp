#ifndef SAMPLE_PROFILER_HPP
#define SAMPLE_PROFILER_HPP

// ours: a sampling profiler for finding where the frame time goes, for
// working on the engine's speed (not part of the game). Off unless asked
// for: ZT_SAMPLE_PROFILE=<seconds> samples the calling thread every
// millisecond, from ZT_SAMPLE_DELAY seconds on (default 25), and writes the
// busiest functions (their own time, and with what they call) to
// ZT_SAMPLE_PROFILE_OUT (default <ZT_GAME_BENCH_OUT>/samples.txt).
// 64-bit Windows only; elsewhere it does nothing.
namespace SampleProfiler {
void startFromEnvironment();
}

#endif
