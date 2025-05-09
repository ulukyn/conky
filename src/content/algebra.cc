/*
 *
 * Conky, a system monitor, based on torsmo
 *
 * Any original torsmo code is licensed under the BSD license
 *
 * All code written since the fork of torsmo is licensed under the GPL
 *
 * Please see COPYING for details
 *
 * Copyright (c) 2004, Hannu Saransaari and Lauri Hakkarainen
 * Copyright (c) 2005-2024 Brenden Matthews, Philip Kovacs, et. al.
 *	(see AUTHORS)
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "algebra.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include "config.h"
#include "../conky.h"
#include "../logging.h"

/* find the operand in the given expression
 * returns the index of the first op character or -1 on error
 */
int find_match_op(const char *expr) {
  unsigned int idx = 0;

  /* if first operand is a string, skip it */
  if (expr[idx] == '"') {
    for (idx=1; expr[idx] && expr[idx] != '"'; idx++);
    idx++;
  }

  for (; idx < strlen(expr); idx++) {
    switch (expr[idx]) {
      case '=':
      case '!':
        if (expr[idx + 1] != '=') { return -1; }
        /* falls through */
      case '<':
      case '>':
        return idx;
    }
  }
  return -1;
}

int get_match_type(const char *expr) {
  int idx;
  const char *str;

  if ((idx = find_match_op(expr)) == -1) { return -1; }
  str = expr + idx;

  if (*str == '=' && *(str + 1) == '=') { return OP_EQ; }
  if (*str == '!' && *(str + 1) == '=') { return OP_NEQ; }
  if (*str == '>') {
    if (*(str + 1) == '=') { return OP_GEQ; }
    return OP_GT;
  }
  if (*str == '<') {
    if (*(str + 1) == '=') { return OP_LEQ; }
    return OP_LT;
  }
  return -1;
}

/* generic compare function
 *
 * v is actually the difference of the compared values. For strings
 * this is equal to the output of str(n)cmp(). Use a macro here, as
 * it's type-independent.
 */
#define COMPARE(v, t)    \
  switch (t) {           \
    case OP_GT:          \
      return ((v) > 0);  \
    case OP_LT:          \
      return ((v) < 0);  \
    case OP_EQ:          \
      return ((v) == 0); \
    case OP_GEQ:         \
      return ((v) >= 0); \
    case OP_LEQ:         \
      return ((v) <= 0); \
    case OP_NEQ:         \
      return ((v) != 0); \
  }                      \
  return 0

int lcompare(long a, enum match_type mtype, long b) {
  DBGP2("comparing longs '%ld' and '%ld'", a, b);
  COMPARE((a - b), mtype);
}
int dcompare(double a, enum match_type mtype, double b) {
  DBGP2("comparing doubles '%.lf' and '%.lf'", a, b);
  COMPARE((a - b), mtype);
}

int scompare(const char *a, enum match_type mtype, const char *b) {
  DBGP2("comparing strings '%s' and '%s'", a, b);
  COMPARE(strcmp(a, b), mtype);
}

enum arg_type get_arg_type(const char *arg) {
  const char *p, *e;

  p = arg;
  e = arg + strlen(arg) - 1;

  while (p != e && (*e != 0) && *e == ' ') { e--; }
  while (p != e && *p == ' ') { p++; }

  if (*p == '"' && *e == '"') { return ARG_STRING; }

  if (*p == '-') {  // allow negative values
    p++;
  }
  while (p <= e) {
    if (isdigit(static_cast<unsigned char>(*p)) == 0) { break; }
    p++;
  }
  if (p == e + 1) { return ARG_LONG; }
  if (*p == '.' || *p == ',') {
    p++;
    while (p <= e) {
      if (isdigit(static_cast<unsigned char>(*p)) == 0) { return ARG_BAD; }
      p++;
    }
    return ARG_DOUBLE;
  }
  return ARG_BAD;
}

char *arg_to_string(const char *arg) {
  const char *start;
  int len;

  start = arg;
  len = 0;
  while ((*start != 0) && *start == ' ') { start++; }
  if (!(*(start++) == '"')) { return nullptr; }
  while (start[len] != '"') { len++; }
  return strndup(start, len);
}
double arg_to_double(const char *arg) {
  double d;
  if (sscanf(arg, "%lf", &d) != 1) {
    NORM_ERR("converting '%s' to double failed", arg);
    return 0.0;
  }
  return d;
}
long arg_to_long(const char *arg) {
  long l;
  if (sscanf(arg, "%ld", &l) != 1) {
    NORM_ERR("converting '%s' to long failed", arg);
    return 0;
  }
  return l;
}

int compare(const char *expr) {
  char *expr_dup;
  int idx, mtype;
  enum arg_type type1, type2;
  long lng_a, lng_b;
  double dbl_a, dbl_b;

  idx = find_match_op(expr);
  mtype = get_match_type(expr);

  if ((idx <= 0) || mtype == -1) {
    NORM_ERR("failed to parse compare string '%s'", expr);
    return -2;
  }

  expr_dup = strdup(expr);
  expr_dup[idx] = '\0';
  if (expr_dup[idx + 1] == '=') { expr_dup[++idx] = '\0'; }

  type1 = get_arg_type(expr_dup);
  type2 = get_arg_type(expr_dup + idx + 1);
  if (type1 == ARG_BAD || type2 == ARG_BAD) {
    NORM_ERR("Bad arguments: '%s' and '%s'", expr_dup, (expr_dup + idx + 1));
    free(expr_dup);
    return -2;
  }
  if (type1 == ARG_LONG && type2 == ARG_DOUBLE) { type1 = ARG_DOUBLE; }
  if (type1 == ARG_DOUBLE && type2 == ARG_LONG) { type2 = ARG_DOUBLE; }
  if (type1 != type2) {
    NORM_ERR("trying to compare args '%s' and '%s' of different type", expr_dup,
             (expr_dup + idx + 1));
    free(expr_dup);
    return -2;
  }
  switch (type1) {
    case ARG_STRING: {
      char *a, *b;
      a = arg_to_string(expr_dup);
      b = arg_to_string(expr_dup + idx + 1);
      idx = scompare(a, static_cast<enum match_type>(mtype), b);
      free(a);
      free(b);
      free(expr_dup);
      return idx;
    }
    case ARG_LONG:
      lng_a = arg_to_long(expr_dup);
      lng_b = arg_to_long(expr_dup + idx + 1);
      free(expr_dup);
      return lcompare(lng_a, static_cast<enum match_type>(mtype), lng_b);
    case ARG_DOUBLE:
      dbl_a = arg_to_double(expr_dup);
      dbl_b = arg_to_double(expr_dup + idx + 1);
      free(expr_dup);
      return dcompare(dbl_a, static_cast<enum match_type>(mtype), dbl_b);
    case ARG_BAD: /* make_gcc_happy() */;
  }
  /* not reached */
  free(expr_dup);
  return -2;
}

int check_if_match(struct text_object *obj) {
  std::unique_ptr<char[]> expression(new char[max_user_text.get(*state)]);
  int val;
  int result = 1;

  generate_text_internal(expression.get(), max_user_text.get(*state),
                         *obj->sub);
  DBGP("parsed arg into '%s'", expression.get());

  val = compare(expression.get());
  if (val == -2) {
    NORM_ERR("compare failed for expression '%s'", expression.get());
  } else if (val == 0) {
    result = 0;
  }
  return result;
}
