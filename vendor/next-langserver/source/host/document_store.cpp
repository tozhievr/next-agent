#include "host/document_store.hpp"

namespace lx::host {

DocumentStore::DocumentStore(LanguageRegistry& registry,
                             Scheduler& scheduler,
                             ParseBackendFn parse_fn,
                             HostConfig config,
                             HostMetrics* metrics,
                             TraceLog* trace)
    : registry_(registry),
      scheduler_(scheduler),
      parse_fn_(std::move(parse_fn)),
      config_(config),
      metrics_(metrics),
      trace_(trace) {}

DocumentStore::~DocumentStore() {
  std::vector<std::string> uris = open_uris();
  for (const auto& u : uris) {
    scheduler_.remove_key(u);
  }
  std::unique_lock lock(mu_);
  sessions_.clear();
}

void DocumentStore::set_publish_callback(PublishCallback cb) {
  std::lock_guard lock(cb_mu_);
  publish_cb_ = std::move(cb);
}

std::shared_ptr<Session> DocumentStore::open_document(
    std::string uri,
    std::string_view text,
    std::string_view language_id,
    int64_t lsp_version,
    bool sync_parse,
    std::string* out_error) {
  std::shared_ptr<const LoadedModule> mod;
  if (!language_id.empty()) {
    mod = registry_.get_or_load(language_id, out_error);
  }
  if (!mod) {
    mod = registry_.get_or_load_for_uri(uri, out_error);
  }
  if (!mod) {
    return nullptr;
  }

  auto session = std::make_shared<Session>(
      uri, std::move(mod), text, registry_, parse_fn_, config_, metrics_, trace_);
  {
    std::lock_guard wlock(session->writer_mutex());
    session->apply_lsp_edits({}, lsp_version);
  }

  {
    std::unique_lock lock(mu_);
    sessions_[uri] = session;
  }

  if (sync_parse) {
    {
      std::lock_guard wlock(session->writer_mutex());
      session->reparse(nullptr);
    }
    PublishCallback cb;
    {
      std::lock_guard clock(cb_mu_);
      cb = publish_cb_;
    }
    if (cb) {
      auto snap = session->snapshot();
      auto diags = session->layered_cursor().all_diagnostics();
      cb(uri, snap, diags, session->lsp_version());
    }
  } else {
    schedule_reparse(session, TaskPriority::Interactive, std::chrono::milliseconds(0));
  }

  return session;
}

bool DocumentStore::apply_edits(const std::string& uri,
                                std::span<const TextEdit> edits,
                                std::optional<int64_t> lsp_version,
                                TaskPriority priority) {
  auto session = get_session(uri);
  if (!session) return false;

  // Cancel any in-flight parse before acquiring writer lock so it unwinds quickly (§10.2)
  scheduler_.cancel(uri);
  {
    std::lock_guard wlock(session->writer_mutex());
    session->apply_lsp_edits(edits, lsp_version);
  }
  schedule_reparse(session, priority, config_.debounce);
  return true;
}

bool DocumentStore::apply_byte_edits(const std::string& uri,
                                     std::span<const ByteEdit> edits,
                                     TaskPriority priority) {
  auto session = get_session(uri);
  if (!session) return false;

  scheduler_.cancel(uri);
  {
    std::lock_guard wlock(session->writer_mutex());
    session->apply_byte_edits(edits);
  }
  schedule_reparse(session, priority, config_.debounce);
  return true;
}

void DocumentStore::close_document(const std::string& uri) {
  scheduler_.remove_key(uri);
  std::unique_lock lock(mu_);
  sessions_.erase(uri);
}

std::shared_ptr<Session> DocumentStore::get_session(const std::string& uri) const {
  std::shared_lock lock(mu_);
  auto it = sessions_.find(uri);
  return it == sessions_.end() ? nullptr : it->second;
}

std::shared_ptr<const Snapshot> DocumentStore::ensure_parsed(const std::string& uri) {
  auto session = get_session(uri);
  if (!session) return nullptr;

  auto needs_reparse_locked = [&]() {
    auto snap = session->snapshot();
    if (!snap || session->has_pending_edits() ||
        snap->version != session->doc_version()) {
      return true;
    }
    const auto& cur_mod = session->module();
    return cur_mod && (snap->module != cur_mod ||
                       snap->module_epoch != cur_mod->module_epoch);
  };

  bool need_now = false;
  {
    std::lock_guard wlock(session->writer_mutex());
    need_now = needs_reparse_locked();
  }

  if (need_now) {
    scheduler_.cancel(uri);
    {
      std::lock_guard wlock(session->writer_mutex());
      if (needs_reparse_locked()) {
        session->reparse(nullptr);
      }
    }
  }
  return session->snapshot();
}

void DocumentStore::notify_language_reloaded(std::string_view lang_id) {
  auto new_mod = registry_.get_or_load(lang_id);
  if (!new_mod) return;

  std::vector<std::shared_ptr<Session>> candidates;
  {
    std::shared_lock lock(mu_);
    candidates.reserve(sessions_.size());
    for (const auto& [_, sess] : sessions_) {
      candidates.push_back(sess);
    }
  }

  for (const auto& sess : candidates) {
    auto snap = sess->snapshot();
    bool matches = snap && snap->module && snap->module->id == lang_id;
    if (!matches && !snap) {
      std::lock_guard wlock(sess->writer_mutex());
      const auto& cur = sess->module();
      matches = cur && cur->id == lang_id;
    }
    if (!matches) continue;

    scheduler_.cancel(sess->uri());
    {
      std::lock_guard wlock(sess->writer_mutex());
      sess->set_module(new_mod);
    }
    schedule_reparse(sess, TaskPriority::Background, std::chrono::milliseconds(0));
  }
}

std::vector<std::string> DocumentStore::open_uris() const {
  std::shared_lock lock(mu_);
  std::vector<std::string> out;
  out.reserve(sessions_.size());
  for (const auto& [u, _] : sessions_) out.push_back(u);
  return out;
}

void DocumentStore::schedule_reparse(const std::shared_ptr<Session>& session,
                                     TaskPriority priority,
                                     std::chrono::milliseconds debounce) {
  std::string uri = session->uri();
  std::weak_ptr<Session> weak_sess = session;
  scheduler_.schedule(
      uri,
      [this, weak_sess, uri](const std::shared_ptr<CancelToken>& tok) {
        auto sess = weak_sess.lock();
        if (!sess || (tok && tok->is_cancelled())) return;

        lx_status st = LX_OK;
        {
          std::lock_guard wlock(sess->writer_mutex());
          st = sess->reparse(tok);
        }
        if (st == LX_CANCELLED || (tok && tok->is_cancelled())) return;

        PublishCallback cb;
        {
          std::lock_guard clock(cb_mu_);
          cb = publish_cb_;
        }
        if (cb) {
          auto snap = sess->snapshot();
          auto diags = sess->layered_cursor().all_diagnostics();
          cb(uri, snap, diags, sess->lsp_version());
        }
      },
      priority,
      debounce);
}

}  // namespace lx::host
