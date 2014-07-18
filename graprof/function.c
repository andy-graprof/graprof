
/******************************************************************************
 *             graprof - a profiling and trace analysis tool                  *
 *                                                                            *
 *    Copyright (C) 2013 - 2014  Andreas Grapentin                            *
 *                                                                            *
 *    This program is free software: you can redistribute it and/or modify    *
 *    it under the terms of the GNU General Public License as published by    *
 *    the Free Software Foundation, either version 3 of the License, or       *
 *    (at your option) any later version.                                     *
 *                                                                            *
 *    This program is distributed in the hope that it will be useful,         *
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *    GNU General Public License for more details.                            *
 *                                                                            *
 *    You should have received a copy of the GNU General Public License       *
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 ******************************************************************************/

#include "function.h"
#include "addr.h"
#include "bitmask.h"

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include <grapes/util.h>
#include <grapes/feedback.h>

static function *functions = NULL;
static unsigned int nfunctions = 0;
static function **sorted_functions = NULL;

static unsigned long long total_calls = 0;

static tree_entry call_tree_root = { -1, 0, 0, 0, 0, 0, NULL, NULL, 0, NULL, 0 };
static tree_entry *call_tree_current_node = &call_tree_root;

static void
function_init (function *f)
{
  f->address = 0;
  f->name = NULL;
  f->file = NULL;
  f->line = 0;
  f->self_time = 0;
  f->children_time = 0;
  f->calls = 0;
  f->callers = NULL;
  f->ncallers = 0;
  f->callees = NULL;
  f->ncallees = 0;
  f->primaries = NULL;
  f->nprimaries = 0;
  f->secondaries = NULL;
  f->nsecondaries = 0;
}

static void
function_call_tree_entry_init (tree_entry *e)
{
  e->function_id = -1;
  e->entry_time = 0;
  e->exit_time = 0;
  e->self_time = 0;
  e->children_time = 0;
  e->orphan_time = 0;
  e->parent = NULL;
  e->children = NULL;
  e->nchildren = 0;
  e->orphans = NULL;
  e->norphans = 0;
}

static void
function_call_data_init (call_data *d)
{
  d->function_id = 0;
  d->calls = 0;
  d->self_time = 0;
  d->children_time = 0;
}

static function* may_fail
function_push (uintptr_t address)
{
  ++nfunctions;
  functions = realloc(functions, sizeof(*functions) * nfunctions);
  assert_inner_ptr(functions, "realloc");

  function *f = functions + nfunctions - 1;
  function_init(f);

  f->address = address;
  int res = addr_translate(f->address, &(f->name), &(f->file), &(f->line));
  assert_inner_ptr(!res, "addr_translate");

  feedback_assert_wrn(strcmp(f->name, "??"), "unable to identify instrumented function at 0x%" PRIxPTR " - missing debug symbols?", address);

  return f;
}

static int may_fail
function_add_caller (function *f, unsigned int caller_id)
{
  unsigned int i;
  for (i = 0; i < f->ncallers; ++i)
    if (f->callers[i].function_id == caller_id)
      {
        ++(f->callers[i].calls);
        return 0;
      }

  ++(f->ncallers);
  f->callers = realloc(f->callers, sizeof(*(f->callers)) * f->ncallers);
  assert_inner(f->callers, "realloc");

  call_data *d = f->callers + f->ncallers - 1;
  function_call_data_init(d);
  d->function_id = caller_id;
  d->calls = 1;

  return 0;
}

static int may_fail
function_add_callee (function *f, unsigned int callee_id)
{
  unsigned int i;
  for (i = 0; i < f->ncallees; ++i)
    if (f->callees[i].function_id == callee_id)
      {
        ++(f->callees[i].calls);
        return 0;
      }

  ++(f->ncallees);
  f->callees = realloc(f->callees, sizeof(*(f->callees)) * f->ncallees);
  assert_inner(f->callees, "realloc");

  call_data *d = f->callees + f->ncallees - 1;
  function_call_data_init(d);
  d->function_id = callee_id;
  d->calls = 1;

  return 0;
}

