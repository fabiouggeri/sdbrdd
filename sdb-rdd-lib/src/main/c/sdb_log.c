#include <stdlib.h>
#include <time.h>

#include "hbapiitm.h"
#include "hbcomp.h"
#include "hbdefs.h"
#include "hbset.h"

#include "cfl_socket.h"
#include "cfl_thread.h"

#include "sdb_api.h"
#include "sdb_log.h"

#define SDB_LOG_LEVEL_VAR "SDB_LOG_LEVEL"
#define SDB_LOG_PATH_NAME_VAR "SDB_LOG_NAME"

static char s_logPathName[FILENAME_MAX + 1] = {0};

static FILE *s_logHandle = NULL;

static CFL_BOOL s_tryActive = CFL_FALSE;

static int s_logFormat = SDB_LOG_FORMAT_DEFAULT;

static char s_hostName[256] = {0};

unsigned int __sdbLoglevel = SDB_LOG_LEVEL_OFF;

/* Public variable to store returning of clock() at TRACE function */
clock_t _sdbTraceLastClock;

static char *hostName(void) {
   if (s_hostName[0] == '\0') {
      sdb_api_lockApi();
      cfl_socket_hostname(s_hostName, sizeof(s_hostName) - 1);
      sdb_api_unlockApi();
   }
   return s_hostName;
}

CFL_STRP sdb_log_formatLog(const char *format, ...) {
   CFL_STRP pStr;
   va_list pArgs;

   va_start(pArgs, format);
   pStr = cfl_str_appendFormatArgs(NULL, format, pArgs);
   va_end(pArgs);
   return pStr;
}

static char *getPathLog(char *path) {
#if defined(HB_OS_WIN) || defined(HB_OS_WIN32) || defined(HB_OS_WIN64) || defined(HB_OS_WIN_32)
   char *envPath = getenv("TEMP");
   size_t len;
   if (envPath != NULL) {
      strncpy(path, envPath, FILENAME_MAX);
      len = strlen(path);
      if (len > 0 && len < FILENAME_MAX && path[len - 1] != '\\') {
         path[len++] = '\\';
         path[len] = '\0';
      }
   } else {
      path[0] = '\0';
   }
#elif defined(HB_OS_LINUX)
   strcpy(path, "./");
#else
   path[0] = '\0';
#endif
   return path;
}

