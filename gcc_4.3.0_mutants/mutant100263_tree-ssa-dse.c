/* Dead store elimination
   Copyright (C) 2004, 2005, 2006, 2007 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "tree.h"
#include "rtl.h"
#include "tm_p.h"
#include "basic-block.h"
#include "timevar.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "tree-dump.h"
#include "domwalk.h"
#include "flags.h"

/* This file implements dead store elimination.

   A dead store is a store into a memory location which will later be
   overwritten by another store without any intervening loads.  In this
   case the earlier store can be deleted.

   In our SSA + virtual operand world we use immediate uses of virtual
   operands to detect dead stores.  If a store's virtual definition
   is used precisely once by a later store to the same location which
   post dominates the first store, then the first store is dead. 

   The single use of the store's virtual definition ensures that
   there are no intervening aliased loads and the requirement that
   the second load post dominate the first ensures that if the earlier
   store executes, then the later stores will execute before the function
   exits.

   It may help to think of this as first moving the earlier store to
   the point immediately before the later store.  Again, the single
   use of the virtual definition and the post-dominance relationship
   ensure that such movement would be safe.  Clearly if there are 
   back to back stores, then the second is redundant.

   Reviewing section 10.7.2 in Morgan's "Building an Optimizing Compiler"
   may also help in understanding this code since it discusses the
   relationship between dead store and redundant load elimination.  In
   fact, they are the same transformation applied to different views of
   the CFG.  */
   

struct dse_global_data
{
  /* This is the global bitmap for store statements.

     Each statement has a unique ID.  When we encounter a store statement
     that we want to record, set the bit corresponding to the statement's
     unique ID in this bitmap.  */
  bitmap stores;
};

/* We allocate a bitmap-per-block for stores which are encountered
   during the scan of that block.  This allows us to restore the 
   global bitmap of stores when we finish processing a block.  */
struct dse_block_local_data
{
  bitmap stores;
};

/* Basic blocks of the potentially dead store and the following
   store, for memory_address_same.  */
struct address_walk_data
{
  basic_block store1_bb, store2_bb;
};

static bool gate_dse (void);
static unsigned int tree_ssa_dse (void);
static void dse_initialize_block_local_data (struct dom_walk_data *,
					     basic_block,
					     bool);
static void dse_optimize_stmt (struct dom_walk_data *,
			       basic_block,
			       block_stmt_iterator);
static void dse_record_phis (struct dom_walk_data *, basic_block);
static void dse_finalize_block (struct dom_walk_data *, basic_block);
static void record_voperand_set (bitmap, bitmap *, unsigned int);

static unsigned max_stmt_uid;	/* Maximal uid of a statement.  Uids to phi
				   nodes are assigned using the versions of
				   ssa names they define.  */

/* Returns uid of statement STMT.  */

static unsigned
get_stmt_uid (tree stmt)
{
  if (TREE_CODE (stmt) == PHI_NODE)
    return SSA_NAME_VERSION (PHI_RESULT (stmt)) + max_stmt_uid;

  return stmt_ann (stmt)->uid;
}

/* Set bit UID in bitmaps GLOBAL and *LOCAL, creating *LOCAL as needed.  */

static void
record_voperand_set (bitmap global, bitmap *local, unsigned int uid)
{
  /* Lazily allocate the bitmap.  Note that we do not get a notification
     when the block local data structures die, so we allocate the local
     bitmap backed by the GC system.  */
  if (*local == NULL)
    *local = BITMAP_GGC_ALLOC ();

  /* Set the bit in the local and global bitmaps.  */
  bitmap_set_bit (*local, uid);
  bitmap_set_bit (global, uid);
}

/* Initialize block local data structures.  */

