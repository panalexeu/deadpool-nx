// deadpool-nx: profile collection of the PGO-instrumented build (PGO=generate), see deadpool_pgo.cpp. In every other
// build these do nothing.
#pragma once

namespace deadpool::pgo {
// Picks this session's file (pgo/deadpool_pgo_<N>.stream next to the NRO) and starts the 2-minute dumps.
void Start();
// Writes every count so far to this session's file (the whole session each time, replacing the previous dump).
void DumpNow();
// The last dump, then the dump thread ends.
void Stop();
}  // namespace deadpool::pgo
