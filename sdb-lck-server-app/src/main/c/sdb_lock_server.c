#include <stdarg.h>
#include <stdio.h>

#include "cfl_buffer.h"
#include "cfl_iterator.h"
#include "sdb_lock_server.h"

#define SILENT_LEVEL 0
#define ERROR_LEVEL 1
#define WARN_LEVEL 2
#define INFO_LEVEL 3
#define TRACE_LEVEL 4

#define STR_TRIMUP(s) cfl_str_toUpper(cfl_str_trim(s))

#if defined(__BORLANDC__)
#define sockaddr_storage sockaddr_in
#define INET6_ADDRSTRLEN 65
#endif

static DWORD WINAPI workerThreadProc(LPVOID param);
static void releaseClient(SDB_LS_CLIENTP client, CFL_BOOL bGraceful);

static int s_serversCount = 0;
static int s_serversRunning = 0;
static int s_iMessageLevel = INFO_LEVEL;
static CFL_STR s_emptyString = CFL_STR_EMPTY;

#define LOG_MESSAGE(l, x)                                                                                                          \
   if (s_iMessageLevel >= l)                                                                                                       \
   showMessage x
#define LOG_LAST_ERROR(l, x)                                                                                                       \
   if (s_iMessageLevel >= l)                                                                                                       \
   showLastError x

static void showMessage(const char *format, ...) {
   va_list pArgs;
   va_start(pArgs, format);
   vprintf(format, pArgs);
   va_end(pArgs);
}

static void showLastError(const char *format) {
   LPVOID msgBuffer;
   FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, GetLastError(),
                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&msgBuffer, 0, NULL);
   printf("%s %d - %s", format, GetLastError(), msgBuffer);
   LocalFree(msgBuffer);
}

static CFL_UINT32 calcStringHash(void *str) {
   LOG_MESSAGE(TRACE_LEVEL, ("\ncalcHashString(%s)=%u", cfl_str_getPtr((CFL_STRP)str), cfl_str_hashCode((CFL_STRP)str)));
   return cfl_str_hashCode((CFL_STRP)str);
}

static int compareStrings(void *k1, void *k2) {
   LOG_MESSAGE(TRACE_LEVEL, ("\ncompareStrings(%s,%s)", cfl_str_getPtr((CFL_STRP)k1), cfl_str_getPtr((CFL_STRP)k2)));
   return cfl_str_compare((CFL_STRP)k1, (CFL_STRP)k2, CFL_TRUE) == 0;
}

static CFL_UINT32 calcIdHash(void *ctxIdPtr) {
   LOG_MESSAGE(TRACE_LEVEL, ("\ncalcHashId(%p)=%u", ctxIdPtr, *((CFL_UINT32 *)ctxIdPtr)));
   return *((CFL_UINT32 *)ctxIdPtr);
}

static CFL_UINT32 calcRecnoHash(void *recnoPtr) {
   SDB_LS_RECNO recno = *((SDB_LS_RECNO *)recnoPtr);
   return (CFL_UINT32)(recno ^ (((SDB_LS_RECNO)recno) >> 32));
}

static int compareIds(void *k1, void *k2) {
   LOG_MESSAGE(TRACE_LEVEL, ("\ncompareIds(%u,%u)", *((CFL_UINT32 *)k1), *((CFL_UINT32 *)k2)));
   return *((CFL_UINT32 *)k1) == *((CFL_UINT32 *)k2);
}

static int compareRecno(void *k1, void *k2) {
   LOG_MESSAGE(TRACE_LEVEL, ("\ncompareRecno(%u,%u)", *((SDB_LS_RECNO *)k1), *((SDB_LS_RECNO *)k2)));
   return *((SDB_LS_RECNO *)k1) == *((SDB_LS_RECNO *)k2);
}

static void hashFreeLockRecord(void *k, void *v) {
   if (v != NULL) {
      SDB_LS_REC_LOCKP rec = (SDB_LS_REC_LOCKP)v;
      LOG_MESSAGE(TRACE_LEVEL,
                  ("\nReleasing record %u de %s.%s(%s)@%s", rec->recno, cfl_str_getPtr(rec->context->table->schema->name),
                   cfl_str_getPtr(rec->context->table->name), cfl_str_getPtr(rec->context->contextValue),
                   cfl_str_getPtr(rec->context->table->schema->database->name)));
      free(rec);
   }
}

static void hashFreeContext(void *k, void *v) {
   if (v != NULL) {
      SDB_LS_CONTEXTP context = (SDB_LS_CONTEXTP)v;
      LOG_MESSAGE(TRACE_LEVEL, ("\nReleasing context %s de %s.%s@%s", cfl_str_getPtr(context->contextValue),
                                cfl_str_getPtr(context->table->schema->name), cfl_str_getPtr(context->table->name),
                                cfl_str_getPtr(context->table->schema->database->name)));
      cfl_str_free(context->contextValue);
      cfl_hash_free(context->lockedRecords, CFL_TRUE);
      cfl_lock_free(&context->lock);
      free(context);
   }
}

static void hashFreeTable(void *k, void *v) {
   if (v != NULL) {
      SDB_LS_TABLEP table = (SDB_LS_TABLEP)v;
      LOG_MESSAGE(TRACE_LEVEL, ("\nReleasing table %s.%s@%s", cfl_str_getPtr(table->schema->name), cfl_str_getPtr(table->name),
                                cfl_str_getPtr(table->schema->database->name)));
      cfl_str_free(table->name);
      cfl_hash_free(table->defaultContext.lockedRecords, CFL_TRUE);
      cfl_lock_free(&table->defaultContext.lock);
      cfl_hash_free(table->contexts, CFL_TRUE);
      free(table);
   }
}

static void hashFreeSchema(void *k, void *v) {
   if (v != NULL) {
      SDB_LS_SCHEMAP schema = (SDB_LS_SCHEMAP)v;
      LOG_MESSAGE(TRACE_LEVEL, ("\nReleasing schema: %s@%s", cfl_str_getPtr(schema->name), cfl_str_getPtr(schema->database->name)));
      cfl_str_free(schema->name);
      cfl_hash_free(schema->tables, CFL_TRUE);
      free(schema);
   }
}

static void hashFreeDatabase(void *k, void *v) {
   if (v != NULL) {
      SDB_LS_DATABASEP database = (SDB_LS_DATABASEP)v;
      LOG_MESSAGE(TRACE_LEVEL, ("\nReleasing database %s", cfl_str_getPtr(database->name)));
      cfl_str_free(database->name);
      cfl_hash_free(database->schemas, CFL_TRUE);
      free(database);
   }
}

static void hashFreeClient(void *k, void *v) {
   if (v != NULL) {
      SDB_LS_CLIENTP client = (SDB_LS_CLIENTP)v;
      LOG_MESSAGE(TRACE_LEVEL, ("\nReleasing client %u", client->id));
      releaseClient(client, CFL_FALSE);
   }
}

static void printableAddress(const struct sockaddr_storage *address, char *addrBuffer, size_t addrBufLen) {
   void *numericAddress; // Pointer to binary address

   LOG_MESSAGE(TRACE_LEVEL, ("\nprintableAddress"));
   // Test for address and stream
   if (address == NULL) {
      return;
   }
#ifndef _SDB_CC_BCC_
   // Set pointer to address based on address family
   switch (address->ss_family) {
   case AF_INET:
      numericAddress = &((struct sockaddr_in *)address)->sin_addr;
      break;
   case AF_INET6:
      numericAddress = &((struct sockaddr_in6 *)address)->sin6_addr;
      break;
   default:
      strncpy(addrBuffer, "[tipo desconhecido]", addrBufLen - 1);
      addrBuffer[addrBufLen - 1] = '\0';
      return;
   }
   // Convert binary to printable address
   if (inet_ntop(address->ss_family, numericAddress, addrBuffer, addrBufLen) == NULL) {
      strncpy(addrBuffer, "[invalid address]", addrBufLen - 1);
      addrBuffer[addrBufLen - 1] = '\0';
   }
#else
   if (address->sin_family == AF_INET) {
      snprintf(addrBuffer, addrBufLen, "%s", inet_ntoa(address->sin_addr));
   } else {
      strncpy(addrBuffer, "[invalid address]", addrBufLen - 1);
      addrBuffer[addrBufLen - 1] = '\0';
   }
#endif
}