static void
function_aggregate_call_tree_node_times (tree_entry *t)
{
  unsigned int i;
  for (i = 0; i < t->nchildren; ++i)
    {
      function_aggregate_call_tree_node_times(t->children[i]);
      t->children_time += t->children[i]->self_time + t->children[i]->children_time;
      t->orphan_time += t->children[i]->orphan_time;
    }

  for (i = 0; i < t->norphans; ++i)
    {
      function_aggregate_call_tree_node_times(t->orphans[i]);
      t->orphan_time += t->orphans[i]->self_time + t->orphans[i]->children_time + t->orphans[i]->orphan_time;
    }

  t->self_time = t->exit_time - t->entry_time - t->children_time - t->orphan_time;
}

static void
function_add_caller_self_time (unsigned int caller_id, unsigned int callee_id, unsigned long long time)
{
  function *f = functions + caller_id;

  unsigned int i;
  for (i = 0; i < f->ncallees; ++i)
    if (f->callees[i].function_id == callee_id)
      f->callees[i].self_time += time;

  f = functions + callee_id;

  for (i = 0; i < f->ncallers; ++i)
    if (f->callers[i].function_id == caller_id)
      f->callers[i].self_time += time;
}

static void
function_add_caller_children_time (unsigned int caller_id, unsigned int callee_id, unsigned long long time)
{
  function *f = functions + caller_id;

  unsigned int i;
  for (i = 0; i < f->ncallees; ++i)
    if (f->callees[i].function_id == callee_id)
      f->callees[i].children_time += time;

  f = functions + callee_id;

  for (i = 0; i < f->ncallers; ++i)
    if (f->callers[i].function_id == caller_id)
      f->callers[i].children_time += time;
}

static int may_fail
function_create_call_vector_from_node(tree_entry *e, bitmask *b)
{
  if (e->function_id != (unsigned int)-1)
    {
      function *f = functions + e->function_id;

      f->self_time += e->self_time;
      if (e->parent != NULL && e->parent->function_id != (unsigned int)-1)
        function_add_caller_self_time(e->parent->function_id, e->function_id, e->self_time);
      if (!bitmask_get(b, e->function_id))
        {
          f->children_time += e->children_time;
          if (e->parent != NULL && e->parent->function_id != (unsigned int)-1)
            function_add_caller_children_time(e->parent->function_id, e->function_id, e->children_time);
          bitmask_set(b, e->function_id);

          ++(f->nprimaries);
          f->primaries = realloc(f->primaries, sizeof(*f->primaries) * f->nprimaries);
          assert_inner(f->primaries, "realloc");
          f->primaries[f->nprimaries - 1] = e;
        }
      else
        {
          ++(f->nsecondaries);
          f->secondaries = realloc(f->secondaries, sizeof(*f->secondaries) * f->nsecondaries);
          assert_inner(f->secondaries, "realloc");
          f->secondaries[f->nsecondaries - 1] = e;
        }
    }

  unsigned int i;
  for (i = 0; i < e->nchildren; ++i)
    {
      bitmask *b2 = bitmask_copy(b);
      assert_inner(b2, "bitmask_copy");
      int res = function_create_call_vector_from_node(e->children[i], b2);
      assert_inner(!res, "function_call_vector_from_node");
      bitmask_destroy(&b2);
    }
  for (i = 0; i < e->norphans; ++i)
    {
      bitmask *b2 = bitmask_copy(b);
      assert_inner(b2, "bitmask_copy");
      int res = function_create_call_vector_from_node(e->orphans[i], b2);
      assert_inner(!res, "function_call_vector_from_node");
      bitmask_destroy(&b2);
    }

  return 0;
}

static int may_fail
function_aggregate_function_times (void)
{
  bitmask *b = bitmask_create(nfunctions);
  assert_inner(b, "bitmask_create");

  int res = function_create_call_vector_from_node(&call_tree_root, b);
  assert_inner(!res, "function_create_call_vector_from_node");

  bitmask_destroy(&b);

  return 0;
}

static int
cmpfunction (const void *p1, const void *p2)
{
  function *f1 = *(function**)p1;
  function *f2 = *(function**)p2;

  if (f2->self_time == f1->self_time)
    return strcmp(f1->name, f2->name);

  if (f2->self_time < f1->self_time)
    return -1;
  return 1;
}

