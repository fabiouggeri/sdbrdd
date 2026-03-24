#include <stdio.h>

#include "cfl_list.h"

#include "sdb_lock_client.h"
#include "sdb_database.h"
#include "sdb_table.h"
#include "sdb_api.h"
#include "sdb_errors.ch"
#include "sdb_schema.h"
#include "sdb_field.h"
#include "sdb_util.h"
#include "sdb_log.h"
#include "sdb_thread.h"


static CFL_BOOL receiveAll(SDB_LS_CLIENTP client) {
   int retVal;
   char headerBuffer[4];
   CFL_UINT32 bodyLen;
   char bodyBuffer[SDB_LS_IO_BUFFER_SIZE];
   int readLen;

   ENTER_FUN;
   cfl_buffer_reset(client->buffer);

   // wait until timeout or data received
   retVal = cfl_socket_selectRead(client->socket, 30000);
   if (retVal == 0) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_SOCKET, "Timeout waiting data from Lock Server");
      RETURN CFL_FALSE;
   }
   if (retVal == -1) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_SOCKET, "Error waiting data from Lock Server: %ld", cfl_socket_lastErrorCode());
      RETURN CFL_FALSE;
   }

   // data must be here, so do a normal cfl_socket_receive()
   retVal = cfl_socket_receive(client->socket, headerBuffer, sizeof(headerBuffer));
   if (retVal == CFL_SOCKET_ERROR) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_SOCKET, "Error reading data from Lock Server: %ld", cfl_socket_lastErrorCode());
      RETURN CFL_FALSE;
   } else if (retVal == 0) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_SOCKET, "Connection was closed");
      RETURN CFL_FALSE;
   }
   bodyLen = *((CFL_UINT32 *)headerBuffer);
   while (bodyLen > 0) {
      readLen = bodyLen > SDB_LS_IO_BUFFER_SIZE ? SDB_LS_IO_BUFFER_SIZE : bodyLen;
      retVal = cfl_socket_receive(client->socket, bodyBuffer, readLen);
      if (retVal == CFL_SOCKET_ERROR) {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_SOCKET, "Error reading data from Lock Server: %ld", cfl_socket_lastErrorCode());
         RETURN CFL_FALSE;
      } else if (retVal == 0) {
         sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_SOCKET, "Connection was closed");
         RETURN CFL_FALSE;
      }
      bodyLen -= retVal;
      cfl_buffer_put(client->buffer, bodyBuffer, retVal);
   }
   cfl_buffer_rewind(client->buffer);
   RETURN CFL_TRUE;
}

static void freeClient(SDB_LS_CLIENTP client) {
   if (client) {
      cfl_socket_close(client->socket);
      cfl_buffer_free(client->buffer);
      SDB_MEM_FREE(client);
   }
}

static CFL_BOOL sendAndReceiveAll(SDB_LS_CLIENTP client) {
   cfl_buffer_rewind(client->buffer);
   cfl_buffer_putInt32(client->buffer, cfl_buffer_length(client->buffer) - 4);
   cfl_buffer_rewind(client->buffer);
   if (! cfl_socket_sendAllBuffer(client->socket, client->buffer)) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_SOCKET, "Error sendind data to Lock Server: %ld", cfl_socket_lastErrorCode());
      return CFL_FALSE;
   }
   return receiveAll(client);
}

static void prepareCommand(SDB_LS_CLIENTP client, char cmdId) {
   cfl_buffer_reset(client->buffer);
   cfl_buffer_putInt32(client->buffer, 0);
   cfl_buffer_putInt8(client->buffer, (CFL_INT8) cmdId);
}

SDB_LS_CLIENTP sdb_lckcli_connect(const char *serverAddress, CFL_UINT16 serverPort) {
   SDB_LS_CLIENTP client;
   CFL_SOCKET clientSocket;
   CFL_INT16 errCode;

   ENTER_FUNP(("address=%s, port=%d", serverAddress, serverPort));
   clientSocket = cfl_socket_open(serverAddress, serverPort);

   if (clientSocket == CFL_INVALID_SOCKET) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_SOCKET, "Error connecting Lock Client: %ld", cfl_socket_lastErrorCode());
      RETURN NULL;
   }

   client = (SDB_LS_CLIENTP) SDB_MEM_ALLOC(sizeof (SDB_LS_CLIENT));
   client->serverAddress = cfl_str_newBuffer(serverAddress);
   client->serverPort = serverPort;
   client->socket = clientSocket;
   client->buffer = cfl_buffer_new();
   prepareCommand(client, SDB_LS_CMD_CONNECT);
   cfl_buffer_putInt8(client->buffer, sizeof(SDB_RECNO) < 8 ? 4 : 8);
   if (! sendAndReceiveAll(client)) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_SOCKET, "Error communicating with Lock Server");
      freeClient(client);
      RETURN NULL;
   }
   errCode = cfl_buffer_getInt16(client->buffer);
   if (errCode != SDB_LS_RET_SUCCESS) {
   //if (cfl_buffer_getInt16(client->buffer) != SDB_LS_RET_SUCCESS) {
      sdb_thread_setError(SDB_ERROR_TYPE_SDB, SDB_ERROR_SOCKET, "Error connecting to Lock Server");
      freeClient(client);
      RETURN NULL;
   }
   client->clientId = (CFL_UINT32) cfl_buffer_getInt32(client->buffer);
   RETURN client;
}