static void
dse_initialize_block_local_data (struct dom_walk_data *walk_data,
				 basic_block bb ATTRIBUTE_UNUSED,
				 bool recycled)
{
  struct dse_block_local_data *bd
    = (struct dse_block_local_data *)
	VEC_last (void_p, walk_data->block_data_stack);

  /* If we are given a recycled block local data structure, ensure any
     bitmap associated with the block is cleared.  */
  if (recycled)
    {
      if (bd->stores)
	bitmap_clear (bd->stores);
    }
}

/* Helper function for memory_address_same via walk_tree.  Returns
   non-NULL if it finds an SSA_NAME which is part of the address,
   such that the definition of the SSA_NAME post-dominates the store
   we want to delete but not the store that we believe makes it
   redundant.  This indicates that the address may change between
   the two stores.  */

static tree
memory_ssa_name_same (tree *expr_p, int *walk_subtrees ATTRIBUTE_UNUSED,
		      void *data)
{
  struct address_walk_data *walk_data = (struct address_walk_data *) data;
  tree expr = *expr_p;
  tree def_stmt;
  basic_block def_bb;

  if (TREE_CODE (expr) != SSA_NAME)
    return NULL_TREE;

  /* If we've found a default definition, then there's no problem.  Both
     stores will post-dominate it.  And def_bb will be NULL.  */
  if (SSA_NAME_IS_DEFAULT_DEF (expr))
    return NULL_TREE;

  def_stmt = SSA_NAME_DEF_STMT (expr);
  def_bb = bb_for_stmt (def_stmt);

  /* DEF_STMT must dominate both stores.  So if it is in the same
     basic block as one, it does not post-dominate that store.  */
  if (walk_data->store1_bb != def_bb
      && dominated_by_p (CDI_POST_DOMINATORS, walk_data->store1_bb, def_bb))
    {
      if (walk_data->store2_bb == def_bb
	  || !dominated_by_p (CDI_POST_DOMINATORS, walk_data->store2_bb,
			      def_bb))
	/* Return non-NULL to stop the walk.  */
	return def_stmt;
    }

  return NULL_TREE;
}

/* Return TRUE if the destination memory address in STORE1 and STORE2
   might be modified after STORE1, before control reaches STORE2.  */

static bool
memory_address_same (tree store1, tree store2)
{
  struct address_walk_data walk_data;

  walk_data.store1_bb = bb_for_stmt (store1);
  walk_data.store2_bb = bb_for_stmt (store2);

  return (walk_tree (&GIMPLE_STMT_OPERAND (store1, 0), memory_ssa_name_same,
		     &walk_data, NULL)
	  == NULL);
}

/* Return true if there is a stmt that kills the lhs of STMT and is in the
   virtual def-use chain of STMT without a use inbetween the kill and STMT.
   Returns false if no such stmt is found.
   *FIRST_USE_P is set to the first use of the single virtual def of
   STMT.  *USE_P is set to the vop killed by *USE_STMT.  */

static bool
get_kill_of_stmt_lhs (tree stmt,
		      use_operand_p * first_use_p,
 		      use_operand_p * use_p, tree * use_stmt)
{
  tree lhs;

  gcc_assert (TREE_CODE (stmt) == GIMPLE_MODIFY_STMT);

  lhs = GIMPLE_STMT_OPERAND (stmt, 0);

  /* We now walk the chain of single uses of the single VDEFs.
     We succeeded finding a kill if the lhs of the use stmt is
     equal to the original lhs.  We can keep walking to the next
     use if there are no possible uses of the original lhs in
     the stmt.  */
  do
    {
      tree use_lhs, use_rhs;
      def_operand_p def_p;

      /* The stmt must have a single VDEF.  */
      def_p = SINGLE_SSA_DEF_OPERAND (stmt, SSA_OP_VDEF);
      if (def_p == NULL_DEF_OPERAND_P)
	return false;

      /* Get the single immediate use of the def.  */
      if (!single_imm_use (DEF_FROM_PTR (def_p), first_use_p, &stmt))
	return false;
      first_use_p = use_p;

      /* If there are possible hidden uses, give up.  */
      if (TREE_CODE (stmt) != GIMPLE_MODIFY_STMT)
	return false;
      use_rhs = GIMPLE_STMT_OPERAND (stmt, 1);
      if (TREE_CODE (use_rhs) == CALL_EXPR
	  || (!is_gimple_min_invariant (use_rhs)
	      && TREE_CODE (use_rhs) != SSA_NAME))
	return false;

      /* If the use stmts lhs matches the original lhs we have
	 found the kill, otherwise continue walking.  */
      use_lhs = GIMPLE_STMT_OPERAND (stmt, 0);
      if (operand_equal_p (use_lhs, lhs, 0))
	{
	  *use_stmt = stmt;
	  return true;
	}
    }
  while (1);
}

