#include <conio.h>
#include <stdio.h>
#include <string.h>

#include "cfl_str.h"
#include "sdb_lock_server.h"

#ifdef __BORLANDC__
#define _strcmpi strcmpi
#endif

#define DEFAULT_PORT_NUMBER 7654
#define MAX_ARGS_LEN 100
#define MAX_STR_LEN 100

#define SERVICE_NAME_PATTERN "SDB-LOCKSERVER-%s-%u"

static SERVICE_STATUS_HANDLE s_serviceStatusHandle;
static SERVICE_STATUS s_serviceStatus;
static DWORD s_dwCheckPoint = 1;
static char s_szServiceName[MAX_STR_LEN];
static char s_szAdress[MAX_STR_LEN];
static CFL_UINT16 s_port;
SDB_LS_SERVERP s_server = NULL;

static int cpuCount(void) {
#if defined(_MSC_VER) || defined(__BORLANDC__) || defined(__MINGW64__) || defined(__MINGW32__)
#ifndef __BORLANDC__
   return (int)GetMaximumProcessorCount(ALL_PROCESSOR_GROUPS);
#else
   char buf[10];
   GetEnvironmentVariable("NUMBER_OF_PROCESSORS", buf, sizeof(buf));
   return atoi(buf);
#endif
#else
   return get_nprocs();
#endif
}

static void setServiceStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint) {

   // Fill in the SERVICE_STATUS structure.
   s_serviceStatus.dwCurrentState = dwCurrentState;
   s_serviceStatus.dwWin32ExitCode = dwWin32ExitCode;
   s_serviceStatus.dwWaitHint = dwWaitHint;

   if (dwCurrentState == SERVICE_START_PENDING) {
      s_serviceStatus.dwControlsAccepted = 0;
   } else {
      s_serviceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
   }

   if (dwCurrentState == SERVICE_RUNNING || dwCurrentState == SERVICE_STOPPED) {
      s_serviceStatus.dwCheckPoint = 0;
   } else {
      s_serviceStatus.dwCheckPoint = s_dwCheckPoint++;
   }

   // Report the status of the service to the SCM.
   SetServiceStatus(s_serviceStatusHandle, &s_serviceStatus);
}

static void installService(char *address, CFL_UINT16 port) {
   char szPath[MAX_PATH];
   char szCmdLine[MAX_PATH + MAX_ARGS_LEN];
   SC_HANDLE schSCManager;
   SC_HANDLE schService;
   char szServiceName[MAX_STR_LEN];
   char szServiceDisplay[MAX_STR_LEN];

   sprintf(szServiceName, SERVICE_NAME_PATTERN, address, port);
   sprintf(szServiceDisplay, "SDB Lock Server(%s:%u)", address, port);

   if (GetModuleFileName(NULL, szPath, MAX_PATH) == 0) {
      printf("Failed to get module file name. Error %ld.", GetLastError());
      return;
   }

   sprintf(szCmdLine, "%s %s %u run", szPath, address, port);

   // Open the local default service control manager database
   schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
   if (schSCManager == NULL) {
      printf("\nFailed to open service control manager. Error %ld.", GetLastError());
      return;
   }
   // Install the service into SCM by calling CreateService
   schService = CreateService(schSCManager,              // SCManager database
                              szServiceName,             // Name of service
                              szServiceDisplay,          // Name to display
                              SERVICE_QUERY_STATUS,      // Desired access
                              SERVICE_WIN32_OWN_PROCESS, // Service type
                              SERVICE_AUTO_START,        // Service start type
                              SERVICE_ERROR_NORMAL,      // Error control type
                              szCmdLine,                 // Service's command line
                              NULL,                      // No load ordering group
                              NULL,                      // No tag identifier
                              TEXT(""),                  // Dependencies
                              NULL,                      // Service running account
                              NULL);
   if (schService == NULL) {
      printf("\nFailed to create service. Error: %ld.", GetLastError());
      CloseServiceHandle(schSCManager);
      return;
   }

   printf("\nService %s is installed.", szServiceName);
   CloseServiceHandle(schSCManager);
   CloseServiceHandle(schService);
}

