#pragma once
#include "lx_language.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Зарезервированные идентификаторы символов (lx_symbol, uint16_t) —
 * индексы в lx_language_info::symbols.
 * Пользовательские терминалы и нетерминалы начинаются с 4.
 * Терминалы (включая EOF=1) лежат в [0 .. terminal_count - 1].
 */
#define LX_SYMBOL_INVALID    ((lx_symbol)0u)
#define LX_SYMBOL_EOF        ((lx_symbol)1u)
#define LX_SYMBOL_ERROR      ((lx_symbol)2u)
#define LX_SYMBOL_MISSING    ((lx_symbol)3u)
#define LX_FIRST_USER_SYMBOL ((lx_symbol)4u)

/* Отсутствующее поле у ребёнка (GreenRef::field_id) и на переходе ATN.
 * fields[0] зарезервировано под пустое имя (""); пользовательские поля >= 1.
 */
#define LX_FIELD_NONE        ((lx_field)0u)

/* Режим лексера по умолчанию (DEFAULT_MODE). */
#define LX_MODE_DEFAULT      ((lx_mode)0u)

/* Версия бинарного формата LXATN / LXTBL. */
#define LX_ATN_FORMAT_VERSION 1u

#ifdef __cplusplus
}
#endif
