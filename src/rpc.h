/* Newline-delimited JSON-RPC 2.0 server and an MCP stdio bridge. */
#ifndef NA_RPC_H
#define NA_RPC_H
#include <stdio.h>
#include "json.h"
#include "store.h"

typedef struct {
    Store *store;
    Validator validator;
    bool has_validator;
    const char *session; /* "cli" or "jsonrpc" */
} Rpc;

/* Handle one JSON-RPC request. Returns the reply or NULL (notification). */
J *rpc_dispatch(Rpc *r, const J *request);
/* Handle one MCP JSON-RPC message (initialize, tools/list, tools/call, ping). */
J *rpc_mcp_dispatch(Rpc *r, const J *request);

/* Read one message per line until EOF. `mcp` selects the protocol. */
int rpc_serve(Rpc *r, FILE *in, FILE *out, bool mcp);

/* Shared helper: one edit/undo/redo result as JSON. */
J *edit_result_json(const EditResult *res);

#endif