static void removeService(char *address, CFL_UINT16 port) {
   SC_HANDLE schSCManager;
   SC_HANDLE schService;
   SERVICE_STATUS ssSvcStatus = {0};
   char szServiceName[MAX_STR_LEN];

   sprintf(szServiceName, SERVICE_NAME_PATTERN, address, port);

   // Open the local default service control manager database
   schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
   if (schSCManager == NULL) {
      printf("\nFailed to open service control manager. Error %ld.", GetLastError());
      return;
   }
   schService = OpenService(schSCManager, szServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);

   if (schService == NULL) {
      printf("\nFailed to open service. Error %ld.", GetLastError());
      CloseServiceHandle(schSCManager);
      return;
   }

   // Try to stop the service
   if (ControlService(schService, SERVICE_CONTROL_STOP, &ssSvcStatus)) {
      printf("Stopping %s.", szServiceName);
      Sleep(1000);

      while (QueryServiceStatus(schService, &ssSvcStatus)) {
         if (ssSvcStatus.dwCurrentState == SERVICE_STOP_PENDING) {
            printf(".");
            Sleep(1000);
         } else {
            break;
         }
      }

      if (ssSvcStatus.dwCurrentState == SERVICE_STOPPED) {
         printf("\n%s is stopped.", szServiceName);
      } else {
         printf("\nFailed to stop %s.", szServiceName);
      }
   }

   // Now remove the service by calling DeleteService.
   if (DeleteService(schService)) {
      printf("\nService %s was removed.", szServiceName);
   } else {

      printf("\nFailed to delete service. Error %ld", GetLastError());
   }

   CloseServiceHandle(schSCManager);
   CloseServiceHandle(schService);
}

static void stopService(char *address, CFL_UINT16 port) {
   SC_HANDLE schSCManager;
   SC_HANDLE schService;
   SERVICE_STATUS ssSvcStatus = {0};
   char szServiceName[MAX_STR_LEN];

   sprintf(szServiceName, SERVICE_NAME_PATTERN, address, port);

   // Open the local default service control manager database
   schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
   if (schSCManager == NULL) {
      printf("\nFailed to open service control manager. Error %ld.", GetLastError());
      return;
   }
   schService = OpenService(schSCManager, szServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS);

   if (schService == NULL) {
      printf("\nFailed to open service. Error %ld.", GetLastError());
      CloseServiceHandle(schSCManager);
      return;
   }

   // Try to stop the service
   if (ControlService(schService, SERVICE_CONTROL_STOP, &ssSvcStatus)) {
      printf("Stopping %s.", szServiceName);
      Sleep(1000);

      while (QueryServiceStatus(schService, &ssSvcStatus)) {
         if (ssSvcStatus.dwCurrentState == SERVICE_STOP_PENDING) {
            printf(".");
            Sleep(1000);
         } else {
            break;
         }
      }

      if (ssSvcStatus.dwCurrentState == SERVICE_STOPPED) {
         printf("\n%s is stopped.", szServiceName);
      } else {
         printf("\nFailed to stop %s.", szServiceName);
      }
   }

   CloseServiceHandle(schSCManager);
   CloseServiceHandle(schService);
}

//
//   PARAMETERS:
//   * dwCtrlCode - the control code. This parameter can be one of the
//   following values:
//
//     SERVICE_CONTROL_CONTINUE
//     SERVICE_CONTROL_INTERROGATE
//     SERVICE_CONTROL_NETPARAMADD
//     SERVICE_CONTROL_NETPARAMDISABLE
//     SERVICE_CONTROL_NETPARAMREMOVE
//     SERVICE_CONTROL_PARAMCHANGE
//     SERVICE_CONTROL_PAUSE
//     SERVICE_CONTROL_SHUTDOWN
//     SERVICE_CONTROL_STOP
//
//   This parameter can also be a user-defined control code ranges from 128
//   to 255.
//

static void stopServer(void) {
   // Tell SCM that the service is stopping.
   setServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);

   // Perform service-specific stop operations.
   sdb_lcksrv_shutdownServer(s_server);
   sdb_lcksrv_releaseServer(s_server);
   s_server = NULL;

   // Tell SCM that the service is stopped.
   setServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

static void WINAPI serviceCtrlHandler(DWORD dwCtrl) {
   switch (dwCtrl) {
   case SERVICE_CONTROL_STOP:
      stopServer();
      break;
   case SERVICE_CONTROL_INTERROGATE:
      break;
   default:
      break;
   }
}