static CFL_BOOL setupTCPServerSocket(SDB_LS_SERVERP server) {
   /* If not Borland C. */
#if !defined(__BORLANDC__)
   CFL_BOOL bSuccess = CFL_FALSE;
   struct addrinfo addrCriteria;
   struct addrinfo *serverAddr;
   int retVal;
   struct addrinfo *addr;
   WSADATA winsockinfo;
   char addrBuffer[INET6_ADDRSTRLEN];
   char *serverAddress;
   char serverPort[7];
   size_t addrSize;

   if (s_serversRunning == 0) {
      retVal = WSAStartup(MAKEWORD(2, 2), &winsockinfo);
      if (retVal != 0) {
         LOG_LAST_ERROR(ERROR_LEVEL, ("\nError initializing socket environment:"));
         return CFL_FALSE;
      }
   }

   memset(&addrCriteria, 0, sizeof(addrCriteria)); // Zero out structure
   addrCriteria.ai_family = AF_INET;               // AF_UNSPEC; // Any address family
   addrCriteria.ai_flags = AI_PASSIVE;             // Accept on any address/port
   addrCriteria.ai_socktype = SOCK_STREAM;         // Only stream sockets
   addrCriteria.ai_protocol = IPPROTO_TCP;         // Only TCP protocol

   serverAddress = server->address ? cfl_str_getPtr(server->address) : "";
   sprintf(serverPort, "%u", server->port);
   retVal = getaddrinfo(serverAddress, serverPort, &addrCriteria, &serverAddr);
   if (retVal != 0) {
      LOG_MESSAGE(ERROR_LEVEL, ("\nSocket environment not initialized: %s", gai_strerror(retVal)));
      return CFL_FALSE;
   }

   for (addr = serverAddr; addr != NULL; addr = addr->ai_next) {
      struct sockaddr_storage localAddress;

      // Create a TCP socket
      server->listenSocket = WSASocket(addr->ai_family, addr->ai_socktype, addr->ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED);

      /* Try next */
      if (server->listenSocket == INVALID_SOCKET) {
         continue;
      }

      server->listenEvent = WSACreateEvent();
      if (server->listenEvent == WSA_INVALID_EVENT) {
         LOG_LAST_ERROR(WARN_LEVEL, ("\nError in WSACreateEvent():"));
         closesocket(server->listenSocket);
         continue;
      }

      retVal = WSAEventSelect(server->listenSocket, server->listenEvent, FD_ACCEPT);
      if (retVal == SOCKET_ERROR) {
         LOG_LAST_ERROR(ERROR_LEVEL, ("\nError in WSAEventSelect():"));
         closesocket(server->listenSocket);
         continue;
      }

      retVal = bind(server->listenSocket, addr->ai_addr, (int)addr->ai_addrlen);
      if (retVal == SOCKET_ERROR) {
         LOG_LAST_ERROR(WARN_LEVEL, ("\nError in bind():"));
         closesocket(server->listenSocket);
         continue;
      }

      retVal = listen(server->listenSocket, SOMAXCONN);
      if (retVal == SOCKET_ERROR) {
         LOG_LAST_ERROR(WARN_LEVEL, ("\nError in listen():"));
         closesocket(server->listenSocket);
         continue;
      }

      // Get local address of socket
      addrSize = sizeof(localAddress);
      retVal = getsockname(server->listenSocket, (struct sockaddr *)&localAddress, (int *)&addrSize);
      if (retVal == SOCKET_ERROR) {
         LOG_LAST_ERROR(WARN_LEVEL, ("\nError in getsockname():"));
         closesocket(server->listenSocket);
         continue;
      }
      printableAddress(&localAddress, addrBuffer, sizeof(addrBuffer));
      if (server->address) {
         cfl_str_setValue(server->address, addrBuffer);
      } else {
         server->address = cfl_str_newBuffer(addrBuffer);
      }
      bSuccess = CFL_TRUE;
      break; // Bind and listen successful
   }

   // Free address list allocated by getaddrinfo()
   freeaddrinfo(serverAddr);
   return bSuccess;
#else
   int retVal;
   WSADATA winsockinfo;
   int on = 1;
   struct sockaddr_in addr;
   size_t addrSize;

   if (s_serversRunning == 0) {
      retVal = WSAStartup(MAKEWORD(2, 2), &winsockinfo);
      if (retVal != 0) {
         LOG_LAST_ERROR(ERROR_LEVEL, ("\nError initializing socket environment:"));
         return CFL_FALSE;
      }
   }
   server->listenSocket = socket(AF_INET, SOCK_STREAM, 0);

   if (server->listenSocket == INVALID_SOCKET) {
      LOG_LAST_ERROR(ERROR_LEVEL, ("\nError creating socket:"));
      return CFL_FALSE;
   }

   retVal = setsockopt(server->listenSocket, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
   if (retVal < 0) {
      closesocket(server->listenSocket);
      LOG_LAST_ERROR(ERROR_LEVEL, ("\nsetsockopt() failed:"));
      return CFL_FALSE;
   }
   server->listenEvent = WSACreateEvent();
   if (server->listenEvent == WSA_INVALID_EVENT) {
      LOG_LAST_ERROR(ERROR_LEVEL, ("\nError in WSACreateEvent():"));
      closesocket(server->listenSocket);
      return CFL_FALSE;
   }

   retVal = WSAEventSelect(server->listenSocket, server->listenEvent, FD_ACCEPT);
   if (retVal == SOCKET_ERROR) {
      LOG_LAST_ERROR(ERROR_LEVEL, ("\nError in WSAEventSelect():"));
      closesocket(server->listenSocket);
      WSACloseEvent(server->listenEvent);
      return CFL_FALSE;
   }

   memset(&addr, 0, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = server->address ? inet_addr(cfl_str_getPtr(server->address)) : htonl(INADDR_LOOPBACK);
   addr.sin_port = htons(server->port);

   retVal = bind(server->listenSocket, (struct sockaddr *)&addr, sizeof(addr));
   if (retVal == SOCKET_ERROR) {
      LOG_LAST_ERROR(ERROR_LEVEL, ("\nError in bind():"));
      closesocket(server->listenSocket);
      WSACloseEvent(server->listenEvent);
      return CFL_FALSE;
   }

   retVal = listen(server->listenSocket, SOMAXCONN);
   if (retVal == SOCKET_ERROR) {
      LOG_LAST_ERROR(ERROR_LEVEL, ("\nError in listen():"));
      closesocket(server->listenSocket);
      WSACloseEvent(server->listenEvent);
      return CFL_FALSE;
   }
   addrSize = sizeof(addr);
   retVal = getsockname(server->listenSocket, (struct sockaddr *)&addr, &addrSize);
   if (retVal == -1) {
      LOG_LAST_ERROR(ERROR_LEVEL, ("\ngetsockname() failed:"));
      closesocket(server->listenSocket);
      WSACloseEvent(server->listenEvent);
      return CFL_FALSE;
   }
   if (server->address == NULL) {
      server->address = cfl_str_newBuffer("127.0.0.1");
   }
   return CFL_TRUE;
#endif
}

static void releaseWorker(SDB_LS_WORKERP worker) {
   if (worker) {
      SDB_LS_SERVERP server;
      LOG_MESSAGE(TRACE_LEVEL, ("\nReleasing worker data..."));
      server = worker->server;
      cfl_lock_acquire(&server->criticalSection);
      if (server->tailWorker == worker) {
         if (worker->previous) {
            server->tailWorker = worker->previous;
            worker->previous->next = worker->next;
         } else {
            server->tailWorker = NULL;
         }
      } else {
         if (worker->previous) {
            worker->previous->next = worker->next;
         }
         if (worker->next) {
            worker->next->previous = worker->previous;
         }
      }
      --server->workersCount;
      cfl_lock_release(&server->criticalSection);
      CloseHandle(worker->hThread);
      free(worker);
   }
}

static void releaseWorkers(SDB_LS_SERVERP server) {
   SDB_LS_WORKERP worker = server->tailWorker;
   LOG_MESSAGE(INFO_LEVEL, ("\nReleasing worker threads..."));
   while (worker) {
      SDB_LS_WORKERP previous;
      previous = worker->previous;
      releaseWorker(worker);
      worker = previous;
   }
}

static void shutdownServer(SDB_LS_SERVERP server) {
   ULONG_PTR completionKey;

   if (server->isRunning) {
      LOG_MESSAGE(INFO_LEVEL, ("\nShutting down server..."));
      server->isRunning = CFL_FALSE;
      if (server->completionPort) {
         completionKey = COMPLETION_KEY_KILL_WORKER;
         while (server->workersCount > 0) {
            PostQueuedCompletionStatus(server->completionPort, 0, completionKey, NULL);
            Sleep(100);
         }
         CloseHandle(server->completionPort);
         server->completionPort = NULL;
      }
      if (server->listenSocket != INVALID_SOCKET) {
         closesocket(server->listenSocket);
         server->listenSocket = INVALID_SOCKET;
      }
      if (server->listenThread != NULL) {
         CloseHandle(server->listenThread);
         server->listenThread = NULL;
      }
      if (server->listenEvent != WSA_INVALID_EVENT) {
         WSACloseEvent(server->listenEvent);
         server->listenEvent = WSA_INVALID_EVENT;
      }
      cfl_hash_clear(server->clients, CFL_TRUE);
      cfl_hash_clear(server->databases, CFL_TRUE);
      if (--s_serversRunning == 0) {
         LOG_MESSAGE(TRACE_LEVEL, ("\nCleaning socket environment..."));
         if (WSACleanup() != 0) {
            LOG_LAST_ERROR(WARN_LEVEL, ("\nError cleaning up socket environment:"));
         }
      }
   }
}

static void releaseServer(SDB_LS_SERVERP server) {
   if (server) {
      LOG_MESSAGE(INFO_LEVEL, ("\nReleasing server..."));
      --s_serversCount;
      shutdownServer(server);
      releaseWorkers(server);
      cfl_hash_free(server->clients, CFL_TRUE);
      cfl_hash_free(server->databases, CFL_TRUE);
      cfl_lock_free(&server->criticalSection);
      cfl_lock_free(&server->lockDictionary);
      if (server->address) {
         cfl_str_free(server->address);
      }
      free(server);

      LOG_MESSAGE(INFO_LEVEL, ("\nServer released."));
   }
}

static void prepareResponse(SDB_LS_CLIENTP client, CFL_INT16 errCode) {
   cfl_buffer_reset(client->data);
   cfl_buffer_putInt32(client->data, 0);
   cfl_buffer_putInt16(client->data, errCode);
}

static void executeLogLevelCmd(SDB_LS_CLIENTP client) {
   CFL_UINT8 newLevel;
   prepareResponse(client, SDB_LS_RET_SUCCESS);
   newLevel = cfl_buffer_getUInt8(client->data);
   if (newLevel <= TRACE_LEVEL) {
      LOG_MESSAGE(WARN_LEVEL, ("\nLog level modified from %u to %u", s_iMessageLevel, newLevel));
      s_iMessageLevel = newLevel;
      prepareResponse(client, SDB_LS_RET_SUCCESS);
   } else {
      LOG_MESSAGE(ERROR_LEVEL, ("\nInvalid log level: %u", newLevel));
      prepareResponse(client, SDB_LS_RET_FAILED);
   }
}

static void executeConnectCmd(SDB_LS_CLIENTP client) {
   client->recnoSize = cfl_buffer_getUInt8(client->data);
   prepareResponse(client, SDB_LS_RET_SUCCESS);
   cfl_buffer_putInt32(client->data, client->id);
}

static void executeDisconnectCmd(SDB_LS_CLIENTP client) {
   prepareResponse(client, SDB_LS_RET_SUCCESS);
   client->isLoggingOut = CFL_TRUE;
}

static void postShutdownEvent(SDB_LS_SERVERP server) {
   ULONG_PTR completionKey = COMPLETION_KEY_SHUTDOWN;
   PostQueuedCompletionStatus(server->completionPort, 0, completionKey, NULL);
}

static void startExternalShutdownProc(SDB_LS_SERVERP server) {
   DWORD threadId = 0;
   CreateThread(NULL, 0, server->externalShutdownProc, (void *)server, 0,
                &threadId); // Thread address
}

static void executeShutdownCmd(SDB_LS_CLIENTP client) {
   if (client->server->externalShutdownProc) {
      client->execAfterResponse = startExternalShutdownProc;
   } else {
      client->execAfterResponse = postShutdownEvent;
   }
   prepareResponse(client, SDB_LS_RET_SUCCESS);
}

static CFL_BOOL commandIsCompleted(SDB_LS_CLIENTP client) {
   CFL_UINT32 bufferLen = cfl_buffer_length(client->data);
   LOG_MESSAGE(TRACE_LEVEL, ("\nCommand is completed: %u", bufferLen));
   if (bufferLen >= 4) {
      if (client->commandLen == 0) {
         CFL_UINT32 currentPos = cfl_buffer_position(client->data);
         cfl_buffer_rewind(client->data);
         client->commandLen = cfl_buffer_getInt32(client->data);
         cfl_buffer_setPosition(client->data, currentPos);
      }
      return bufferLen >= client->commandLen + 4;
   }
   return CFL_FALSE;
}

// Percorrer todos os registros bloqueados pelo cliente, liberando-os
static void clientReleaseLockedRecords(SDB_LS_CLIENTP client) {
   CFL_ITERATORP it;

   LOG_MESSAGE(TRACE_LEVEL,
               ("\nclientReleaseLockedRecords: client=%u total=%u", client->id, cfl_hash_count(client->lockedRecords)));
   it = cfl_hash_iterator(client->lockedRecords);
   while (cfl_iterator_hasNext(it)) {
      SDB_LS_REC_LOCKP recLock = (SDB_LS_REC_LOCKP)cfl_iterator_next(it);
      cfl_lock_acquire(&recLock->context->lock);
      cfl_hash_remove(recLock->context->lockedRecords, &recLock->recno);
      cfl_lock_release(&recLock->context->lock);
      LOG_MESSAGE(TRACE_LEVEL, ("\nRecord %u released", recLock->recno));
   }
   cfl_iterator_free(it);
   cfl_hash_clear(client->lockedRecords, CFL_TRUE);
}

static void clientReleaseContextLockedRecords(SDB_LS_CLIENTP client, SDB_LS_CONTEXTP ctx) {
   CFL_ITERATORP it;
   LOG_MESSAGE(TRACE_LEVEL, ("\nclientReleaseContextLockedRecords: context=%u", ctx->id));
   it = cfl_hash_iterator(client->lockedRecords);
   while (cfl_iterator_hasNext(it)) {
      SDB_LS_REC_LOCKP recLock;
      recLock = (SDB_LS_REC_LOCKP)cfl_iterator_next(it);
      if (recLock->context->id == ctx->id) {
         cfl_hash_remove(ctx->lockedRecords, &recLock->recno);
         cfl_iterator_remove(it);
         hashFreeLockRecord(NULL, recLock);
      }
   }
   cfl_iterator_free(it);
}

static void clientCloseContext(SDB_LS_CLIENTP client, SDB_LS_CONTEXTP ctx) {
   LOG_MESSAGE(TRACE_LEVEL,
               ("\nclientCloseContext: %s.%s@%s(%s)", cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue)));
   cfl_lock_acquire(&ctx->lock);
   clientReleaseContextLockedRecords(client, ctx);
   if (ctx->isOpenExclusive) {
      if (ctx->clientLocking == client) {
         ctx->isOpenExclusive = CFL_FALSE;
         ctx->clientLocking = NULL;
      }
   } else if (ctx->openShared > 0) {
      --ctx->openShared;
   }
   cfl_lock_release(&ctx->lock);
}

static void clientCloseAllContexts(SDB_LS_CLIENTP client) {
   CFL_ITERATORP it;
   LOG_MESSAGE(TRACE_LEVEL, ("\nClient closing all contexts: %s", cfl_hash_count(client->openContexts)));
   it = cfl_hash_iterator(client->openContexts);
   while (cfl_iterator_hasNext(it)) {
      clientCloseContext(client, (SDB_LS_CONTEXTP)cfl_iterator_next(it));
   }
   cfl_iterator_free(it);
}

static SDB_LS_DATABASEP getDatabase(SDB_LS_SERVERP server, CFL_STRP dbName) {
   LOG_MESSAGE(TRACE_LEVEL, ("\ngetDatabase: %s", cfl_str_getPtr(dbName)));
   return (SDB_LS_DATABASEP)cfl_hash_search(server->databases, dbName);
}

static SDB_LS_SCHEMAP getSchema(SDB_LS_DATABASEP database, CFL_STRP schName) {
   LOG_MESSAGE(TRACE_LEVEL, ("\ngetSchema: %s", cfl_str_getPtr(schName)));
   return (SDB_LS_SCHEMAP)cfl_hash_search(database->schemas, schName);
}

static SDB_LS_TABLEP getTable(SDB_LS_SCHEMAP schema, CFL_STRP tabName) {
   LOG_MESSAGE(TRACE_LEVEL, ("\ngetTable: %s", cfl_str_getPtr(tabName)));
   return (SDB_LS_TABLEP)cfl_hash_search(schema->tables, tabName);
}

static SDB_LS_CONTEXTP getContext(SDB_LS_TABLEP table, CFL_STRP ctxVal) {
   LOG_MESSAGE(TRACE_LEVEL, ("\ngetContext: %s", ctxVal ? cfl_str_getPtr(ctxVal) : "null"));
   if (ctxVal == NULL || cfl_str_isEmpty(ctxVal)) {
      return &table->defaultContext;
   } else {
      return (SDB_LS_CONTEXTP)cfl_hash_search(table->contexts, ctxVal);
   }
}

static SDB_LS_DATABASEP databaseNew(SDB_LS_SERVERP server, CFL_STRP dbName) {
   SDB_LS_DATABASEP database;

   database = (SDB_LS_DATABASEP)malloc(sizeof(SDB_LS_DATABASE));
   database->server = server;
   database->name = cfl_str_newStr(dbName);
   database->schemas = cfl_hash_new(16, &calcStringHash, &compareStrings, &hashFreeSchema);
   LOG_MESSAGE(TRACE_LEVEL, ("\nDatabase created: %s", cfl_str_getPtr(dbName)));
   return database;
}

static SDB_LS_DATABASEP getCreateDatabase(SDB_LS_SERVERP server, CFL_STRP dbName) {
   SDB_LS_DATABASEP database;

   database = getDatabase(server, dbName);
   if (database == NULL) {
      database = databaseNew(server, dbName);
      cfl_hash_insert(server->databases, database->name, database);
   }
   return database;
}

static SDB_LS_SCHEMAP schemaNew(SDB_LS_DATABASEP database, CFL_STRP schName) {
   SDB_LS_SCHEMAP schema = (SDB_LS_SCHEMAP)malloc(sizeof(SDB_LS_SCHEMA));
   schema->database = database;
   schema->name = cfl_str_newStr(schName);
   schema->tables = cfl_hash_new(32, &calcStringHash, &compareStrings, &hashFreeTable);
   LOG_MESSAGE(TRACE_LEVEL, ("\nSchema created: %s@%s", cfl_str_getPtr(schName), cfl_str_getPtr(database->name)));
   return schema;
}

static SDB_LS_SCHEMAP getCreateSchema(SDB_LS_DATABASEP database, CFL_STRP schName) {
   SDB_LS_SCHEMAP schema;

   schema = getSchema(database, schName);
   if (schema == NULL) {
      schema = schemaNew(database, schName);
      cfl_hash_insert(database->schemas, schema->name, schema);
   }
   return schema;
}

static SDB_LS_TABLEP tableNew(SDB_LS_SCHEMAP schema, CFL_STRP tabName) {
   SDB_LS_TABLEP table = (SDB_LS_TABLEP)malloc(sizeof(SDB_LS_TABLE));
   cfl_lock_init(&(table->defaultContext.lock));
   table->schema = schema;
   table->name = cfl_str_newStr(tabName);
   table->defaultContext.id = schema->database->server->nextContextId++;
   table->defaultContext.table = table;
   table->defaultContext.contextValue = &s_emptyString;
   table->defaultContext.openShared = 0;
   table->defaultContext.isOpenExclusive = CFL_FALSE;
   table->defaultContext.isLocked = CFL_FALSE;
   table->defaultContext.clientLocking = NULL;
   table->defaultContext.lockedRecords = cfl_hash_new(64, &calcRecnoHash, &compareRecno, &hashFreeLockRecord);
   table->contexts = cfl_hash_new(6, &calcStringHash, &compareStrings, &hashFreeContext);
   LOG_MESSAGE(TRACE_LEVEL, ("\nTable created: %s.%s@%s", cfl_str_getPtr(schema->name), cfl_str_getPtr(tabName),
                             cfl_str_getPtr(schema->database->name)));
   return table;
}

static SDB_LS_TABLEP getCreateTable(SDB_LS_SCHEMAP schema, CFL_STRP tabName) {
   SDB_LS_TABLEP table;

   table = getTable(schema, tabName);
   if (table == NULL) {
      table = tableNew(schema, tabName);
      cfl_hash_insert(schema->tables, table->name, table);
   }
   return table;
}

static SDB_LS_CONTEXTP contextNew(SDB_LS_TABLEP table, CFL_STRP ctxVal) {
   SDB_LS_CONTEXTP context = (SDB_LS_CONTEXTP)malloc(sizeof(SDB_LS_CONTEXT));
   cfl_lock_init(&(context->lock));
   context->id = table->schema->database->server->nextContextId++;
   context->table = table;
   context->contextValue = cfl_str_newStr(ctxVal);
   context->openShared = 0;
   context->isOpenExclusive = CFL_FALSE;
   context->isLocked = CFL_FALSE;
   context->clientLocking = NULL;
   context->lockedRecords = cfl_hash_new(64, &calcRecnoHash, &compareRecno, &hashFreeLockRecord);
   LOG_MESSAGE(TRACE_LEVEL, ("\nContext created %s for %s.%s@%s", cfl_str_getPtr(ctxVal), cfl_str_getPtr(table->schema->name),
                             cfl_str_getPtr(table->name), cfl_str_getPtr(table->schema->database->name)));
   return context;
}

static SDB_LS_CONTEXTP getCreateContext(SDB_LS_TABLEP table, CFL_STRP ctxVal) {
   SDB_LS_CONTEXTP context;

   context = getContext(table, ctxVal);
   if (context == NULL) {
      context = contextNew(table, ctxVal);
      cfl_hash_insert(table->contexts, context->contextValue, context);
   }
   return context;
}

static SDB_LS_CONTEXTP findCreateContext(SDB_LS_SERVERP server, CFL_STRP dbName, CFL_STRP schName, CFL_STRP tabName,
                                         CFL_STRP ctxVal) {
   SDB_LS_DATABASEP database;
   SDB_LS_SCHEMAP schema = NULL;
   SDB_LS_TABLEP table = NULL;
   SDB_LS_CONTEXTP context = NULL;

   LOG_MESSAGE(TRACE_LEVEL, ("\nfindCreateContext: %s.%s@%s(%s)", cfl_str_getPtr(schName), cfl_str_getPtr(tabName),
                             cfl_str_getPtr(dbName), cfl_str_getPtr(ctxVal)));
   cfl_lock_acquire(&server->lockDictionary);
   database = getDatabase(server, dbName);
   if (database) {
      schema = getSchema(database, schName);
      if (schema) {
         table = getTable(schema, tabName);
         if (table) {
            context = getContext(table, ctxVal);
         }
      }
   }
   cfl_lock_release(&server->lockDictionary);
   if (context == NULL) {
      cfl_lock_acquire(&server->lockDictionary);
      if (table == NULL) {
         if (schema == NULL) {
            if (database == NULL) {
               database = getCreateDatabase(server, dbName);
            }
            schema = getCreateSchema(database, schName);
         }
         table = getCreateTable(schema, tabName);
      }
      context = getCreateContext(table, ctxVal);
      cfl_lock_release(&server->lockDictionary);
   }
   return context;
}

static void executeOpenSharedCmd(SDB_LS_CLIENTP client) {
   CFL_STRP dbName;
   CFL_STRP schName;
   CFL_STRP tabName;
   CFL_STRP ctxVal;
   SDB_LS_CONTEXTP ctx;
   CFL_BOOL bOpen;

   dbName = STR_TRIMUP(cfl_buffer_getString(client->data));
   schName = STR_TRIMUP(cfl_buffer_getString(client->data));
   tabName = STR_TRIMUP(cfl_buffer_getString(client->data));
   ctxVal = STR_TRIMUP(cfl_buffer_getString(client->data));

   ctx = findCreateContext(client->server, dbName, schName, tabName, ctxVal);
   cfl_lock_acquire(&ctx->lock);
   if (!ctx->isOpenExclusive) {
      ++ctx->openShared;
      bOpen = CFL_TRUE;
   } else {
      bOpen = CFL_FALSE;
   }
   cfl_lock_release(&ctx->lock);

   if (bOpen) {
      prepareResponse(client, SDB_LS_RET_SUCCESS);
      cfl_buffer_putInt32(client->data, ctx->id);
      cfl_hash_insert(client->openContexts, &ctx->id, ctx);
      LOG_MESSAGE(TRACE_LEVEL, ("\n<SUCCESS> OPEN SHARED(%u): %u - %s.%s@%s[%s]", ctx->id, client->id, cfl_str_getPtr(schName),
                                cfl_str_getPtr(tabName), cfl_str_getPtr(dbName), cfl_str_getPtr(ctxVal), ctx->id));
   } else {
      prepareResponse(client, SDB_LS_RET_FAILED);
      LOG_MESSAGE(TRACE_LEVEL, ("\n<FAILED> OPEN SHARED(%u): %u - %s.%s@%s[%s]", ctx->id, client->id, cfl_str_getPtr(schName),
                                cfl_str_getPtr(tabName), cfl_str_getPtr(dbName), cfl_str_getPtr(ctxVal)));
   }

   cfl_str_free(dbName);
   cfl_str_free(schName);
   cfl_str_free(tabName);
   cfl_str_free(ctxVal);
}

static void executeOpenExclusiveCmd(SDB_LS_CLIENTP client) {
   CFL_STRP dbName;
   CFL_STRP schName;
   CFL_STRP tabName;
   CFL_STRP ctxVal;
   SDB_LS_CONTEXTP ctx;
   CFL_BOOL bOpen;

   dbName = STR_TRIMUP(cfl_buffer_getString(client->data));
   schName = STR_TRIMUP(cfl_buffer_getString(client->data));
   tabName = STR_TRIMUP(cfl_buffer_getString(client->data));
   ctxVal = STR_TRIMUP(cfl_buffer_getString(client->data));

   ctx = findCreateContext(client->server, dbName, schName, tabName, ctxVal);
   cfl_lock_acquire(&ctx->lock);
   if (ctx->openShared == 0 && !ctx->isOpenExclusive) {
      ctx->isOpenExclusive = CFL_TRUE;
      ctx->clientLocking = client;
      bOpen = CFL_TRUE;
   } else {
      bOpen = CFL_FALSE;
   }
   cfl_lock_release(&ctx->lock);

   if (bOpen) {
      prepareResponse(client, SDB_LS_RET_SUCCESS);
      cfl_buffer_putInt32(client->data, ctx->id);
      cfl_hash_insert(client->openContexts, &ctx->id, ctx);
      LOG_MESSAGE(TRACE_LEVEL, ("\n<SUCCESS> OPEN EXCLUSIVE(%u): %u - %s.%s@%s[%s]", ctx->id, client->id, cfl_str_getPtr(schName),
                                cfl_str_getPtr(tabName), cfl_str_getPtr(dbName), cfl_str_getPtr(ctxVal)));
   } else {
      prepareResponse(client, SDB_LS_RET_FAILED);
      LOG_MESSAGE(TRACE_LEVEL, ("\n<FAILED> OPEN EXCLUSIVE(%u): %u - %s.%s@%s[%s]", ctx->id, client->id, cfl_str_getPtr(schName),
                                cfl_str_getPtr(tabName), cfl_str_getPtr(dbName), cfl_str_getPtr(ctxVal)));
   }

   cfl_str_free(dbName);
   cfl_str_free(schName);
   cfl_str_free(tabName);
   cfl_str_free(ctxVal);
}

static void executeCloseCmd(SDB_LS_CLIENTP client) {
   CFL_UINT32 ctxId;
   SDB_LS_CONTEXTP ctx;

   ctxId = (CFL_UINT32)cfl_buffer_getInt32(client->data);
   ctx = (SDB_LS_CONTEXTP)cfl_hash_search(client->openContexts, &ctxId);

   if (ctx) {
      clientCloseContext(client, ctx);
      cfl_hash_remove(client->openContexts, &ctx->id);
      prepareResponse(client, SDB_LS_RET_SUCCESS);
      LOG_MESSAGE(TRACE_LEVEL, ("\n<SUCCESS> CLOSE: %u - %s.%s@%s[%s]", client->id, cfl_str_getPtr(ctx->table->schema->name),
                                cfl_str_getPtr(ctx->table->name), cfl_str_getPtr(ctx->table->schema->database->name),
                                cfl_str_getPtr(ctx->contextValue)));
   } else {
      prepareResponse(client, SDB_LS_RET_FAILED);
      LOG_MESSAGE(ERROR_LEVEL, ("\n<FAILED> CLOSE: %u - context %u not found", client->id, ctxId));
   }
}

static void executeCloseAllCmd(SDB_LS_CLIENTP client) {
   clientCloseAllContexts(client);
   prepareResponse(client, SDB_LS_RET_SUCCESS);
}

static void executeLockFileCmd(SDB_LS_CLIENTP client) {
   CFL_UINT32 ctxId;
   SDB_LS_CONTEXTP ctx;
   CFL_INT16 errCode;

   ctxId = (CFL_UINT32)cfl_buffer_getInt32(client->data);
   ctx = (SDB_LS_CONTEXTP)cfl_hash_search(client->openContexts, &ctxId);

   if (ctx) {
      cfl_lock_acquire(&ctx->lock);
      if (!ctx->isLocked) {
         if (cfl_hash_count(ctx->lockedRecords) == 0) {
            ctx->isLocked = CFL_TRUE;
            ctx->clientLocking = client;
            errCode = SDB_LS_RET_SUCCESS;
            LOG_MESSAGE(TRACE_LEVEL, ("\n<SUCCESS> LOCK FILE: %u - %s.%s@%s[%s]", client->id,
                                      cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                      cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue)));
         } else {
            errCode = SDB_LS_RET_FAILED;
            LOG_MESSAGE(TRACE_LEVEL, ("\n<FAILED> LOCK FILE: %u - %s.%s@%s[%s] with locked records", client->id,
                                      cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                      cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue)));
         }
      } else if (ctx->clientLocking == client) {
         errCode = SDB_LS_RET_SUCCESS;
         LOG_MESSAGE(TRACE_LEVEL, ("\n<SUCCESS> LOCK FILE: %u - %s.%s@%s[%s] already locked by "
                                   "client himslef",
                                   client->id, cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                   cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue)));
      } else {
         errCode = SDB_LS_RET_FAILED;
         LOG_MESSAGE(TRACE_LEVEL, ("\n<FAILED> LOCK FILE: %u - %s.%s@%s[%s] locked by client %u", client->id,
                                   cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                   cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue),
                                   ctx->clientLocking->id));
      }
      cfl_lock_release(&ctx->lock);
   } else {
      errCode = SDB_LS_RET_FAILED;
      LOG_MESSAGE(ERROR_LEVEL, ("\n<FAILED> LOCK FILE: %u - context %u not found", client->id, ctxId));
   }
   prepareResponse(client, errCode);
}

