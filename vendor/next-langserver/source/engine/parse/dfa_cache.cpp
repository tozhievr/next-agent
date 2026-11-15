#include "engine/parse/parse_internal.h"

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/common/hash.h"

namespace lx::parse {

namespace {

constexpr int32_t kDfaNonAccept = -1;
constexpr int32_t kDfaFullCtx = -2;
constexpr int32_t kDfaError = -3;

struct SllCtxNode {
  uint32_t parent = 0; // 0 = empty root context
  uint32_t return_state = atn::kIndexNoneU32;
};

struct SllConfig {
  uint32_t state_id = 0;
  uint16_t alt = 0;
  uint32_t ctx_id = 0;

  bool operator==(const SllConfig& o) const noexcept {
    return state_id == o.state_id && alt == o.alt && ctx_id == o.ctx_id;
  }
  bool operator<(const SllConfig& o) const noexcept {
    if (alt != o.alt) return alt < o.alt;
    if (state_id != o.state_id) return state_id < o.state_id;
    return ctx_id < o.ctx_id;
  }
};

struct EdgeChunk {
  static constexpr uint32_t kCap = 8;
  struct Entry {
    lx_symbol sym = 0;
    struct DfaState* target = nullptr;
  };
  std::atomic<uint32_t> count{0};
  std::atomic<EdgeChunk*> next{nullptr};
  Entry entries[kCap]{};
};

struct DfaState {
  int32_t accept_alt = kDfaNonAccept;
  bool reached_outer_stop = false;
  bool reached_dynamic = false; // Encountered Pred or Precedence inside SLL closure
  std::vector<SllConfig> configs;
  std::atomic<EdgeChunk*> edges_head{nullptr};
};

struct DecisionDfa {
  std::mutex mu;
  std::atomic<DfaState*> root{nullptr};
  std::vector<std::unique_ptr<DfaState>> states;
  std::vector<std::unique_ptr<EdgeChunk>> chunks;
  std::vector<SllCtxNode> ctx_nodes;
  std::unordered_map<uint64_t, uint32_t> ctx_dedup;
  std::unordered_map<uint64_t, std::vector<DfaState*>> state_index;

  DecisionDfa() {
    ctx_nodes.push_back(SllCtxNode{0u, atn::kIndexNoneU32}); // id 0 = empty
  }

  uint32_t intern_ctx(uint32_t parent, uint32_t return_state) {
    const uint64_t key = (static_cast<uint64_t>(parent) << 32) | return_state;
    auto it = ctx_dedup.find(key);
    if (it != ctx_dedup.end()) {
      return it->second;
    }
    const uint32_t id = static_cast<uint32_t>(ctx_nodes.size());
    ctx_nodes.push_back(SllCtxNode{parent, return_state});
    ctx_dedup.emplace(key, id);
    return id;
  }

  DfaState* find_edge_lockfree(const DfaState* s, lx_symbol sym) const noexcept {
    const EdgeChunk* chunk = s->edges_head.load(std::memory_order_acquire);
    while (chunk) {
      const uint32_t n = chunk->count.load(std::memory_order_acquire);
      for (uint32_t i = 0; i < n; ++i) {
        if (chunk->entries[i].sym == sym) {
          return chunk->entries[i].target;
        }
      }
      chunk = chunk->next.load(std::memory_order_acquire);
    }
    return nullptr;
  }

