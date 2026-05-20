// channels/channels.inc — X-macro list of all upload channels
// Two inclusion modes:
//   1. Define CHANNEL_INCLUDES before including → expands to #include directives
//   2. Define CHANNEL(Ns, M) before including    → expands per-channel code
//
// Ns: namespace (e.g. Telegram → TelegramChannel), display name for logging
// M:  member name in Config, INI toggle key and section name
//
// When adding a new channel:
//   1. Create channels/xxx.hpp and channels/xxx.cpp
//   2. Add one line to BOTH sections below

// --- Section 1: includes ---
#ifdef CHANNEL_INCLUDES
#include "channels/telegram.hpp"
#include "channels/ntfy.hpp"
#include "channels/discord.hpp"
#include "channels/immich.hpp"

// --- Section 2: X-macro entries ---
#else
#ifndef CHANNEL
#error Define CHANNEL() before including channels.inc
#endif

CHANNEL(Telegram, telegram)
CHANNEL(Ntfy, ntfy)
CHANNEL(Discord, discord)
CHANNEL(Immich, immich)

#endif
