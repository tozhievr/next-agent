#include "agent.h"
#include "platform.h"
#include "rpc.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <memory>
#include <vector>
#include <algorithm>
int native_check(Rpc *rpc, const J *params, J **out) {
    if (rpc->read_only)
        return na_fail("read_only: project checks execute code and are disabled");
    const J *k = j_get(params, "kind");
    if (!j_is_str(k))
        return na_fail("check kind is required");
    const char *executable = nullptr;
    std::string args;
    char *directory = nullptr;
    if (!strcmp(k->s, "build") || !strcmp(k->s, "test")) {
        const J *build = j_get(params, "build_dir");
        if (!j_is_str(build) || strpbrk(build->s, "\"\r\n"))
            return na_fail("a valid build_dir is required");
        directory = store_canon(rpc->store, build->s);
        if (!directory)
            return -1;
        const std::string full = std::string(store_root(rpc->store)) + "/" + directory;
        free(directory);
        executable = !strcmp(k->s, "build") ? "cmake" : "ctest";
        args = (!strcmp(k->s, "build") ? "--build " : "--test-dir ") + std::string("\"") + full + "\"";
        if (!strcmp(k->s, "test"))
            args += " --output-on-failure";
    } else if (!strcmp(k->s, "python")) {
#ifdef _WIN32
        executable = "python";
#else
        executable = "python3";
#endif
        args = "-m unittest discover -s tests";
    } else
        return na_fail("check kind must be build, test or python");
    const J *timeout = j_get(params, "timeout_ms");
    if (timeout && !j_is_int(timeout))
        return na_fail("timeout_ms must be an integer");
    int64_t budget = j_is_int(timeout) ? timeout->i : 120000;
    if (budget < 100 || budget > 300000)
        return na_fail("timeout_ms must be 100..300000");
    J *before_proof = nullptr;
    const J *inputs = j_get(params, "inputs");
    int64_t checked_revision = store_head(rpc->store);
    if (check_fingerprint(rpc->store, inputs, &before_proof))
        return -1;
    std::unique_ptr<J, decltype(&json_free)> proof_owner(before_proof, json_free);
    PfProc *process = pf_spawn_capture(executable, args.c_str(), store_root(rpc->store));
    if (!process)
        return -1;
    const auto started = pf_now_ms();
    bool timed_out = false, cancelled = false, truncated = false;
    std::string output;
    for (;;) {
        char bytes[4096];
        int n = pf_proc_read(process, bytes, sizeof bytes, 20);
        if (n > 0) {
            size_t count = (std::min)(size_t(n), size_t(64000) - output.size());
            output.append(bytes, count);
            if (count < size_t(n))
                truncated = true;
        }
        if (rpc->cancelled && rpc->cancelled(rpc->cancel_context)) {
            cancelled = true;
            break;
        }
        if (pf_now_ms() - started >= uint64_t(budget)) {
            timed_out = true;
            break;
        }
        if (!pf_proc_alive(process)) {
            while ((n = pf_proc_read(process, bytes, sizeof bytes, 0)) > 0) {
                size_t count = (std::min)(size_t(n), size_t(64000) - output.size());
                output.append(bytes, count);
                if (count < size_t(n))
                    truncated = true;
            }
            break;
        }
    }
    int code = pf_proc_exit_code(process);
    pf_proc_close(process, 0);
    std::string safe;
    for (size_t i = 0; i < output.size();) {
        auto count = utf8_seq_len(static_cast<unsigned char>(output[i]));
        if (count && i + count <= output.size() && utf8_valid(output.data() + i, count)) {
            safe.append(output, i, count);
            i += count;
        } else {
            safe += "\xef\xbf\xbd";
            i++;
        }
    }
    *out = j_obj();
    j_set(*out, "kind", j_str(k->s));
    j_set(*out, "exit_code", j_int(code));
    j_set(*out, "output", j_str(safe.c_str()));
    j_set(*out, "timed_out", j_bool(timed_out));
    j_set(*out, "cancelled", j_bool(cancelled));
    j_set(*out, "truncated", j_bool(truncated));
    j_set(*out, "duration_ms", j_int(pf_now_ms() - started));
    if (code || timed_out || cancelled)
        j_set(*out, "error", j_str(cancelled ? "cancelled" : timed_out ? "timeout" : "check_failed"));
    J *after_proof = nullptr;
    int proof_rc = check_fingerprint(rpc->store, inputs, &after_proof);
    std::unique_ptr<J, decltype(&json_free)> after_owner(after_proof, json_free);
    int64_t current_revision = store_head(rpc->store);
    const J *before_hash = j_get(before_proof, "hash"), *after_hash = j_get(after_proof, "hash");
    bool fresh = proof_rc == 0 && checked_revision == current_revision &&
                 j_get(before_proof, "complete")->b && j_get(after_proof, "complete")->b &&
                 j_is_str(before_hash) && j_is_str(after_hash) && !strcmp(before_hash->s, after_hash->s);
    j_set(*out, "checked_revision", j_int(checked_revision));
    j_set(*out, "current_revision", j_int(current_revision));
    j_set(*out, "fresh", j_bool(fresh));
    char *proof_text = json_dump(before_proof);
    j_set(*out, "inputs", json_parse(proof_text, strlen(proof_text)));
    free(proof_text);
    if (inputs) {
        char *encoded = json_dump(inputs);
        j_set(*out, "input_paths", json_parse(encoded, strlen(encoded)));
        free(encoded);
    }
    std::string id = std::to_string(pf_unix_ms()) + "-" + std::to_string(pf_now_ms());
    j_set(*out, "check_id", j_str(id.c_str()));
    if (store_meta_put(rpc->store, ("check/" + id).c_str(), *out, nullptr) ||
        store_meta_put(rpc->store, "check/latest", *out, nullptr))
        j_set(*out, "storage_error", j_str(na_errmsg()));
    return 0;
}