  void add_edge_locked(DfaState* s, lx_symbol sym, DfaState* target) {
    EdgeChunk* head = s->edges_head.load(std::memory_order_relaxed);
    if (head) {
      const uint32_t n = head->count.load(std::memory_order_relaxed);
      if (n < EdgeChunk::kCap) {
        head->entries[n] = EdgeChunk::Entry{sym, target};
        head->count.store(n + 1, std::memory_order_release);
        return;
      }
    }
    auto new_chunk = std::make_unique<EdgeChunk>();
    new_chunk->entries[0] = EdgeChunk::Entry{sym, target};
    new_chunk->count.store(1, std::memory_order_relaxed);
    new_chunk->next.store(head, std::memory_order_relaxed);
    EdgeChunk* raw = new_chunk.get();
    chunks.push_back(std::move(new_chunk));
    s->edges_head.store(raw, std::memory_order_release);
  }
};

uint64_t hash_sll_configs(const std::vector<SllConfig>& cfgs,
                          bool outer_stop,
                          bool dynamic) noexcept {
  uint64_t h = lx::kFnv1a64OffsetBasis;
  h = lx::hash_combine_64(h, outer_stop ? 1ULL : 0ULL);
  h = lx::hash_combine_64(h, dynamic ? 1ULL : 0ULL);
  for (const auto& c : cfgs) {
    h = lx::hash_combine_64(h, (static_cast<uint64_t>(c.state_id) << 16) | c.alt);
    h = lx::hash_combine_64(h, c.ctx_id);
  }
  return h;
}

void sll_closure_step(const atn::Atn& atn,
                      DecisionDfa& dfa,
                      std::vector<SllConfig>& worklist,
                      std::vector<SllConfig>& out_configs,
                      bool& out_reached_outer_stop,
                      bool& out_reached_dynamic) {
  struct VisitedHash {
    size_t operator()(const SllConfig& c) const noexcept {
      uint64_t h = lx::hash_combine_64(c.state_id, c.alt);
      h = lx::hash_combine_64(h, c.ctx_id);
      return static_cast<size_t>(h);
    }
  };

  std::unordered_set<SllConfig, VisitedHash> visited;
  visited.reserve(worklist.size() * 4 + 8);
  for (const auto& c : worklist) {
    visited.insert(c);
  }

  for (size_t idx = 0; idx < worklist.size(); ++idx) {
    const SllConfig cfg = worklist[idx];
    const auto& st = atn.state(cfg.state_id);

    if (st.kind == atn::StateKind::RuleStop) {
      if (cfg.ctx_id != 0) {
        const SllCtxNode& node = dfa.ctx_nodes[cfg.ctx_id];
        SllConfig popped{node.return_state, cfg.alt, node.parent};
        if (visited.insert(popped).second) {
          worklist.push_back(popped);
        }
      } else {
        out_reached_outer_stop = true;
        out_configs.push_back(cfg);
      }
      continue;
    }

    const auto trans = atn.state_transitions(cfg.state_id);
    bool has_consume_edge = false;
    for (const auto& tr : trans) {
      switch (tr.kind) {
        case atn::TransitionKind::Epsilon:
        case atn::TransitionKind::Action: {
          SllConfig next{tr.to, cfg.alt, cfg.ctx_id};
          if (visited.insert(next).second) {
            worklist.push_back(next);
          }
          break;
        }
        case atn::TransitionKind::Rule: {
          const uint16_t callee = static_cast<uint16_t>(tr.arg0);
          const uint32_t new_ctx = dfa.intern_ctx(cfg.ctx_id, tr.to);
          SllConfig next{atn.rule(callee).start_state, cfg.alt, new_ctx};
          if (visited.insert(next).second) {
            worklist.push_back(next);
          }
          break;
        }
        case atn::TransitionKind::Pred:
        case atn::TransitionKind::Precedence: {
          out_reached_dynamic = true;
          break;
        }
        case atn::TransitionKind::Atom:
        case atn::TransitionKind::Set:
        case atn::TransitionKind::NotSet:
        case atn::TransitionKind::Range:
        case atn::TransitionKind::Wildcard:
          has_consume_edge = true;
          break;
      }
    }
    if (has_consume_edge) {
      out_configs.push_back(cfg);
    }
  }

  std::sort(out_configs.begin(), out_configs.end());
  out_configs.erase(std::unique(out_configs.begin(), out_configs.end()),
                    out_configs.end());
}

int32_t classify_sll_state(const std::vector<SllConfig>& configs,
                           bool reached_outer_stop,
                           bool reached_dynamic) {
  if (reached_dynamic) {
    return kDfaFullCtx;
  }
  if (configs.empty()) {
    return reached_outer_stop ? kDfaFullCtx : kDfaError;
  }
  const uint16_t first_alt = configs[0].alt;
  bool same_alt = true;
  for (size_t i = 1; i < configs.size(); ++i) {
    if (configs[i].alt != first_alt) {
      same_alt = false;
      break;
    }
  }
  if (same_alt) {
    return static_cast<int32_t>(first_alt);
  }
  if (reached_outer_stop) {
    return kDfaFullCtx;
  }
  // SLL cannot distinguish alternatives that conflict in every ATN state.
  // Fall back now instead of scanning the entire suffix and caching it.
  std::unordered_map<uint32_t, std::pair<uint16_t, bool>> state_alts;
  std::unordered_map<uint64_t, uint16_t> context_alts;
  bool conflict = false;
  for (const auto& cfg : configs) {
    auto [state, added] = state_alts.emplace(cfg.state_id, std::pair{cfg.alt, false});
    if (!added && state->second.first != cfg.alt) state->second.second = true;
    const uint64_t key = (static_cast<uint64_t>(cfg.state_id) << 32) | cfg.ctx_id;
    auto [context, inserted] = context_alts.emplace(key, cfg.alt);
    if (!inserted && context->second != cfg.alt) conflict = true;
  }
  if (conflict && std::all_of(state_alts.begin(), state_alts.end(),
                            [](const auto& entry) { return entry.second.second; })) {
    return kDfaFullCtx;
  }
  return kDfaNonAccept;
}

DfaState* intern_dfa_state(DecisionDfa& dfa,
                           uint32_t max_states,
                           std::vector<SllConfig> configs,
                           bool outer_stop,
                           bool dynamic) {
  const uint64_t h = hash_sll_configs(configs, outer_stop, dynamic);
  auto it = dfa.state_index.find(h);
  if (it != dfa.state_index.end()) {
    for (DfaState* existing : it->second) {
      if (existing->reached_outer_stop == outer_stop &&
          existing->reached_dynamic == dynamic &&
          existing->configs == configs) {
        return existing;
      }
    }
  }

  if (dfa.states.size() >= max_states) {
    return nullptr;
  }

  auto s = std::make_unique<DfaState>();
  s->reached_outer_stop = outer_stop;
  s->reached_dynamic = dynamic;
  s->accept_alt = classify_sll_state(configs, outer_stop, dynamic);
  s->configs = std::move(configs);
  DfaState* raw = s.get();
  dfa.states.push_back(std::move(s));
  dfa.state_index[h].push_back(raw);
  return raw;
}

DfaState* get_or_create_sll_root(const atn::Atn& atn,
                                 DecisionDfa& dfa,
                                 uint32_t decision_state_id,
                                 uint32_t max_states) {
  DfaState* r = dfa.root.load(std::memory_order_acquire);
  if (r) {
    return r;
  }

  std::lock_guard<std::mutex> lock(dfa.mu);
  r = dfa.root.load(std::memory_order_relaxed);
  if (r) {
    return r;
  }

  const auto trans = atn.state_transitions(decision_state_id);
  std::vector<SllConfig> all_configs;
  bool any_outer_stop = false;
  bool any_dynamic = false;

  for (uint16_t alt = 0; alt < trans.size(); ++alt) {
    const auto& tr = trans[alt];
    std::vector<SllConfig> worklist;
    if (tr.kind == atn::TransitionKind::Epsilon ||
        tr.kind == atn::TransitionKind::Action) {
      worklist.push_back(SllConfig{tr.to, alt, 0u});
    } else if (tr.kind == atn::TransitionKind::Pred ||
               tr.kind == atn::TransitionKind::Precedence) {
      any_dynamic = true;
      break;
    } else if (tr.kind == atn::TransitionKind::Rule) {
      const uint16_t callee = static_cast<uint16_t>(tr.arg0);
      const uint32_t new_ctx = dfa.intern_ctx(0u, tr.to);
      worklist.push_back(SllConfig{atn.rule(callee).start_state, alt, new_ctx});
    } else {
      // Consuming transition directly on decision state: keep decision_state_id or step?
      // Wait: if trans[alt] itself is a consuming transition, represent via a virtual?
      // Normally decision states have Epsilon transitions to alts, except direct terminal
      // checks. Handle direct consuming transitions safely by falling back if mixed.
      all_configs.push_back(SllConfig{decision_state_id, alt, 0u});
      continue;
    }

    sll_closure_step(atn, dfa, worklist, all_configs, any_outer_stop, any_dynamic);
    if (any_dynamic) {
      break;
    }
  }

  std::sort(all_configs.begin(), all_configs.end());
  all_configs.erase(std::unique(all_configs.begin(), all_configs.end()),
                    all_configs.end());

  r = intern_dfa_state(dfa, max_states, std::move(all_configs),
                       any_outer_stop, any_dynamic);
  if (r) {
    dfa.root.store(r, std::memory_order_release);
  }
  return r;
}

} // namespace

struct DfaCache::Impl {
  const atn::Atn* atn = nullptr;
  uint32_t max_states_per_decision = 4096;
  std::vector<std::unique_ptr<DecisionDfa>> decisions;
};

DfaCache::DfaCache(const atn::Atn* atn, uint32_t max_states_per_decision)
    : impl_(std::make_unique<Impl>()) {
  reset(atn, max_states_per_decision);
}

DfaCache::~DfaCache() = default;

void DfaCache::reset(const atn::Atn* atn, uint32_t max_states_per_decision) {
  impl_->atn = atn;
  impl_->max_states_per_decision = std::max<uint32_t>(16u, max_states_per_decision);
  impl_->decisions.clear();
  if (atn) {
    const size_t n = atn->decisions().size();
    impl_->decisions.reserve(n);
    for (size_t i = 0; i < n; ++i) {
      impl_->decisions.push_back(std::make_unique<DecisionDfa>());
    }
  }
}

size_t DfaCache::decision_count() const noexcept {
  return impl_->decisions.size();
}

uint64_t DfaCache::total_states() const noexcept {
  uint64_t total = 0;
  for (const auto& d : impl_->decisions) {
    std::lock_guard<std::mutex> lock(d->mu);
    total += d->states.size();
  }
  return total;
}

namespace internal {

namespace {

int32_t run_sll_dfa(const atn::Atn& atn,
                    DfaCache& cache,
                    uint16_t decision_id,
                    uint32_t decision_state_id,
                    LookaheadAdapter& la,
                    bool& out_hit,
                    bool& out_miss) {
  auto* impl = cache.impl();
  if (!impl || impl->atn != &atn || decision_id >= impl->decisions.size()) {
    return kDfaFullCtx;
  }

  // If decision state has direct non-epsilon transitions, fall back to full LL
  // (in standard ATN decision states always have epsilon transitions to alts,
  //  except if a manual fixture has direct terminal edges).
  for (const auto& tr : atn.state_transitions(decision_state_id)) {
    if (tr.kind != atn::TransitionKind::Epsilon &&
        tr.kind != atn::TransitionKind::Action &&
        tr.kind != atn::TransitionKind::Rule) {
      return kDfaFullCtx;
    }
  }

  DecisionDfa& dfa = *impl->decisions[decision_id];
  DfaState* s = get_or_create_sll_root(atn, dfa, decision_state_id,
                                       impl->max_states_per_decision);
  if (!s) {
    return kDfaFullCtx;
  }

  if (s->accept_alt >= 0) {
    out_hit = true;
    return s->accept_alt;
  }
  if (s->accept_alt == kDfaFullCtx) {
    return kDfaFullCtx;
  }
  if (s->accept_alt == kDfaError) {
    out_hit = true;
    return -1;
  }

  constexpr uint32_t kMaxLookahead = 4096;
  bool any_miss = false;

  for (uint32_t k = 0; k < kMaxLookahead; ++k) {
    if (la.is_cancelled()) {
      return -1;
    }
    const LexToken& tok = la.peek(k);
    const lx_symbol sym = tok.symbol;

    DfaState* next = dfa.find_edge_lockfree(s, sym);
    if (!next) {
      any_miss = true;
      std::lock_guard<std::mutex> lock(dfa.mu);
      next = dfa.find_edge_lockfree(s, sym);
      if (!next) {
        std::vector<SllConfig> worklist;
        for (const auto& cfg : s->configs) {
          for (const auto& tr : atn.state_transitions(cfg.state_id)) {
            if (atn.transition_matches(tr, sym)) {
              worklist.push_back(SllConfig{tr.to, cfg.alt, cfg.ctx_id});
            }
          }
        }
        std::vector<SllConfig> closed;
        bool outer_stop = false;
        bool dynamic = false;
        if (!worklist.empty()) {
          sll_closure_step(atn, dfa, worklist, closed, outer_stop, dynamic);
        } else if (s->reached_outer_stop) {
          outer_stop = true;
        }
        next = intern_dfa_state(dfa, impl->max_states_per_decision,
                                std::move(closed), outer_stop, dynamic);
        if (!next) {
          return kDfaFullCtx;
        }
        dfa.add_edge_locked(s, sym, next);
      }
    }

    s = next;
    if (s->accept_alt >= 0) {
      out_hit = !any_miss;
      out_miss = any_miss;
      return s->accept_alt;
    }
    if (s->accept_alt == kDfaFullCtx) {
      out_miss = any_miss;
      return kDfaFullCtx;
    }
    if (s->accept_alt == kDfaError) {
      out_hit = !any_miss;
      out_miss = any_miss;
      return -1;
    }

    if (tok.is_eof()) {
      // Multiple alts survived up to EOF: ANTLR priority chooses lowest alt.
      uint16_t best_alt = s->configs[0].alt;
      for (const auto& c : s->configs) {
        best_alt = std::min(best_alt, c.alt);
      }
      out_hit = !any_miss;
      out_miss = any_miss;
      return static_cast<int32_t>(best_alt);
    }
  }

  return kDfaFullCtx;
}

struct FullCtxNode {
  uint32_t parent = 0; // 0 = empty local context (caller_idx tracks outer frames)
  uint32_t return_state = atn::kIndexNoneU32;
  uint16_t saved_prec = 0;
};

struct FullConfig {
  uint32_t state_id = 0;
  uint16_t alt = 0;
  uint16_t prec = 0;
  int32_t caller_idx = -1; // Index into caller_stack (-1 = past root)
  uint32_t local_ctx = 0;  // Index into local_ctx_pool

