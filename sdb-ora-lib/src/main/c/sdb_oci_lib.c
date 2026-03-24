#ifdef _WIN32
   #include <windows.h>
#endif

#include "sdb_oci_lib.h"
#include "sdb_thread.h"
#include "sdb_error.h"
#include "sdb_log.h"

#ifdef _WIN32
   #ifndef isnan
      #define isnan                   _isnan
   #endif
#else
   #include <pthread.h>
   #include <sys/time.h>
   #include <dlfcn.h>
#endif
#ifdef __linux
   #include <sys/syscall.h>
#endif

#define LOAD_SYMBOL(libHandle, table, symbol) if (! loadSymbol(libHandle, #symbol, (void**) &table->symbol)) return CFL_FALSE

#define LOAD_SYMBOL_MIN(libHandle, table, symbol, version, minNum, minRel) \
        if (sdb_oci_minimalClientVersion(version, minNum, minRel)) { \
           if (! loadSymbol(libHandle, #symbol, (void**) &table->symbol)) { \
              return CFL_FALSE; \
           } \
        } else table->symbol = NULL;

static const char *ociLibNames[] = {
#if defined _WIN32 || defined __CYGWIN__
    "oci.dll",
#elif __APPLE__
    "libclntsh.dylib",
    "libclntsh.dylib.19.1",
    "libclntsh.dylib.18.1",
    "libclntsh.dylib.12.1",
    "libclntsh.dylib.11.1",
    "libclntsh.dylib.21.1",
#else
    "libclntsh.so",
    "libclntsh.so.19.1",
    "libclntsh.so.18.1",
    "libclntsh.so.12.1",
    "libclntsh.so.11.1",
    "libclntsh.so.20.1",
    "libclntsh.so.21.1",
#endif
    NULL
};
static SDB_OCI_SYMBOLS s_ociSymbols;

static void *s_ociLibHandle = NULL;

static void *s_ociEnvHandle = NULL;

CFL_BOOL sdb_oci_minimalClientVersion(SDB_OCI_VERSION_INFO *versionInfo, int minVersionNum, int minReleaseNum) {
    if (versionInfo->versionNum < minVersionNum ||
            (versionInfo->versionNum == minVersionNum && versionInfo->releaseNum < minReleaseNum)) {
        return CFL_FALSE;
    }
    return CFL_TRUE;
}

static CFL_BOOL loadSymbol(void *ociLibHandle, const char *symbolName, void **symbol) {
    if (ociLibHandle != NULL) {
#ifdef _WIN32
       *symbol = GetProcAddress(ociLibHandle, symbolName);
#else
       *symbol = dlsym(ociLibHandle, symbolName);
#endif
       if (! *symbol) {
          sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_SYMBOL_NOT_LOADED, "Symbol %s not loaded", symbolName);
          return CFL_FALSE;
       }
    } else {
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_LIB_NOT_LOADED, "OCI Library not loaded");
      return CFL_FALSE;
    }
    return CFL_TRUE;
}

static void getClientVersion(SDB_OCI_SYMBOLS* symbols, SDB_OCI_VERSION_INFO* version){
   symbols->OCIClientVersion(&version->versionNum, &version->releaseNum, &version->updateNum,
           &version->portReleaseNum, &version->portUpdateNum);
   version->fullVersionNum = (CFL_UINT32) SDB_ORACLE_VERSION_TO_NUMBER(version->versionNum, version->releaseNum, version->updateNum,
           version->portReleaseNum, version->portUpdateNum);
}