void sdb_lckcli_disconnect(SDB_LS_CLIENTP client) {
   prepareCommand(client, SDB_LS_CMD_DISCONNECT);
   sendAndReceiveAll(client);
   freeClient(client);
}

static CFL_UINT32 openTable(SDB_LS_CLIENTP client, SDB_DATABASEP database, SDB_TABLEP table, CFL_STRP context, CFL_BOOL bExclusive) {
   CFL_UINT32 contextId;

   cfl_buffer_reset(client->buffer);
   prepareCommand(client, bExclusive ? SDB_LS_CMD_OPEN_EXCLUSIVE : SDB_LS_CMD_OPEN_SHARED);
   cfl_buffer_putString(client->buffer, database->name);
   cfl_buffer_putString(client->buffer, table->clpSchema->name);
   cfl_buffer_putString(client->buffer, table->clpName);
   if (context != NULL) {
      cfl_buffer_putString(client->buffer, context);
   }
   if (sendAndReceiveAll(client)) {
      if (cfl_buffer_getInt16(client->buffer) == SDB_LS_RET_SUCCESS) {
         contextId = (CFL_UINT32) cfl_buffer_getInt32(client->buffer);
      } else {
         contextId = 0;
      }
   } else {
      contextId = 0;
   }
   return contextId;
}

CFL_UINT32 sdb_lckcli_openShared(SDB_LS_CLIENTP client, SDB_DATABASEP database, SDB_TABLEP table, CFL_STRP context) {
   return openTable(client, database, table, context, CFL_FALSE);
}

CFL_UINT32 sdb_lckcli_openExclusive(SDB_LS_CLIENTP client, SDB_DATABASEP database, SDB_TABLEP table, CFL_STRP context) {
   return openTable(client, database, table, context, CFL_TRUE);
}

static CFL_BOOL contextSimpleCommand(SDB_LS_CLIENTP client, CFL_UINT32 contextId, CFL_UINT8 commandId) {
   prepareCommand(client, commandId);
   cfl_buffer_putInt32(client->buffer, contextId);
   return sendAndReceiveAll(client) && cfl_buffer_getInt16(client->buffer) == SDB_LS_RET_SUCCESS;
}

CFL_BOOL sdb_lckcli_lockTable(SDB_LS_CLIENTP client, CFL_UINT32 contextId) {
   return contextSimpleCommand(client, contextId, SDB_LS_CMD_LOCK_FILE);
}

CFL_BOOL sdb_lckcli_closeTable(SDB_LS_CLIENTP client, CFL_UINT32 contextId) {
   return contextSimpleCommand(client, contextId, SDB_LS_CMD_CLOSE);
}

CFL_BOOL sdb_lckcli_closeAll(SDB_LS_CLIENTP client) {
   prepareCommand(client, SDB_LS_CMD_CLOSE_ALL);
   return sendAndReceiveAll(client);
}

static CFL_BOOL recnoSimpleCommand(SDB_LS_CLIENTP client, CFL_UINT32 contextId, SDB_RECNO recno, CFL_UINT8 commandId) {
   prepareCommand(client, commandId);
   cfl_buffer_putInt32(client->buffer, contextId);
   cfl_buffer_putInt64(client->buffer, (CFL_INT64) recno);
   return sendAndReceiveAll(client) && cfl_buffer_getInt16(client->buffer) == SDB_LS_RET_SUCCESS;
}

CFL_BOOL sdb_lckcli_lockRecord(SDB_LS_CLIENTP client, CFL_UINT32 contextId, SDB_RECNO recno) {
   return recnoSimpleCommand(client, contextId, recno, SDB_LS_CMD_LOCK_REC);
}

CFL_BOOL sdb_lckcli_unlockRecord(SDB_LS_CLIENTP client, CFL_UINT32 contextId, SDB_RECNO recno) {
   return recnoSimpleCommand(client, contextId, recno, SDB_LS_CMD_UNLOCK_REC);
}

CFL_BOOL sdb_lckcli_unlockAll(SDB_LS_CLIENTP client, CFL_UINT32 contextId) {
   return contextSimpleCommand(client, contextId, SDB_LS_CMD_UNLOCK_ALL);
}