const char *sdb_log_getPathName(void) {
   if (s_logPathName[0] == '\0') {
      struct tm *tm = sdb_log_localtime();
      char path[FILENAME_MAX] = {0};
      snprintf(s_logPathName, sizeof(s_logPathName), "%ssdb_%u_%04u%02u%02u_%02u%02u%02u.log", getPathLog(path), SDB_GET_PID,
               1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
   }
   return s_logPathName;
}

void sdb_log_setPathName(const char *logPathName) {
   strncpy(s_logPathName, logPathName, FILENAME_MAX);
   if (s_logHandle) {
      fclose(s_logHandle);
      s_tryActive = CFL_FALSE;
      s_logHandle = NULL;
   }
}

void sdb_log_setHandle(FILE *logHandle) {
   if (s_logHandle) {
      fclose(s_logHandle);
   }
   s_tryActive = CFL_TRUE;
   s_logHandle = logHandle;
}

int sdb_log_getLevel(void) {
   return __sdbLoglevel;
}

void sdb_log_format(int format) {
   if (format == SDB_LOG_FORMAT_GELF) {
      s_logFormat = SDB_LOG_FORMAT_GELF;
   } else {
      s_logFormat = SDB_LOG_FORMAT_DEFAULT;
   }
}

int sdb_log_getFormat(void) {
   return s_logFormat;
}

void sdb_log_setLevel(int level) {
   if (level >= SDB_LOG_LEVEL_OFF && level <= SDB_LOG_LEVEL_TRACE) {
      __sdbLoglevel = level;
      if (level == SDB_LOG_LEVEL_OFF && s_logHandle) {
         fclose(s_logHandle);
         s_logHandle = NULL;
         s_tryActive = CFL_FALSE;
      }
   }
}

static CFL_BOOL logActive(void) {
   if (__sdbLoglevel > SDB_LOG_LEVEL_OFF) {
      if (s_logHandle == NULL && !s_tryActive) {
         s_logHandle = fopen(sdb_log_getPathName(), "a");
         s_tryActive = CFL_TRUE;
      }
      return s_logHandle != NULL;
   }
   return CFL_FALSE;
}

static int gelf_log_level(char *levelName) {
   if (strncmp(levelName, "ERROR", 5) == 0) {
      return 3;
   } else if (strncmp(levelName, "WARN", 4) == 0) {
      return 4;
   } else if (strncmp(levelName, "INFO", 4) == 0) {
      return 6;
   } else if (strncmp(levelName, "DEBUG", 5) == 0 || strncmp(levelName, "TRACE", 5) == 0) {
      return 7;
   } else {
      return 0;
   }
}

static void writeGELFFormat(char *levelName, CFL_STRP out) {
   fprintf(s_logHandle,
           "{\"version\":\"1.1\","
           "\"timestamp\":%ld,"
           "\"host\":\"%s\","
           "\"_thread_id\":%u,"
           "\"short_message\":\"%s\","
           "\"level\":%d}\n",
           time(NULL), hostName(), (CFL_UINT32)SDB_GET_THREAD_ID, cfl_str_getPtr(out), gelf_log_level(levelName));
}

static void writeDefaultFormat(char *levelName, CFL_STRP out) {
   time_t curTime;
   struct tm *tm;
   time(&curTime);
   tm = localtime(&curTime);
   if (__sdbLoglevel >= SDB_LOG_LEVEL_DEBUG) {
      fprintf(s_logHandle, "%04d-%02d-%02dT%02d:%02d:%02d - %010u/%010u - %s: %s\n", 1900 + tm->tm_year, tm->tm_mon + 1,
              tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (CFL_UINT32)SDB_GET_PID, (CFL_UINT32)SDB_GET_THREAD_ID, levelName,
              cfl_str_getPtr(out));
   } else {
      fprintf(s_logHandle, "%04d-%02d-%02dT%02d:%02d:%02d - %010u            - %s: %s\n", 1900 + tm->tm_year, tm->tm_mon + 1,
              tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (CFL_UINT32)SDB_GET_PID, levelName, cfl_str_getPtr(out));
   }
}

void sdb_log_write(char *levelName, CFL_STRP out, CFL_LISTP freePtrs) {
   CFL_UINT32 i;
   if (logActive()) {
      if (s_logFormat == SDB_LOG_FORMAT_GELF) {
         writeGELFFormat(levelName, out);
      } else {
         writeDefaultFormat(levelName, out);
      }
      fflush(s_logHandle);
   }
   cfl_str_free(out);
   for (i = 0; i < cfl_list_length(freePtrs); i++) {
      hb_xfree((char *)cfl_list_get(freePtrs, i));
   }
   cfl_list_free(freePtrs);
}

static void writeEnterExitGELFFormat(CFL_BOOL enter, const char *funName, CFL_UINT32 line, CFL_STRP out) {
   if (out != NULL) {
      fprintf(s_logHandle,
              "{\"version\":\"1.1\","
              "\"timestamp\":%ld,"
              "\"host\":\"%s\","
              "\"_thread_id\":%u,"
              "\"short_message\":\"%s\","
              "\"full_message\":\"%s\","
              "\"level\":7,"
              "\"_logger_name\":\"%s\","
              "\"_logger_line\":%d}\n",
              time(NULL), hostName(), (CFL_UINT32)SDB_GET_THREAD_ID, (enter ? "enter" : "exit"), cfl_str_getPtr(out), funName,
              line);
   } else {
      fprintf(s_logHandle,
              "{\"version\":\"1.1\","
              "\"timestamp\":%ld,"
              "\"host\":\"%s\","
              "\"_thread_id\":%u,"
              "\"short_message\":\"%s\","
              "\"level\":7,"
              "\"_logger_name\":\"%s\","
              "\"_logger_line\":%d}\n",
              time(NULL), hostName(), (CFL_UINT32)SDB_GET_THREAD_ID, (enter ? "enter" : "exit"), funName, line);
   }
}

static void writeEnterExitDefaultFormat(CFL_BOOL enter, const char *funName, CFL_UINT32 line, CFL_STRP out) {
   time_t curTime;
   struct tm *tm;
   time(&curTime);
   tm = localtime(&curTime);
   if (out != NULL) {
      fprintf(s_logHandle, "%04d-%02d-%02dT%02d:%02d:%02d - %010u/%010u - TRACE %s %s(%d) %s\n", 1900 + tm->tm_year, tm->tm_mon + 1,
              tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (CFL_UINT32)SDB_GET_PID, (CFL_UINT32)SDB_GET_THREAD_ID,
              (enter ? "=>" : "<="), funName, line, cfl_str_getPtr(out));
   } else {
      fprintf(s_logHandle, "%04d-%02d-%02dT%02d:%02d:%02d - %010u/%010u - TRACE %s %s(%d)\n", 1900 + tm->tm_year, tm->tm_mon + 1,
              tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (CFL_UINT32)SDB_GET_PID, (CFL_UINT32)SDB_GET_THREAD_ID,
              (enter ? "=>" : "<="), funName, line);
   }
}

void sdb_log_writeEnter(const char *funName, CFL_UINT32 line, CFL_STRP out, CFL_LISTP freePtrs) {
   CFL_UINT32 i;
   if (logActive()) {
      if (s_logFormat == SDB_LOG_FORMAT_GELF) {
         writeEnterExitGELFFormat(CFL_TRUE, funName, line, out);
      } else {
         writeEnterExitDefaultFormat(CFL_TRUE, funName, line, out);
      }
      fflush(s_logHandle);
   }
   if (out) {
      cfl_str_free(out);
   }
   if (freePtrs) {
      for (i = 0; i < cfl_list_length(freePtrs); i++) {
         hb_xfree((char *)cfl_list_get(freePtrs, i));
      }
      cfl_list_free(freePtrs);
   }
}

void sdb_log_writeExit(const char *funName, CFL_UINT32 line) {
   if (logActive()) {
      if (s_logFormat == SDB_LOG_FORMAT_GELF) {
         writeEnterExitGELFFormat(CFL_FALSE, funName, line, NULL);
      } else {
         writeEnterExitDefaultFormat(CFL_FALSE, funName, line, NULL);
      }
      fflush(s_logHandle);
   }
}

struct tm *sdb_log_localtime(void) {
   time_t curTime;
   time(&curTime);
   return localtime(&curTime);
}

char *sdb_log_traceItem(CFL_LISTP freeArrayString, PHB_ITEM pItem) {
   char *buffer;
   HB_SIZE len = 0;
   HB_BOOL bFreeReq = CFL_FALSE;

   if (pItem) {
      buffer = hb_itemString(pItem, &len, &bFreeReq);
      if (bFreeReq) {
         cfl_list_add(freeArrayString, buffer);
      }
      return buffer;
   }
   return "NULL";
}

static void initLogLevel(void) {
   const char *logLevel = getenv(SDB_LOG_LEVEL_VAR);
   if (logLevel != NULL) {
      int iLevel;

      if (hb_stricmp(logLevel, "OFF") == 0) {
         iLevel = SDB_LOG_LEVEL_OFF;

      } else if (hb_stricmp(logLevel, "ERROR") == 0) {
         iLevel = SDB_LOG_LEVEL_ERROR;

      } else if (hb_stricmp(logLevel, "WARN") == 0) {
         iLevel = SDB_LOG_LEVEL_WARN;

      } else if (hb_stricmp(logLevel, "INFO") == 0) {
         iLevel = SDB_LOG_LEVEL_INFO;

      } else if (hb_stricmp(logLevel, "DEBUG") == 0) {
         iLevel = SDB_LOG_LEVEL_DEBUG;

      } else if (hb_stricmp(logLevel, "TRACE") == 0) {
         iLevel = SDB_LOG_LEVEL_TRACE;

      } else {
         iLevel = atoi(logLevel);
         if (iLevel < SDB_LOG_LEVEL_OFF || iLevel > SDB_LOG_LEVEL_TRACE) {
            iLevel = SDB_LOG_LEVEL_ERROR;
         }
      }
      sdb_log_setLevel(iLevel);
   }
}

static void initLogPathName(void) {
   const char *logPathName = getenv(SDB_LOG_PATH_NAME_VAR);
   if (logPathName != NULL) {
      sdb_log_setPathName(logPathName);
   }
}

void sdb_log_initEnv(void) {
   initLogLevel();
   initLogPathName();
}

HB_FUNC(SDB_LOGINITENV) {
   sdb_log_initEnv();
}