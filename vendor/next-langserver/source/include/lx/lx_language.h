#pragma once
#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
  #define LX_EXPORT __declspec(dllexport)
  #define LX_CALL   __cdecl
#else
  #define LX_EXPORT __attribute__((visibility("default")))
  #define LX_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LX_ABI_VERSION 1

/* ---------- примитивы ---------- */
typedef struct { const char* ptr; uint32_t len; } lx_str;     /* UTF-8, без NUL */
typedef struct { uint32_t start, end; } lx_range;             /* байты, [start,end) */
typedef uint16_t lx_symbol;
typedef uint16_t lx_field;
typedef uint16_t lx_mode;

typedef enum {
  LX_OK = 0, LX_CANCELLED, LX_OOM, LX_BAD_ARG, LX_BAD_STATE, LX_INTERNAL
} lx_status;

/* ---------- сервисы хоста ---------- */
typedef struct lx_host_api {
  uint32_t size, abi_version;
  void*    (LX_CALL *alloc)(size_t n);
  void     (LX_CALL *free)(void* p);
  void     (LX_CALL *log)(int level, lx_str msg);
  /* Интернирование внешнего состояния лексера по содержимому (стек отступов и т.п.). */
  uint64_t (LX_CALL *state_intern)(lx_str blob);
  lx_str   (LX_CALL *state_resolve)(uint64_t id);        /* валидно пока жив хост */
} lx_host_api;

/* ---------- метаданные языка ---------- */
enum {
  LX_SYM_TERMINAL  = 1u<<0,
  LX_SYM_NAMED     = 1u<<1,   /* значим для пользователя */
  LX_SYM_HIDDEN    = 1u<<2,   /* trivia: пробелы, комментарии */
  LX_SYM_ERROR     = 1u<<3,
  LX_SYM_MISSING   = 1u<<4,
  LX_SYM_SUPERTYPE = 1u<<5,
  LX_SYM_ALT_LABEL = 1u<<6    /* помеченная альтернатива (#Label) */
};
typedef struct { lx_str name; uint32_t flags; } lx_symbol_info;
typedef struct { lx_symbol sub, super; } lx_supertype_edge;

enum {
  LX_CAP_INCREMENTAL_LEX   = 1u<<0,
  LX_CAP_INCREMENTAL_PARSE = 1u<<1,
  LX_CAP_ERROR_RECOVERY    = 1u<<2,
  LX_CAP_INJECTIONS        = 1u<<3,
  LX_CAP_NATIVE_PARSER     = 1u<<4
};

typedef struct {
  uint32_t size;
  lx_str   id;                       /* "python" */
  lx_str   display_name;
  const lx_str* extensions;  uint32_t extension_count;
  uint32_t lang_version;
  uint64_t grammar_hash;
  const lx_symbol_info* symbols;     uint32_t symbol_count;
  const lx_str*         fields;      uint32_t field_count;
  const lx_supertype_edge* supertypes; uint32_t supertype_count;
  const lx_str*         modes;       uint32_t mode_count;
  uint32_t capabilities;
} lx_language_info;

/* ---------- лексер ---------- */
typedef struct {
  void* ctx;
  uint32_t length;                                            /* длина текста (вида) */
  /* Непрерывный фрагмент, начинающийся в offset; *len = 0 и NULL при offset >= length. */
  const uint8_t* (LX_CALL *chunk)(void* ctx, uint32_t offset, uint32_t* len);
} lx_input;

typedef struct {
  const lx_input* input;
  uint32_t offset;
  lx_mode  mode;                    /* верхушка стека режимов */
  uint64_t ext;                     /* внешнее состояние (0 = начальное) */
} lx_lex_request;

enum { LX_CMD_NONE = 0, LX_CMD_PUSH, LX_CMD_POP, LX_CMD_MODE };
enum { LX_CH_DEFAULT = 0, LX_CH_HIDDEN = 1 };

typedef struct {
  lx_symbol symbol;                 /* LX_SYMBOL_EOF / LX_SYMBOL_ERROR зарезервированы */
  uint8_t   channel;
  uint8_t   cmd;
  lx_mode   cmd_arg;
  uint32_t  width;                  /* > 0, кроме EOF */
  uint32_t  reach_ahead;            /* байтов прочитано ЗА концом токена */
  uint64_t  ext_next;
} lx_lex_result;

/* ---------- предикаты ---------- */
typedef struct {
  void* ctx;
  lx_symbol (LX_CALL *la_symbol)(void* ctx, int32_t i);       /* i=1 следующий, i<=-1 назад */
  lx_str    (LX_CALL *la_text)(void* ctx, int32_t i);
  int       (LX_CALL *la_line_start)(void* ctx, int32_t i);
  uint64_t  (LX_CALL *env_get)(void* ctx, uint32_t dep_id);   /* версия/значение зависимости */
} lx_pred_view;

/* ---------- данные парсера ---------- */
typedef struct {
  uint32_t size, format_version;
  const uint8_t* atn;  uint32_t atn_size;
  const uint8_t* tables; uint32_t tables_size;
  uint32_t start_rule;
  uint32_t pred_count;
  const lx_str* pred_names;
} lx_parser_tables;

/* Опционально: нативные функции правил (семантика идентична data-mode). */
typedef struct lx_native_parser {
  uint32_t size;
  void* reserved[8];
} lx_native_parser;

/* ---------- таблица языка ---------- */
typedef struct lx_language_v1 {
  uint32_t size;
  uint32_t abi_version;

  const lx_language_info* (LX_CALL *info)(void);

  /* Чистая функция: никакого скрытого изменяемого состояния. */
  lx_status (LX_CALL *lex)(const lx_lex_request*, lx_lex_result*);

  const lx_parser_tables*  parser;
  const lx_native_parser*  native;                            /* может быть NULL */

  /* Чистый предикат, зависит только от окна токенов и env_get. */
  int  (LX_CALL *eval_pred)(uint32_t pred_id, const lx_pred_view*);

  const void* (LX_CALL *get_extension)(lx_str name);          /* может быть NULL */
  void (LX_CALL *shutdown)(void);
} lx_language_v1;

/* Единственный экспорт DLL. */
LX_EXPORT const lx_language_v1* LX_CALL
lx_get_language(uint32_t host_abi_version, const lx_host_api* host);

#ifdef __cplusplus
}
#endif