static void executeLockRecCmd(SDB_LS_CLIENTP client) {
   CFL_UINT32 ctxId;
   SDB_LS_RECNO recno;
   SDB_LS_CONTEXTP ctx;
   CFL_INT16 errCode;
   SDB_LS_REC_LOCKP recLock = NULL;

   ctxId = (CFL_UINT32)cfl_buffer_getInt32(client->data);
   if (client->recnoSize == 4) {
      recno = (SDB_LS_RECNO)cfl_buffer_getInt32(client->data);
   } else {
      recno = (SDB_LS_RECNO)cfl_buffer_getInt64(client->data);
   }
   ctx = (SDB_LS_CONTEXTP)cfl_hash_search(client->openContexts, &ctxId);

   if (ctx) {
      cfl_lock_acquire(&ctx->lock);
      if (!ctx->isLocked) {
         if (!ctx->isOpenExclusive) {
            recLock = (SDB_LS_REC_LOCKP)cfl_hash_search(ctx->lockedRecords, &recno);
            if (recLock) {
               if (recLock->clientLocking == client) {
                  errCode = SDB_LS_RET_SUCCESS;
                  LOG_MESSAGE(TRACE_LEVEL,
                              ("\n<SUCCESS> LOCK REC: %u - record %u of %s.%s@%s[%s] "
                               "locked by client himself",
                               client->id, recno, cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                               cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue)));
               } else {
                  errCode = SDB_LS_RET_FAILED;
                  LOG_MESSAGE(TRACE_LEVEL, ("\n<FAILED> LOCK REC: %u - record %u of %s.%s@%s[%s] "
                                            "locked by %u",
                                            client->id, recno, cfl_str_getPtr(ctx->table->schema->name),
                                            cfl_str_getPtr(ctx->table->name), cfl_str_getPtr(ctx->table->schema->database->name),
                                            cfl_str_getPtr(ctx->contextValue), recLock->clientLocking->id));
               }
               recLock = NULL;
            } else {
               recLock = (SDB_LS_REC_LOCKP)malloc(sizeof(SDB_LS_REC_LOCK));
               recLock->context = ctx;
               recLock->recno = recno;
               recLock->clientLocking = client;
               cfl_hash_insert(ctx->lockedRecords, &recLock->recno, recLock);
               errCode = SDB_LS_RET_SUCCESS;
               LOG_MESSAGE(TRACE_LEVEL, ("\n<SUCCESS> LOCK REC: %u - record %u of %s.%s@%s[%s] locked", client->id, recno,
                                         cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                         cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue)));
            }
         } else if (ctx->clientLocking == client) {
            errCode = SDB_LS_RET_SUCCESS;
            LOG_MESSAGE(TRACE_LEVEL, ("\n<SUCCESS> LOCK REC: %u - record %u of %s.%s@%s[%s]. "
                                      "Context open in exclusive mode by client himself",
                                      client->id, recno, cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                      cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue)));
         } else {
            errCode = SDB_LS_RET_FAILED;
            LOG_MESSAGE(TRACE_LEVEL, ("\n<FAILED> LOCK REC: %u - record %u of %s.%s@%s[%s]. Context "
                                      "open in exclusive mode by %u",
                                      client->id, recno, cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                      cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue),
                                      ctx->clientLocking->id));
         }
      } else if (ctx->clientLocking == client) {
         errCode = SDB_LS_RET_SUCCESS;
         LOG_MESSAGE(TRACE_LEVEL, ("\n<SUCCESS> LOCK REC: %u - record %u of %s.%s@%s[%s]. "
                                   "Context locked by client himself",
                                   client->id, recno, cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                   cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue)));
      } else {
         errCode = SDB_LS_RET_FAILED;
         LOG_MESSAGE(TRACE_LEVEL, ("\n<FAILED> LOCK REC: %u - record %u of %s.%s@%s[%s]. "
                                   "Context locked by %u",
                                   client->id, recno, cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                   cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue),
                                   ctx->clientLocking->id));
      }
      cfl_lock_release(&ctx->lock);
   } else {
      errCode = SDB_LS_RET_FAILED;
      LOG_MESSAGE(ERROR_LEVEL, ("\n<FAILED> LOCK REC: %u - record %u - context %u not found", client->id, recno, ctxId));
   }
   if (recLock != NULL) {
      cfl_hash_insert(client->lockedRecords, &recLock->recno, recLock);
   }
   prepareResponse(client, errCode);
}

