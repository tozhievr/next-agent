#include "platform.h"
#include "rpc.h"
#include <algorithm>
#include <cstring>
#include <string>
int native_check(Rpc *rpc, const J *params, J **out) {
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
    return 0;
}