static CFL_BOOL loadOCISymbols(void *ociLibHandle, SDB_OCI_SYMBOLS *symbols, SDB_OCI_VERSION_INFO *version) {
   LOAD_SYMBOL(ociLibHandle, symbols, OCIClientVersion);
   getClientVersion(symbols, version);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIEnvNlsCreate);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIEnvCreate);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIHandleAlloc);
   LOAD_SYMBOL(ociLibHandle, symbols, OCISessionBegin);
   LOAD_SYMBOL(ociLibHandle, symbols, OCISessionEnd);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIHandleFree);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIAttrGet);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIAttrSet);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIServerAttach);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIServerDetach);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIStmtPrepare2);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIStmtRelease);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIDefineByPos);
   LOAD_SYMBOL_MIN(ociLibHandle, symbols, OCIDefineByPos2, version, 12, 0);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIDefineDynamic);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIStmtExecute);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIStmtFetch2);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIBindByName);
   LOAD_SYMBOL_MIN(ociLibHandle, symbols, OCIBindByName2, version, 12, 0);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIBindByPos);
   LOAD_SYMBOL_MIN(ociLibHandle, symbols, OCIBindByPos2, version, 12, 0);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIBindDynamic);
   LOAD_SYMBOL(ociLibHandle, symbols, OCITransCommit);
   LOAD_SYMBOL(ociLibHandle, symbols, OCITransPrepare);
   LOAD_SYMBOL(ociLibHandle, symbols, OCITransRollback);
   LOAD_SYMBOL(ociLibHandle, symbols, OCITransStart);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIBreak);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobClose);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobRead2);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobWrite2);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobCreateTemporary);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobFileExists);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobFileGetName);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobFileSetName);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobFreeTemporary);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobGetChunkSize);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobGetLength2);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobIsOpen);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobIsTemporary);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobLocatorAssign);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobOpen);
   LOAD_SYMBOL(ociLibHandle, symbols, OCILobTrim2);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIParamGet);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIStmtGetBindInfo);
   LOAD_SYMBOL_MIN(ociLibHandle, symbols, OCIStmtGetNextResult, version, 12, 0);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIErrorGet);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIDescribeAny);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIDescriptorAlloc);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIDescriptorFree);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIRowidToChar);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIPing);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIServerRelease);
   LOAD_SYMBOL_MIN(ociLibHandle, symbols, OCIServerRelease2, version, 18, 0);
   LOAD_SYMBOL(ociLibHandle, symbols, OCIRowidToChar);
   LOAD_SYMBOL(ociLibHandle, symbols, OCINumberFromInt);
   LOAD_SYMBOL(ociLibHandle, symbols, OCINumberFromReal);
   LOAD_SYMBOL(ociLibHandle, symbols, OCINumberToInt);
   LOAD_SYMBOL(ociLibHandle, symbols, OCINumberToReal);
   LOAD_SYMBOL(ociLibHandle, symbols, OCINumberSetZero);
   return CFL_TRUE;
}

#ifdef _WIN32
static int checkDllArchitecture(const char *name) {
    IMAGE_DOS_HEADER dosHeader;
    IMAGE_NT_HEADERS ntHeaders;
    FILE *fp;

    fp = fopen(name, "rb");
    if (!fp)
        return -1;
    fread(&dosHeader, sizeof(dosHeader), 1, fp);
    if (dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
        fclose(fp);
        return -2;
    }
    fseek(fp, dosHeader.e_lfanew, SEEK_SET);
    fread(&ntHeaders, sizeof(ntHeaders), 1, fp);
    fclose(fp);
    if (ntHeaders.Signature != IMAGE_NT_SIGNATURE)
        return -2;
#if defined _M_AMD64
    if (ntHeaders.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64)
        return 1;
#elif defined _M_IX86
    if (ntHeaders.FileHeader.Machine == IMAGE_FILE_MACHINE_I386)
        return 1;
#endif
    return 0;
}