static void executeUnlockRecCmd(SDB_LS_CLIENTP client) {
   CFL_UINT32 ctxId;
   SDB_LS_RECNO recno;
   SDB_LS_CONTEXTP ctx;
   CFL_INT16 errCode;
   SDB_LS_REC_LOCKP recLock;

   ctxId = (CFL_UINT32)cfl_buffer_getInt32(client->data);
   if (client->recnoSize == 4) {
      recno = (SDB_LS_RECNO)cfl_buffer_getInt32(client->data);
   } else {
      recno = (SDB_LS_RECNO)cfl_buffer_getInt64(client->data);
   }
   ctx = (SDB_LS_CONTEXTP)cfl_hash_search(client->openContexts, &ctxId);

   if (ctx) {
      recLock = (SDB_LS_REC_LOCKP)cfl_hash_search(client->lockedRecords, &recno);
      if (recLock) {
         cfl_lock_acquire(&ctx->lock);
         cfl_hash_remove(ctx->lockedRecords, &recLock->recno);
         /* Context locked by client? */
         if (ctx->isLocked && ctx->clientLocking == client) {
            ctx->isLocked = CFL_FALSE;
            ctx->clientLocking = NULL;
         }
         cfl_lock_release(&ctx->lock);
         cfl_hash_remove(client->lockedRecords, &recLock->recno);
         hashFreeLockRecord(NULL, recLock);
         errCode = SDB_LS_RET_SUCCESS;
         LOG_MESSAGE(TRACE_LEVEL, ("\n<SUCCESS> UNLOCK REC: %u - record %u of %s.%s@%s[%s] unlocked", client->id, recno,
                                   cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                   cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue)));
         /* Context locked by client? */
      } else if (ctx->isLocked && ctx->clientLocking == client) {
         cfl_lock_acquire(&ctx->lock);
         ctx->isLocked = CFL_FALSE;
         ctx->clientLocking = NULL;
         cfl_lock_release(&ctx->lock);
         errCode = SDB_LS_RET_SUCCESS;
         LOG_MESSAGE(TRACE_LEVEL, ("\n<SUCCESS> UNLOCK REC: %u - record %u of %s.%s@%s[%s] not "
                                   "found. Context unlocked",
                                   client->id, recno, cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                   cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue)));
      } else {
         errCode = SDB_LS_RET_FAILED;
         LOG_MESSAGE(TRACE_LEVEL, ("\n<FAILED> UNLOCK REC: %u - record %u of %s.%s@%s[%s] not found", client->id, recno,
                                   cfl_str_getPtr(ctx->table->schema->name), cfl_str_getPtr(ctx->table->name),
                                   cfl_str_getPtr(ctx->table->schema->database->name), cfl_str_getPtr(ctx->contextValue)));
      }
   } else {
      errCode = SDB_LS_RET_FAILED;
      LOG_MESSAGE(ERROR_LEVEL, ("\n<FAILED> UNLOCK REC: %u - record %u - context %u not found", client->id, recno, ctxId));
   }
   prepareResponse(client, errCode);
}

