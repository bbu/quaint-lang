#pragma once

struct codegen_obj;

int exec(const struct codegen_obj *);

enum {
    EXEC_OK = 0,
    EXEC_NOMEM,
    EXEC_ILLEGAL,
};