static void getLoadErrorOnWindows(char *loadError, size_t loadErrorLength) {
    DWORD length = 0, errorNum, status;
    wchar_t *wLoadError = NULL;

    // if DLL is of the wrong architecture, attempt to locate the DLL that was
    // loaded and use that information if it can be found
    errorNum = GetLastError();

    // get error message in Unicode first
    // use English unless English error messages aren't available
    status = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
            NULL, errorNum, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
            (LPWSTR) &wLoadError, 0, NULL);
    if (!status && GetLastError() == ERROR_MUI_FILE_NOT_FOUND) {
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                NULL, errorNum, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPWSTR) &wLoadError, 0, NULL);
    }

    if (wLoadError) {
        // strip trailing period and carriage return from message, if needed
        length = (DWORD) wcslen(wLoadError);
        while (length > 0) {
            if (wLoadError[length - 1] > 127 || (wLoadError[length - 1] != L'.' && !isspace(wLoadError[length - 1]))) {
                break;
            }
            length--;
        }
        wLoadError[length] = L'\0';

        // convert to a multi-byte string in UTF-8 encoding
        if (length > 0) {
            length = WideCharToMultiByte(CP_UTF8, 0, wLoadError, -1, loadError, (int) loadErrorLength, NULL, NULL);
        }
        LocalFree(wLoadError);
    }

    // fallback in case message cannot be determined
    if (length == 0) {
        sprintf(loadError, "DLL load failed: Windows Error %lu", errorNum);
    }
}

static CFL_BOOL findDllArchitecture(const char *dllName, char *dllPathName, char *loadError, size_t loadErrorLength) {
   char fullName[_MAX_PATH + 1], *path, *temp;
   size_t length;
   int archError;
   char *envVarArch;
   size_t maxErrorLen = loadErrorLength - 1;

#if defined _M_AMD64
   envVarArch = "OCI64_PATH";
#elif defined _M_IX86
   envVarArch = "OCI32_PATH";
#else
   envVarArch = NULL;
#endif

   if (envVarArch != NULL) {
      path = getenv(envVarArch);
      if (path) {
         length = strlen(path);
         if (path[length - 1] == '\\') {
            --length;
         }
         snprintf(fullName, sizeof(fullName), "%.*s\\%s", (int) length, path, dllName);
         archError = checkDllArchitecture(fullName);
         if (archError == 1) {
            length = strlen(fullName);
            strncpy(dllPathName, fullName, length);
            dllPathName[length] = '\0';
            return CFL_TRUE;
         } else if (archError == 0) {
            snprintf(loadError, maxErrorLen, "%s is not the correct architecture", fullName);
            loadError[maxErrorLen] = '\0';
            return CFL_FALSE;
         }
      }
   }

   // first search executable directory
   if (GetModuleFileName(NULL, fullName, sizeof(fullName)) != 0) {
      temp = strrchr(fullName, '\\');
      if (temp) {
         *(temp + 1) = '\0';
         strncat(fullName, dllName, sizeof(fullName) - strlen(fullName) - 1);
         archError = checkDllArchitecture(fullName);
         if (archError == 1) {
            length = strlen(fullName);
            strncpy(dllPathName, fullName, length);
            dllPathName[length] = '\0';
            return CFL_TRUE;
         } else if (archError == 0) {
            snprintf(loadError, maxErrorLen, "%s is not the correct architecture", fullName);
            loadError[maxErrorLen] = '\0';
            return CFL_FALSE;
         }
      }
   }

   // check current directory
   if (GetCurrentDirectory(sizeof(fullName), fullName) != 0) {
      temp = fullName + strlen(fullName);
      snprintf(temp, sizeof (fullName) - strlen(fullName), "\\%s", dllName);
      archError = checkDllArchitecture(fullName);
      if (archError == 1) {
         length = strlen(fullName);
         strncpy(dllPathName, fullName, length);
         dllPathName[length] = '\0';
         return CFL_TRUE;
      } else if (archError == 0) {
         snprintf(loadError, maxErrorLen, "%s is not the correct architecture", fullName);
         loadError[maxErrorLen] = '\0';
         return CFL_FALSE;
      }
   }

   // search ORACLE_HOME
   path = getenv("ORACLE_HOME");
   if (path) {
      length = strlen(path);
      if (path[length - 1] == '\\') {
         --length;
      }
      snprintf(fullName, sizeof(fullName), "%.*s\\bin\\%s", (int) length, path, dllName);
      archError = checkDllArchitecture(fullName);
      if (archError == 1) {
         length = strlen(fullName);
         strncpy(dllPathName, fullName, length);
         dllPathName[length] = '\0';
         return CFL_TRUE;
      } else if (archError == 0) {
         snprintf(loadError, maxErrorLen, "%s is not the correct architecture", fullName);
         loadError[maxErrorLen] = '\0';
         return CFL_FALSE;
      }
   }

   // search all paths in PATH environment var until find dll of correct architecture
   path = getenv("PATH");
   if (path) {
      for(;;) {
         temp = strchr(path, ';');
         if (!temp) {
            length = strlen(path);
         } else {
            length = temp - path;
         }
         if (length > 0 && length <= _MAX_DIR) {
            if (path[length - 1] == '\\') {
               --length;
            }
            snprintf(fullName, sizeof(fullName), "%.*s\\%s", (int) length, path, dllName);
            archError = checkDllArchitecture(fullName);
            if (archError == 1) {
               length = strlen(fullName);
               strncpy(dllPathName, fullName, length);
               dllPathName[length] = '\0';
               return CFL_TRUE;
            } else if (archError == 0 && loadError[0] == '\0') {
               snprintf(loadError, maxErrorLen, "%s is not the correct architecture", fullName);
               loadError[maxErrorLen] = '\0';
            }
         }
         if (!temp) {
            break;
         }
         path = temp + 1;
      }
   }
   return CFL_FALSE;
}
#endif