static void executeUnlockAllCmd(SDB_LS_CLIENTP client) {
   CFL_UINT32 ctxId;
   SDB_LS_CONTEXTP ctx;
   CFL_INT16 errCode;

   ctxId = (CFL_UINT32)cfl_buffer_getInt32(client->data);
   ctx = (SDB_LS_CONTEXTP)cfl_hash_search(client->openContexts, &ctxId);

   if (ctx) {
      CFL_ITERATORP it;
      // Percorrer todos os registros bloqueados pelo cliente, verificando os
      // bloqueios do contexto e liberando-os
      it = cfl_hash_iterator(client->lockedRecords);
      cfl_lock_acquire(&ctx->lock);
      while (cfl_iterator_hasNext(it)) {
         SDB_LS_REC_LOCKP recLock = (SDB_LS_REC_LOCKP)cfl_iterator_next(it);
         if (recLock->context == ctx) {
            cfl_hash_remove(ctx->lockedRecords, &recLock->recno);
            cfl_iterator_remove(it);
            hashFreeLockRecord(NULL, recLock);
         }
      }
      if (ctx->isLocked && ctx->clientLocking == client) {
         ctx->isLocked = CFL_FALSE;
         ctx->clientLocking = NULL;
      }
      cfl_lock_release(&ctx->lock);
      cfl_iterator_free(it);
      errCode = SDB_LS_RET_SUCCESS;
      LOG_MESSAGE(TRACE_LEVEL, ("\n<SUCCESS> UNLOCK ALL: %u - context %u. Records unlocked", client->id, ctxId));
   } else {
      errCode = SDB_LS_RET_FAILED;
      LOG_MESSAGE(ERROR_LEVEL, ("\n<FAILED> UNLOCK ALL: %u - context %u not found", client->id, ctxId));
   }
   prepareResponse(client, errCode);
}

