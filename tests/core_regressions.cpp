#include "src/platform.h"
#include "src/rpc.h"
#include <atomic>
#include <barrier>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

void require(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
J *call(Rpc &rpc, const char *method, J *params) {
    J *req = j_obj();
    j_set(req, "jsonrpc", j_str("2.0"));
    j_set(req, "id", j_int(1));
    j_set(req, "method", j_str(method));
    j_set(req, "params", params);
    J *response = rpc_dispatch(&rpc, req);
    json_free(req);
    if (j_get(response, "error")) {
        std::string error = j_get(j_get(response, "error"), "message")->s;
        json_free(response);
        throw std::runtime_error(error);
    }
    return response;
}
int main() try {
    std::atomic<bool> errors_ok = true;
    std::barrier sync(2);
    auto check = [&](const char *value) {
        for (int i = 0; i < 1000; i++) {
            na_seterr("%s", value);
            sync.arrive_and_wait();
            if (strcmp(na_errmsg(), value))
                errors_ok = false;
            sync.arrive_and_wait();
        }
    };
    std::jthread first(check, "first"), second(check, "second");
    first.join();
    second.join();
    require(errors_ok, "error messages leaked between threads");
    const auto root =
        std::filesystem::temp_directory_path() / ("nextagent-regression-" + std::to_string(pf_now_ms()));
    std::filesystem::create_directories(root);
    Store *store = store_open(root.string().c_str());
    require(store, "store open");
    Rpc rpc{};
    rpc.store = store;
    rpc.autoflush = true;
    J *params = j_obj();
    J *edits = j_arr();
    for (int i = 0; i < 70; i++) {
        J *edit = j_obj();
        const auto name = "file-" + std::to_string(i) + ".txt";
        j_set(edit, "path", j_str(name.c_str()));
        j_set(edit, "new", j_str("content"));
        j_set(edit, "create", j_bool(true));
        j_push(edits, edit);
    }
    j_set(params, "edits", edits);
    json_free(call(rpc, "patch", params));
    for (int i = 0; i < 70; i++)
        require(pf_is_file((root / ("file-" + std::to_string(i) + ".txt")).string().c_str()),
                "large batch left a committed file unflushed");
    params = j_obj();
    j_set(params, "recursive", j_bool(true));
    j_set(params, "limit", j_int(5));
    J *result = call(rpc, "tree", params);
    require(j_get(j_get(result, "result"), "entries")->n == 5, "tree limit ignored");
    require(j_get(j_get(result, "result"), "truncated")->b, "tree omitted truncation flag");
    json_free(result);
    params = j_obj();
    edits = j_arr();
    J *initial = j_obj();
    j_set(initial, "path", j_str("lines.txt"));
    j_set(initial, "new", j_str("a\r\nb\r\n"));
    j_set(initial, "create", j_bool(true));
    j_push(edits, initial);
    j_set(params, "edits", edits);
    json_free(call(rpc, "patch", params));
    params = j_obj();
    j_set(params, "target", j_str("lines.txt"));
    j_set(params, "start_line", j_int(1));
    j_set(params, "end_line", j_int(2));
    j_set(params, "new", j_str("c\r\nd\ne\n"));
    json_free(call(rpc, "replace_text", params));
    Buf text{};
    require(pf_read_file((root / "lines.txt").string().c_str(), &text) == 0, "read result");
    require(std::string(text.p, text.len) == "c\r\nd\r\ne\r\n",
            "mixed replacement introduced mixed line endings");
    buf_free(&text);
    rpc_release(&rpc);
    store_close(store);
    std::filesystem::remove_all(root);
    std::cout << "Core regressions: thread isolation, 70-file flush, tree limits, CRLF passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
