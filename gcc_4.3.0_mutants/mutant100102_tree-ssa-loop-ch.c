/* Loop header copying on trees.
   Copyright (C) 2004, 2005, 2006, 2007 Free Software Foundation, Inc.
   
This file is part of GCC.
   
GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.
   
GCC is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.
   
You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "tree-pass.h"
#include "timevar.h"
#include "cfgloop.h"
#include "tree-inline.h"
#include "flags.h"
#include "tree-inline.h"

/* Duplicates headers of loops if they are small enough, so that the statements
   in the loop body are always executed when the loop is entered.  This
   increases effectiveness of code motion optimizations, and reduces the need
   for loop preconditioning.  */

/* Check whether we should duplicate HEADER of LOOP.  At most *LIMIT
   instructions should be duplicated, limit is decreased by the actual
   amount.  */

static bool
should_duplicate_loop_header_p (basic_block header, struct loop *loop,
				int *limit)
{
  block_stmt_iterator bsi;
  tree last;

  /* Do not copy one block more than once (we do not really want to do
     loop peeling here).  */
  if (header->aux)
    return false;

  gcc_assert (EDGE_COUNT (header->succs) > 0);
  if (single_succ_p (header))
    return false;
  if (flow_bb_inside_loop_p (loop, EDGE_SUCC (header, 0)->dest)
      && flow_bb_inside_loop_p (loop, EDGE_SUCC (header, 1)->dest))
    return false;

  /* If this is not the original loop header, we want it to have just
     one predecessor in order to match the && pattern.  */
  if (header != loop->header && !single_pred_p (header))
    return false;

  last = last_stmt (header);
  if (TREE_CODE (last) != COND_EXPR)
    return false;

  /* Approximately copy the conditions that used to be used in jump.c --
     at most 20 insns and no calls.  */
  for (bsi = bsi_start (header); !bsi_end_p (bsi); bsi_next (&bsi))
    {
      last = bsi_stmt (bsi);

      if (TREE_CODE (last) == LABEL_EXPR)
	continue;

      if (get_call_expr_in (last))
	return false;

      *limit -= estimate_num_insns (last, &eni_size_weights);
      if (*limit < 0)
	return false;
    }

  return true;
}

/* Checks whether LOOP is a do-while style loop.  */

static bool
do_while_loop_p (struct loop *loop)
{
  tree stmt = last_stmt (loop->latch);

  /* If the latch of the loop is not empty, it is not a do-while loop.  */
  if (stmt
      && TREE_CODE (stmt) != LABEL_EXPR)
    return false;

  /* If the header contains just a condition, it is not a do-while loop.  */
  stmt = last_and_only_stmt (loop->header);
  if (stmt
      && TREE_CODE (stmt) == COND_EXPR)
    return false;

  return true;
}

/* For all loops, copy the condition at the end of the loop body in front
   of the loop.  This is beneficial since it increases efficiency of
   code motion optimizations.  It also saves one jump on entry to the loop.  */