static void executeListSessionsCmd(SDB_LS_CLIENTP client) {
   SDB_LS_SERVERP server = client->server;
   SDB_LS_CLIENTP clientList;
   CFL_ITERATORP it;

   LOG_MESSAGE(TRACE_LEVEL, ("\nListing sessions..."));
   prepareResponse(client, SDB_LS_RET_SUCCESS);
   cfl_lock_acquire(&server->criticalSection);
   it = cfl_hash_iterator(server->clients);
   while (cfl_iterator_hasNext(it)) {
      clientList = (SDB_LS_CLIENTP)cfl_iterator_next(it);
      cfl_buffer_putInt8(client->data, (CFL_INT8)'C');
      cfl_buffer_putInt32(client->data, clientList->id);
      cfl_buffer_putString(client->data, clientList->address);
      cfl_buffer_putInt32(client->data, cfl_hash_count(clientList->openContexts));
      cfl_buffer_putInt32(client->data, cfl_hash_count(clientList->lockedRecords));
   }
   cfl_lock_release(&server->criticalSection);
   cfl_iterator_free(it);
   cfl_buffer_putInt8(client->data, (CFL_INT8)'E');
}

static void executeSessionInfoCmd(SDB_LS_CLIENTP client) {
   CFL_UINT32 clientId;
   SDB_LS_CLIENTP clientInfo;

   clientId = (CFL_UINT32)cfl_buffer_getInt32(client->data);
   cfl_lock_acquire(&client->server->criticalSection);
   clientInfo = cfl_hash_search(client->server->clients, &clientId);
   /* If found client, lock it in shared mode */
   if (clientInfo) {
      cfl_lock_acquire(&clientInfo->lock);
   }
   cfl_lock_release(&client->server->criticalSection);
   if (clientInfo) {
      CFL_ITERATORP it;
      /* Send using contexts */
      it = cfl_hash_iterator(clientInfo->openContexts);
      prepareResponse(client, SDB_LS_RET_SUCCESS);
      while (cfl_iterator_hasNext(it)) {
         SDB_LS_CONTEXTP ctx = (SDB_LS_CONTEXTP)cfl_iterator_next(it);
         cfl_lock_acquire(&ctx->lock);
         cfl_buffer_putInt8(client->data, (CFL_INT8)'T');
         cfl_buffer_putInt32(client->data, ctx->id);
         cfl_buffer_putString(client->data, ctx->table->schema->database->name);
         cfl_buffer_putString(client->data, ctx->table->schema->name);
         cfl_buffer_putString(client->data, ctx->table->name);
         cfl_buffer_putString(client->data, ctx->contextValue);
         if (ctx->openShared > 0) {
            cfl_buffer_putInt8(client->data, (CFL_INT8)'S');
         } else {
            cfl_buffer_putInt8(client->data, (CFL_INT8)'X');
         }
         if (ctx->isLocked && ctx->clientLocking == clientInfo) {
            cfl_buffer_putInt8(client->data, (CFL_INT8)'Y');
         } else {
            cfl_buffer_putInt8(client->data, (CFL_INT8)'N');
         }
         cfl_lock_release(&ctx->lock);
      }
      cfl_iterator_free(it);
      /* Send locked records */
      it = cfl_hash_iterator(clientInfo->lockedRecords);
      while (cfl_iterator_hasNext(it)) {
         SDB_LS_REC_LOCKP rec = (SDB_LS_REC_LOCKP)cfl_iterator_next(it);
         cfl_lock_acquire(&rec->context->lock);
         cfl_buffer_putInt8(client->data, (CFL_INT8)'R');
         cfl_buffer_putInt32(client->data, rec->context->id);
         cfl_buffer_putInt64(client->data, (CFL_INT64)rec->recno);
         cfl_lock_release(&rec->context->lock);
      }
      /* Release client lock */
      cfl_lock_release(&clientInfo->lock);
      cfl_iterator_free(it);
      cfl_buffer_putInt8(client->data, (CFL_INT8)'E');
   } else {
      prepareResponse(client, SDB_LS_RET_FAILED);
      cfl_buffer_putCharArrayLen(client->data, "Cliente nao encontrado", 22);
   }
}

static void executeCommand(SDB_LS_CLIENTP client) {
   ULONG_PTR completionKey;
   char cmdType;
   cfl_buffer_rewind(client->data);
   cfl_buffer_skip(client->data, 4);
   cmdType = (char)cfl_buffer_getInt8(client->data);
   switch (cmdType) {
   case SDB_LS_CMD_OPEN_SHARED:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[OPEN SHARED] command."));
      executeOpenSharedCmd(client);
      break;

   case SDB_LS_CMD_OPEN_EXCLUSIVE:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[OPEN EXCLUSIVE] command."));
      executeOpenExclusiveCmd(client);
      break;

   case SDB_LS_CMD_CLOSE:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[CLOSE] command."));
      executeCloseCmd(client);
      break;

   case SDB_LS_CMD_CLOSE_ALL:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[CLOSE ALL] command."));
      executeCloseAllCmd(client);
      break;

   case SDB_LS_CMD_LOCK_FILE:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[LOCK FILE] command."));
      executeLockFileCmd(client);
      break;

   case SDB_LS_CMD_LOCK_REC:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[LOCK RECORD] command."));
      executeLockRecCmd(client);
      break;

   case SDB_LS_CMD_UNLOCK_REC:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[UNLOCK RECORD] command."));
      executeUnlockRecCmd(client);
      break;

   case SDB_LS_CMD_UNLOCK_ALL:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[UNLOCK ALL] command."));
      executeUnlockAllCmd(client);
      break;

   case SDB_LS_CMD_LIST_SESSIONS:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[LIST SESSIONS] command."));
      executeListSessionsCmd(client);
      break;

   case SDB_LS_CMD_SESSION_INFO:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[SESSION INFO] command."));
      executeSessionInfoCmd(client);
      break;

   case SDB_LS_CMD_CONNECT:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[CONNECT] command."));
      executeConnectCmd(client);
      break;

   case SDB_LS_CMD_DISCONNECT:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[DISCONNECT] command."));
      executeDisconnectCmd(client);
      break;

   case SDB_LS_CMD_LOG_LEVEL:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[LOG LEVEL] command."));
      executeLogLevelCmd(client);
      break;

   case SDB_LS_CMD_SHUTDOWN:
      LOG_MESSAGE(TRACE_LEVEL, ("\n[SHUTDOWN] command."));
      executeShutdownCmd(client);
      break;

   default:
      LOG_MESSAGE(ERROR_LEVEL, ("\nInvalid command."));
      prepareResponse(client, SDB_LS_RET_INVALID_REQUEST);
      cfl_buffer_putCharArray(client->data, "Unknown command");
      break;
   }
   // Manda escrever a resposta
   client->operation = WAIT_WRITE;
   completionKey = COMPLETION_KEY_IO;
   /* Escreve o tamanho do pacote no inicio do buffer */
   cfl_buffer_rewind(client->data);
   cfl_buffer_putInt32(client->data, cfl_buffer_length(client->data) - 4);
   /* Reposiciona no inicio do buffer para escrever no socket */
   cfl_buffer_rewind(client->data);
   PostQueuedCompletionStatus(client->server->completionPort, 0, completionKey, &client->overlapped);
}

static void clientDisconnection(SDB_LS_CLIENTP client, CFL_BOOL bGracefull) {
   LOG_MESSAGE(TRACE_LEVEL, ("\nClient disconnected %u.", client->id));
   cfl_lock_acquire(&client->server->criticalSection);
   cfl_hash_remove(client->server->clients, &client->id);
   cfl_lock_release(&client->server->criticalSection);
   releaseClient(client, bGracefull);
}

static void waitRead(SDB_LS_CLIENTP client) {
   WSABUF wsaBuffer;
   DWORD flags = MSG_PARTIAL;
   DWORD bytesRead = 0;
   int result;

   LOG_MESSAGE(TRACE_LEVEL, ("\nReading socket."));
   client->operation = WAIT_READ;
   wsaBuffer.buf = (char *)client->ioBuffer;
   wsaBuffer.len = sizeof(client->ioBuffer);
   result = WSARecv(client->socket, &wsaBuffer, 1, &bytesRead, &flags, &client->overlapped, NULL);
   if (result == SOCKET_ERROR && WSAGetLastError() != ERROR_IO_PENDING) {
      LOG_LAST_ERROR(ERROR_LEVEL, ("\nClient disconnected. Possible socket error:"));
      clientDisconnection(client, CFL_FALSE);
   }
}

