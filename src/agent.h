#pragma once
#include "rpc.h"
int agent_method(Rpc *rpc, const char *method, const J *params, J **out);
int agent_reconcile(Rpc *rpc, const char *path);

int check_fingerprint(Store *store, const J *inputs, J **out);