/* A helper of dse_optimize_stmt.
   Given a GIMPLE_MODIFY_STMT in STMT, check that each VDEF has one
   use, and that one use is another VDEF clobbering the first one.

   Return TRUE if the above conditions are met, otherwise FALSE.  */

static bool
dse_possible_dead_store_p (tree stmt,
			   use_operand_p *first_use_p,
			   use_operand_p *use_p,
			   tree *use_stmt,
			   struct dse_global_data *dse_gd,
			   struct dse_block_local_data *bd)
{
  ssa_op_iter op_iter;
  bool fail = false;
  def_operand_p var1;
  vuse_vec_p vv;
  tree defvar = NULL_TREE, temp;
  tree prev_defvar = NULL_TREE;
  stmt_ann_t ann = stmt_ann (stmt);

  /* We want to verify that each virtual definition in STMT has
     precisely one use and that all the virtual definitions are
     used by the same single statement.  When complete, we
     want USE_STMT to refer to the one statement which uses
     all of the virtual definitions from STMT.  */
  *use_stmt = NULL;
  FOR_EACH_SSA_VDEF_OPERAND (var1, vv, stmt, op_iter)
    {
      defvar = DEF_FROM_PTR (var1);

      /* If this virtual def does not have precisely one use, then
	 we will not be able to eliminate STMT.  */
      if (!has_single_use (defvar))
	{
	  fail = true;
	  break;
	}

      /* Get the one and only immediate use of DEFVAR.  */
      single_imm_use (defvar, use_p, &temp);
      gcc_assert (*use_p != NULL_USE_OPERAND_P);
      *first_use_p = *use_p;

      /* In the case of memory partitions, we may get:

	   # MPT.764_162 = VDEF <MPT.764_161(D)>
	   x = {};
	   # MPT.764_167 = VDEF <MPT.764_162>
	   y = {};

	   So we must make sure we're talking about the same LHS.
      */
      if (TREE_CODE (temp) == GIMPLE_MODIFY_STMT)
	{
	  tree base1 = get_base_address (GIMPLE_STMT_OPERAND (stmt, 0));
	  tree base2 =  get_base_address (GIMPLE_STMT_OPERAND (temp, 0));

	  while (base1 && INDIRECT_REF_P (base1))
	    base1 = TREE_OPERAND (base1, 0);
	  while (base2 && INDIRECT_REF_P (base2))
	    base2 = TREE_OPERAND (base2, 0);

	  if (base1 != base2)
	    {
	      fail = true;
	      break;
	    }
	}

      /* If the immediate use of DEF_VAR is not the same as the
	 previously find immediate uses, then we will not be able
	 to eliminate STMT.  */
      if (*use_stmt == NULL)
	{
	  *use_stmt = temp;
	  prev_defvar = defvar;
	}
      else if (temp != *use_stmt)
	{
	  fail = true;
	  break;
	}
    }

  if (fail)
    {
      record_voperand_set (dse_gd->stores, &bd->stores, ann->uid);
      return false;
    }