static SDB_LS_CLIENTP createClient(SDB_LS_SERVERP server, SOCKET clientSocket, struct sockaddr_storage *sockAddr) {
   SDB_LS_CLIENTP client;
   char addrBuffer[INET6_ADDRSTRLEN];

   LOG_MESSAGE(TRACE_LEVEL, ("\nCreating client..."));
   client = (SDB_LS_CLIENTP)malloc(sizeof(SDB_LS_CLIENT));
   cfl_lock_init(&(client->lock));
   memset(&(client->overlapped), 0, sizeof(WSAOVERLAPPED));
   client->server = server;
   client->socket = clientSocket;
   client->commandLen = 0;
   client->execAfterResponse = NULL;
   client->recnoSize = sizeof(SDB_LS_RECNO);
   client->data = cfl_buffer_new();
   client->openContexts = cfl_hash_new(64, &calcIdHash, &compareIds, &hashFreeContext);
   client->lockedRecords = cfl_hash_new(16, &calcRecnoHash, &compareRecno, &hashFreeLockRecord);
   printableAddress((struct sockaddr_storage *)sockAddr, addrBuffer, sizeof(addrBuffer));
   client->address = cfl_str_newBuffer(addrBuffer);
   client->isLoggingOut = CFL_FALSE;
   cfl_lock_acquire(&server->criticalSection);
   client->id = server->nextClientId++;
   cfl_hash_insert(server->clients, &client->id, client);
   cfl_lock_release(&server->criticalSection);
   LOG_MESSAGE(TRACE_LEVEL, ("\nClient %u created", client->id));
   return client;
}

static void releaseClient(SDB_LS_CLIENTP client, CFL_BOOL bGraceful) {
   if (client) {
      LINGER lingerStruct;

      LOG_MESSAGE(TRACE_LEVEL, ("\nReleasing client %u...", client->id));
      cfl_lock_acquire(&client->lock);

      //
      // If we're supposed to abort the connection, set the linger value
      // on the socket to 0.
      //
      if (!bGraceful) {
         lingerStruct.l_onoff = 1;
         lingerStruct.l_linger = 0;
         setsockopt(client->socket, SOL_SOCKET, SO_LINGER, (char *)&lingerStruct, sizeof(lingerStruct));
      }

      // Now close the socket handle.  This will do an abortive or  graceful
      // close, as requested.
      CancelIo((HANDLE)client->socket);
      closesocket(client->socket);
      client->socket = INVALID_SOCKET;
      cfl_buffer_free(client->data);
      cfl_str_free(client->address);
      clientReleaseLockedRecords(client);
      cfl_hash_free(client->lockedRecords, CFL_FALSE);
      clientCloseAllContexts(client);
      cfl_hash_free(client->openContexts, CFL_FALSE);
      cfl_lock_release(&client->lock);
      cfl_lock_free(&client->lock);
      LOG_MESSAGE(TRACE_LEVEL, ("\nClient %u removed.", client->id));
      free(client);
   }
}

static SDB_LS_WORKERP createWorkerThread(SDB_LS_SERVERP server) {
   SDB_LS_WORKERP worker;
   DWORD threadId = 0;

   LOG_MESSAGE(TRACE_LEVEL, ("\nCreating worker thread..."));
   worker = (SDB_LS_WORKERP)malloc(sizeof(SDB_LS_WORKER));
   if (worker) {
      worker->server = server;
      worker->isRunning = CFL_FALSE;
      worker->hThread = CreateThread(NULL,             // Security
                                     0,                // Stack size - use default
                                     workerThreadProc, // Thread fn entry point
                                     (void *)worker,   // Param
                                     0,                // Init flag
                                     &threadId);       // Thread address

      if (worker->hThread != NULL) {
         // add to list of worker threads
         cfl_lock_acquire(&server->criticalSection);
         if (server->tailWorker) {
            server->tailWorker->next = worker;
         }
         worker->previous = server->tailWorker;
         worker->next = NULL;
         server->tailWorker = worker;
         ++server->workersCount;
         cfl_lock_release(&server->criticalSection);
      } else {
         free(worker);
         worker = NULL;
      }
   }
   return worker;
}

static void readRequest(SDB_LS_CLIENTP client, DWORD ioSize) {
   LOG_MESSAGE(TRACE_LEVEL, ("\nreadRequest: client=%u, ioSize=%u.", client->id, ioSize));
   cfl_buffer_put(client->data, client->ioBuffer, ioSize);

   // Has the client finished sending the request?
   if (commandIsCompleted(client)) {
      LOG_MESSAGE(TRACE_LEVEL, ("\nRequest completed."));
      executeCommand(client);
   } else {
      // The client is not finished. If data was read this pass, we
      // assume the connection is still good and read more.  If not,
      // we assume that the client closed the socket prematurely.
      if (ioSize > 0) {
         LOG_MESSAGE(TRACE_LEVEL, ("\nPartial request received. Waiting more data..."));
         waitRead(client);
      } else {
         if (!client->isLoggingOut) {
            LOG_LAST_ERROR(ERROR_LEVEL, ("\nConnection lost in readRequest (ioSize == 0):"));
         }
         clientDisconnection(client, CFL_FALSE);
         return;
      }
   }
}

static void waitNextCommand(SDB_LS_CLIENTP client) {
   client->commandLen = 0;
   cfl_buffer_reset(client->data);
   waitRead(client);
}

static void writeResponse(SDB_LS_CLIENTP client) {
   if (cfl_buffer_remaining(client->data) == 0) {
      LOG_MESSAGE(TRACE_LEVEL, ("\nwriteResponse: all data sent."));
      if (client->execAfterResponse) {
         SDB_LS_REQ_FUN fun = client->execAfterResponse;
         client->execAfterResponse = NULL;
         fun(client->server);
      }
      waitNextCommand(client);
   } else {
      WSABUF wsaBuffer;
      int retVal;
      CFL_UINT32 flags = MSG_PARTIAL;
      CFL_INT32 len;
      DWORD bytesSent;

      if (cfl_buffer_remaining(client->data) > sizeof(client->ioBuffer)) {
         len = SDB_LS_IO_BUFFER_SIZE;
         cfl_buffer_copy(client->data, client->ioBuffer, len);
      } else {
         len = cfl_buffer_remaining(client->data);
         cfl_buffer_copy(client->data, client->ioBuffer, len);
      }
      client->operation = WAIT_WRITE;
      wsaBuffer.buf = (char *)client->ioBuffer;
      wsaBuffer.len = len;
      LOG_MESSAGE(TRACE_LEVEL, ("\nwriteResponse: data length: %u.", len));
      retVal = WSASend(client->socket, &wsaBuffer, 1, &bytesSent, flags, &client->overlapped, NULL);

      if (retVal == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
         LOG_LAST_ERROR(ERROR_LEVEL, ("\nConnection lost in writeResponse:"));
         clientDisconnection(client, CFL_FALSE);
      }
   }
}

static void processIOMessage(SDB_LS_CLIENTP client, DWORD ioSize) {
   switch (client->operation) {

   case WAIT_READ:
      LOG_MESSAGE(TRACE_LEVEL, ("\nRead operation."));
      readRequest(client, ioSize);
      break;

   case WAIT_WRITE:
      LOG_MESSAGE(TRACE_LEVEL, ("\nWrite operation."));
      writeResponse(client);
      break;
   }
}

static DWORD WINAPI workerThreadProc(LPVOID param) {
   SDB_LS_WORKERP worker;
   SDB_LS_SERVERP server;
   HANDLE completionPort;
   ULONG_PTR completionKey;
   SDB_LS_CLIENTP client;
   LPWSAOVERLAPPED lpOverlapped;
   CFL_BOOL bIORet;
   DWORD ioSize;
   CFL_BOOL shutdownSrv = CFL_FALSE;

   LOG_MESSAGE(TRACE_LEVEL, ("\nStarting worker procedure..."));
   worker = (SDB_LS_WORKERP)param;
   server = worker->server;
   completionPort = server->completionPort;

   InterlockedIncrement((LONG *)&server->workersBusy);
   worker->isRunning = CFL_TRUE;

   //
   // Loop round and round servicing I/O completions.
   //
   while (worker->isRunning && server->isRunning) {

      completionKey = COMPLETION_KEY_NONE;
      InterlockedDecrement((LONG *)&server->workersBusy);
      // Get a completed IO request.
      bIORet = (CFL_BOOL)GetQueuedCompletionStatus(completionPort, &ioSize, &completionKey, &lpOverlapped, 30000);
      InterlockedIncrement((LONG *)&server->workersBusy);

      if (bIORet) {
         switch (completionKey) {
         case COMPLETION_KEY_IO:
            LOG_MESSAGE(TRACE_LEVEL, ("\nClient IO..."));
            /* If is possible, keep one thread free to response to new IO operations
             */
            if (server->workersBusy == server->workersCount && server->workersCount < server->maxWorkers) {
               createWorkerThread(server);
            }
            if (lpOverlapped) {
               client = CONTAINING_RECORD(lpOverlapped, SDB_LS_CLIENT, overlapped);
               if (client) {
                  processIOMessage(client, ioSize);
               }
            }
            break;

         case COMPLETION_KEY_KILL_WORKER:
            LOG_MESSAGE(TRACE_LEVEL, ("\nKill worker..."));
            worker->isRunning = CFL_FALSE;
            break;

         case COMPLETION_KEY_SHUTDOWN:
            LOG_MESSAGE(TRACE_LEVEL, ("\nShutdown server..."));
            worker->isRunning = CFL_FALSE;
            shutdownSrv = CFL_TRUE;
            break;

         case COMPLETION_KEY_DISCONNECT:
            if (lpOverlapped) {
               client = CONTAINING_RECORD(lpOverlapped, SDB_LS_CLIENT, overlapped);
               if (client) {
                  clientDisconnection(client, CFL_TRUE);
               }
            }
            break;
         }
      } else {
         if (GetLastError() == WAIT_TIMEOUT) {
            if (server->workersCount > server->minWorkers) {
               LOG_MESSAGE(TRACE_LEVEL, ("\nTimeout waiting work. Worker thread released."));
               worker->isRunning = CFL_FALSE;
            }
         } else {
            if (lpOverlapped) {
               client = CONTAINING_RECORD(lpOverlapped, SDB_LS_CLIENT, overlapped);
               if (client) {
                  LOG_LAST_ERROR(ERROR_LEVEL, ("\nConnection lost:"));
                  clientDisconnection(client, CFL_FALSE);
               }
            }
            continue;
         }
      }
   }
   LOG_MESSAGE(TRACE_LEVEL, ("\nFinishing worker thread..."));
   InterlockedDecrement((LONG *)&server->workersBusy);
   releaseWorker(worker);
   if (shutdownSrv) {
      shutdownServer(server);
   }
   return 0;
}

