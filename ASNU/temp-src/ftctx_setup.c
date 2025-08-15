#include "flanterm.h"

struct flanterm_context* global_ft_ctx;

void SetGlobalFtCtx(struct flanterm_context *ctx) {
    global_ft_ctx = ctx;
}

struct flanterm_context *GetGlobalFtCtx() {
    return global_ft_ctx;
}