int check_fingerprint(Store *store, const J *inputs, J **out) {
    std::vector<std::string> paths;
    bool complete = true;
    if (inputs && inputs->k != J_NULL) {
        if (inputs->k != J_ARR || inputs->n == 0 || inputs->n > 20000)
            return na_fail("inputs must contain 1..20000 paths");
        for (size_t i = 0; i < inputs->n; i++) {
            if (!j_is_str(inputs->items[i]))
                return na_fail("check input paths must be strings");
            paths.emplace_back(inputs->items[i]->s);
        }
    } else {
        Rpc rpc{};
        rpc.store = store;
        J *request = j_obj();
        j_set(request, "jsonrpc", j_str("2.0"));
        j_set(request, "id", j_int(1));
        j_set(request, "method", j_str("tree"));
        J *params = j_obj();
        j_set(params, "recursive", j_bool(true));
        j_set(params, "limit", j_int(20000));
        j_set(request, "params", params);
        std::unique_ptr<J, decltype(&json_free)> response(rpc_dispatch(&rpc, request), json_free);
        json_free(request);
        const J *result = j_get(response.get(), "result"), *entries = j_get(result, "entries");
        if (!entries || entries->k != J_ARR)
            return na_fail("cannot enumerate check inputs");
        const J *truncated = j_get(result, "truncated");
        complete = !(truncated && truncated->b);
        for (size_t i = 0; i < entries->n; i++) {
            const J *entry = entries->items[i], *dir = j_get(entry, "directory");
            if (!(dir && dir->b))
                paths.emplace_back(j_get(entry, "path")->s);
        }
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    std::string aggregate;
    size_t bytes = 0, count = 0;
    for (auto &path : paths) {
        std::unique_ptr<char, decltype(&free)> canonical(store_canon(store, path.c_str()), free);
        if (!canonical)
            return -1;
        auto full = std::string(store_root(store)) + "/" + canonical.get();
        PfStat stat{}, after{};
        aggregate += canonical.get();
        aggregate.push_back('\0');
        if (!pf_stat(full.c_str(), &stat)) {
            aggregate += "missing";
            aggregate.push_back('\0');
            continue;
        }
        if (stat.size > 32 * 1024 * 1024 || bytes + stat.size > 256 * 1024 * 1024) {
            complete = false;
            continue;
        }
        Buf data;
        buf_init(&data);
        if (pf_read_file(full.c_str(), &data)) {
            buf_free(&data);
            return na_fail("cannot fingerprint check input: %s", path.c_str());
        }
        uint8_t hash[16];
        char hex[33];
        blake2b16(data.p ? data.p : "", data.len, hash);
        hex_encode(hash, 16, hex);
        bytes += data.len;
        buf_free(&data);
        aggregate += hex;
        aggregate.push_back('\0');
        count++;
        if (!pf_stat(full.c_str(), &after) || after.size != stat.size || after.mtime_ms != stat.mtime_ms)
            complete = false;
    }
    uint8_t hash[16];
    char hex[33];
    blake2b16(aggregate.data(), aggregate.size(), hash);
    hex_encode(hash, 16, hex);
    *out = j_obj();
    j_set(*out, "hash", j_str(hex));
    j_set(*out, "files", j_int(count));
    j_set(*out, "bytes", j_int(bytes));
    j_set(*out, "complete", j_bool(complete));
    j_set(*out, "scope",
          j_str(inputs && inputs->k == J_ARR
                    ? "explicit input paths"
                    : "workspace tree, excluding .git/.aed/build/dist/node_modules and reparse entries"));
    return 0;
}