static CFL_BOOL initializeIOCP(SDB_LS_SERVERP server) {
   LOG_MESSAGE(INFO_LEVEL, ("\nStarting IOCP..."));
   server->completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
   if (server->completionPort == NULL) {
      return CFL_FALSE;
   }

   return CFL_TRUE;
}

static CFL_BOOL createWorkerThreads(SDB_LS_SERVERP server) {
   int i;
   LOG_MESSAGE(INFO_LEVEL, ("\nCreating worker threads..."));
   for (i = 0; i < server->minWorkers; i++) {
      if (!createWorkerThread(server)) {
         return CFL_FALSE;
      }
   }
   return CFL_TRUE;
}

static void acceptConnection(SDB_LS_SERVERP server) {
   struct sockaddr_storage sockAddr;
   SOCKET clientSocket;
   int result;
   int len;
   SDB_LS_CLIENTP client;
   const char chOpt = 1;
   HANDLE handle;
   ULONG_PTR completionKey;

   if (!server->isRunning) {
      return;
   }

   LOG_MESSAGE(TRACE_LEVEL, ("\nReceiving connection..."));
   //
   // accept the new socket descriptor
   //
   len = sizeof(sockAddr);
   clientSocket = accept(server->listenSocket, (LPSOCKADDR)&sockAddr, &len);

   if (clientSocket == SOCKET_ERROR) {
      //      if (WSAGetLastError() != WSAEWOULDBLOCK) {
      LOG_LAST_ERROR(ERROR_LEVEL, ("\naccept() error:"));
      return;
      //      }
   }

   result = setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, &chOpt, sizeof(char));
   if (result == SOCKET_ERROR) {
      LOG_LAST_ERROR(ERROR_LEVEL, ("\nsetsockopt() error:"));
      return;
   }

   // Create the Client context to be associted with the completion port
   client = createClient(server, clientSocket, &sockAddr);

   // Associate the new socket with a completion port.
   completionKey = COMPLETION_KEY_IO;
   handle = CreateIoCompletionPort((HANDLE)clientSocket, server->completionPort, completionKey, 0);
   if (handle != server->completionPort) {
      LOG_LAST_ERROR(ERROR_LEVEL, ("\nCreateIoCompletionPort() error:"));
      clientDisconnection(client, CFL_FALSE);
      closesocket(clientSocket);
      return;
   }

   LOG_MESSAGE(TRACE_LEVEL, ("\nConnection accepted."));
   waitRead(client);
}

static DWORD WINAPI listenThreadProc(LPVOID param) {
   SDB_LS_SERVERP server = (SDB_LS_SERVERP)param;
   WSANETWORKEVENTS events;
   int result;

   while (server->isRunning) {

      if (WSAWaitForMultipleEvents(1, &server->listenEvent, CFL_FALSE, 100, CFL_FALSE) == WSA_WAIT_TIMEOUT) {
         continue;
      }

      //
      // Figure out what happened
      //
      result = WSAEnumNetworkEvents(server->listenSocket, server->listenEvent, &events);

      if (result == SOCKET_ERROR) {
         LOG_LAST_ERROR(ERROR_LEVEL, ("\nWSAEnumNetworkEvents() error:"));
         break;
      }

      // Handle Network events //
      // ACCEPT
      if (events.lNetworkEvents & FD_ACCEPT) {
         if (events.iErrorCode[FD_ACCEPT_BIT] == 0) {
            acceptConnection(server);
         } else {
            LOG_LAST_ERROR(ERROR_LEVEL, ("\nUnknown network event error:"));
            break;
         }
      }

   } // while....

   return 0;
}

static CFL_BOOL startListenThread(SDB_LS_SERVERP server, CFL_BOOL bWait) {
   DWORD threadId = 0;

   LOG_MESSAGE(INFO_LEVEL, ("\nStarting listen thread..."));

   if (!setupTCPServerSocket(server)) {
      LOG_MESSAGE(ERROR_LEVEL, ("\nFailed to configure listen socket."));
      return CFL_FALSE;
   }

   ++s_serversRunning;
   if (bWait) {
      listenThreadProc((void *)server);
   } else {
      server->listenThread = CreateThread(NULL,             // Security
                                          0,                // Stack size - use default
                                          listenThreadProc, // Thread fn entry point
                                          (void *)server,   // Param
                                          0,                // Init flag
                                          &threadId);       // Thread address

      if (server->listenThread == NULL) {
         server->isRunning = CFL_FALSE;
         --s_serversRunning;
         return CFL_FALSE;
      }
   }
   return CFL_TRUE;
}

SDB_LS_SERVERP sdb_lcksrv_createServer(char *address, int serverPort, int minWorkers, int maxWorkers) {
   SDB_LS_SERVERP server;

   if (serverPort == 0) {
      LOG_MESSAGE(ERROR_LEVEL, ("\nInvalid port: %d", serverPort));
      return NULL;
   }

   server = (SDB_LS_SERVERP)malloc(sizeof(SDB_LS_SERVER));
   cfl_lock_init(&(server->criticalSection));
   cfl_lock_init(&(server->lockDictionary));
   server->address = address ? cfl_str_newBuffer(address) : NULL;
   server->port = serverPort == 0 ? SDB_DEFAULT_LOCK_SERVER_PORT : (unsigned short)serverPort;
   server->listenSocket = INVALID_SOCKET;
   server->listenEvent = WSA_INVALID_EVENT;
   server->listenThread = NULL;
   server->completionPort = NULL;
   server->isRunning = CFL_FALSE;
   server->minWorkers = minWorkers;
   server->maxWorkers = maxWorkers;
   server->workersBusy = 0;
   server->workersCount = 0;
   server->tailWorker = NULL;
   server->clients = cfl_hash_new(8192, &calcIdHash, &compareIds, &hashFreeClient);
   server->nextContextId = 1;
   server->nextClientId = 1;
   server->databases = cfl_hash_new(8, &calcStringHash, &compareStrings, &hashFreeDatabase);
   ++s_serversCount;
   return server;
}

void sdb_lcksrv_runServer(SDB_LS_SERVERP server, CFL_BOOL wait) {
   if (!server->isRunning) {
      LOG_MESSAGE(INFO_LEVEL, ("\nStarting server..."));
      server->isRunning = CFL_TRUE;
      // Inicializa o socket environment
      if (!initializeIOCP(server)) {
         shutdownServer(server);
         LOG_MESSAGE(ERROR_LEVEL, ("\nError initializing I/O completion port."));
      } else if (!createWorkerThreads(server)) {
         shutdownServer(server);
         LOG_MESSAGE(ERROR_LEVEL, ("\nError creating worker threads."));
      } else if (!startListenThread(server, wait)) {
         shutdownServer(server);
         LOG_MESSAGE(ERROR_LEVEL, ("\nError creating main thread."));
      } else {
         LOG_MESSAGE(INFO_LEVEL,
                     ("\nServer started!\nWaiting requests at %s:%u...", cfl_str_getPtr(server->address), server->port));
      }
   }
}

void sdb_lcksrv_shutdownServer(SDB_LS_SERVERP server) {
   if (server) {
      shutdownServer(server);
   }
}

void sdb_lcksrv_releaseServer(SDB_LS_SERVERP server) {
   if (server) {
      releaseServer(server);
   }
}

void sdb_lcksrv_disconnectClient(SDB_LS_CLIENTP client) {
   if (client) {
      ULONG_PTR completionKey;
      LOG_MESSAGE(TRACE_LEVEL, ("\nDisconnection requested by client."));
      completionKey = COMPLETION_KEY_DISCONNECT;
      PostQueuedCompletionStatus(client->server->completionPort, 0, completionKey, &client->overlapped);
   }
}

void sdb_lcksrv_disconnectAll(SDB_LS_SERVERP server) {
   if (server) {
      cfl_hash_clear(server->clients, CFL_TRUE);
   }
}

int sdb_lcksrv_serverGetMinWorkers(SDB_LS_SERVERP server) {
   if (server) {
      return server->minWorkers;
   }
   return 0;
}

void sdb_lcksrv_serverSetMinWorkers(SDB_LS_SERVERP server, int minWorkers) {
   if (server) {
      server->minWorkers = minWorkers;
   }
}

int sdb_lcksrv_serverGetMaxWorkers(SDB_LS_SERVERP server) {
   if (server) {
      return server->maxWorkers;
   }
   return 0;
}

void sdb_lcksrv_serverSetMaxWorkers(SDB_LS_SERVERP server, int maxWorkers) {
   if (server) {
      server->maxWorkers = maxWorkers;
   }
}
