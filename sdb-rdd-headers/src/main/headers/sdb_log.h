#ifndef SDB_LOG_H_

#define SDB_LOG_H_

#include <time.h>

#if defined(_SDB_OS_LINUX_)
   #include <unistd.h>
   #include <pthread.h>
#endif

#include "hbapi.h"

#include "cfl_types.h"
#include "cfl_list.h"
#include "cfl_str.h"
#include "cfl_process.h"

#include "sdb_defs.h"

extern clock_t _sdbTraceLastClock;

extern unsigned int __sdbLoglevel;

#define SDB_GET_PID       cfl_process_getId()
#define SDB_GET_THREAD_ID cfl_thread_id()

#define SDB_LOG_LEVEL_OFF       0
#define SDB_LOG_LEVEL_ERROR     1
#define SDB_LOG_LEVEL_WARN      2
#define SDB_LOG_LEVEL_INFO      3
#define SDB_LOG_LEVEL_DEBUG     4
#define SDB_LOG_LEVEL_TRACE     5

#define SDB_LOG_FORMAT_DEFAULT 0
#define SDB_LOG_FORMAT_GELF    1

#define BOOL_STR(b) (b ? "true" : "false")

//#define SDB_DISABLE_LOG

#ifdef SDB_DISABLE_LOG

   #define TRACE_OUT_FILE(x)
   #define TRACE_BUFFER_HEX(m,s,l)
   #define ITEM_STR(i)
   #define LOG_OUT_FILE(l, x)
   #define DEBUG(x)

   #define SDB_LOG_ERROR(x)
   #define SDB_LOG_WARN(x)
   #define SDB_LOG_INFO(x)
   #define SDB_LOG_DEBUG(x)
   #define SDB_LOG_TRACE(x)
   #define TRACE(x)
   #define ENTER_FUNP(x)
   #define ENTER_FUN
   #define ENTER_FUN_NAME(n)
   #define RETURN  return