  /* Skip through any PHI nodes we have already seen if the PHI
     represents the only use of this store.

     Note this does not handle the case where the store has
     multiple VDEFs which all reach a set of PHI nodes in the same block.  */
  while (*use_p != NULL_USE_OPERAND_P
	 && TREE_CODE (*use_stmt) == PHI_NODE
	 && bitmap_bit_p (dse_gd->stores, get_stmt_uid (*use_stmt)))
    {
      /* A PHI node can both define and use the same SSA_NAME if
	 the PHI is at the top of a loop and the PHI_RESULT is
	 a loop invariant and copies have not been fully propagated.

	 The safe thing to do is exit assuming no optimization is
	 possible.  */
      if (SSA_NAME_DEF_STMT (PHI_RESULT (*use_stmt)) == *use_stmt)
	return false;

      /* Skip past this PHI and loop again in case we had a PHI
	 chain.  */
      single_imm_use (PHI_RESULT (*use_stmt), use_p, use_stmt);
    }

  return true;
}


/* Attempt to eliminate dead stores in the statement referenced by BSI.

   A dead store is a store into a memory location which will later be
   overwritten by another store without any intervening loads.  In this
   case the earlier store can be deleted.

   In our SSA + virtual operand world we use immediate uses of virtual
   operands to detect dead stores.  If a store's virtual definition
   is used precisely once by a later store to the same location which
   post dominates the first store, then the first store is dead.  */