static CFL_BOOL searchOCILib(const char *libName, char *foundLibPath, char *loadError, size_t loadErrorLength) {
#ifdef _WIN32
   return findDllArchitecture(libName, foundLibPath, loadError, loadErrorLength);
#else
   strncpy(foundLibPath, libName, MAX_PATH);
   return CFL_TRUE;
#endif
}

CFL_BOOL sdb_oci_loadOCILibrary(void) {
   const char *libName;
   int i;
   char ociPathName[MAX_PATH + 1];
   char loadError[512];

   ENTER_FUN_NAME("sdb_oci_loadOCILibrary");

   if (s_ociLibHandle == NULL) {
      memset(&s_ociSymbols, 0, sizeof(s_ociSymbols));
      memset(loadError, 0, sizeof(loadError));
      i = 0;
      libName = ociLibNames[i++];
      while (libName != NULL && s_ociLibHandle == NULL) {
         if (searchOCILib(libName, ociPathName, loadError, sizeof(loadError))) {
#ifdef _WIN32
            s_ociLibHandle = LoadLibrary(ociPathName);
#else
            s_ociLibHandle = dlopen(ociPathName, RTLD_LAZY);
#endif
         }
         libName = ociLibNames[i++];
      }
      if (s_ociLibHandle != NULL) {
         SDB_OCI_VERSION_INFO version;
         if (! loadOCISymbols(s_ociLibHandle, &s_ociSymbols, &version)) {
#ifdef _WIN32
            FreeLibrary(s_ociLibHandle);
#else
            dlclose(s_ociLibHandle);
#endif
            s_ociLibHandle = NULL;
         } else if (! sdb_oci_minimalClientVersion(&version, 11, 2)) {
            sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_OLD_CLIENT, "OCI Library too old: %d.%d",
                    version.versionNum, version.releaseNum);
#ifdef _WIN32
            FreeLibrary(s_ociLibHandle);
#else
            dlclose(s_ociLibHandle);
#endif
            s_ociLibHandle = NULL;

         }
      } else if (loadError[0] == '\0') {
#ifdef _WIN32
         getLoadErrorOnWindows(loadError, sizeof(loadError));
#else
         strncpy(loadError, dlerror(), sizeof(loadError) - 1);
         loadError[sizeof(loadError) - 1] = '\0';
#endif
         sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_LOADING_LIB, "Error loading lib: %s", loadError);
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_LOADING_LIB, "Error loading lib: %s", loadError);
      }
   }
   RETURN s_ociLibHandle != NULL ? CFL_TRUE : CFL_FALSE;
}