  bool operator==(const FullConfig& o) const noexcept {
    return state_id == o.state_id && alt == o.alt && prec == o.prec &&
           caller_idx == o.caller_idx && local_ctx == o.local_ctx;
  }
};

struct FullConfigHash {
  size_t operator()(const FullConfig& c) const noexcept {
    uint64_t h = lx::hash_combine_64(c.state_id, c.alt);
    h = lx::hash_combine_64(h, (static_cast<uint64_t>(c.prec) << 32) |
                                   static_cast<uint32_t>(c.caller_idx));
    h = lx::hash_combine_64(h, c.local_ctx);
    return static_cast<size_t>(h);
  }
};

struct FullCtxPool {
  std::vector<FullCtxNode> nodes;
  std::unordered_map<uint64_t, uint32_t> dedup;

  FullCtxPool() { nodes.push_back(FullCtxNode{0u, atn::kIndexNoneU32, 0u}); }

  uint32_t push(uint32_t parent, uint32_t return_state, uint16_t saved_prec) {
    const uint64_t k1 = (static_cast<uint64_t>(parent) << 32) | return_state;
    const uint64_t key = lx::hash_combine_64(k1, saved_prec);
    auto it = dedup.find(key);
    if (it != dedup.end()) {
      const auto& n = nodes[it->second];
      if (n.parent == parent && n.return_state == return_state &&
          n.saved_prec == saved_prec) {
        return it->second;
      }
    }
    const uint32_t id = static_cast<uint32_t>(nodes.size());
    nodes.push_back(FullCtxNode{parent, return_state, saved_prec});
    dedup.try_emplace(key, id);
    return id;
  }
};

void full_closure(const atn::Atn& atn,
                  std::span<const ActiveCallerFrame> caller_stack,
                  FullCtxPool& pool,
                  LookaheadAdapter& la,
                  uint32_t k_offset,
                  std::vector<FullConfig>& worklist,
                  std::vector<FullConfig>& out_configs,
                  bool& out_reached_root_eof,
                  uint32_t& out_max_caller_popped) {
  std::unordered_set<FullConfig, FullConfigHash> visited;
  visited.reserve(worklist.size() * 4 + 8);
  // Different consuming paths can reach the same configuration. Expanding
  // duplicate seeds also duplicates the output and multiplies work at each
  // subsequent token, even though newly discovered epsilon edges are deduped.
  size_t unique_count = 0;
  for (size_t i = 0; i < worklist.size(); ++i) {
    if (visited.insert(worklist[i]).second) worklist[unique_count++] = worklist[i];
  }
  worklist.resize(unique_count);

  for (size_t idx = 0; idx < worklist.size(); ++idx) {
    if (la.is_cancelled()) {
      return;
    }
    const FullConfig cfg = worklist[idx];
    const auto& st = atn.state(cfg.state_id);

    if (st.kind == atn::StateKind::RuleStop) {
      if (cfg.local_ctx != 0) {
        const auto& node = pool.nodes[cfg.local_ctx];
        FullConfig next{node.return_state, cfg.alt, node.saved_prec,
                        cfg.caller_idx, node.parent};
        if (visited.insert(next).second) {
          worklist.push_back(next);
        }
      } else if (cfg.caller_idx >= 0) {
        const auto& cf = caller_stack[static_cast<size_t>(cfg.caller_idx)];
        const uint32_t depth_popped =
            static_cast<uint32_t>(caller_stack.size() - static_cast<size_t>(cfg.caller_idx));
        out_max_caller_popped = std::max(out_max_caller_popped, depth_popped);
        FullConfig next{cf.return_state, cfg.alt, cf.prec,
                        cfg.caller_idx - 1, 0u};
        if (visited.insert(next).second) {
          worklist.push_back(next);
        }
      } else {
        out_reached_root_eof = true;
        out_configs.push_back(cfg);
      }
      continue;
    }

    const auto trans = atn.state_transitions(cfg.state_id);
    bool has_consume = false;
    for (const auto& tr : trans) {
      switch (tr.kind) {
        case atn::TransitionKind::Epsilon:
        case atn::TransitionKind::Action: {
          FullConfig next{tr.to, cfg.alt, cfg.prec, cfg.caller_idx, cfg.local_ctx};
          if (visited.insert(next).second) {
            worklist.push_back(next);
          }
          break;
        }
        case atn::TransitionKind::Precedence: {
          if (tr.arg0 >= cfg.prec) {
            FullConfig next{tr.to, cfg.alt, cfg.prec, cfg.caller_idx, cfg.local_ctx};
            if (visited.insert(next).second) {
              worklist.push_back(next);
            }
          }
          break;
        }
        case atn::TransitionKind::Pred: {
          if (la.eval_predicate(tr.arg0, tr.arg1, k_offset) != 0) {
            FullConfig next{tr.to, cfg.alt, cfg.prec, cfg.caller_idx, cfg.local_ctx};
            if (visited.insert(next).second) {
              worklist.push_back(next);
            }
          }
          break;
        }
        case atn::TransitionKind::Rule: {
          const uint16_t callee = static_cast<uint16_t>(tr.arg0);
          const uint16_t callee_prec =
              (tr.flags & atn::kTransPrecedenceArg) ? static_cast<uint16_t>(tr.arg1) : 0u;
          const uint32_t new_ctx = pool.push(cfg.local_ctx, tr.to, cfg.prec);
          FullConfig next{atn.rule(callee).start_state, cfg.alt, callee_prec,
                          cfg.caller_idx, new_ctx};
          if (visited.insert(next).second) {
            worklist.push_back(next);
          }
          break;
        }
        case atn::TransitionKind::Atom:
        case atn::TransitionKind::Set:
        case atn::TransitionKind::NotSet:
        case atn::TransitionKind::Range:
        case atn::TransitionKind::Wildcard:
          has_consume = true;
          break;
      }
    }
    if (has_consume) {
      out_configs.push_back(cfg);
    }
  }
}

int32_t unique_alt(const std::vector<FullConfig>& cfgs) noexcept {
  if (cfgs.empty()) return -1;
  const uint16_t first = cfgs[0].alt;
  for (size_t i = 1; i < cfgs.size(); ++i) {
    if (cfgs[i].alt != first) return -2;
  }
  return static_cast<int32_t>(first);
}

// Once all configurations agree on the same priority alternative for each
// ATN state/context, further lookahead cannot distinguish those alternatives.
// This is the full-context single-viable-alt termination used by ALL(*).
int32_t single_viable_alt(const std::vector<FullConfig>& configs) {
  std::unordered_map<FullConfig, uint16_t, FullConfigHash> subsets;
  for (auto cfg : configs) {
    const uint16_t alt = cfg.alt;
    cfg.alt = 0;
    auto [it, added] = subsets.emplace(cfg, alt);
    if (!added) it->second = std::min(it->second, alt);
  }
  int32_t viable = -1;
  for (const auto& [cfg, alt] : subsets) {
    if (viable >= 0 && viable != alt) return -1;
    viable = alt;
  }
  return viable;
}

uint16_t lowest_alt(const std::vector<FullConfig>& cfgs) noexcept {
  uint16_t best = cfgs[0].alt;
  for (size_t i = 1; i < cfgs.size(); ++i) {
    best = std::min(best, cfgs[i].alt);
  }
  return best;
}

int32_t run_full_ll(const atn::Atn& atn,
                    uint32_t decision_state_id,
                    uint16_t current_prec,
                    std::span<const ActiveCallerFrame> caller_stack,
                    LookaheadAdapter& la,
                    uint32_t& out_used_ctx_depth) {
  FullCtxPool pool;
  const int32_t top_caller = static_cast<int32_t>(caller_stack.size()) - 1;
  const auto trans = atn.state_transitions(decision_state_id);

  std::vector<FullConfig> configs;
  std::vector<FullConfig> worklist;
  bool reached_root_eof = false;
  uint32_t max_popped = 0;

  // Initial closure for each alternative at k_offset = 0.
  for (uint16_t alt = 0; alt < trans.size(); ++alt) {
    const auto& tr = trans[alt];
    worklist.clear();
    switch (tr.kind) {
      case atn::TransitionKind::Epsilon:
      case atn::TransitionKind::Action:
        worklist.push_back(FullConfig{tr.to, alt, current_prec, top_caller, 0u});
        break;
      case atn::TransitionKind::Precedence:
        if (tr.arg0 >= current_prec) {
          worklist.push_back(FullConfig{tr.to, alt, current_prec, top_caller, 0u});
        }
        break;
      case atn::TransitionKind::Pred:
        if (la.eval_predicate(tr.arg0, tr.arg1, 0u) != 0) {
          worklist.push_back(FullConfig{tr.to, alt, current_prec, top_caller, 0u});
        }
        break;
      case atn::TransitionKind::Rule: {
        const uint16_t callee = static_cast<uint16_t>(tr.arg0);
        const uint16_t callee_prec =
            (tr.flags & atn::kTransPrecedenceArg) ? static_cast<uint16_t>(tr.arg1) : 0u;
        const uint32_t new_ctx = pool.push(0u, tr.to, current_prec);
        worklist.push_back(FullConfig{atn.rule(callee).start_state, alt, callee_prec,
                                      top_caller, new_ctx});
        break;
      }
      case atn::TransitionKind::Atom:
      case atn::TransitionKind::Set:
      case atn::TransitionKind::NotSet:
      case atn::TransitionKind::Range:
      case atn::TransitionKind::Wildcard:
        configs.push_back(FullConfig{decision_state_id, alt, current_prec, top_caller, 0u});
        break;
    }
    if (!worklist.empty()) {
      full_closure(atn, caller_stack, pool, la, 0u, worklist, configs,
                   reached_root_eof, max_popped);
    }
  }

  if (configs.empty()) {
    out_used_ctx_depth = max_popped;
    return -1;
  }

  const int32_t init_uniq = unique_alt(configs);
  if (init_uniq >= 0) {
    out_used_ctx_depth = max_popped;
    return init_uniq;
  }

  constexpr uint32_t kMaxLookahead = 4096;
  for (uint32_t k = 0; k < kMaxLookahead; ++k) {
    if (la.is_cancelled()) {
      out_used_ctx_depth = max_popped;
      return -1;
    }

    const LexToken& tok = la.peek(k);
    const lx_symbol sym = tok.symbol;

    worklist.clear();
    for (const auto& cfg : configs) {
      const auto& st = atn.state(cfg.state_id);
      if (st.kind == atn::StateKind::RuleStop) {
        continue;
      }
      if (cfg.state_id == decision_state_id) {
        // Direct consuming transition on decision state (only alt cfg.alt).
        const auto& tr = trans[cfg.alt];
        if (atn.transition_matches(tr, sym)) {
          worklist.push_back(FullConfig{tr.to, cfg.alt, cfg.prec,
                                        cfg.caller_idx, cfg.local_ctx});
        }
        continue;
      }
      for (const auto& tr : atn.state_transitions(cfg.state_id)) {
        if (atn.transition_matches(tr, sym)) {
          worklist.push_back(FullConfig{tr.to, cfg.alt, cfg.prec,
                                        cfg.caller_idx, cfg.local_ctx});
        }
      }
    }

    if (worklist.empty()) {
      // If we hit EOF and some config reached root EOF, that config's alt wins
      // if no transition explicitly consumed EOF.
      if (tok.is_eof() && reached_root_eof) {
        uint16_t best = atn::kIndexNoneU16;
        for (const auto& c : configs) {
          if (atn.state(c.state_id).kind == atn::StateKind::RuleStop) {
            best = std::min(best, c.alt);
          }
        }
        if (best != atn::kIndexNoneU16) {
          out_used_ctx_depth = max_popped;
          return static_cast<int32_t>(best);
        }
      }
      out_used_ctx_depth = max_popped;
      return -1;
    }

    configs.clear();
    reached_root_eof = false;
    full_closure(atn, caller_stack, pool, la, k + 1, worklist, configs,
                 reached_root_eof, max_popped);

    if (configs.empty()) {
      out_used_ctx_depth = max_popped;
      return -1;
    }

    const int32_t uniq = single_viable_alt(configs);
    if (uniq >= 0) {
      out_used_ctx_depth = max_popped;
      return uniq;
    }

    if (tok.is_eof()) {
      out_used_ctx_depth = max_popped;
      return static_cast<int32_t>(lowest_alt(configs));
    }
  }

  out_used_ctx_depth = max_popped;
  return static_cast<int32_t>(lowest_alt(configs));
}

} // namespace

PredictOutcome predict_decision(
    const atn::Atn& atn,
    DfaCache* dfa_cache,
    uint32_t decision_state_id,
    uint16_t current_prec,
    std::span<const ActiveCallerFrame> caller_stack,
    LookaheadAdapter& la) {
  PredictOutcome out;
  const auto& st = atn.state(decision_state_id);
  if (st.trans_count <= 1) {
    out.alt = st.trans_count == 1 ? 0 : -1;
    return out;
  }

  if (st.decision_id != atn::kIndexNoneU16 &&
      st.decision_id < atn.decisions().size()) {
    const auto& dec = atn.decision(st.decision_id);
    const bool has_dynamic =
        (dec.flags & atn::kDecHasPred) != 0 ||
        (st.flags & atn::kStateLeftRecEntry) != 0;

    if (!has_dynamic && dec.kind == atn::DecisionKind::LL1) {
      const LexToken& tok = la.peek(0);
      const uint16_t res = atn.ll1_lookup(st.decision_id, tok.symbol);
      if (res < dec.alt_count) {
        out.alt = static_cast<int32_t>(res);
        out.used_ll1 = true;
        return out;
      }
      if (res == atn::kTblError) {
        out.alt = -1;
        out.used_ll1 = true;
        return out;
      }
      // kTblAmbig -> fall through to SLL+DFA / Full LL
    } else if (!has_dynamic && dec.kind == atn::DecisionKind::LLK) {
      const auto llk_states = atn.llk_states(st.decision_id);
      if (!llk_states.empty()) {
        uint32_t dfa_s = 0;
        bool ambig = false;
        uint32_t k = 0;
        while (k < 4096) {
          const uint16_t acc = llk_states[dfa_s].accept_alt;
          if (acc < dec.alt_count) {
            out.alt = static_cast<int32_t>(acc);
            out.used_llk = true;
            return out;
          }
          if (acc == atn::kTblAmbig) {
            ambig = true;
            break;
          }
          const LexToken& tok = la.peek(k++);
          const auto next = atn.llk_step(st.decision_id, dfa_s, tok.symbol);
          if (!next) {
            out.alt = -1;
            out.used_llk = true;
            return out;
          }
          dfa_s = *next;
        }
        if (!ambig) {
          out.alt = -1;
          out.used_llk = true;
          return out;
        }
      }
    }

    if (!has_dynamic && dfa_cache) {
      bool hit = false;
      bool miss = false;
      const int32_t sll_res = run_sll_dfa(atn, *dfa_cache, st.decision_id,
                                          decision_state_id, la, hit, miss);
      out.used_sll_hit = hit;
      out.used_sll_miss = miss;
      if (sll_res != kDfaFullCtx) {
        out.alt = sll_res;
        return out;
      }
    }
  }

  out.used_full_ll = true;
  out.alt = run_full_ll(atn, decision_state_id, current_prec, caller_stack,
                        la, out.used_ctx_depth);
  return out;
}

bool can_reach_symbol_in_one_step(
    const atn::Atn& atn,
    uint32_t start_state,
    uint16_t current_prec,
    std::span<const ActiveCallerFrame> caller_stack,
    lx_symbol target_sym,
    uint32_t* out_used_ctx_depth) {
  FullCtxPool pool;
  const int32_t top_caller = static_cast<int32_t>(caller_stack.size()) - 1;
  int32_t min_caller_accessed = top_caller + 1;
  auto finish = [&](bool result) noexcept {
    if (out_used_ctx_depth) {
      *out_used_ctx_depth =
          (min_caller_accessed <= top_caller)
              ? static_cast<uint32_t>(top_caller - min_caller_accessed + 1)
              : 0u;
    }
    return result;
  };
  std::vector<FullConfig> worklist;
  worklist.push_back(FullConfig{start_state, 0u, current_prec, top_caller, 0u});

  std::unordered_set<FullConfig, FullConfigHash> visited;
  visited.insert(worklist[0]);

  for (size_t idx = 0; idx < worklist.size(); ++idx) {
    const FullConfig cfg = worklist[idx];
    const auto& st = atn.state(cfg.state_id);

    if (st.kind == atn::StateKind::RuleStop) {
      if (cfg.local_ctx != 0) {
        const auto& node = pool.nodes[cfg.local_ctx];
        FullConfig next{node.return_state, 0u, node.saved_prec,
                        cfg.caller_idx, node.parent};
        if (visited.insert(next).second) worklist.push_back(next);
      } else if (cfg.caller_idx >= 0) {
        if (cfg.caller_idx < min_caller_accessed) {
          min_caller_accessed = cfg.caller_idx;
        }
        const auto& cf = caller_stack[static_cast<size_t>(cfg.caller_idx)];
        FullConfig next{cf.return_state, 0u, cf.prec, cfg.caller_idx - 1, 0u};
        if (visited.insert(next).second) worklist.push_back(next);
      } else if (target_sym == LX_SYMBOL_EOF) {
        return finish(true);
      }
      continue;
    }

    for (const auto& tr : atn.state_transitions(cfg.state_id)) {
      if (atn.transition_matches(tr, target_sym)) {
        return finish(true);
      }
      switch (tr.kind) {
        case atn::TransitionKind::Epsilon:
        case atn::TransitionKind::Action: {
          FullConfig next{tr.to, 0u, cfg.prec, cfg.caller_idx, cfg.local_ctx};
          if (visited.insert(next).second) worklist.push_back(next);
          break;
        }
        case atn::TransitionKind::Precedence: {
          if (tr.arg0 >= cfg.prec) {
            FullConfig next{tr.to, 0u, cfg.prec, cfg.caller_idx, cfg.local_ctx};
            if (visited.insert(next).second) worklist.push_back(next);
          }
          break;
        }
        case atn::TransitionKind::Rule: {
          const uint16_t callee = static_cast<uint16_t>(tr.arg0);
          const uint16_t callee_prec =
              (tr.flags & atn::kTransPrecedenceArg) ? static_cast<uint16_t>(tr.arg1) : 0u;
          const uint32_t new_ctx = pool.push(cfg.local_ctx, tr.to, cfg.prec);
          FullConfig next{atn.rule(callee).start_state, 0u, callee_prec,
                          cfg.caller_idx, new_ctx};
          if (visited.insert(next).second) worklist.push_back(next);
          break;
        }
        default:
          break;
      }
    }
  }
  return finish(false);
}

bool symbol_in_sync_set(
    const atn::Atn& atn,
    uint16_t sync_set_id,
    lx_symbol sym) noexcept {
  if (sym == LX_SYMBOL_EOF) {
    return true;
  }
  if (sync_set_id == atn::kIndexNoneU16 ||
      sync_set_id >= atn.sets().size()) {
    return false;
  }
  return atn.set_contains(sync_set_id, sym);
}

} // namespace internal
} // namespace lx::parse