static void
dse_optimize_stmt (struct dom_walk_data *walk_data,
		   basic_block bb ATTRIBUTE_UNUSED,
		   block_stmt_iterator bsi)
{
  struct dse_block_local_data *bd
    = (struct dse_block_local_data *)
	VEC_last (void_p, walk_data->block_data_stack);
  struct dse_global_data *dse_gd
    = (struct dse_global_data *) walk_data->global_data;
  tree stmt = bsi_stmt (bsi);
  stmt_ann_t ann = stmt_ann (stmt);

  /* If this statement has no virtual defs, then there is nothing
     to do.  */
  if (ZERO_SSA_OPERANDS (stmt, SSA_OP_VDEF))
    return;

  /* We know we have virtual definitions.  If this is a GIMPLE_MODIFY_STMT
     that's not also a function call, then record it into our table.  */
  if (get_call_expr_in (stmt))
    return;

  if (ann->has_volatile_ops)
    return;

  if (TREE_CODE (stmt) == GIMPLE_MODIFY_STMT)
    {
      use_operand_p first_use_p = NULL_USE_OPERAND_P;
      use_operand_p use_p = NULL;
      tree use_stmt;

      if (!dse_possible_dead_store_p (stmt, &first_use_p, &use_p, &use_stmt,
				      dse_gd, bd))
	return;

      /* If we have precisely one immediate use at this point, then we may
	 have found redundant store.  Make sure that the stores are to
	 the same memory location.  This includes checking that any
	 SSA-form variables in the address will have the same values.  */
      if (use_p != NULL_USE_OPERAND_P
          && bitmap_bit_p (dse_gd->stores, get_stmt_uid (use_stmt))
          && !operand_equal_p (GIMPLE_STMT_OPERAND (stmt, 0),
                               GIMPLE_STMT_OPERAND (use_stmt, 0), 0)
          && memory_address_same (stmt, use_stmt))
        {
          /* If we have precisely one immediate use at this point, but
             the stores are not to the same memory location then walk the
             virtual def-use chain to get the stmt which stores to that same
             memory location.  */
          if (!get_kill_of_stmt_lhs (stmt, &first_use_p, &use_p, &use_stmt))
            {
              record_voperand_set (dse_gd->stores, &bd->stores, ann->uid);
              return;
            }
        }

      /* If we have precisely one immediate use at this point and the
	 stores are to the same memory location or there is a chain of
	 virtual uses from stmt and the stmt which stores to that same
	 memory location, then we may have found redundant store.  */
      if (use_p != NULL_USE_OPERAND_P
	  && bitmap_bit_p (dse_gd->stores, get_stmt_uid (use_stmt))
	  && operand_equal_p (GIMPLE_STMT_OPERAND (stmt, 0),
			      GIMPLE_STMT_OPERAND (use_stmt, 0), 0)
	  && memory_address_same (stmt, use_stmt))
	{
	  ssa_op_iter op_iter;
	  def_operand_p var1;
	  vuse_vec_p vv;
	  tree stmt_lhs;

	  if (LOADED_SYMS (use_stmt))
	    {
	      tree use_base
		= get_base_address (GIMPLE_STMT_OPERAND (use_stmt, 0));
	      /* If use_stmt is or might be a nop assignment, e.g. for
		 struct { ... } S a, b, *p; ...
		 b = a; b = b;
		 or
		 b = a; b = *p; where p might be &b, then USE_STMT
		 acts as a use as well as definition, so store in STMT
		 is not dead.  */
	      if (TREE_CODE (use_base) == VAR_DECL
		  && bitmap_bit_p (LOADED_SYMS (use_stmt),
				   DECL_UID (use_base)))
		{
		  record_voperand_set (dse_gd->stores, &bd->stores, ann->uid);
		  return;
		}
	    }

	  if (dump_file && (dump_flags & TDF_DETAILS))
            {
              fprintf (dump_file, "  Deleted dead store '");
              print_generic_expr (dump_file, bsi_stmt (bsi), dump_flags);
              fprintf (dump_file, "'\n");
            }

	  /* Then we need to fix the operand of the consuming stmt.  */
	  stmt_lhs = USE_FROM_PTR (first_use_p);
	  FOR_EACH_SSA_VDEF_OPERAND (var1, vv, stmt, op_iter)
	    {
	      tree usevar, temp;

	      single_imm_use (DEF_FROM_PTR (var1), &use_p, &temp);
	      gcc_assert (VUSE_VECT_NUM_ELEM (*vv) == 1);
	      usevar = VUSE_ELEMENT_VAR (*vv, 0);
	      SET_USE (use_p, usevar);

	      /* Make sure we propagate the ABNORMAL bit setting.  */
	      if (SSA_NAME_OCCURS_IN_ABNORMAL_PHI (stmt_lhs))
		SSA_NAME_OCCURS_IN_ABNORMAL_PHI (usevar) = 1;
	    }

	  /* Remove the dead store.  */
	  bsi_remove (&bsi, true);

	  /* And release any SSA_NAMEs set in this statement back to the
	     SSA_NAME manager.  */
	  release_defs (stmt);
	}

      record_voperand_set (dse_gd->stores, &bd->stores, ann->uid);
    }
}

/* Record that we have seen the PHIs at the start of BB which correspond
   to virtual operands.  */
static void
dse_record_phis (struct dom_walk_data *walk_data, basic_block bb)
{
  struct dse_block_local_data *bd
    = (struct dse_block_local_data *)
	VEC_last (void_p, walk_data->block_data_stack);
  struct dse_global_data *dse_gd
    = (struct dse_global_data *) walk_data->global_data;
  tree phi;

  for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
    if (!is_gimple_reg (PHI_RESULT (phi)))
      record_voperand_set (dse_gd->stores,
			   &bd->stores,
			   get_stmt_uid (phi));
}

static void
dse_finalize_block (struct dom_walk_data *walk_data,
		    basic_block bb ATTRIBUTE_UNUSED)
{
  struct dse_block_local_data *bd
    = (struct dse_block_local_data *)
	VEC_last (void_p, walk_data->block_data_stack);
  struct dse_global_data *dse_gd
    = (struct dse_global_data *) walk_data->global_data;
  bitmap stores = dse_gd->stores;
  unsigned int i;
  bitmap_iterator bi;

  /* Unwind the stores noted in this basic block.  */
  if (bd->stores)
    EXECUTE_IF_SET_IN_BITMAP (bd->stores, 0, i, bi)
      {
	bitmap_clear_bit (stores, i);
      }
}