CFL_BOOL sdb_oci_freeOCILibrary(void) {
   if (s_ociLibHandle != NULL) {
#ifdef _WIN32
      FreeLibrary(s_ociLibHandle);
#else
      dlclose(s_ociLibHandle);
#endif
      s_ociLibHandle = NULL;
   }
   return CFL_TRUE;
}

void * sdb_oci_getEnv(void) {
   return s_ociEnvHandle;
}

CFL_BOOL sdb_oci_createEnv(CFL_UINT32 mode) {
   int status;

   ENTER_FUN_NAME("sdb_oci_createEnv");
   if (s_ociEnvHandle == NULL) {
      status = s_ociSymbols.OCIEnvNlsCreate(&s_ociEnvHandle, mode, NULL, NULL, NULL, NULL, 0, NULL, 0, 0);
      //status = s_ociSymbols.OCIEnvCreate(&s_ociEnvHandle, mode, NULL, NULL, NULL, NULL, 0, NULL);
      if (status != SDB_OCI_SUCCESS && status != SDB_OCI_SUCCESS_WITH_INFO) {
         if (s_ociEnvHandle != NULL) {
            sdb_oci_setErrorFromOCI(s_ociEnvHandle, SDB_OCI_HTYPE_ENV, "Error initializing OCI environemnt");
         } else {
            sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_CREATE_ENV, "Error initializing OCI environemnt.");
         }
         RETURN CFL_FALSE;
      }
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_CREATE_ENV, "OCI environemnt already initialized.");
      RETURN CFL_FALSE;
   }
   RETURN CFL_TRUE;
}

CFL_BOOL sdb_oci_freeEnv(void) {
   if (s_ociEnvHandle != NULL) {
      s_ociSymbols.OCIHandleFree(s_ociEnvHandle, SDB_OCI_HTYPE_ENV);
      s_ociEnvHandle = NULL;
   }
   return CFL_TRUE;
}

CFL_BOOL sdb_oci_isInitialized(void) {
   return s_ociLibHandle != NULL ? CFL_TRUE : CFL_FALSE;
}

SDB_OCI_SYMBOLS * sdb_oci_getSymbols(void) {
   return &s_ociSymbols;
}

void *sdb_oci_getErrorHandle(void) {
   SDB_THREAD_DATAP thData = sdb_thread_getData();
   if (thData->dbAPIData == NULL) {
      void *errorHandle;
      int status = s_ociSymbols.OCIHandleAlloc(s_ociEnvHandle, &errorHandle, SDB_OCI_HTYPE_ERROR, 0, NULL);
      if (status == SDB_OCI_SUCCESS) {
         thData->dbAPIData = errorHandle;
      } else {
         sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_LOGIN, "Error creating thread error handle");
         return NULL;
      }
   }
   return thData->dbAPIData;
}

void sdb_oci_freeErrorHandle(void) {
   SDB_THREAD_DATAP thData = sdb_thread_getData();
   if (thData->dbAPIData != NULL) {
      int status = s_ociSymbols.OCIHandleFree(thData->dbAPIData, SDB_OCI_HTYPE_ERROR);
      CHECK_STATUS_LOG(status, ("Free error handle %p failed", thData->dbAPIData));
      thData->dbAPIData = NULL;
   }
}

CFL_BOOL sdb_oci_setErrorFromOCI(void *handle, CFL_UINT32 handleType, const char * message) {
   CFL_INT32 errorCode;
   int status;
   char errorMessage[SDB_OCI_ERROR_MAX_LEN];

   status = s_ociSymbols.OCIErrorGet(handle, 1, NULL, &errorCode, errorMessage, sizeof(errorMessage), handleType);
   if (status == SDB_OCI_SUCCESS) {
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, errorCode, "%s: %d - %s", message, errorCode, errorMessage);
   } else {
      sdb_thread_setError(SDB_ERROR_TYPE_DB_ERROR, SDB_OCI_ERROR_GET_ERROR, "%s: %s", message, "failed to get error");
   }
   return CFL_TRUE;
}
