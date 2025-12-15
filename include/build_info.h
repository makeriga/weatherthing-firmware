#pragma once

#ifndef WT_FW_GIT_SHA
#define WT_FW_GIT_SHA "unknown"
#endif

#ifndef WT_FW_GIT_SHA_SHORT
#define WT_FW_GIT_SHA_SHORT "unknown"
#endif

#ifndef WT_FW_BUILD_DATE
#define WT_FW_BUILD_DATE "unknown"
#endif

#ifndef WT_FW_GIT_DIRTY
#define WT_FW_GIT_DIRTY 0
#endif

static inline const char* wt_fw_git_sha() { return WT_FW_GIT_SHA; }
static inline const char* wt_fw_git_sha_short() { return WT_FW_GIT_SHA_SHORT; }
static inline const char* wt_fw_build_date() { return WT_FW_BUILD_DATE; }
static inline bool wt_fw_git_dirty() { return WT_FW_GIT_DIRTY != 0; }