/* Main entry point.  */

static unsigned int
tree_ssa_dse (void)
{
  struct dom_walk_data walk_data;
  struct dse_global_data dse_gd;
  basic_block bb;

  /* Create a UID for each statement in the function.  Ordering of the
     UIDs is not important for this pass.  */
  max_stmt_uid = 0;
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator bsi;

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	stmt_ann (bsi_stmt (bsi))->uid = max_stmt_uid++;
    }

  /* We might consider making this a property of each pass so that it
     can be [re]computed on an as-needed basis.  Particularly since
     this pass could be seen as an extension of DCE which needs post
     dominators.  */
  calculate_dominance_info (CDI_POST_DOMINATORS);

  /* Dead store elimination is fundamentally a walk of the post-dominator
     tree and a backwards walk of statements within each block.  */
  walk_data.walk_stmts_backward = true;
  walk_data.dom_direction = CDI_POST_DOMINATORS;
  walk_data.initialize_block_local_data = dse_initialize_block_local_data;
  walk_data.before_dom_children_before_stmts = NULL;
  walk_data.before_dom_children_walk_stmts = dse_optimize_stmt;
  walk_data.before_dom_children_after_stmts = dse_record_phis;
  walk_data.after_dom_children_before_stmts = NULL;
  walk_data.after_dom_children_walk_stmts = NULL;
  walk_data.after_dom_children_after_stmts = dse_finalize_block;
  walk_data.interesting_blocks = NULL;

  walk_data.block_local_data_size = sizeof (struct dse_block_local_data);

  /* This is the main hash table for the dead store elimination pass.  */
  dse_gd.stores = BITMAP_ALLOC (NULL);
  walk_data.global_data = &dse_gd;

  /* Initialize the dominator walker.  */
  init_walk_dominator_tree (&walk_data);

  /* Recursively walk the dominator tree.  */
  walk_dominator_tree (&walk_data, EXIT_BLOCK_PTR);

  /* Finalize the dominator walker.  */
  fini_walk_dominator_tree (&walk_data);

  /* Release the main bitmap.  */
  BITMAP_FREE (dse_gd.stores);

  /* For now, just wipe the post-dominator information.  */
  free_dominance_info (CDI_POST_DOMINATORS);
  return 0;
}

static bool
gate_dse (void)
{
  return flag_tree_dse != 0;
}

struct tree_opt_pass pass_dse = {
  "dse",			/* name */
  gate_dse,			/* gate */
  tree_ssa_dse,			/* execute */
  NULL,				/* sub */
  NULL,				/* next */
  0,				/* static_pass_number */
  TV_TREE_DSE,			/* tv_id */
  PROP_cfg
    | PROP_ssa
    | PROP_alias,		/* properties_required */
  0,				/* properties_provided */
  0,				/* properties_destroyed */
  0,				/* todo_flags_start */
  TODO_dump_func
    | TODO_ggc_collect
    | TODO_verify_ssa,		/* todo_flags_finish */
  0				/* letter */
};

/* A very simple dead store pass eliminating write only local variables.
   The pass does not require alias information and thus can be run before
   inlining to quickly eliminate artifacts of some common C++ constructs.  */

