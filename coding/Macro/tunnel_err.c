/*
 * @Copyright:  Fisec Information Technology Co., Ltd.
 * @Author: lrz0919
 * @Date: 2025-12-31 10:52:23
 * @LastEditors: Do not edit
 * @LastEditTime: 2025-12-31 15:55:22
 * @Description: what this
 * @FilePath: /ssl-config-manage/source/tunnel_err.c
 */
#include "tunnel_err.h"
#include <stddef.h>

char* TunnelStrError(TunnelErrorCode err) {
    switch (err) {
        #define X(name, code, msg, tag) case name: return msg;
        ALL_ERRORS(X)
        #undef X
        default: return "Undefined error code";
    }
}