static DWORD WINAPI shutdownServer(LPVOID data) {
   if (data) {
      SDB_LS_SERVERP server = (SDB_LS_SERVERP)data;
      stopService(cfl_str_getPtr(server->address), server->port);
   }
   return 0;
}

static VOID WINAPI startServer(DWORD dwArgc, LPTSTR *lpszArgv) {
   s_serviceStatusHandle = RegisterServiceCtrlHandler(s_szServiceName, serviceCtrlHandler);
   if (s_serviceStatusHandle == NULL) {
      printf("\nFailed to register service controle handler. Error %ld.", GetLastError());
      return;
   }

   s_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
   setServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
   s_server = sdb_lcksrv_createServer(s_szAdress, s_port, cpuCount(), cpuCount() * 4);
   s_server->externalShutdownProc = shutdownServer;
   sdb_lcksrv_runServer(s_server, CFL_FALSE);
   setServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
}

static CFL_BOOL runService(char *address, CFL_UINT16 port) {
   SERVICE_TABLE_ENTRY serviceTable[] = {{NULL, NULL}, {NULL, NULL}};

   snprintf(s_szServiceName, sizeof(s_szServiceName), SERVICE_NAME_PATTERN, address, port);
   serviceTable[0].lpServiceName = s_szServiceName;
   serviceTable[0].lpServiceProc = startServer;
   strncpy(s_szAdress, address, sizeof(s_szAdress) - 1);
   s_szAdress[sizeof(s_szAdress) - 1] = '\0';
   s_port = port;

   // Connects the main thread of a service process to the service control
   // manager, which causes the thread to be the service control dispatcher
   // thread for the calling process. This call returns when the service has
   // stopped. The process should simply terminate when the call returns.
   return (CFL_BOOL)StartServiceCtrlDispatcher(serviceTable);
}

static CFL_BOOL isValidPortNumber(char *port) {
   if (port) {
      int i = 0;

      while (port[i] != 0) {
         if (port[i] < '0' || port[i] > '9') {

            return CFL_FALSE;
         }
         ++i;
      }
      return CFL_TRUE;
   }
   return CFL_FALSE;
}

int main(int argc, char *argv[]) {
   int ch;
   CFL_BOOL bInteractive = CFL_FALSE;
   SDB_LS_SERVERP server;
   char *address = NULL;
   CFL_UINT16 port = DEFAULT_PORT_NUMBER;

   if (argc > 1) {
      address = argv[1];
      if (argc > 2) {
         if (isValidPortNumber(argv[2])) {
            port = (CFL_UINT16)atoi(argv[2]);
            if (port == 0) {
               port = DEFAULT_PORT_NUMBER;
            }
            if (argc > 3) {
               if (_strcmpi(argv[3], "install") == 0) {
                  installService(argv[1], port);
               } else if (_strcmpi(argv[3], "remove") == 0) {
                  removeService(argv[1], port);
                  //               } else if (_strcmpi(argv[3], "start") == 0) {
                  //                  //startService(argv[1], port);
               } else if (_strcmpi(argv[3], "stop") == 0) {
                  stopService(argv[1], port);
               } else if (_strcmpi(argv[3], "run") == 0) {
                  runService(argv[1], port);
               } else {
                  printf("Unknown option '%s'", argv[3]);
                  printf("\nPossible options are: install, remove, start and stop");
                  printf("\nExample: cfl_lock_server.exe 127.0.0.1 install");
               }
               return 1;
            } else {
               bInteractive = CFL_TRUE;
            }
         } else {
            printf("invalid port number '%s'", argv[2]);
            return 1;
         }
      }
   }

   /* Run as standalone app */
   server = sdb_lcksrv_createServer(address, port, cpuCount(), cpuCount() * 4);
   sdb_lcksrv_runServer(server, CFL_FALSE);

   if (bInteractive) {
      printf("\nModo interativo iniciado!");
      printf("\nOpcoes:");
      printf("\n   Q - Sair");
      do {
         ch = _getch();
      } while (ch != 'q' && ch != 'Q' && server->isRunning);
      sdb_lcksrv_shutdownServer(server);
      sdb_lcksrv_releaseServer(server);
   }
   return 0;
}
