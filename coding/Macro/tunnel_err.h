/*
 * @Copyright:  Fisec Information Technology Co., Ltd.
 * @Author: lrz0919
 * @Date: 2025-12-31 10:51:39
 * @LastEditors: Do not edit
 * @LastEditTime: 2025-12-31 15:55:31
 * @Description: x宏形式的错误输出
 * @FilePath: /ssl-config-manage/include/tunnel_err.h
 */
#ifndef TUNNEL_ERR_H
#define TUNNEL_ERR_H


#define BASE_GEN   0x00000000        // 通用错误从      1 开始 
#define BASE_DB    0x00001000        // 数据库错误从 4096 开始

/* --- 解析错误 (Generic) --- */
#define ERR_TABLE_GEN(X) \
    X(TUNNEL_OK,               BASE_GEN + 0,  "Success",   "[GENERIC]") \
    X(TUNNEL_ERR_PARAM,        BASE_GEN + 1,  "Invalid parameters",   "[GENERIC]") \
    X(TUNNEL_ERR_ALLOC,        BASE_GEN + 2, "Memory allocation failed",   "[GENERIC]") \
    X(TUNNEL_ERR_JSON,         BASE_GEN + 3, "json parse error",   "[GENERIC]") \
    X(TUNNEL_ERR_UNKNOWN,      BASE_GEN + 4, "Unknown error",   "[GENERIC]") \


/* --- 数据库错误 (Database) --- */
#define ERR_TABLE_DB(X) \
    X(TUNNEL_DB_OPEN_FAIL,       BASE_DB + 1,   "Failed to open database",   "[DATABASE]") \
    X(TUNNEL_DB_QUERY_FAIL,      BASE_DB + 2,   "SQL query execution failed",   "[DATABASE]") \
    X(TUNNEL_DB_LOCKED,          BASE_DB + 3,   "Database is locked",   "[DATABASE]") \
    X(TUNNEL_ERR_SQL_PREPARE,    BASE_DB + 4, "SQL statement preparation failed",   "[DATABASE]") \
    X(TUNNEL_ERR_SQL_BIND,       BASE_DB + 5, "Failed to bind parameters to SQL",   "[DATABASE]") \
    X(TUNNEL_ERR_SQL_EXEC,       BASE_DB + 6, "SQL execution failed (step error)",   "[DATABASE]") \
    X(TUNNEL_ERR_TRANSACTION,    BASE_DB + 7, "Transaction failed (Begin/Commit/Rollback error)",   "[DATABASE]") \



#define ALL_ERRORS(X) \
    ERR_TABLE_GEN(X) \
    ERR_TABLE_DB(X) \


typedef enum {
    #define X(name, code, msg, tag) name = code,
    ALL_ERRORS(X)
    #undef X
} TunnelErrorCode;


char* TunnelStrError(TunnelErrorCode err);

#endif 