static int may_fail
function_sort (void)
{
  sorted_functions = malloc(sizeof(function*) * nfunctions);
  assert_inner(sorted_functions, "malloc");

  unsigned int i;
  for (i = 0; i < nfunctions; ++i)
    sorted_functions[i] = functions + i;

  qsort(sorted_functions, nfunctions, sizeof(function*), cmpfunction);

  return 0;
}

int
function_enter (uintptr_t address, uintptr_t caller, unsigned long long time)
{
  ++total_calls;

  function *f = function_get_by_address(address);
  if (!f)
    f = function_push(address);
  assert_inner(f, "function_push");

  ++(f->calls);

  tree_entry *n = call_tree_current_node;
  tree_entry *next = malloc(sizeof(*next));
  assert_inner(next,  "malloc");

  unsigned int caller_id = (unsigned int)-1;

  if (n->parent != NULL && !function_compare(functions + n->function_id, caller))
    {
      caller_id = n->function_id;

      if (n->function_id != (unsigned int)-1)
        {
          int res = function_add_callee(functions + n->function_id, f - functions);
          assert_inner(!res, "function_add_callee");
        }

      ++(n->nchildren);
      n->children = realloc(n->children, sizeof(*(n->children)) * n->nchildren);
      assert_inner(n->children, "realloc");

      n->children[n->nchildren - 1] = next;
    }
  else
    {
      ++(n->norphans);
      n->orphans = realloc(n->orphans, sizeof(*(n->orphans)) * n->norphans);
      assert_inner(n->orphans, "realloc");

      n->orphans[n->norphans - 1] = next;
    }

  int res = function_add_caller(f, caller_id);
  assert_inner(!res, "function_add_callee");

  function_call_tree_entry_init(next);
  next->function_id = f - functions;
  next->entry_time = time;
  next->parent = call_tree_current_node;

  call_tree_current_node = next;

  return 0;
}

void
function_exit (unsigned long long time)
{
  if (call_tree_current_node->parent == NULL)
    feedback_warning("exit called on empty call stack - skewed trace data?");

  call_tree_current_node->exit_time = time;
  call_tree_current_node = call_tree_current_node->parent;
}

int
function_exit_all (unsigned long long time)
{
  while (call_tree_current_node->parent != NULL)
    function_exit(time);

  function_aggregate_call_tree_node_times(&call_tree_root);
  int res = function_aggregate_function_times();
  assert_inner(!res, "function_aggregate_function_times");

  return 0;
}

function*
function_get_by_address (uintptr_t address)
{
  unsigned int i;
  for (i = 0; i < nfunctions; ++i)
    if (functions[i].address == address)
      return (functions + i);

  return NULL;
}

function*
function_get_all (void)
{
  return functions;
}

function**
function_get_all_sorted (void)
{
  if (!sorted_functions)
    {
      int res = function_sort();
      assert_inner_ptr(!res, "function_sort");
    }

  return sorted_functions;
}

function*
function_get_current (void)
{
  if (call_tree_current_node->function_id != (unsigned int)-1)
    return functions + call_tree_current_node->function_id;
  return NULL;
}

int
function_compare (function *f, uintptr_t addr)
{
  if (!f)
    return -1;

  char *name;
  char *file;
  int res = addr_translate(addr, &name, &file, NULL);
  assert_inner(!res, "addr_translate");

  int x = (strcmp(f->name, name) || strcmp(f->file, file));

  free(name);
  free(file);

  return x;
}

unsigned long long
function_get_total_calls (void)
{
  return total_calls;
}

unsigned int
function_get_nfunctions (void)
{
  return nfunctions;
}

static void
free_call_tree (tree_entry *e)
{
  unsigned int i;
  for (i = 0; i < e->nchildren; ++i)
    {
      free_call_tree(e->children[i]);
      free(e->children[i]);
    }
  for (i = 0; i < e->norphans; ++i)
    {
      free_call_tree(e->orphans[i]);
      free(e->orphans[i]);
    }

  free(e->children);
  free(e->orphans);
}

static void
__attribute__((destructor))
function_fini ()
{
  unsigned int i;
  for (i = 0; i < nfunctions; ++i)
    {
      free(functions[i].name);
      free(functions[i].file);
      free(functions[i].callers);
      free(functions[i].callees);
      free(functions[i].primaries);
      free(functions[i].secondaries);
    }
  free(functions);
  free(sorted_functions);

  free_call_tree(&call_tree_root);
}