#else
   #define TRACE_OUT_FILE(x) do { \
                                FILE *_f = fopen(sdb_log_getPathName(), "a"); \
                                CFL_STRP _out; \
                                CFL_LISTP _freePtrs = cfl_list_new(10); \
                                CFL_UINT32 _i; \
                                struct tm *tm; \
                                float clockDiff = ((float)(clock() - _sdbTraceLastClock))/CLOCKS_PER_SEC; \
                                tm = sdb_log_localtime(); \
                                fprintf(_f,"\n%d/%d - %d/%d/%d %d:%d:%d(%f) - %s(%d):", SDB_GET_PID, SDB_GET_THREAD_ID, tm->tm_mday, tm->tm_mon + 1, 1900 + tm->tm_year, tm->tm_hour, tm->tm_min, tm->tm_sec, clockDiff, (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__), __LINE__); \
                                _out = sdb_log_formatLog x; \
                                fprintf(_f, cfl_str_getPtr(_out)); \
                                fclose(_f); \
                                cfl_str_free(_out); \
                                for(_i = 0; _i < cfl_list_length(_freePtrs); _i++) hb_xfree((char *)cfl_list_get(_freePtrs, _i)); \
                                cfl_list_free(_freePtrs);\
                                _sdbTraceLastClock = clock(); \
                             } while( 0 )

   #define TRACE_BUFFER_HEX(m,s,l) do { \
                                      FILE *_f = fopen(sdb_log_getPathName(), "a"); \
                                      CFL_UINT32 _i; \
                                      struct tm *tm; \
                                      tm = sdb_log_localtime(); \
                                      fprintf(_f,"\n%d - %d/%d/%d %d:%d:%d - %s(%d): %s", SDB_GET_PID, tm->tm_mday, tm->tm_mon + 1, 1900 + tm->tm_year, tm->tm_hour, tm->tm_min, tm->tm_sec, (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__), __LINE__, m); \
                                      for (_i=0;_i<l;_i++) { \
                                         fprintf(_f,"%02x", ((char *)s)[_i]); \
                                      } \
                                      fclose(_f); \
                                   } while( 0 )

   #define ITEM_STR(i) sdb_log_traceItem(_freePtrs, i)

   #define LOG_OUT_FILE(l, x) do { \
                              CFL_STRP _out; \
                              CFL_LISTP _freePtrs = cfl_list_new(10); \
                              _out = sdb_log_formatLog x; \
                              sdb_log_write(l, _out, _freePtrs); \
                           } while (0)

   #define DEBUG(x) TRACE_OUT_FILE(x)

   #define DBG_OUT(x) hb_gtOutStd(x, sizeof(x));getch()

   #ifndef __func__
      #ifdef __FUNCTION__
         #define __func__ __FUNCTION__
      #elif __FUNC__
         #define __func__ __FUNC__
      #else
         #ifdef __linux__
            #define __func__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') : __FILE__)
         #else
            #define __func__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') : __FILE__)
         #endif
      #endif
   #endif

   #define SDB_LOG_ERROR(x) if (__sdbLoglevel >= SDB_LOG_LEVEL_ERROR) LOG_OUT_FILE("ERROR", x)
   #define SDB_LOG_WARN(x) if (__sdbLoglevel >= SDB_LOG_LEVEL_WARN) LOG_OUT_FILE("WARN", x)
   #define SDB_LOG_INFO(x) if (__sdbLoglevel >= SDB_LOG_LEVEL_INFO) LOG_OUT_FILE("INFO", x)
   #define SDB_LOG_DEBUG(x) if (__sdbLoglevel >= SDB_LOG_LEVEL_DEBUG) LOG_OUT_FILE("DEBUG", x)

   #ifdef SDB_DISABLE_TRACE
      #define SDB_LOG_TRACE(x)
      #define TRACE(x)
      #define ENTER_FUNP(x)
      #define ENTER_FUN_NAME(n)
      #define ENTER_FUN
      #define RETURN return
   #else
      #define SDB_LOG_TRACE(x) if (__sdbLoglevel >= SDB_LOG_LEVEL_TRACE) LOG_OUT_FILE("TRACE", x)
      #define TRACE(x)

      #define ENTER_FUNP(x) if (__sdbLoglevel >= SDB_LOG_LEVEL_TRACE) do { \
                              CFL_STRP _out; \
                              CFL_LISTP _freePtrs = cfl_list_new(10); \
                              _out = sdb_log_formatLog x; \
                              sdb_log_writeEnter(__func__, __LINE__, _out, _freePtrs); \
                           } while (0)

      #define ENTER_FUN_NAME(n) if (__sdbLoglevel >= SDB_LOG_LEVEL_TRACE) sdb_log_writeEnter(n, __LINE__, NULL, NULL)
      #define ENTER_FUN         if (__sdbLoglevel >= SDB_LOG_LEVEL_TRACE) sdb_log_writeEnter(__func__, __LINE__, NULL, NULL)

      #define RETURN if (__sdbLoglevel >= SDB_LOG_LEVEL_TRACE) { \
                        sdb_log_writeExit(__func__, __LINE__); \
                     } \
                     return
   #endif
#endif

#define sdb_log_isLevelActive(l) (__sdbLoglevel >= (l))

extern CFL_STRP sdb_log_formatLog(const char * format, ...);
extern int sdb_log_getLevel(void);
extern void sdb_log_setLevel(int level);
extern void sdb_log_write(char *levelName, CFL_STRP out, CFL_LISTP freePtrs);
extern void sdb_log_writeEnter(const char *funName, CFL_UINT32 line, CFL_STRP out, CFL_LISTP freePtrs);
extern void sdb_log_writeExit(const char *funName, CFL_UINT32 line);
extern struct tm *sdb_log_localtime(void);
extern char *sdb_log_traceItem(CFL_LISTP freeArrayString, PHB_ITEM pitem);
extern void sdb_log_setPathName(const char *logPathName);
extern const char *sdb_log_getPathName(void);
extern void sdb_log_setHandle(FILE *logHandle);
extern void sdb_log_format(int format);
extern int sdb_log_getFormat(void);
extern void sdb_log_initEnv(void);

#endif