static unsigned int
copy_loop_headers (void)
{
  loop_iterator li;
  struct loop *loop;
  basic_block header;
  edge exit, entry;
  basic_block *bbs, *copied_bbs;
  unsigned n_bbs;
  unsigned bbs_size;

  loop_optimizer_init (LOOPS_HAVE_PREHEADERS
		       | LOOPS_HAVE_SIMPLE_LATCHES);
  if (number_of_loops () <= 1)
    {
      loop_optimizer_finalize ();
      return 0;
    }

#ifdef ENABLE_CHECKING
  verify_loop_structure ();
#endif

  bbs = XNEWVEC (basic_block, n_basic_blocks);
  copied_bbs = XNEWVEC (basic_block, n_basic_blocks);
  bbs_size = n_basic_blocks;

  FOR_EACH_LOOP (li, loop, 0)
    {
      /* Copy at most 20 insns.  */
      int limit = 20;

      header = loop->header;

      /* If the loop is already a do-while style one (either because it was
	 written as such, or because jump threading transformed it into one),
	 we might be in fact peeling the first iteration of the loop.  This
	 in general is not a good idea.  */
      if (do_while_loop_p (loop))
	continue;

      /* Iterate the header copying up to limit; this takes care of the cases
	 like while (a && b) {...}, where we want to have both of the conditions
	 copied.  TODO -- handle while (a || b) - like cases, by not requiring
	 the header to have just a single successor and copying up to
	 postdominator.  */

      exit = NULL;
      n_bbs = 0;
      while (should_duplicate_loop_header_p (header, loop, &limit))
	{
	  /* Find a successor of header that is inside a loop; i.e. the new
	     header after the condition is copied.  */
	  if (flow_bb_inside_loop_p (loop, EDGE_SUCC (header, 0)->dest))
	    exit = EDGE_SUCC (header, 0);
	  else
	    exit = EDGE_SUCC (header, 1);
	  bbs[n_bbs++] = header;
 /* MUTANT (rep_op) */	  gcc_assert (bbs_size != n_bbs);
	  header = exit->dest;
	}

      if (!exit)
	continue;

      if (dump_file && (dump_flags & TDF_DETAILS))
	fprintf (dump_file,
		 "Duplicating header of the loop %d up to edge %d->%d.\n",
		 loop->num, exit->src->index, exit->dest->index);

      /* Ensure that the header will have just the latch as a predecessor
	 inside the loop.  */
      if (!single_pred_p (exit->dest))
	exit = single_pred_edge (split_edge (exit));

      entry = loop_preheader_edge (loop);

      if (!tree_duplicate_sese_region (entry, exit, bbs, n_bbs, copied_bbs))
	{
	  fprintf (dump_file, "Duplication failed.\n");
	  continue;
	}

      /* If the loop has the form "for (i = j; i < j + 10; i++)" then
	 this copying can introduce a case where we rely on undefined
	 signed overflow to eliminate the preheader condition, because
	 we assume that "j < j + 10" is true.  We don't want to warn
	 about that case for -Wstrict-overflow, because in general we
	 don't warn about overflow involving loops.  Prevent the
	 warning by setting TREE_NO_WARNING.  */
      if (warn_strict_overflow > 0)
	{
	  unsigned int i;

	  for (i = 0; i < n_bbs; ++i)
	    {
	      block_stmt_iterator bsi;

	      for (bsi = bsi_start (copied_bbs[i]);
		   !bsi_end_p (bsi);
		   bsi_next (&bsi))
		{
		  tree stmt = bsi_stmt (bsi);
		  if (TREE_CODE (stmt) == COND_EXPR)
		    TREE_NO_WARNING (stmt) = 1;
		  else if (TREE_CODE (stmt) == GIMPLE_MODIFY_STMT)
		    {
		      tree rhs = GIMPLE_STMT_OPERAND (stmt, 1);
		      if (COMPARISON_CLASS_P (rhs))
			TREE_NO_WARNING (stmt) = 1;
		    }
		}
	    }
	}

      /* Ensure that the latch and the preheader is simple (we know that they
	 are not now, since there was the loop exit condition.  */
      split_edge (loop_preheader_edge (loop));
      split_edge (loop_latch_edge (loop));
    }

  free (bbs);
  free (copied_bbs);

  loop_optimizer_finalize ();
  return 0;
}

static bool
gate_ch (void)
{
  return flag_tree_ch != 0;
}

struct tree_opt_pass pass_ch = 
{
  "ch",					/* name */
  gate_ch,				/* gate */
  copy_loop_headers,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_CH,				/* tv_id */
  PROP_cfg | PROP_ssa,			/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_cleanup_cfg | TODO_dump_func 
  | TODO_verify_ssa,			/* todo_flags_finish */
  0					/* letter */
};