static unsigned int
execute_simple_dse (void)
{
  block_stmt_iterator bsi;
  basic_block bb;
  bitmap variables_loaded = BITMAP_ALLOC (NULL);
  unsigned int todo = 0;

  /* Collect into VARIABLES LOADED all variables that are read in function
     body.  */
  FOR_EACH_BB (bb)
    for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
      if (LOADED_SYMS (bsi_stmt (bsi)))
	bitmap_ior_into (variables_loaded,
			 LOADED_SYMS (bsi_stmt (bsi)));

  /* Look for statements writing into the write only variables.
     And try to remove them.  */

  FOR_EACH_BB (bb)
    for (bsi = bsi_start (bb); !bsi_end_p (bsi);)
      {
	tree stmt = bsi_stmt (bsi), op;
	bool removed = false;
        ssa_op_iter iter;

 /* MUTANT (rep_op) */	if (STORED_SYMS (stmt) && TREE_CODE (stmt) != GIMPLE_MODIFY_STMT
	    && TREE_CODE (stmt) != RETURN_EXPR
	    && !bitmap_intersect_p (STORED_SYMS (stmt), variables_loaded))
	  {
	    unsigned int i;
	    bitmap_iterator bi;
	    bool dead = true;



	    /* See if STMT only stores to write-only variables and
	       verify that there are no volatile operands.  tree-ssa-operands
	       sets has_volatile_ops flag for all statements involving
	       reads and writes when aliases are not built to prevent passes
	       from removing them as dead.  The flag thus has no use for us
	       and we need to look into all operands.  */
	      
	    EXECUTE_IF_SET_IN_BITMAP (STORED_SYMS (stmt), 0, i, bi)
	      {
		tree var = referenced_var_lookup (i);
		if (TREE_ADDRESSABLE (var)
		    || is_global_var (var)
		    || TREE_THIS_VOLATILE (var))
		  dead = false;
	      }

	    if (dead && LOADED_SYMS (stmt))
	      EXECUTE_IF_SET_IN_BITMAP (LOADED_SYMS (stmt), 0, i, bi)
		if (TREE_THIS_VOLATILE (referenced_var_lookup (i)))
		  dead = false;

	    if (dead)
	      FOR_EACH_SSA_TREE_OPERAND (op, stmt, iter, SSA_OP_ALL_OPERANDS)
		if (TREE_THIS_VOLATILE (op))
		  dead = false;

	    /* Look for possible occurence var = indirect_ref (...) where
	       indirect_ref itself is volatile.  */

	    if (dead && TREE_THIS_VOLATILE (GIMPLE_STMT_OPERAND (stmt, 1)))
	      dead = false;

	    if (dead)
	      {
		tree call = get_call_expr_in (stmt);

		/* When LHS of var = call (); is dead, simplify it into
		   call (); saving one operand.  */
		if (TREE_CODE (stmt) == GIMPLE_MODIFY_STMT
		    && call
		    && TREE_SIDE_EFFECTS (call))
		  {
		    if (dump_file && (dump_flags & TDF_DETAILS))
		      {
			fprintf (dump_file, "Deleted LHS of call: ");
			print_generic_stmt (dump_file, stmt, TDF_SLIM);
			fprintf (dump_file, "\n");
		      }
		    push_stmt_changes (bsi_stmt_ptr (bsi));
		    TREE_BLOCK (call) = TREE_BLOCK (stmt);
		    bsi_replace (&bsi, call, false);
		    maybe_clean_or_replace_eh_stmt (stmt, call);
		    mark_symbols_for_renaming (call);
		    pop_stmt_changes (bsi_stmt_ptr (bsi));
		  }
		else
		  {
		    if (dump_file && (dump_flags & TDF_DETAILS))
		      {
			fprintf (dump_file, "  Deleted dead store '");
			print_generic_expr (dump_file, stmt, dump_flags);
			fprintf (dump_file, "'\n");
		      }
		    removed = true;
		    bsi_remove (&bsi, true);
		    todo |= TODO_cleanup_cfg;
		  }
		todo |= TODO_remove_unused_locals | TODO_ggc_collect;
	      }
	  }
	if (!removed)
	  bsi_next (&bsi);
      }
  BITMAP_FREE (variables_loaded);
  return todo;
}

struct tree_opt_pass pass_simple_dse =
{
  "sdse",				/* name */
  NULL,					/* gate */
  execute_simple_dse,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_ssa,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func,          	        /* todo_flags_finish */
  0				        /* letter */
};
