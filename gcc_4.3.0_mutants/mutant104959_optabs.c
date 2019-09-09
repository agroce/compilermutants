/* Expand the basic unary and binary arithmetic operations, for GNU compiler.
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "toplev.h"

/* Include insn-config.h before expr.h so that HAVE_conditional_move
   is properly defined.  */
#include "insn-config.h"
#include "rtl.h"
#include "tree.h"
#include "tm_p.h"
#include "flags.h"
#include "function.h"
#include "except.h"
#include "expr.h"
#include "optabs.h"
#include "libfuncs.h"
#include "recog.h"
#include "reload.h"
#include "ggc.h"
#include "real.h"
#include "basic-block.h"
#include "target.h"

/* Each optab contains info on how this target machine
   can perform a particular operation
   for all sizes and kinds of operands.

   The operation to be performed is often specified
   by passing one of these optabs as an argument.

   See expr.h for documentation of these optabs.  */

#if GCC_VERSION >= 4000
__extension__ struct optab optab_table[OTI_MAX]
  = { [0 ... OTI_MAX - 1].handlers[0 ... NUM_MACHINE_MODES - 1].insn_code
      = CODE_FOR_nothing };
#else
/* init_insn_codes will do runtime initialization otherwise.  */
struct optab optab_table[OTI_MAX];
#endif

rtx libfunc_table[LTI_MAX];

/* Tables of patterns for converting one mode to another.  */
#if GCC_VERSION >= 4000
__extension__ struct convert_optab convert_optab_table[COI_MAX]
  = { [0 ... COI_MAX - 1].handlers[0 ... NUM_MACHINE_MODES - 1]
	[0 ... NUM_MACHINE_MODES - 1].insn_code
      = CODE_FOR_nothing };
#else
/* init_convert_optab will do runtime initialization otherwise.  */
struct convert_optab convert_optab_table[COI_MAX];
#endif

/* Contains the optab used for each rtx code.  */
optab code_to_optab[NUM_RTX_CODE + 1];

/* Indexed by the rtx-code for a conditional (eg. EQ, LT,...)
   gives the gen_function to make a branch to test that condition.  */

rtxfun bcc_gen_fctn[NUM_RTX_CODE];

/* Indexed by the rtx-code for a conditional (eg. EQ, LT,...)
   gives the insn code to make a store-condition insn
   to test that condition.  */

enum insn_code setcc_gen_code[NUM_RTX_CODE];

#ifdef HAVE_conditional_move
/* Indexed by the machine mode, gives the insn code to make a conditional
   move insn.  This is not indexed by the rtx-code like bcc_gen_fctn and
   setcc_gen_code to cut down on the number of named patterns.  Consider a day
   when a lot more rtx codes are conditional (eg: for the ARM).  */

enum insn_code movcc_gen_code[NUM_MACHINE_MODES];
#endif

/* Indexed by the machine mode, gives the insn code for vector conditional
   operation.  */

enum insn_code vcond_gen_code[NUM_MACHINE_MODES];
enum insn_code vcondu_gen_code[NUM_MACHINE_MODES];

/* The insn generating function can not take an rtx_code argument.
   TRAP_RTX is used as an rtx argument.  Its code is replaced with
   the code to be used in the trap insn and all other fields are ignored.  */
static GTY(()) rtx trap_rtx;

static void prepare_float_lib_cmp (rtx *, rtx *, enum rtx_code *,
				   enum machine_mode *, int *);
static rtx expand_unop_direct (enum machine_mode, optab, rtx, rtx, int);

/* Debug facility for use in GDB.  */
void debug_optab_libfuncs (void);

#ifndef HAVE_conditional_trap
#define HAVE_conditional_trap 0
#define gen_conditional_trap(a,b) (gcc_unreachable (), NULL_RTX)
#endif

/* Prefixes for the current version of decimal floating point (BID vs. DPD) */
#if ENABLE_DECIMAL_BID_FORMAT
#define DECIMAL_PREFIX "bid_"
#else
#define DECIMAL_PREFIX "dpd_"
#endif


/* Info about libfunc.  We use same hashtable for normal optabs and conversion
   optab.  In the first case mode2 is unused.  */
struct libfunc_entry GTY(())
{
  size_t optab;
  enum machine_mode mode1, mode2;
  rtx libfunc;
};

/* Hash table used to convert declarations into nodes.  */
static GTY((param_is (struct libfunc_entry))) htab_t libfunc_hash;

/* Used for attribute_hash.  */

static hashval_t
hash_libfunc (const void *p)
{
  const struct libfunc_entry *const e = (const struct libfunc_entry *) p;

  return (((int) e->mode1 + (int) e->mode2 * NUM_MACHINE_MODES)
	  ^ e->optab);
}

/* Used for optab_hash.  */

static int
eq_libfunc (const void *p, const void *q)
{
  const struct libfunc_entry *const e1 = (const struct libfunc_entry *) p;
  const struct libfunc_entry *const e2 = (const struct libfunc_entry *) q;

  return (e1->optab == e2->optab
	  && e1->mode1 == e2->mode1
	  && e1->mode2 == e2->mode2);
}

/* Return libfunc corresponding operation defined by OPTAB converting
   from MODE2 to MODE1.  Trigger lazy initialization if needed, return NULL
   if no libfunc is available.  */
rtx
convert_optab_libfunc (convert_optab optab, enum machine_mode mode1,
		       enum machine_mode mode2)
{
  struct libfunc_entry e;
  struct libfunc_entry **slot;

  e.optab = (size_t) (optab - &convert_optab_table[0]);
  e.mode1 = mode1;
  e.mode2 = mode2;
  slot = (struct libfunc_entry **) htab_find_slot (libfunc_hash, &e, NO_INSERT);
  if (!slot)
    {
      if (optab->libcall_gen)
	{
	  optab->libcall_gen (optab, optab->libcall_basename, mode1, mode2);
          slot = (struct libfunc_entry **) htab_find_slot (libfunc_hash, &e, NO_INSERT);
	  if (slot)
	    return (*slot)->libfunc;
	  else
	    return NULL;
	}
      return NULL;
    }
  return (*slot)->libfunc;
}

/* Return libfunc corresponding operation defined by OPTAB in MODE.
   Trigger lazy initialization if needed, return NULL if no libfunc is
   available.  */
rtx
optab_libfunc (optab optab, enum machine_mode mode)
{
  struct libfunc_entry e;
  struct libfunc_entry **slot;

  e.optab = (size_t) (optab - &optab_table[0]);
  e.mode1 = mode;
  e.mode2 = VOIDmode;
  slot = (struct libfunc_entry **) htab_find_slot (libfunc_hash, &e, NO_INSERT);
  if (!slot)
    {
      if (optab->libcall_gen)
	{
	  optab->libcall_gen (optab, optab->libcall_basename,
			      optab->libcall_suffix, mode);
          slot = (struct libfunc_entry **) htab_find_slot (libfunc_hash,
							   &e, NO_INSERT);
	  if (slot)
	    return (*slot)->libfunc;
	  else
	    return NULL;
	}
      return NULL;
    }
  return (*slot)->libfunc;
}


/* Add a REG_EQUAL note to the last insn in INSNS.  TARGET is being set to
   the result of operation CODE applied to OP0 (and OP1 if it is a binary
   operation).

   If the last insn does not set TARGET, don't do anything, but return 1.

   If a previous insn sets TARGET and TARGET is one of OP0 or OP1,
   don't add the REG_EQUAL note but return 0.  Our caller can then try
   again, ensuring that TARGET is not one of the operands.  */

static int
add_equal_note (rtx insns, rtx target, enum rtx_code code, rtx op0, rtx op1)
{
  rtx last_insn, insn, set;
  rtx note;

  gcc_assert (insns && INSN_P (insns) && NEXT_INSN (insns));

  if (GET_RTX_CLASS (code) != RTX_COMM_ARITH
      && GET_RTX_CLASS (code) != RTX_BIN_ARITH
      && GET_RTX_CLASS (code) != RTX_COMM_COMPARE
      && GET_RTX_CLASS (code) != RTX_COMPARE
      && GET_RTX_CLASS (code) != RTX_UNARY)
    return 1;

  if (GET_CODE (target) == ZERO_EXTRACT)
    return 1;

  for (last_insn = insns;
       NEXT_INSN (last_insn) != NULL_RTX;
       last_insn = NEXT_INSN (last_insn))
    ;

  set = single_set (last_insn);
  if (set == NULL_RTX)
    return 1;

  if (! rtx_equal_p (SET_DEST (set), target)
      /* For a STRICT_LOW_PART, the REG_NOTE applies to what is inside it.  */
      && (GET_CODE (SET_DEST (set)) != STRICT_LOW_PART
	  || ! rtx_equal_p (XEXP (SET_DEST (set), 0), target)))
    return 1;

  /* If TARGET is in OP0 or OP1, check if anything in SEQ sets TARGET
     besides the last insn.  */
  if (reg_overlap_mentioned_p (target, op0)
      || (op1 && reg_overlap_mentioned_p (target, op1)))
    {
      insn = PREV_INSN (last_insn);
      while (insn != NULL_RTX)
	{
	  if (reg_set_p (target, insn))
	    return 0;

	  insn = PREV_INSN (insn);
	}
    }

  if (GET_RTX_CLASS (code) == RTX_UNARY)
    note = gen_rtx_fmt_e (code, GET_MODE (target), copy_rtx (op0));
  else
    note = gen_rtx_fmt_ee (code, GET_MODE (target), copy_rtx (op0), copy_rtx (op1));

  set_unique_reg_note (last_insn, REG_EQUAL, note);

  return 1;
}

/* Widen OP to MODE and return the rtx for the widened operand.  UNSIGNEDP
   says whether OP is signed or unsigned.  NO_EXTEND is nonzero if we need
   not actually do a sign-extend or zero-extend, but can leave the
   higher-order bits of the result rtx undefined, for example, in the case
   of logical operations, but not right shifts.  */

static rtx
widen_operand (rtx op, enum machine_mode mode, enum machine_mode oldmode,
	       int unsignedp, int no_extend)
{
  rtx result;

  /* If we don't have to extend and this is a constant, return it.  */
  if (no_extend && GET_MODE (op) == VOIDmode)
    return op;

  /* If we must extend do so.  If OP is a SUBREG for a promoted object, also
     extend since it will be more efficient to do so unless the signedness of
     a promoted object differs from our extension.  */
  if (! no_extend
      || (GET_CODE (op) == SUBREG && SUBREG_PROMOTED_VAR_P (op)
	  && SUBREG_PROMOTED_UNSIGNED_P (op) == unsignedp))
    return convert_modes (mode, oldmode, op, unsignedp);

  /* If MODE is no wider than a single word, we return a paradoxical
     SUBREG.  */
  if (GET_MODE_SIZE (mode) <= UNITS_PER_WORD)
    return gen_rtx_SUBREG (mode, force_reg (GET_MODE (op), op), 0);

  /* Otherwise, get an object of MODE, clobber it, and set the low-order
     part to OP.  */

  result = gen_reg_rtx (mode);
  emit_insn (gen_rtx_CLOBBER (VOIDmode, result));
  emit_move_insn (gen_lowpart (GET_MODE (op), result), op);
  return result;
}

/* Return the optab used for computing the operation given by
   the tree code, CODE.  This function is not always usable (for
   example, it cannot give complete results for multiplication
   or division) but probably ought to be relied on more widely
   throughout the expander.  */
optab
optab_for_tree_code (enum tree_code code, const_tree type)
{
  bool trapv;
  switch (code)
    {
    case BIT_AND_EXPR:
      return and_optab;

    case BIT_IOR_EXPR:
      return ior_optab;

    case BIT_NOT_EXPR:
      return one_cmpl_optab;

    case BIT_XOR_EXPR:
      return xor_optab;

    case TRUNC_MOD_EXPR:
    case CEIL_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case ROUND_MOD_EXPR:
      return TYPE_UNSIGNED (type) ? umod_optab : smod_optab;

    case RDIV_EXPR:
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case EXACT_DIV_EXPR:
      if (TYPE_SATURATING(type))
	return TYPE_UNSIGNED(type) ? usdiv_optab : ssdiv_optab;
      return TYPE_UNSIGNED (type) ? udiv_optab : sdiv_optab;

    case LSHIFT_EXPR:
      if (TYPE_SATURATING(type))
	return TYPE_UNSIGNED(type) ? usashl_optab : ssashl_optab;
      return ashl_optab;

    case RSHIFT_EXPR:
      return TYPE_UNSIGNED (type) ? lshr_optab : ashr_optab;

    case LROTATE_EXPR:
      return rotl_optab;

    case RROTATE_EXPR:
      return rotr_optab;

    case MAX_EXPR:
      return TYPE_UNSIGNED (type) ? umax_optab : smax_optab;

    case MIN_EXPR:
      return TYPE_UNSIGNED (type) ? umin_optab : smin_optab;

    case REALIGN_LOAD_EXPR:
      return vec_realign_load_optab;

    case WIDEN_SUM_EXPR:
      return TYPE_UNSIGNED (type) ? usum_widen_optab : ssum_widen_optab;

    case DOT_PROD_EXPR:
      return TYPE_UNSIGNED (type) ? udot_prod_optab : sdot_prod_optab;

    case REDUC_MAX_EXPR:
      return TYPE_UNSIGNED (type) ? reduc_umax_optab : reduc_smax_optab;

    case REDUC_MIN_EXPR:
      return TYPE_UNSIGNED (type) ? reduc_umin_optab : reduc_smin_optab;

    case REDUC_PLUS_EXPR:
      return TYPE_UNSIGNED (type) ? reduc_uplus_optab : reduc_splus_optab;

    case VEC_LSHIFT_EXPR:
      return vec_shl_optab;

    case VEC_RSHIFT_EXPR:
      return vec_shr_optab;

    case VEC_WIDEN_MULT_HI_EXPR:
      return TYPE_UNSIGNED (type) ? 
	vec_widen_umult_hi_optab : vec_widen_smult_hi_optab;

    case VEC_WIDEN_MULT_LO_EXPR:
      return TYPE_UNSIGNED (type) ? 
	vec_widen_umult_lo_optab : vec_widen_smult_lo_optab;

    case VEC_UNPACK_HI_EXPR:
      return TYPE_UNSIGNED (type) ?
	vec_unpacku_hi_optab : vec_unpacks_hi_optab;

    case VEC_UNPACK_LO_EXPR:
      return TYPE_UNSIGNED (type) ? 
	vec_unpacku_lo_optab : vec_unpacks_lo_optab;

    case VEC_UNPACK_FLOAT_HI_EXPR:
      /* The signedness is determined from input operand.  */
      return TYPE_UNSIGNED (type) ?
	vec_unpacku_float_hi_optab : vec_unpacks_float_hi_optab;

    case VEC_UNPACK_FLOAT_LO_EXPR:
      /* The signedness is determined from input operand.  */
      return TYPE_UNSIGNED (type) ? 
	vec_unpacku_float_lo_optab : vec_unpacks_float_lo_optab;

    case VEC_PACK_TRUNC_EXPR:
      return vec_pack_trunc_optab;

    case VEC_PACK_SAT_EXPR:
      return TYPE_UNSIGNED (type) ? vec_pack_usat_optab : vec_pack_ssat_optab;

    case VEC_PACK_FIX_TRUNC_EXPR:
      /* The signedness is determined from output operand.  */
      return TYPE_UNSIGNED (type) ?
	vec_pack_ufix_trunc_optab : vec_pack_sfix_trunc_optab;

    default:
      break;
    }

  trapv = INTEGRAL_TYPE_P (type) && TYPE_OVERFLOW_TRAPS (type);
  switch (code)
    {
    case POINTER_PLUS_EXPR:
    case PLUS_EXPR:
      if (TYPE_SATURATING(type))
	return TYPE_UNSIGNED(type) ? usadd_optab : ssadd_optab;
      return trapv ? addv_optab : add_optab;

    case MINUS_EXPR:
      if (TYPE_SATURATING(type))
	return TYPE_UNSIGNED(type) ? ussub_optab : sssub_optab;
      return trapv ? subv_optab : sub_optab;

    case MULT_EXPR:
      if (TYPE_SATURATING(type))
	return TYPE_UNSIGNED(type) ? usmul_optab : ssmul_optab;
      return trapv ? smulv_optab : smul_optab;

    case NEGATE_EXPR:
      if (TYPE_SATURATING(type))
	return TYPE_UNSIGNED(type) ? usneg_optab : ssneg_optab;
      return trapv ? negv_optab : neg_optab;

    case ABS_EXPR:
      return trapv ? absv_optab : abs_optab;

    case VEC_EXTRACT_EVEN_EXPR:
      return vec_extract_even_optab;

    case VEC_EXTRACT_ODD_EXPR:
      return vec_extract_odd_optab;

    case VEC_INTERLEAVE_HIGH_EXPR:
      return vec_interleave_high_optab;

    case VEC_INTERLEAVE_LOW_EXPR:
      return vec_interleave_low_optab;

    default:
      return NULL;
    }
}


/* Expand vector widening operations.

   There are two different classes of operations handled here:
   1) Operations whose result is wider than all the arguments to the operation.
      Examples: VEC_UNPACK_HI/LO_EXPR, VEC_WIDEN_MULT_HI/LO_EXPR
      In this case OP0 and optionally OP1 would be initialized,
      but WIDE_OP wouldn't (not relevant for this case).
   2) Operations whose result is of the same size as the last argument to the
      operation, but wider than all the other arguments to the operation.
      Examples: WIDEN_SUM_EXPR, VEC_DOT_PROD_EXPR.
      In the case WIDE_OP, OP0 and optionally OP1 would be initialized.

   E.g, when called to expand the following operations, this is how
   the arguments will be initialized:
                                nops    OP0     OP1     WIDE_OP
   widening-sum                 2       oprnd0  -       oprnd1          
   widening-dot-product         3       oprnd0  oprnd1  oprnd2
   widening-mult                2       oprnd0  oprnd1  -
   type-promotion (vec-unpack)  1       oprnd0  -       -  */

rtx
expand_widen_pattern_expr (tree exp, rtx op0, rtx op1, rtx wide_op, rtx target,
                           int unsignedp)
{   
  tree oprnd0, oprnd1, oprnd2;
  enum machine_mode wmode = 0, tmode0, tmode1 = 0;
  optab widen_pattern_optab;
  int icode; 
  enum machine_mode xmode0, xmode1 = 0, wxmode = 0;
  rtx temp;
  rtx pat;
  rtx xop0, xop1, wxop;
  int nops = TREE_OPERAND_LENGTH (exp);

  oprnd0 = TREE_OPERAND (exp, 0);
  tmode0 = TYPE_MODE (TREE_TYPE (oprnd0));
  widen_pattern_optab =
        optab_for_tree_code (TREE_CODE (exp), TREE_TYPE (oprnd0));
  icode = (int) optab_handler (widen_pattern_optab, tmode0)->insn_code;
  gcc_assert (icode != CODE_FOR_nothing);
  xmode0 = insn_data[icode].operand[1].mode;

  if (nops >= 2)
    {
      oprnd1 = TREE_OPERAND (exp, 1);
      tmode1 = TYPE_MODE (TREE_TYPE (oprnd1));
      xmode1 = insn_data[icode].operand[2].mode;
    }

  /* The last operand is of a wider mode than the rest of the operands.  */
  if (nops == 2)
    {
      wmode = tmode1;
      wxmode = xmode1;
    }
  else if (nops == 3)
    {
      gcc_assert (tmode1 == tmode0);
      gcc_assert (op1);
      oprnd2 = TREE_OPERAND (exp, 2);
      wmode = TYPE_MODE (TREE_TYPE (oprnd2));
      wxmode = insn_data[icode].operand[3].mode;
    }

  if (!wide_op)
    wmode = wxmode = insn_data[icode].operand[0].mode;

  if (!target
      || ! (*insn_data[icode].operand[0].predicate) (target, wmode))
    temp = gen_reg_rtx (wmode);
  else
    temp = target;

  xop0 = op0;
  xop1 = op1;
  wxop = wide_op;

  /* In case the insn wants input operands in modes different from
     those of the actual operands, convert the operands.  It would
     seem that we don't need to convert CONST_INTs, but we do, so
     that they're properly zero-extended, sign-extended or truncated
     for their mode.  */

  if (GET_MODE (op0) != xmode0 && xmode0 != VOIDmode)
    xop0 = convert_modes (xmode0,
                          GET_MODE (op0) != VOIDmode
                          ? GET_MODE (op0)
                          : tmode0,
                          xop0, unsignedp);

  if (op1)
    if (GET_MODE (op1) != xmode1 && xmode1 != VOIDmode)
      xop1 = convert_modes (xmode1,
                            GET_MODE (op1) != VOIDmode
                            ? GET_MODE (op1)
                            : tmode1,
                            xop1, unsignedp);

  if (wide_op)
    if (GET_MODE (wide_op) != wxmode && wxmode != VOIDmode)
      wxop = convert_modes (wxmode,
                            GET_MODE (wide_op) != VOIDmode
                            ? GET_MODE (wide_op)
                            : wmode,
                            wxop, unsignedp);

  /* Now, if insn's predicates don't allow our operands, put them into
     pseudo regs.  */

  if (! (*insn_data[icode].operand[1].predicate) (xop0, xmode0)
      && xmode0 != VOIDmode)
    xop0 = copy_to_mode_reg (xmode0, xop0);

  if (op1)
    {
      if (! (*insn_data[icode].operand[2].predicate) (xop1, xmode1)
          && xmode1 != VOIDmode)
        xop1 = copy_to_mode_reg (xmode1, xop1);

      if (wide_op)
        {
          if (! (*insn_data[icode].operand[3].predicate) (wxop, wxmode)
              && wxmode != VOIDmode)
            wxop = copy_to_mode_reg (wxmode, wxop);

          pat = GEN_FCN (icode) (temp, xop0, xop1, wxop);
        }
      else
        pat = GEN_FCN (icode) (temp, xop0, xop1);
    }
  else
    {
      if (wide_op)
        {
          if (! (*insn_data[icode].operand[2].predicate) (wxop, wxmode)
              && wxmode != VOIDmode)
            wxop = copy_to_mode_reg (wxmode, wxop);

          pat = GEN_FCN (icode) (temp, xop0, wxop);
        }
      else
        pat = GEN_FCN (icode) (temp, xop0);
    }

  emit_insn (pat);
  return temp;
}

/* Generate code to perform an operation specified by TERNARY_OPTAB
   on operands OP0, OP1 and OP2, with result having machine-mode MODE.

   UNSIGNEDP is for the case where we have to widen the operands
   to perform the operation.  It says to use zero-extension.

   If TARGET is nonzero, the value
   is generated there, if it is convenient to do so.
   In all cases an rtx is returned for the locus of the value;
   this may or may not be TARGET.  */

rtx
expand_ternary_op (enum machine_mode mode, optab ternary_optab, rtx op0,
		   rtx op1, rtx op2, rtx target, int unsignedp)
{
  int icode = (int) optab_handler (ternary_optab, mode)->insn_code;
  enum machine_mode mode0 = insn_data[icode].operand[1].mode;
  enum machine_mode mode1 = insn_data[icode].operand[2].mode;
  enum machine_mode mode2 = insn_data[icode].operand[3].mode;
  rtx temp;
  rtx pat;
  rtx xop0 = op0, xop1 = op1, xop2 = op2;

  gcc_assert (optab_handler (ternary_optab, mode)->insn_code
	      != CODE_FOR_nothing);

  if (!target || !insn_data[icode].operand[0].predicate (target, mode))
    temp = gen_reg_rtx (mode);
  else
    temp = target;

  /* In case the insn wants input operands in modes different from
     those of the actual operands, convert the operands.  It would
     seem that we don't need to convert CONST_INTs, but we do, so
     that they're properly zero-extended, sign-extended or truncated
     for their mode.  */

  if (GET_MODE (op0) != mode0 && mode0 != VOIDmode)
    xop0 = convert_modes (mode0,
                          GET_MODE (op0) != VOIDmode
                          ? GET_MODE (op0)
                          : mode,
                          xop0, unsignedp);

  if (GET_MODE (op1) != mode1 && mode1 != VOIDmode)
    xop1 = convert_modes (mode1,
                          GET_MODE (op1) != VOIDmode
                          ? GET_MODE (op1)
                          : mode,
                          xop1, unsignedp);

  if (GET_MODE (op2) != mode2 && mode2 != VOIDmode)
    xop2 = convert_modes (mode2,
                          GET_MODE (op2) != VOIDmode
                          ? GET_MODE (op2)
                          : mode,
                          xop2, unsignedp);

  /* Now, if insn's predicates don't allow our operands, put them into
     pseudo regs.  */

  if (!insn_data[icode].operand[1].predicate (xop0, mode0)
      && mode0 != VOIDmode)
    xop0 = copy_to_mode_reg (mode0, xop0);

  if (!insn_data[icode].operand[2].predicate (xop1, mode1)
      && mode1 != VOIDmode)
    xop1 = copy_to_mode_reg (mode1, xop1);

  if (!insn_data[icode].operand[3].predicate (xop2, mode2)
      && mode2 != VOIDmode)
    xop2 = copy_to_mode_reg (mode2, xop2);

  pat = GEN_FCN (icode) (temp, xop0, xop1, xop2);

  emit_insn (pat);
  return temp;
}


/* Like expand_binop, but return a constant rtx if the result can be
   calculated at compile time.  The arguments and return value are
   otherwise the same as for expand_binop.  */

static rtx
simplify_expand_binop (enum machine_mode mode, optab binoptab,
		       rtx op0, rtx op1, rtx target, int unsignedp,
		       enum optab_methods methods)
{
  if (CONSTANT_P (op0) && CONSTANT_P (op1))
    {
      rtx x = simplify_binary_operation (binoptab->code, mode, op0, op1);

      if (x)
	return x;
    }

  return expand_binop (mode, binoptab, op0, op1, target, unsignedp, methods);
}

/* Like simplify_expand_binop, but always put the result in TARGET.
   Return true if the expansion succeeded.  */

bool
force_expand_binop (enum machine_mode mode, optab binoptab,
		    rtx op0, rtx op1, rtx target, int unsignedp,
		    enum optab_methods methods)
{
  rtx x = simplify_expand_binop (mode, binoptab, op0, op1,
				 target, unsignedp, methods);
  if (x == 0)
    return false;
  if (x != target)
    emit_move_insn (target, x);
  return true;
}

/* Generate insns for VEC_LSHIFT_EXPR, VEC_RSHIFT_EXPR.  */

rtx
expand_vec_shift_expr (tree vec_shift_expr, rtx target)
{
  enum insn_code icode;
  rtx rtx_op1, rtx_op2;
  enum machine_mode mode1;
  enum machine_mode mode2;
  enum machine_mode mode = TYPE_MODE (TREE_TYPE (vec_shift_expr));
  tree vec_oprnd = TREE_OPERAND (vec_shift_expr, 0);
  tree shift_oprnd = TREE_OPERAND (vec_shift_expr, 1);
  optab shift_optab;
  rtx pat;

  switch (TREE_CODE (vec_shift_expr))
    {
      case VEC_RSHIFT_EXPR:
	shift_optab = vec_shr_optab;
	break;
      case VEC_LSHIFT_EXPR:
	shift_optab = vec_shl_optab;
	break;
      default:
	gcc_unreachable ();
    }

  icode = (int) optab_handler (shift_optab, mode)->insn_code;
  gcc_assert (icode != CODE_FOR_nothing);

  mode1 = insn_data[icode].operand[1].mode;
  mode2 = insn_data[icode].operand[2].mode;

  rtx_op1 = expand_normal (vec_oprnd);
  if (!(*insn_data[icode].operand[1].predicate) (rtx_op1, mode1)
      && mode1 != VOIDmode)
    rtx_op1 = force_reg (mode1, rtx_op1);

  rtx_op2 = expand_normal (shift_oprnd);
  if (!(*insn_data[icode].operand[2].predicate) (rtx_op2, mode2)
      && mode2 != VOIDmode)
    rtx_op2 = force_reg (mode2, rtx_op2);

  if (!target
      || ! (*insn_data[icode].operand[0].predicate) (target, mode))
    target = gen_reg_rtx (mode);

  /* Emit instruction */
  pat = GEN_FCN (icode) (target, rtx_op1, rtx_op2);
  gcc_assert (pat);
  emit_insn (pat);

  return target;
}

/* This subroutine of expand_doubleword_shift handles the cases in which
   the effective shift value is >= BITS_PER_WORD.  The arguments and return
   value are the same as for the parent routine, except that SUPERWORD_OP1
   is the shift count to use when shifting OUTOF_INPUT into INTO_TARGET.
   INTO_TARGET may be null if the caller has decided to calculate it.  */

static bool
expand_superword_shift (optab binoptab, rtx outof_input, rtx superword_op1,
			rtx outof_target, rtx into_target,
			int unsignedp, enum optab_methods methods)
{
  if (into_target != 0)
    if (!force_expand_binop (word_mode, binoptab, outof_input, superword_op1,
			     into_target, unsignedp, methods))
      return false;

  if (outof_target != 0)
    {
      /* For a signed right shift, we must fill OUTOF_TARGET with copies
	 of the sign bit, otherwise we must fill it with zeros.  */
      if (binoptab != ashr_optab)
	emit_move_insn (outof_target, CONST0_RTX (word_mode));
      else
	if (!force_expand_binop (word_mode, binoptab,
				 outof_input, GEN_INT (BITS_PER_WORD - 1),
				 outof_target, unsignedp, methods))
	  return false;
    }
  return true;
}

/* This subroutine of expand_doubleword_shift handles the cases in which
   the effective shift value is < BITS_PER_WORD.  The arguments and return
   value are the same as for the parent routine.  */

static bool
expand_subword_shift (enum machine_mode op1_mode, optab binoptab,
		      rtx outof_input, rtx into_input, rtx op1,
		      rtx outof_target, rtx into_target,
		      int unsignedp, enum optab_methods methods,
		      unsigned HOST_WIDE_INT shift_mask)
{
  optab reverse_unsigned_shift, unsigned_shift;
  rtx tmp, carries;

  reverse_unsigned_shift = (binoptab == ashl_optab ? lshr_optab : ashl_optab);
  unsigned_shift = (binoptab == ashl_optab ? ashl_optab : lshr_optab);

  /* The low OP1 bits of INTO_TARGET come from the high bits of OUTOF_INPUT.
     We therefore need to shift OUTOF_INPUT by (BITS_PER_WORD - OP1) bits in
     the opposite direction to BINOPTAB.  */
  if (CONSTANT_P (op1) || shift_mask >= BITS_PER_WORD)
    {
      carries = outof_input;
      tmp = immed_double_const (BITS_PER_WORD, 0, op1_mode);
      tmp = simplify_expand_binop (op1_mode, sub_optab, tmp, op1,
				   0, true, methods);
    }
  else
    {
      /* We must avoid shifting by BITS_PER_WORD bits since that is either
	 the same as a zero shift (if shift_mask == BITS_PER_WORD - 1) or
	 has unknown behavior.  Do a single shift first, then shift by the
	 remainder.  It's OK to use ~OP1 as the remainder if shift counts
	 are truncated to the mode size.  */
      carries = expand_binop (word_mode, reverse_unsigned_shift,
			      outof_input, const1_rtx, 0, unsignedp, methods);
      if (shift_mask == BITS_PER_WORD - 1)
	{
	  tmp = immed_double_const (-1, -1, op1_mode);
	  tmp = simplify_expand_binop (op1_mode, xor_optab, op1, tmp,
				       0, true, methods);
	}
      else
	{
	  tmp = immed_double_const (BITS_PER_WORD - 1, 0, op1_mode);
	  tmp = simplify_expand_binop (op1_mode, sub_optab, tmp, op1,
				       0, true, methods);
	}
    }
  if (tmp == 0 || carries == 0)
    return false;
  carries = expand_binop (word_mode, reverse_unsigned_shift,
			  carries, tmp, 0, unsignedp, methods);
  if (carries == 0)
    return false;

  /* Shift INTO_INPUT logically by OP1.  This is the last use of INTO_INPUT
     so the result can go directly into INTO_TARGET if convenient.  */
  tmp = expand_binop (word_mode, unsigned_shift, into_input, op1,
		      into_target, unsignedp, methods);
  if (tmp == 0)
    return false;

  /* Now OR in the bits carried over from OUTOF_INPUT.  */
  if (!force_expand_binop (word_mode, ior_optab, tmp, carries,
			   into_target, unsignedp, methods))
    return false;

  /* Use a standard word_mode shift for the out-of half.  */
  if (outof_target != 0)
    if (!force_expand_binop (word_mode, binoptab, outof_input, op1,
			     outof_target, unsignedp, methods))
      return false;

  return true;
}


#ifdef HAVE_conditional_move
/* Try implementing expand_doubleword_shift using conditional moves.
   The shift is by < BITS_PER_WORD if (CMP_CODE CMP1 CMP2) is true,
   otherwise it is by >= BITS_PER_WORD.  SUBWORD_OP1 and SUPERWORD_OP1
   are the shift counts to use in the former and latter case.  All other
   arguments are the same as the parent routine.  */

static bool
expand_doubleword_shift_condmove (enum machine_mode op1_mode, optab binoptab,
				  enum rtx_code cmp_code, rtx cmp1, rtx cmp2,
				  rtx outof_input, rtx into_input,
				  rtx subword_op1, rtx superword_op1,
				  rtx outof_target, rtx into_target,
				  int unsignedp, enum optab_methods methods,
				  unsigned HOST_WIDE_INT shift_mask)
{
  rtx outof_superword, into_superword;

  /* Put the superword version of the output into OUTOF_SUPERWORD and
     INTO_SUPERWORD.  */
  outof_superword = outof_target != 0 ? gen_reg_rtx (word_mode) : 0;
  if (outof_target != 0 && subword_op1 == superword_op1)
    {
      /* The value INTO_TARGET >> SUBWORD_OP1, which we later store in
	 OUTOF_TARGET, is the same as the value of INTO_SUPERWORD.  */
      into_superword = outof_target;
      if (!expand_superword_shift (binoptab, outof_input, superword_op1,
				   outof_superword, 0, unsignedp, methods))
	return false;
    }
  else
    {
      into_superword = gen_reg_rtx (word_mode);
      if (!expand_superword_shift (binoptab, outof_input, superword_op1,
				   outof_superword, into_superword,
				   unsignedp, methods))
	return false;
    }

  /* Put the subword version directly in OUTOF_TARGET and INTO_TARGET.  */
  if (!expand_subword_shift (op1_mode, binoptab,
			     outof_input, into_input, subword_op1,
			     outof_target, into_target,
			     unsignedp, methods, shift_mask))
    return false;

  /* Select between them.  Do the INTO half first because INTO_SUPERWORD
     might be the current value of OUTOF_TARGET.  */
  if (!emit_conditional_move (into_target, cmp_code, cmp1, cmp2, op1_mode,
			      into_target, into_superword, word_mode, false))
    return false;

  if (outof_target != 0)
    if (!emit_conditional_move (outof_target, cmp_code, cmp1, cmp2, op1_mode,
				outof_target, outof_superword,
				word_mode, false))
      return false;

  return true;
}
#endif

/* Expand a doubleword shift (ashl, ashr or lshr) using word-mode shifts.
   OUTOF_INPUT and INTO_INPUT are the two word-sized halves of the first
   input operand; the shift moves bits in the direction OUTOF_INPUT->
   INTO_TARGET.  OUTOF_TARGET and INTO_TARGET are the equivalent words
   of the target.  OP1 is the shift count and OP1_MODE is its mode.
   If OP1 is constant, it will have been truncated as appropriate
   and is known to be nonzero.

   If SHIFT_MASK is zero, the result of word shifts is undefined when the
   shift count is outside the range [0, BITS_PER_WORD).  This routine must
   avoid generating such shifts for OP1s in the range [0, BITS_PER_WORD * 2).

   If SHIFT_MASK is nonzero, all word-mode shift counts are effectively
   masked by it and shifts in the range [BITS_PER_WORD, SHIFT_MASK) will
   fill with zeros or sign bits as appropriate.

   If SHIFT_MASK is BITS_PER_WORD - 1, this routine will synthesize
   a doubleword shift whose equivalent mask is BITS_PER_WORD * 2 - 1.
   Doing this preserves semantics required by SHIFT_COUNT_TRUNCATED.
   In all other cases, shifts by values outside [0, BITS_PER_UNIT * 2)
   are undefined.

   BINOPTAB, UNSIGNEDP and METHODS are as for expand_binop.  This function
   may not use INTO_INPUT after modifying INTO_TARGET, and similarly for
   OUTOF_INPUT and OUTOF_TARGET.  OUTOF_TARGET can be null if the parent
   function wants to calculate it itself.

   Return true if the shift could be successfully synthesized.  */

static bool
expand_doubleword_shift (enum machine_mode op1_mode, optab binoptab,
			 rtx outof_input, rtx into_input, rtx op1,
			 rtx outof_target, rtx into_target,
			 int unsignedp, enum optab_methods methods,
			 unsigned HOST_WIDE_INT shift_mask)
{
  rtx superword_op1, tmp, cmp1, cmp2;
  rtx subword_label, done_label;
  enum rtx_code cmp_code;

  /* See if word-mode shifts by BITS_PER_WORD...BITS_PER_WORD * 2 - 1 will
     fill the result with sign or zero bits as appropriate.  If so, the value
     of OUTOF_TARGET will always be (SHIFT OUTOF_INPUT OP1).   Recursively call
     this routine to calculate INTO_TARGET (which depends on both OUTOF_INPUT
     and INTO_INPUT), then emit code to set up OUTOF_TARGET.

     This isn't worthwhile for constant shifts since the optimizers will
     cope better with in-range shift counts.  */
  if (shift_mask >= BITS_PER_WORD
      && outof_target != 0
      && !CONSTANT_P (op1))
    {
      if (!expand_doubleword_shift (op1_mode, binoptab,
				    outof_input, into_input, op1,
				    0, into_target,
				    unsignedp, methods, shift_mask))
	return false;
      if (!force_expand_binop (word_mode, binoptab, outof_input, op1,
			       outof_target, unsignedp, methods))
	return false;
      return true;
    }

  /* Set CMP_CODE, CMP1 and CMP2 so that the rtx (CMP_CODE CMP1 CMP2)
     is true when the effective shift value is less than BITS_PER_WORD.
     Set SUPERWORD_OP1 to the shift count that should be used to shift
     OUTOF_INPUT into INTO_TARGET when the condition is false.  */
  tmp = immed_double_const (BITS_PER_WORD, 0, op1_mode);
  if (!CONSTANT_P (op1) && shift_mask == BITS_PER_WORD - 1)
    {
      /* Set CMP1 to OP1 & BITS_PER_WORD.  The result is zero iff OP1
	 is a subword shift count.  */
      cmp1 = simplify_expand_binop (op1_mode, and_optab, op1, tmp,
				    0, true, methods);
      cmp2 = CONST0_RTX (op1_mode);
      cmp_code = EQ;
      superword_op1 = op1;
    }
  else
    {
      /* Set CMP1 to OP1 - BITS_PER_WORD.  */
      cmp1 = simplify_expand_binop (op1_mode, sub_optab, op1, tmp,
				    0, true, methods);
      cmp2 = CONST0_RTX (op1_mode);
      cmp_code = LT;
      superword_op1 = cmp1;
    }
  if (cmp1 == 0)
    return false;

  /* If we can compute the condition at compile time, pick the
     appropriate subroutine.  */
  tmp = simplify_relational_operation (cmp_code, SImode, op1_mode, cmp1, cmp2);
  if (tmp != 0 && GET_CODE (tmp) == CONST_INT)
    {
      if (tmp == const0_rtx)
	return expand_superword_shift (binoptab, outof_input, superword_op1,
				       outof_target, into_target,
				       unsignedp, methods);
      else
	return expand_subword_shift (op1_mode, binoptab,
				     outof_input, into_input, op1,
				     outof_target, into_target,
				     unsignedp, methods, shift_mask);
    }

#ifdef HAVE_conditional_move
  /* Try using conditional moves to generate straight-line code.  */
  {
    rtx start = get_last_insn ();
    if (expand_doubleword_shift_condmove (op1_mode, binoptab,
					  cmp_code, cmp1, cmp2,
					  outof_input, into_input,
					  op1, superword_op1,
					  outof_target, into_target,
					  unsignedp, methods, shift_mask))
      return true;
    delete_insns_since (start);
  }
#endif

  /* As a last resort, use branches to select the correct alternative.  */
  subword_label = gen_label_rtx ();
  done_label = gen_label_rtx ();

  NO_DEFER_POP;
  do_compare_rtx_and_jump (cmp1, cmp2, cmp_code, false, op1_mode,
			   0, 0, subword_label);
  OK_DEFER_POP;

  if (!expand_superword_shift (binoptab, outof_input, superword_op1,
			       outof_target, into_target,
			       unsignedp, methods))
    return false;

  emit_jump_insn (gen_jump (done_label));
  emit_barrier ();
  emit_label (subword_label);

  if (!expand_subword_shift (op1_mode, binoptab,
			     outof_input, into_input, op1,
			     outof_target, into_target,
			     unsignedp, methods, shift_mask))
    return false;

  emit_label (done_label);
  return true;
}

/* Subroutine of expand_binop.  Perform a double word multiplication of
   operands OP0 and OP1 both of mode MODE, which is exactly twice as wide
   as the target's word_mode.  This function return NULL_RTX if anything
   goes wrong, in which case it may have already emitted instructions
   which need to be deleted.

   If we want to multiply two two-word values and have normal and widening
   multiplies of single-word values, we can do this with three smaller
   multiplications.  Note that we do not make a REG_NO_CONFLICT block here
   because we are not operating on one word at a time.

   The multiplication proceeds as follows:
			         _______________________
			        [__op0_high_|__op0_low__]
			         _______________________
        *			[__op1_high_|__op1_low__]
        _______________________________________________
			         _______________________
    (1)				[__op0_low__*__op1_low__]
		     _______________________
    (2a)	    [__op0_low__*__op1_high_]
		     _______________________
    (2b)	    [__op0_high_*__op1_low__]
         _______________________
    (3) [__op0_high_*__op1_high_]


  This gives a 4-word result.  Since we are only interested in the
  lower 2 words, partial result (3) and the upper words of (2a) and
  (2b) don't need to be calculated.  Hence (2a) and (2b) can be
  calculated using non-widening multiplication.

  (1), however, needs to be calculated with an unsigned widening
  multiplication.  If this operation is not directly supported we
  try using a signed widening multiplication and adjust the result.
  This adjustment works as follows:

      If both operands are positive then no adjustment is needed.

      If the operands have different signs, for example op0_low < 0 and
      op1_low >= 0, the instruction treats the most significant bit of
      op0_low as a sign bit instead of a bit with significance
      2**(BITS_PER_WORD-1), i.e. the instruction multiplies op1_low
      with 2**BITS_PER_WORD - op0_low, and two's complements the
      result.  Conclusion: We need to add op1_low * 2**BITS_PER_WORD to
      the result.

      Similarly, if both operands are negative, we need to add
      (op0_low + op1_low) * 2**BITS_PER_WORD.

      We use a trick to adjust quickly.  We logically shift op0_low right
      (op1_low) BITS_PER_WORD-1 steps to get 0 or 1, and add this to
      op0_high (op1_high) before it is used to calculate 2b (2a).  If no
      logical shift exists, we do an arithmetic right shift and subtract
      the 0 or -1.  */

static rtx
expand_doubleword_mult (enum machine_mode mode, rtx op0, rtx op1, rtx target,
		       bool umulp, enum optab_methods methods)
{
  int low = (WORDS_BIG_ENDIAN ? 1 : 0);
  int high = (WORDS_BIG_ENDIAN ? 0 : 1);
  rtx wordm1 = umulp ? NULL_RTX : GEN_INT (BITS_PER_WORD - 1);
  rtx product, adjust, product_high, temp;

  rtx op0_high = operand_subword_force (op0, high, mode);
  rtx op0_low = operand_subword_force (op0, low, mode);
  rtx op1_high = operand_subword_force (op1, high, mode);
  rtx op1_low = operand_subword_force (op1, low, mode);

  /* If we're using an unsigned multiply to directly compute the product
     of the low-order words of the operands and perform any required
     adjustments of the operands, we begin by trying two more multiplications
     and then computing the appropriate sum.

     We have checked above that the required addition is provided.
     Full-word addition will normally always succeed, especially if
     it is provided at all, so we don't worry about its failure.  The
     multiplication may well fail, however, so we do handle that.  */

  if (!umulp)
    {
      /* ??? This could be done with emit_store_flag where available.  */
      temp = expand_binop (word_mode, lshr_optab, op0_low, wordm1,
			   NULL_RTX, 1, methods);
      if (temp)
	op0_high = expand_binop (word_mode, add_optab, op0_high, temp,
				 NULL_RTX, 0, OPTAB_DIRECT);
      else
	{
	  temp = expand_binop (word_mode, ashr_optab, op0_low, wordm1,
			       NULL_RTX, 0, methods);
	  if (!temp)
	    return NULL_RTX;
	  op0_high = expand_binop (word_mode, sub_optab, op0_high, temp,
				   NULL_RTX, 0, OPTAB_DIRECT);
	}

      if (!op0_high)
	return NULL_RTX;
    }

  adjust = expand_binop (word_mode, smul_optab, op0_high, op1_low,
			 NULL_RTX, 0, OPTAB_DIRECT);
  if (!adjust)
    return NULL_RTX;

  /* OP0_HIGH should now be dead.  */

  if (!umulp)
    {
      /* ??? This could be done with emit_store_flag where available.  */
      temp = expand_binop (word_mode, lshr_optab, op1_low, wordm1,
			   NULL_RTX, 1, methods);
      if (temp)
	op1_high = expand_binop (word_mode, add_optab, op1_high, temp,
				 NULL_RTX, 0, OPTAB_DIRECT);
      else
	{
	  temp = expand_binop (word_mode, ashr_optab, op1_low, wordm1,
			       NULL_RTX, 0, methods);
	  if (!temp)
	    return NULL_RTX;
	  op1_high = expand_binop (word_mode, sub_optab, op1_high, temp,
				   NULL_RTX, 0, OPTAB_DIRECT);
	}

      if (!op1_high)
	return NULL_RTX;
    }

  temp = expand_binop (word_mode, smul_optab, op1_high, op0_low,
		       NULL_RTX, 0, OPTAB_DIRECT);
  if (!temp)
    return NULL_RTX;

  /* OP1_HIGH should now be dead.  */

  adjust = expand_binop (word_mode, add_optab, adjust, temp,
			 adjust, 0, OPTAB_DIRECT);

  if (target && !REG_P (target))
    target = NULL_RTX;

  if (umulp)
    product = expand_binop (mode, umul_widen_optab, op0_low, op1_low,
			    target, 1, OPTAB_DIRECT);
  else
    product = expand_binop (mode, smul_widen_optab, op0_low, op1_low,
			    target, 1, OPTAB_DIRECT);

  if (!product)
    return NULL_RTX;

  product_high = operand_subword (product, high, 1, mode);
  adjust = expand_binop (word_mode, add_optab, product_high, adjust,
			 REG_P (product_high) ? product_high : adjust,
			 0, OPTAB_DIRECT);
  emit_move_insn (product_high, adjust);
  return product;
}

/* Wrapper around expand_binop which takes an rtx code to specify
   the operation to perform, not an optab pointer.  All other
   arguments are the same.  */
rtx
expand_simple_binop (enum machine_mode mode, enum rtx_code code, rtx op0,
		     rtx op1, rtx target, int unsignedp,
		     enum optab_methods methods)
{
  optab binop = code_to_optab[(int) code];
  gcc_assert (binop);

  return expand_binop (mode, binop, op0, op1, target, unsignedp, methods);
}

/* Return whether OP0 and OP1 should be swapped when expanding a commutative
   binop.  Order them according to commutative_operand_precedence and, if
   possible, try to put TARGET or a pseudo first.  */
static bool
swap_commutative_operands_with_target (rtx target, rtx op0, rtx op1)
{
  int op0_prec = commutative_operand_precedence (op0);
  int op1_prec = commutative_operand_precedence (op1);

  if (op0_prec < op1_prec)
    return true;

  if (op0_prec > op1_prec)
    return false;

  /* With equal precedence, both orders are ok, but it is better if the
     first operand is TARGET, or if both TARGET and OP0 are pseudos.  */
  if (target == 0 || REG_P (target))
    return (REG_P (op1) && !REG_P (op0)) || target == op1;
  else
    return rtx_equal_p (op1, target);
}

/* Return true if BINOPTAB implements a shift operation.  */

static bool
shift_optab_p (optab binoptab)
{
  switch (binoptab->code)
    {
    case ASHIFT:
    case SS_ASHIFT:
    case US_ASHIFT:
    case ASHIFTRT:
    case LSHIFTRT:
    case ROTATE:
    case ROTATERT:
      return true;

    default:
      return false;
    }
}

/* Return true if BINOPTAB implements a commutative binary operation.  */

static bool
commutative_optab_p (optab binoptab)
{
  return (GET_RTX_CLASS (binoptab->code) == RTX_COMM_ARITH
	  || binoptab == smul_widen_optab
	  || binoptab == umul_widen_optab
	  || binoptab == smul_highpart_optab
	  || binoptab == umul_highpart_optab);
}

/* X is to be used in mode MODE as an operand to BINOPTAB.  If we're
   optimizing, and if the operand is a constant that costs more than
   1 instruction, force the constant into a register and return that
   register.  Return X otherwise.  UNSIGNEDP says whether X is unsigned.  */

static rtx
avoid_expensive_constant (enum machine_mode mode, optab binoptab,
			  rtx x, bool unsignedp)
{
  if (mode != VOIDmode
      && optimize
      && CONSTANT_P (x)
      && rtx_cost (x, binoptab->code) > COSTS_N_INSNS (1))
    {
      if (GET_CODE (x) == CONST_INT)
	{
	  HOST_WIDE_INT intval = trunc_int_for_mode (INTVAL (x), mode);
	  if (intval != INTVAL (x))
	    x = GEN_INT (intval);
	}
      else
	x = convert_modes (mode, VOIDmode, x, unsignedp);
      x = force_reg (mode, x);
    }
  return x;
}

/* Helper function for expand_binop: handle the case where there
   is an insn that directly implements the indicated operation.
   Returns null if this is not possible.  */
static rtx
expand_binop_directly (enum machine_mode mode, optab binoptab,
		       rtx op0, rtx op1,
		       rtx target, int unsignedp, enum optab_methods methods,
		       rtx last)
{
  int icode = (int) optab_handler (binoptab, mode)->insn_code;
  enum machine_mode mode0 = insn_data[icode].operand[1].mode;
  enum machine_mode mode1 = insn_data[icode].operand[2].mode;
  enum machine_mode tmp_mode;
  bool commutative_p;
  rtx pat;
  rtx xop0 = op0, xop1 = op1;
  rtx temp;
  rtx swap;
  
  if (target)
    temp = target;
  else
    temp = gen_reg_rtx (mode);

  /* If it is a commutative operator and the modes would match
     if we would swap the operands, we can save the conversions.  */
  commutative_p = commutative_optab_p (binoptab);
  if (commutative_p
      && GET_MODE (xop0) != mode0 && GET_MODE (xop1) != mode1
      && GET_MODE (xop0) == mode1 && GET_MODE (xop1) == mode1)
    {
      swap = xop0;
      xop0 = xop1;
      xop1 = swap;
    }
  
  /* If we are optimizing, force expensive constants into a register.  */
  xop0 = avoid_expensive_constant (mode0, binoptab, xop0, unsignedp);
  if (!shift_optab_p (binoptab))
    xop1 = avoid_expensive_constant (mode1, binoptab, xop1, unsignedp);

  /* In case the insn wants input operands in modes different from
     those of the actual operands, convert the operands.  It would
     seem that we don't need to convert CONST_INTs, but we do, so
     that they're properly zero-extended, sign-extended or truncated
     for their mode.  */
  
  if (GET_MODE (xop0) != mode0 && mode0 != VOIDmode)
    xop0 = convert_modes (mode0,
			  GET_MODE (xop0) != VOIDmode
			  ? GET_MODE (xop0)
			  : mode,
			  xop0, unsignedp);
  
  if (GET_MODE (xop1) != mode1 && mode1 != VOIDmode)
    xop1 = convert_modes (mode1,
			  GET_MODE (xop1) != VOIDmode
			  ? GET_MODE (xop1)
			  : mode,
			  xop1, unsignedp);
  
  /* If operation is commutative,
     try to make the first operand a register.
     Even better, try to make it the same as the target.
     Also try to make the last operand a constant.  */
  if (commutative_p
      && swap_commutative_operands_with_target (target, xop0, xop1))
    {
      swap = xop1;
      xop1 = xop0;
      xop0 = swap;
    }

  /* Now, if insn's predicates don't allow our operands, put them into
     pseudo regs.  */
  
  if (!insn_data[icode].operand[1].predicate (xop0, mode0)
      && mode0 != VOIDmode)
    xop0 = copy_to_mode_reg (mode0, xop0);
  
  if (!insn_data[icode].operand[2].predicate (xop1, mode1)
      && mode1 != VOIDmode)
    xop1 = copy_to_mode_reg (mode1, xop1);
  
  if (binoptab == vec_pack_trunc_optab 
      || binoptab == vec_pack_usat_optab
      || binoptab == vec_pack_ssat_optab
      || binoptab == vec_pack_ufix_trunc_optab
      || binoptab == vec_pack_sfix_trunc_optab)
    {
      /* The mode of the result is different then the mode of the
	 arguments.  */
      tmp_mode = insn_data[icode].operand[0].mode;
      if (GET_MODE_NUNITS (tmp_mode) != 2 * GET_MODE_NUNITS (mode))
	return 0;
    }
  else
    tmp_mode = mode;

  if (!insn_data[icode].operand[0].predicate (temp, tmp_mode))
    temp = gen_reg_rtx (tmp_mode);
  
  pat = GEN_FCN (icode) (temp, xop0, xop1);
  if (pat)
    {
      /* If PAT is composed of more than one insn, try to add an appropriate
	 REG_EQUAL note to it.  If we can't because TEMP conflicts with an
	 operand, call expand_binop again, this time without a target.  */
      if (INSN_P (pat) && NEXT_INSN (pat) != NULL_RTX
	  && ! add_equal_note (pat, temp, binoptab->code, xop0, xop1))
	{
	  delete_insns_since (last);
	  return expand_binop (mode, binoptab, op0, op1, NULL_RTX,
			       unsignedp, methods);
	}
      
      emit_insn (pat);
      return temp;
    }

  delete_insns_since (last);
  return NULL_RTX;
}

/* Generate code to perform an operation specified by BINOPTAB
   on operands OP0 and OP1, with result having machine-mode MODE.

   UNSIGNEDP is for the case where we have to widen the operands
   to perform the operation.  It says to use zero-extension.

   If TARGET is nonzero, the value
   is generated there, if it is convenient to do so.
   In all cases an rtx is returned for the locus of the value;
   this may or may not be TARGET.  */

rtx
expand_binop (enum machine_mode mode, optab binoptab, rtx op0, rtx op1,
	      rtx target, int unsignedp, enum optab_methods methods)
{
  enum optab_methods next_methods
    = (methods == OPTAB_LIB || methods == OPTAB_LIB_WIDEN
       ? OPTAB_WIDEN : methods);
  enum mode_class class;
  enum machine_mode wider_mode;
  rtx libfunc;
  rtx temp;
  rtx entry_last = get_last_insn ();
  rtx last;

  class = GET_MODE_CLASS (mode);

  /* If subtracting an integer constant, convert this into an addition of
     the negated constant.  */

  if (binoptab == sub_optab && GET_CODE (op1) == CONST_INT)
    {
      op1 = negate_rtx (mode, op1);
      binoptab = add_optab;
    }

  /* Record where to delete back to if we backtrack.  */
  last = get_last_insn ();

  /* If we can do it with a three-operand insn, do so.  */

  if (methods != OPTAB_MUST_WIDEN
      && optab_handler (binoptab, mode)->insn_code != CODE_FOR_nothing)
    {
      temp = expand_binop_directly (mode, binoptab, op0, op1, target,
				    unsignedp, methods, last);
      if (temp)
	return temp;
    }

  /* If we were trying to rotate, and that didn't work, try rotating
     the other direction before falling back to shifts and bitwise-or.  */
  if (((binoptab == rotl_optab
	&& optab_handler (rotr_optab, mode)->insn_code != CODE_FOR_nothing)
       || (binoptab == rotr_optab
	   && optab_handler (rotl_optab, mode)->insn_code != CODE_FOR_nothing))
      && class == MODE_INT)
    {
      optab otheroptab = (binoptab == rotl_optab ? rotr_optab : rotl_optab);
      rtx newop1;
      unsigned int bits = GET_MODE_BITSIZE (mode);

      if (GET_CODE (op1) == CONST_INT)
	newop1 = GEN_INT (bits - INTVAL (op1));
      else if (targetm.shift_truncation_mask (mode) == bits - 1)
	newop1 = negate_rtx (mode, op1);
      else
	newop1 = expand_binop (mode, sub_optab,
			       GEN_INT (bits), op1,
			       NULL_RTX, unsignedp, OPTAB_DIRECT);
				   
      temp = expand_binop_directly (mode, otheroptab, op0, newop1,
				    target, unsignedp, methods, last);
      if (temp)
	return temp;
    }

  /* If this is a multiply, see if we can do a widening operation that
     takes operands of this mode and makes a wider mode.  */

  if (binoptab == smul_optab
      && GET_MODE_WIDER_MODE (mode) != VOIDmode
      && ((optab_handler ((unsignedp ? umul_widen_optab : smul_widen_optab),
			  GET_MODE_WIDER_MODE (mode))->insn_code)
	  != CODE_FOR_nothing))
    {
      temp = expand_binop (GET_MODE_WIDER_MODE (mode),
			   unsignedp ? umul_widen_optab : smul_widen_optab,
			   op0, op1, NULL_RTX, unsignedp, OPTAB_DIRECT);

      if (temp != 0)
	{
	  if (GET_MODE_CLASS (mode) == MODE_INT
	      && TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (mode),
                                        GET_MODE_BITSIZE (GET_MODE (temp))))
	    return gen_lowpart (mode, temp);
	  else
	    return convert_to_mode (mode, temp, unsignedp);
	}
    }

  /* Look for a wider mode of the same class for which we think we
     can open-code the operation.  Check for a widening multiply at the
     wider mode as well.  */

  if (CLASS_HAS_WIDER_MODES_P (class)
      && methods != OPTAB_DIRECT && methods != OPTAB_LIB)
    for (wider_mode = GET_MODE_WIDER_MODE (mode);
	 wider_mode != VOIDmode;
	 wider_mode = GET_MODE_WIDER_MODE (wider_mode))
      {
	if (optab_handler (binoptab, wider_mode)->insn_code != CODE_FOR_nothing
	    || (binoptab == smul_optab
		&& GET_MODE_WIDER_MODE (wider_mode) != VOIDmode
		&& ((optab_handler ((unsignedp ? umul_widen_optab
				     : smul_widen_optab),
				     GET_MODE_WIDER_MODE (wider_mode))->insn_code)
		    != CODE_FOR_nothing)))
	  {
	    rtx xop0 = op0, xop1 = op1;
	    int no_extend = 0;

	    /* For certain integer operations, we need not actually extend
	       the narrow operands, as long as we will truncate
	       the results to the same narrowness.  */

	    if ((binoptab == ior_optab || binoptab == and_optab
		 || binoptab == xor_optab
		 || binoptab == add_optab || binoptab == sub_optab
		 || binoptab == smul_optab || binoptab == ashl_optab)
		&& class == MODE_INT)
	      {
		no_extend = 1;
		xop0 = avoid_expensive_constant (mode, binoptab,
						 xop0, unsignedp);
		if (binoptab != ashl_optab)
		  xop1 = avoid_expensive_constant (mode, binoptab,
						   xop1, unsignedp);
	      }

	    xop0 = widen_operand (xop0, wider_mode, mode, unsignedp, no_extend);

	    /* The second operand of a shift must always be extended.  */
	    xop1 = widen_operand (xop1, wider_mode, mode, unsignedp,
				  no_extend && binoptab != ashl_optab);

	    temp = expand_binop (wider_mode, binoptab, xop0, xop1, NULL_RTX,
				 unsignedp, OPTAB_DIRECT);
	    if (temp)
	      {
		if (class != MODE_INT
                    || !TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (mode),
                                               GET_MODE_BITSIZE (wider_mode)))
		  {
		    if (target == 0)
		      target = gen_reg_rtx (mode);
		    convert_move (target, temp, 0);
		    return target;
		  }
		else
		  return gen_lowpart (mode, temp);
	      }
	    else
	      delete_insns_since (last);
	  }
      }

  /* If operation is commutative,
     try to make the first operand a register.
     Even better, try to make it the same as the target.
     Also try to make the last operand a constant.  */
  if (commutative_optab_p (binoptab)
      && swap_commutative_operands_with_target (target, op0, op1))
    {
      temp = op1;
      op1 = op0;
      op0 = temp;
    }

  /* These can be done a word at a time.  */
  if ((binoptab == and_optab || binoptab == ior_optab || binoptab == xor_optab)
      && class == MODE_INT
      && GET_MODE_SIZE (mode) > UNITS_PER_WORD
      && optab_handler (binoptab, word_mode)->insn_code != CODE_FOR_nothing)
    {
      int i;
      rtx insns;
      rtx equiv_value;

      /* If TARGET is the same as one of the operands, the REG_EQUAL note
	 won't be accurate, so use a new target.  */
      if (target == 0 || target == op0 || target == op1)
	target = gen_reg_rtx (mode);

      start_sequence ();

      /* Do the actual arithmetic.  */
      for (i = 0; i < GET_MODE_BITSIZE (mode) / BITS_PER_WORD; i++)
	{
	  rtx target_piece = operand_subword (target, i, 1, mode);
	  rtx x = expand_binop (word_mode, binoptab,
				operand_subword_force (op0, i, mode),
				operand_subword_force (op1, i, mode),
				target_piece, unsignedp, next_methods);

	  if (x == 0)
	    break;

	  if (target_piece != x)
	    emit_move_insn (target_piece, x);
	}

      insns = get_insns ();
      end_sequence ();

      if (i == GET_MODE_BITSIZE (mode) / BITS_PER_WORD)
	{
	  if (binoptab->code != UNKNOWN)
	    equiv_value
	      = gen_rtx_fmt_ee (binoptab->code, mode,
				copy_rtx (op0), copy_rtx (op1));
	  else
	    equiv_value = 0;

	  emit_no_conflict_block (insns, target, op0, op1, equiv_value);
	  return target;
	}
    }

  /* Synthesize double word shifts from single word shifts.  */
  if ((binoptab == lshr_optab || binoptab == ashl_optab
       || binoptab == ashr_optab)
      && class == MODE_INT
      && (GET_CODE (op1) == CONST_INT || !optimize_size)
      && GET_MODE_SIZE (mode) == 2 * UNITS_PER_WORD
      && optab_handler (binoptab, word_mode)->insn_code != CODE_FOR_nothing
      && optab_handler (ashl_optab, word_mode)->insn_code != CODE_FOR_nothing
      && optab_handler (lshr_optab, word_mode)->insn_code != CODE_FOR_nothing)
    {
      unsigned HOST_WIDE_INT shift_mask, double_shift_mask;
      enum machine_mode op1_mode;

      double_shift_mask = targetm.shift_truncation_mask (mode);
      shift_mask = targetm.shift_truncation_mask (word_mode);
      op1_mode = GET_MODE (op1) != VOIDmode ? GET_MODE (op1) : word_mode;

      /* Apply the truncation to constant shifts.  */
      if (double_shift_mask > 0 && GET_CODE (op1) == CONST_INT)
	op1 = GEN_INT (INTVAL (op1) & double_shift_mask);

      if (op1 == CONST0_RTX (op1_mode))
	return op0;

      /* Make sure that this is a combination that expand_doubleword_shift
	 can handle.  See the comments there for details.  */
      if (double_shift_mask == 0
	  || (shift_mask == BITS_PER_WORD - 1
	      && double_shift_mask == BITS_PER_WORD * 2 - 1))
	{
	  rtx insns, equiv_value;
	  rtx into_target, outof_target;
	  rtx into_input, outof_input;
	  int left_shift, outof_word;

	  /* If TARGET is the same as one of the operands, the REG_EQUAL note
	     won't be accurate, so use a new target.  */
	  if (target == 0 || target == op0 || target == op1)
	    target = gen_reg_rtx (mode);

	  start_sequence ();

	  /* OUTOF_* is the word we are shifting bits away from, and
	     INTO_* is the word that we are shifting bits towards, thus
	     they differ depending on the direction of the shift and
	     WORDS_BIG_ENDIAN.  */

	  left_shift = binoptab == ashl_optab;
	  outof_word = left_shift ^ ! WORDS_BIG_ENDIAN;

	  outof_target = operand_subword (target, outof_word, 1, mode);
	  into_target = operand_subword (target, 1 - outof_word, 1, mode);

	  outof_input = operand_subword_force (op0, outof_word, mode);
	  into_input = operand_subword_force (op0, 1 - outof_word, mode);

	  if (expand_doubleword_shift (op1_mode, binoptab,
				       outof_input, into_input, op1,
				       outof_target, into_target,
				       unsignedp, next_methods, shift_mask))
	    {
	      insns = get_insns ();
	      end_sequence ();

	      equiv_value = gen_rtx_fmt_ee (binoptab->code, mode, op0, op1);
	      emit_no_conflict_block (insns, target, op0, op1, equiv_value);
	      return target;
	    }
	  end_sequence ();
	}
    }

  /* Synthesize double word rotates from single word shifts.  */
  if ((binoptab == rotl_optab || binoptab == rotr_optab)
      && class == MODE_INT
      && GET_CODE (op1) == CONST_INT
      && GET_MODE_SIZE (mode) == 2 * UNITS_PER_WORD
      && optab_handler (ashl_optab, word_mode)->insn_code != CODE_FOR_nothing
      && optab_handler (lshr_optab, word_mode)->insn_code != CODE_FOR_nothing)
    {
      rtx insns;
      rtx into_target, outof_target;
      rtx into_input, outof_input;
      rtx inter;
      int shift_count, left_shift, outof_word;

      /* If TARGET is the same as one of the operands, the REG_EQUAL note
	 won't be accurate, so use a new target. Do this also if target is not
	 a REG, first because having a register instead may open optimization
	 opportunities, and second because if target and op0 happen to be MEMs
	 designating the same location, we would risk clobbering it too early
	 in the code sequence we generate below.  */
      if (target == 0 || target == op0 || target == op1 || ! REG_P (target))
	target = gen_reg_rtx (mode);

      start_sequence ();

      shift_count = INTVAL (op1);

      /* OUTOF_* is the word we are shifting bits away from, and
	 INTO_* is the word that we are shifting bits towards, thus
	 they differ depending on the direction of the shift and
	 WORDS_BIG_ENDIAN.  */

      left_shift = (binoptab == rotl_optab);
      outof_word = left_shift ^ ! WORDS_BIG_ENDIAN;

      outof_target = operand_subword (target, outof_word, 1, mode);
      into_target = operand_subword (target, 1 - outof_word, 1, mode);

      outof_input = operand_subword_force (op0, outof_word, mode);
      into_input = operand_subword_force (op0, 1 - outof_word, mode);

      if (shift_count == BITS_PER_WORD)
	{
	  /* This is just a word swap.  */
	  emit_move_insn (outof_target, into_input);
	  emit_move_insn (into_target, outof_input);
	  inter = const0_rtx;
	}
      else
	{
	  rtx into_temp1, into_temp2, outof_temp1, outof_temp2;
	  rtx first_shift_count, second_shift_count;
	  optab reverse_unsigned_shift, unsigned_shift;

	  reverse_unsigned_shift = (left_shift ^ (shift_count < BITS_PER_WORD)
				    ? lshr_optab : ashl_optab);

	  unsigned_shift = (left_shift ^ (shift_count < BITS_PER_WORD)
			    ? ashl_optab : lshr_optab);

	  if (shift_count > BITS_PER_WORD)
	    {
	      first_shift_count = GEN_INT (shift_count - BITS_PER_WORD);
	      second_shift_count = GEN_INT (2 * BITS_PER_WORD - shift_count);
	    }
	  else
	    {
	      first_shift_count = GEN_INT (BITS_PER_WORD - shift_count);
	      second_shift_count = GEN_INT (shift_count);
	    }

	  into_temp1 = expand_binop (word_mode, unsigned_shift,
				     outof_input, first_shift_count,
				     NULL_RTX, unsignedp, next_methods);
	  into_temp2 = expand_binop (word_mode, reverse_unsigned_shift,
				     into_input, second_shift_count,
				     NULL_RTX, unsignedp, next_methods);

	  if (into_temp1 != 0 && into_temp2 != 0)
	    inter = expand_binop (word_mode, ior_optab, into_temp1, into_temp2,
				  into_target, unsignedp, next_methods);
	  else
	    inter = 0;

	  if (inter != 0 && inter != into_target)
	    emit_move_insn (into_target, inter);

	  outof_temp1 = expand_binop (word_mode, unsigned_shift,
				      into_input, first_shift_count,
				      NULL_RTX, unsignedp, next_methods);
	  outof_temp2 = expand_binop (word_mode, reverse_unsigned_shift,
				      outof_input, second_shift_count,
				      NULL_RTX, unsignedp, next_methods);

	  if (inter != 0 && outof_temp1 != 0 && outof_temp2 != 0)
	    inter = expand_binop (word_mode, ior_optab,
				  outof_temp1, outof_temp2,
				  outof_target, unsignedp, next_methods);

	  if (inter != 0 && inter != outof_target)
	    emit_move_insn (outof_target, inter);
	}

      insns = get_insns ();
      end_sequence ();

      if (inter != 0)
	{
	  /* One may be tempted to wrap the insns in a REG_NO_CONFLICT
	     block to help the register allocator a bit.  But a multi-word
	     rotate will need all the input bits when setting the output
	     bits, so there clearly is a conflict between the input and
	     output registers.  So we can't use a no-conflict block here.  */
	  emit_insn (insns);
	  return target;
	}
    }

  /* These can be done a word at a time by propagating carries.  */
  if ((binoptab == add_optab || binoptab == sub_optab)
      && class == MODE_INT
      && GET_MODE_SIZE (mode) >= 2 * UNITS_PER_WORD
      && optab_handler (binoptab, word_mode)->insn_code != CODE_FOR_nothing)
    {
      unsigned int i;
      optab otheroptab = binoptab == add_optab ? sub_optab : add_optab;
      const unsigned int nwords = GET_MODE_BITSIZE (mode) / BITS_PER_WORD;
      rtx carry_in = NULL_RTX, carry_out = NULL_RTX;
      rtx xop0, xop1, xtarget;

      /* We can handle either a 1 or -1 value for the carry.  If STORE_FLAG
	 value is one of those, use it.  Otherwise, use 1 since it is the
	 one easiest to get.  */
#if STORE_FLAG_VALUE == 1 || STORE_FLAG_VALUE == -1
      int normalizep = STORE_FLAG_VALUE;
#else
      int normalizep = 1;
#endif

      /* Prepare the operands.  */
      xop0 = force_reg (mode, op0);
      xop1 = force_reg (mode, op1);

      xtarget = gen_reg_rtx (mode);

      if (target == 0 || !REG_P (target))
	target = xtarget;

      /* Indicate for flow that the entire target reg is being set.  */
      if (REG_P (target))
	emit_insn (gen_rtx_CLOBBER (VOIDmode, xtarget));

      /* Do the actual arithmetic.  */
      for (i = 0; i < nwords; i++)
	{
	  int index = (WORDS_BIG_ENDIAN ? nwords - i - 1 : i);
	  rtx target_piece = operand_subword (xtarget, index, 1, mode);
	  rtx op0_piece = operand_subword_force (xop0, index, mode);
	  rtx op1_piece = operand_subword_force (xop1, index, mode);
	  rtx x;

	  /* Main add/subtract of the input operands.  */
	  x = expand_binop (word_mode, binoptab,
			    op0_piece, op1_piece,
			    target_piece, unsignedp, next_methods);
	  if (x == 0)
	    break;

	  if (i + 1 < nwords)
	    {
	      /* Store carry from main add/subtract.  */
	      carry_out = gen_reg_rtx (word_mode);
	      carry_out = emit_store_flag_force (carry_out,
						 (binoptab == add_optab
						  ? LT : GT),
						 x, op0_piece,
						 word_mode, 1, normalizep);
	    }

	  if (i > 0)
	    {
	      rtx newx;

	      /* Add/subtract previous carry to main result.  */
	      newx = expand_binop (word_mode,
				   normalizep == 1 ? binoptab : otheroptab,
				   x, carry_in,
				   NULL_RTX, 1, next_methods);

	      if (i + 1 < nwords)
		{
		  /* Get out carry from adding/subtracting carry in.  */
		  rtx carry_tmp = gen_reg_rtx (word_mode);
		  carry_tmp = emit_store_flag_force (carry_tmp,
						     (binoptab == add_optab
						      ? LT : GT),
						     newx, x,
						     word_mode, 1, normalizep);

		  /* Logical-ior the two poss. carry together.  */
		  carry_out = expand_binop (word_mode, ior_optab,
					    carry_out, carry_tmp,
					    carry_out, 0, next_methods);
		  if (carry_out == 0)
		    break;
		}
	      emit_move_insn (target_piece, newx);
	    }
	  else
	    {
	      if (x != target_piece)
		emit_move_insn (target_piece, x);
	    }

	  carry_in = carry_out;
	}

      if (i == GET_MODE_BITSIZE (mode) / (unsigned) BITS_PER_WORD)
	{
	  if (optab_handler (mov_optab, mode)->insn_code != CODE_FOR_nothing
	      || ! rtx_equal_p (target, xtarget))
	    {
	      rtx temp = emit_move_insn (target, xtarget);

	      set_unique_reg_note (temp,
				   REG_EQUAL,
				   gen_rtx_fmt_ee (binoptab->code, mode,
						   copy_rtx (xop0),
						   copy_rtx (xop1)));
	    }
	  else
	    target = xtarget;

	  return target;
	}

      else
	delete_insns_since (last);
    }

  /* Attempt to synthesize double word multiplies using a sequence of word
     mode multiplications.  We first attempt to generate a sequence using a
     more efficient unsigned widening multiply, and if that fails we then
     try using a signed widening multiply.  */

  if (binoptab == smul_optab
      && class == MODE_INT
      && GET_MODE_SIZE (mode) == 2 * UNITS_PER_WORD
      && optab_handler (smul_optab, word_mode)->insn_code != CODE_FOR_nothing
      && optab_handler (add_optab, word_mode)->insn_code != CODE_FOR_nothing)
    {
      rtx product = NULL_RTX;

      if (optab_handler (umul_widen_optab, mode)->insn_code
	  != CODE_FOR_nothing)
	{
	  product = expand_doubleword_mult (mode, op0, op1, target,
					    true, methods);
	  if (!product)
	    delete_insns_since (last);
	}

      if (product == NULL_RTX
	  && optab_handler (smul_widen_optab, mode)->insn_code
	     != CODE_FOR_nothing)
	{
	  product = expand_doubleword_mult (mode, op0, op1, target,
					    false, methods);
	  if (!product)
	    delete_insns_since (last);
	}

      if (product != NULL_RTX)
	{
	  if (optab_handler (mov_optab, mode)->insn_code != CODE_FOR_nothing)
	    {
	      temp = emit_move_insn (target ? target : product, product);
	      set_unique_reg_note (temp,
				   REG_EQUAL,
				   gen_rtx_fmt_ee (MULT, mode,
						   copy_rtx (op0),
						   copy_rtx (op1)));
	    }
	  return product;
	}
    }

  /* It can't be open-coded in this mode.
     Use a library call if one is available and caller says that's ok.  */

  libfunc = optab_libfunc (binoptab, mode);
  if (libfunc
      && (methods == OPTAB_LIB || methods == OPTAB_LIB_WIDEN))
    {
      rtx insns;
      rtx op1x = op1;
      enum machine_mode op1_mode = mode;
      rtx value;

      start_sequence ();

      if (shift_optab_p (binoptab))
	{
	  op1_mode = targetm.libgcc_shift_count_mode ();
	  /* Specify unsigned here,
	     since negative shift counts are meaningless.  */
	  op1x = convert_to_mode (op1_mode, op1, 1);
	}

      if (GET_MODE (op0) != VOIDmode
	  && GET_MODE (op0) != mode)
	op0 = convert_to_mode (mode, op0, unsignedp);

      /* Pass 1 for NO_QUEUE so we don't lose any increments
	 if the libcall is cse'd or moved.  */
      value = emit_library_call_value (libfunc,
				       NULL_RTX, LCT_CONST, mode, 2,
				       op0, mode, op1x, op1_mode);

      insns = get_insns ();
      end_sequence ();

      target = gen_reg_rtx (mode);
      emit_libcall_block (insns, target, value,
			  gen_rtx_fmt_ee (binoptab->code, mode, op0, op1));

      return target;
    }

  delete_insns_since (last);

  /* It can't be done in this mode.  Can we do it in a wider mode?  */

  if (! (methods == OPTAB_WIDEN || methods == OPTAB_LIB_WIDEN
	 || methods == OPTAB_MUST_WIDEN))
    {
      /* Caller says, don't even try.  */
      delete_insns_since (entry_last);
      return 0;
    }

  /* Compute the value of METHODS to pass to recursive calls.
     Don't allow widening to be tried recursively.  */

  methods = (methods == OPTAB_LIB_WIDEN ? OPTAB_LIB : OPTAB_DIRECT);

  /* Look for a wider mode of the same class for which it appears we can do
     the operation.  */

  if (CLASS_HAS_WIDER_MODES_P (class))
    {
      for (wider_mode = GET_MODE_WIDER_MODE (mode);
	   wider_mode != VOIDmode;
	   wider_mode = GET_MODE_WIDER_MODE (wider_mode))
	{
	  if ((optab_handler (binoptab, wider_mode)->insn_code
	       != CODE_FOR_nothing)
	      || (methods == OPTAB_LIB
		  && optab_libfunc (binoptab, wider_mode)))
	    {
	      rtx xop0 = op0, xop1 = op1;
	      int no_extend = 0;

	      /* For certain integer operations, we need not actually extend
		 the narrow operands, as long as we will truncate
		 the results to the same narrowness.  */

	      if ((binoptab == ior_optab || binoptab == and_optab
		   || binoptab == xor_optab
		   || binoptab == add_optab || binoptab == sub_optab
		   || binoptab == smul_optab || binoptab == ashl_optab)
		  && class == MODE_INT)
		no_extend = 1;

	      xop0 = widen_operand (xop0, wider_mode, mode,
				    unsignedp, no_extend);

	      /* The second operand of a shift must always be extended.  */
	      xop1 = widen_operand (xop1, wider_mode, mode, unsignedp,
				    no_extend && binoptab != ashl_optab);

	      temp = expand_binop (wider_mode, binoptab, xop0, xop1, NULL_RTX,
				   unsignedp, methods);
	      if (temp)
		{
		  if (class != MODE_INT
		      || !TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (mode),
						 GET_MODE_BITSIZE (wider_mode)))
		    {
		      if (target == 0)
			target = gen_reg_rtx (mode);
		      convert_move (target, temp, 0);
		      return target;
		    }
		  else
		    return gen_lowpart (mode, temp);
		}
	      else
		delete_insns_since (last);
	    }
	}
    }

  delete_insns_since (entry_last);
  return 0;
}

/* Expand a binary operator which has both signed and unsigned forms.
   UOPTAB is the optab for unsigned operations, and SOPTAB is for
   signed operations.

   If we widen unsigned operands, we may use a signed wider operation instead
   of an unsigned wider operation, since the result would be the same.  */

rtx
sign_expand_binop (enum machine_mode mode, optab uoptab, optab soptab,
		   rtx op0, rtx op1, rtx target, int unsignedp,
		   enum optab_methods methods)
{
  rtx temp;
  optab direct_optab = unsignedp ? uoptab : soptab;
  struct optab wide_soptab;

  /* Do it without widening, if possible.  */
  temp = expand_binop (mode, direct_optab, op0, op1, target,
		       unsignedp, OPTAB_DIRECT);
  if (temp || methods == OPTAB_DIRECT)
    return temp;

  /* Try widening to a signed int.  Make a fake signed optab that
     hides any signed insn for direct use.  */
  wide_soptab = *soptab;
  optab_handler (&wide_soptab, mode)->insn_code = CODE_FOR_nothing;
  /* We don't want to generate new hash table entries from this fake
     optab.  */
  wide_soptab.libcall_gen = NULL;

  temp = expand_binop (mode, &wide_soptab, op0, op1, target,
		       unsignedp, OPTAB_WIDEN);

  /* For unsigned operands, try widening to an unsigned int.  */
  if (temp == 0 && unsignedp)
    temp = expand_binop (mode, uoptab, op0, op1, target,
			 unsignedp, OPTAB_WIDEN);
  if (temp || methods == OPTAB_WIDEN)
    return temp;

  /* Use the right width lib call if that exists.  */
  temp = expand_binop (mode, direct_optab, op0, op1, target, unsignedp, OPTAB_LIB);
  if (temp || methods == OPTAB_LIB)
    return temp;

  /* Must widen and use a lib call, use either signed or unsigned.  */
  temp = expand_binop (mode, &wide_soptab, op0, op1, target,
		       unsignedp, methods);
  if (temp != 0)
    return temp;
  if (unsignedp)
    return expand_binop (mode, uoptab, op0, op1, target,
			 unsignedp, methods);
  return 0;
}

/* Generate code to perform an operation specified by UNOPPTAB
   on operand OP0, with two results to TARG0 and TARG1.
   We assume that the order of the operands for the instruction
   is TARG0, TARG1, OP0.

   Either TARG0 or TARG1 may be zero, but what that means is that
   the result is not actually wanted.  We will generate it into
   a dummy pseudo-reg and discard it.  They may not both be zero.

   Returns 1 if this operation can be performed; 0 if not.  */

int
expand_twoval_unop (optab unoptab, rtx op0, rtx targ0, rtx targ1,
		    int unsignedp)
{
  enum machine_mode mode = GET_MODE (targ0 ? targ0 : targ1);
  enum mode_class class;
  enum machine_mode wider_mode;
  rtx entry_last = get_last_insn ();
  rtx last;

  class = GET_MODE_CLASS (mode);

  if (!targ0)
    targ0 = gen_reg_rtx (mode);
  if (!targ1)
    targ1 = gen_reg_rtx (mode);

  /* Record where to go back to if we fail.  */
  last = get_last_insn ();

  if (optab_handler (unoptab, mode)->insn_code != CODE_FOR_nothing)
    {
      int icode = (int) optab_handler (unoptab, mode)->insn_code;
      enum machine_mode mode0 = insn_data[icode].operand[2].mode;
      rtx pat;
      rtx xop0 = op0;

      if (GET_MODE (xop0) != VOIDmode
	  && GET_MODE (xop0) != mode0)
	xop0 = convert_to_mode (mode0, xop0, unsignedp);

      /* Now, if insn doesn't accept these operands, put them into pseudos.  */
      if (!insn_data[icode].operand[2].predicate (xop0, mode0))
	xop0 = copy_to_mode_reg (mode0, xop0);

      /* We could handle this, but we should always be called with a pseudo
	 for our targets and all insns should take them as outputs.  */
      gcc_assert (insn_data[icode].operand[0].predicate (targ0, mode));
      gcc_assert (insn_data[icode].operand[1].predicate (targ1, mode));

      pat = GEN_FCN (icode) (targ0, targ1, xop0);
      if (pat)
	{
	  emit_insn (pat);
	  return 1;
	}
      else
	delete_insns_since (last);
    }

  /* It can't be done in this mode.  Can we do it in a wider mode?  */

  if (CLASS_HAS_WIDER_MODES_P (class))
    {
      for (wider_mode = GET_MODE_WIDER_MODE (mode);
	   wider_mode != VOIDmode;
	   wider_mode = GET_MODE_WIDER_MODE (wider_mode))
	{
	  if (optab_handler (unoptab, wider_mode)->insn_code
	      != CODE_FOR_nothing)
	    {
	      rtx t0 = gen_reg_rtx (wider_mode);
	      rtx t1 = gen_reg_rtx (wider_mode);
	      rtx cop0 = convert_modes (wider_mode, mode, op0, unsignedp);

	      if (expand_twoval_unop (unoptab, cop0, t0, t1, unsignedp))
		{
		  convert_move (targ0, t0, unsignedp);
		  convert_move (targ1, t1, unsignedp);
		  return 1;
		}
	      else
		delete_insns_since (last);
	    }
	}
    }

  delete_insns_since (entry_last);
  return 0;
}

/* Generate code to perform an operation specified by BINOPTAB
   on operands OP0 and OP1, with two results to TARG1 and TARG2.
   We assume that the order of the operands for the instruction
   is TARG0, OP0, OP1, TARG1, which would fit a pattern like
   [(set TARG0 (operate OP0 OP1)) (set TARG1 (operate ...))].

   Either TARG0 or TARG1 may be zero, but what that means is that
   the result is not actually wanted.  We will generate it into
   a dummy pseudo-reg and discard it.  They may not both be zero.

   Returns 1 if this operation can be performed; 0 if not.  */

int
expand_twoval_binop (optab binoptab, rtx op0, rtx op1, rtx targ0, rtx targ1,
		     int unsignedp)
{
  enum machine_mode mode = GET_MODE (targ0 ? targ0 : targ1);
  enum mode_class class;
  enum machine_mode wider_mode;
  rtx entry_last = get_last_insn ();
  rtx last;

  class = GET_MODE_CLASS (mode);

  if (!targ0)
    targ0 = gen_reg_rtx (mode);
  if (!targ1)
    targ1 = gen_reg_rtx (mode);

  /* Record where to go back to if we fail.  */
  last = get_last_insn ();

  if (optab_handler (binoptab, mode)->insn_code != CODE_FOR_nothing)
    {
      int icode = (int) optab_handler (binoptab, mode)->insn_code;
      enum machine_mode mode0 = insn_data[icode].operand[1].mode;
      enum machine_mode mode1 = insn_data[icode].operand[2].mode;
      rtx pat;
      rtx xop0 = op0, xop1 = op1;

      /* If we are optimizing, force expensive constants into a register.  */
      xop0 = avoid_expensive_constant (mode0, binoptab, xop0, unsignedp);
      xop1 = avoid_expensive_constant (mode1, binoptab, xop1, unsignedp);

      /* In case the insn wants input operands in modes different from
	 those of the actual operands, convert the operands.  It would
	 seem that we don't need to convert CONST_INTs, but we do, so
	 that they're properly zero-extended, sign-extended or truncated
	 for their mode.  */

      if (GET_MODE (op0) != mode0 && mode0 != VOIDmode)
	xop0 = convert_modes (mode0,
			      GET_MODE (op0) != VOIDmode
			      ? GET_MODE (op0)
			      : mode,
			      xop0, unsignedp);

      if (GET_MODE (op1) != mode1 && mode1 != VOIDmode)
	xop1 = convert_modes (mode1,
			      GET_MODE (op1) != VOIDmode
			      ? GET_MODE (op1)
			      : mode,
			      xop1, unsignedp);

      /* Now, if insn doesn't accept these operands, put them into pseudos.  */
      if (!insn_data[icode].operand[1].predicate (xop0, mode0))
	xop0 = copy_to_mode_reg (mode0, xop0);

      if (!insn_data[icode].operand[2].predicate (xop1, mode1))
	xop1 = copy_to_mode_reg (mode1, xop1);

      /* We could handle this, but we should always be called with a pseudo
	 for our targets and all insns should take them as outputs.  */
      gcc_assert (insn_data[icode].operand[0].predicate (targ0, mode));
      gcc_assert (insn_data[icode].operand[3].predicate (targ1, mode));

      pat = GEN_FCN (icode) (targ0, xop0, xop1, targ1);
      if (pat)
	{
	  emit_insn (pat);
	  return 1;
	}
      else
	delete_insns_since (last);
    }

  /* It can't be done in this mode.  Can we do it in a wider mode?  */

  if (CLASS_HAS_WIDER_MODES_P (class))
    {
      for (wider_mode = GET_MODE_WIDER_MODE (mode);
	   wider_mode != VOIDmode;
	   wider_mode = GET_MODE_WIDER_MODE (wider_mode))
	{
	  if (optab_handler (binoptab, wider_mode)->insn_code
	      != CODE_FOR_nothing)
	    {
	      rtx t0 = gen_reg_rtx (wider_mode);
	      rtx t1 = gen_reg_rtx (wider_mode);
	      rtx cop0 = convert_modes (wider_mode, mode, op0, unsignedp);
	      rtx cop1 = convert_modes (wider_mode, mode, op1, unsignedp);

	      if (expand_twoval_binop (binoptab, cop0, cop1,
				       t0, t1, unsignedp))
		{
		  convert_move (targ0, t0, unsignedp);
		  convert_move (targ1, t1, unsignedp);
		  return 1;
		}
	      else
		delete_insns_since (last);
	    }
	}
    }

  delete_insns_since (entry_last);
  return 0;
}

/* Expand the two-valued library call indicated by BINOPTAB, but
   preserve only one of the values.  If TARG0 is non-NULL, the first
   value is placed into TARG0; otherwise the second value is placed
   into TARG1.  Exactly one of TARG0 and TARG1 must be non-NULL.  The
   value stored into TARG0 or TARG1 is equivalent to (CODE OP0 OP1).
   This routine assumes that the value returned by the library call is
   as if the return value was of an integral mode twice as wide as the
   mode of OP0.  Returns 1 if the call was successful.  */

bool
expand_twoval_binop_libfunc (optab binoptab, rtx op0, rtx op1,
			     rtx targ0, rtx targ1, enum rtx_code code)
{
  enum machine_mode mode;
  enum machine_mode libval_mode;
  rtx libval;
  rtx insns;
  rtx libfunc;

  /* Exactly one of TARG0 or TARG1 should be non-NULL.  */
  gcc_assert (!targ0 != !targ1);

  mode = GET_MODE (op0);
  libfunc = optab_libfunc (binoptab, mode);
  if (!libfunc)
    return false;

  /* The value returned by the library function will have twice as
     many bits as the nominal MODE.  */
  libval_mode = smallest_mode_for_size (2 * GET_MODE_BITSIZE (mode),
					MODE_INT);
  start_sequence ();
  libval = emit_library_call_value (libfunc, NULL_RTX, LCT_CONST,
				    libval_mode, 2,
				    op0, mode,
				    op1, mode);
  /* Get the part of VAL containing the value that we want.  */
  libval = simplify_gen_subreg (mode, libval, libval_mode,
				targ0 ? 0 : GET_MODE_SIZE (mode));
  insns = get_insns ();
  end_sequence ();
  /* Move the into the desired location.  */
  emit_libcall_block (insns, targ0 ? targ0 : targ1, libval,
		      gen_rtx_fmt_ee (code, mode, op0, op1));

  return true;
}


/* Wrapper around expand_unop which takes an rtx code to specify
   the operation to perform, not an optab pointer.  All other
   arguments are the same.  */
rtx
expand_simple_unop (enum machine_mode mode, enum rtx_code code, rtx op0,
		    rtx target, int unsignedp)
{
  optab unop = code_to_optab[(int) code];
  gcc_assert (unop);

  return expand_unop (mode, unop, op0, target, unsignedp);
}

/* Try calculating
	(clz:narrow x)
   as
	(clz:wide (zero_extend:wide x)) - ((width wide) - (width narrow)).  */
static rtx
widen_clz (enum machine_mode mode, rtx op0, rtx target)
{
  enum mode_class class = GET_MODE_CLASS (mode);
  if (CLASS_HAS_WIDER_MODES_P (class))
    {
      enum machine_mode wider_mode;
      for (wider_mode = GET_MODE_WIDER_MODE (mode);
	   wider_mode != VOIDmode;
	   wider_mode = GET_MODE_WIDER_MODE (wider_mode))
	{
	  if (optab_handler (clz_optab, wider_mode)->insn_code
	      != CODE_FOR_nothing)
	    {
	      rtx xop0, temp, last;

	      last = get_last_insn ();

	      if (target == 0)
		target = gen_reg_rtx (mode);
	      xop0 = widen_operand (op0, wider_mode, mode, true, false);
	      temp = expand_unop (wider_mode, clz_optab, xop0, NULL_RTX, true);
	      if (temp != 0)
		temp = expand_binop (wider_mode, sub_optab, temp,
				     GEN_INT (GET_MODE_BITSIZE (wider_mode)
					      - GET_MODE_BITSIZE (mode)),
				     target, true, OPTAB_DIRECT);
	      if (temp == 0)
		delete_insns_since (last);

	      return temp;
	    }
	}
    }
  return 0;
}

/* Try calculating clz of a double-word quantity as two clz's of word-sized
   quantities, choosing which based on whether the high word is nonzero.  */
static rtx
expand_doubleword_clz (enum machine_mode mode, rtx op0, rtx target)
{
  rtx xop0 = force_reg (mode, op0);
  rtx subhi = gen_highpart (word_mode, xop0);
  rtx sublo = gen_lowpart (word_mode, xop0);
  rtx hi0_label = gen_label_rtx ();
  rtx after_label = gen_label_rtx ();
  rtx seq, temp, result;

  /* If we were not given a target, use a word_mode register, not a
     'mode' register.  The result will fit, and nobody is expecting
     anything bigger (the return type of __builtin_clz* is int).  */
  if (!target)
    target = gen_reg_rtx (word_mode);

  /* In any case, write to a word_mode scratch in both branches of the
     conditional, so we can ensure there is a single move insn setting
     'target' to tag a REG_EQUAL note on.  */
  result = gen_reg_rtx (word_mode);

  start_sequence ();

  /* If the high word is not equal to zero,
     then clz of the full value is clz of the high word.  */
  emit_cmp_and_jump_insns (subhi, CONST0_RTX (word_mode), EQ, 0,
			   word_mode, true, hi0_label);

  temp = expand_unop_direct (word_mode, clz_optab, subhi, result, true);
  if (!temp)
    goto fail;

  if (temp != result)
    convert_move (result, temp, true);

  emit_jump_insn (gen_jump (after_label));
  emit_barrier ();

  /* Else clz of the full value is clz of the low word plus the number
     of bits in the high word.  */
  emit_label (hi0_label);

  temp = expand_unop_direct (word_mode, clz_optab, sublo, 0, true);
  if (!temp)
    goto fail;
  temp = expand_binop (word_mode, add_optab, temp,
		       GEN_INT (GET_MODE_BITSIZE (word_mode)),
		       result, true, OPTAB_DIRECT);
  if (!temp)
    goto fail;
  if (temp != result)
    convert_move (result, temp, true);

  emit_label (after_label);
  convert_move (target, result, true);

  seq = get_insns ();
  end_sequence ();

  add_equal_note (seq, target, CLZ, xop0, 0);
  emit_insn (seq);
  return target;

 fail:
  end_sequence ();
  return 0;
}

/* Try calculating
	(bswap:narrow x)
   as
	(lshiftrt:wide (bswap:wide x) ((width wide) - (width narrow))).  */
static rtx
widen_bswap (enum machine_mode mode, rtx op0, rtx target)
{
  enum mode_class class = GET_MODE_CLASS (mode);
  enum machine_mode wider_mode;
  rtx x, last;

  if (!CLASS_HAS_WIDER_MODES_P (class))
    return NULL_RTX;

  for (wider_mode = GET_MODE_WIDER_MODE (mode);
       wider_mode != VOIDmode;
       wider_mode = GET_MODE_WIDER_MODE (wider_mode))
    if (optab_handler (bswap_optab, wider_mode)->insn_code != CODE_FOR_nothing)
      goto found;
  return NULL_RTX;

 found:
  last = get_last_insn ();

  x = widen_operand (op0, wider_mode, mode, true, true);
  x = expand_unop (wider_mode, bswap_optab, x, NULL_RTX, true);

  if (x != 0)
    x = expand_shift (RSHIFT_EXPR, wider_mode, x,
		      size_int (GET_MODE_BITSIZE (wider_mode)
			        - GET_MODE_BITSIZE (mode)),
		      NULL_RTX, true);

  if (x != 0)
    {
      if (target == 0)
	target = gen_reg_rtx (mode);
      emit_move_insn (target, gen_lowpart (mode, x));
    }
  else
    delete_insns_since (last);

  return target;
}

/* Try calculating bswap as two bswaps of two word-sized operands.  */

static rtx
expand_doubleword_bswap (enum machine_mode mode, rtx op, rtx target)
{
  rtx t0, t1;

  t1 = expand_unop (word_mode, bswap_optab,
		    operand_subword_force (op, 0, mode), NULL_RTX, true);
  t0 = expand_unop (word_mode, bswap_optab,
		    operand_subword_force (op, 1, mode), NULL_RTX, true);

  if (target == 0)
    target = gen_reg_rtx (mode);
  if (REG_P (target))
    emit_insn (gen_rtx_CLOBBER (VOIDmode, target));
  emit_move_insn (operand_subword (target, 0, 1, mode), t0);
  emit_move_insn (operand_subword (target, 1, 1, mode), t1);

  return target;
}

/* Try calculating (parity x) as (and (popcount x) 1), where
   popcount can also be done in a wider mode.  */
static rtx
expand_parity (enum machine_mode mode, rtx op0, rtx target)
{
  enum mode_class class = GET_MODE_CLASS (mode);
  if (CLASS_HAS_WIDER_MODES_P (class))
    {
      enum machine_mode wider_mode;
      for (wider_mode = mode; wider_mode != VOIDmode;
	   wider_mode = GET_MODE_WIDER_MODE (wider_mode))
	{
	  if (optab_handler (popcount_optab, wider_mode)->insn_code
	      != CODE_FOR_nothing)
	    {
	      rtx xop0, temp, last;

	      last = get_last_insn ();

	      if (target == 0)
		target = gen_reg_rtx (mode);
	      xop0 = widen_operand (op0, wider_mode, mode, true, false);
	      temp = expand_unop (wider_mode, popcount_optab, xop0, NULL_RTX,
				  true);
	      if (temp != 0)
		temp = expand_binop (wider_mode, and_optab, temp, const1_rtx,
				     target, true, OPTAB_DIRECT);
	      if (temp == 0)
		delete_insns_since (last);

	      return temp;
	    }
	}
    }
  return 0;
}

/* Try calculating ctz(x) as K - clz(x & -x) ,
   where K is GET_MODE_BITSIZE(mode) - 1.

   Both __builtin_ctz and __builtin_clz are undefined at zero, so we
   don't have to worry about what the hardware does in that case.  (If
   the clz instruction produces the usual value at 0, which is K, the
   result of this code sequence will be -1; expand_ffs, below, relies
   on this.  It might be nice to have it be K instead, for consistency
   with the (very few) processors that provide a ctz with a defined
   value, but that would take one more instruction, and it would be
   less convenient for expand_ffs anyway.  */

static rtx
expand_ctz (enum machine_mode mode, rtx op0, rtx target)
{
  rtx seq, temp;
  
  if (optab_handler (clz_optab, mode)->insn_code == CODE_FOR_nothing)
    return 0;
  
  start_sequence ();

  temp = expand_unop_direct (mode, neg_optab, op0, NULL_RTX, true);
  if (temp)
    temp = expand_binop (mode, and_optab, op0, temp, NULL_RTX,
			 true, OPTAB_DIRECT);
  if (temp)
    temp = expand_unop_direct (mode, clz_optab, temp, NULL_RTX, true);
  if (temp)
    temp = expand_binop (mode, sub_optab, GEN_INT (GET_MODE_BITSIZE (mode) - 1),
			 temp, target,
			 true, OPTAB_DIRECT);
  if (temp == 0)
    {
      end_sequence ();
      return 0;
    }

  seq = get_insns ();
  end_sequence ();

  add_equal_note (seq, temp, CTZ, op0, 0);
  emit_insn (seq);
  return temp;
}


/* Try calculating ffs(x) using ctz(x) if we have that instruction, or
   else with the sequence used by expand_clz.
   
   The ffs builtin promises to return zero for a zero value and ctz/clz
   may have an undefined value in that case.  If they do not give us a
   convenient value, we have to generate a test and branch.  */
static rtx
expand_ffs (enum machine_mode mode, rtx op0, rtx target)
{
  HOST_WIDE_INT val = 0;
  bool defined_at_zero = false;
  rtx temp, seq;

  if (optab_handler (ctz_optab, mode)->insn_code != CODE_FOR_nothing)
    {
      start_sequence ();

      temp = expand_unop_direct (mode, ctz_optab, op0, 0, true);
      if (!temp)
	goto fail;

      defined_at_zero = (CTZ_DEFINED_VALUE_AT_ZERO (mode, val) == 2);
    }
  else if (optab_handler (clz_optab, mode)->insn_code != CODE_FOR_nothing)
    {
      start_sequence ();
      temp = expand_ctz (mode, op0, 0);
      if (!temp)
	goto fail;

      if (CLZ_DEFINED_VALUE_AT_ZERO (mode, val) == 2)
	{
	  defined_at_zero = true;
	  val = (GET_MODE_BITSIZE (mode) - 1) - val;
	}
    }
  else
    return 0;

  if (defined_at_zero && val == -1)
    /* No correction needed at zero.  */;
  else 
    {
      /* We don't try to do anything clever with the situation found
	 on some processors (eg Alpha) where ctz(0:mode) ==
	 bitsize(mode).  If someone can think of a way to send N to -1
	 and leave alone all values in the range 0..N-1 (where N is a
	 power of two), cheaper than this test-and-branch, please add it.

	 The test-and-branch is done after the operation itself, in case
	 the operation sets condition codes that can be recycled for this.
	 (This is true on i386, for instance.)  */

      rtx nonzero_label = gen_label_rtx ();
      emit_cmp_and_jump_insns (op0, CONST0_RTX (mode), NE, 0,
			       mode, true, nonzero_label);

      convert_move (temp, GEN_INT (-1), false);
      emit_label (nonzero_label);
    }

  /* temp now has a value in the range -1..bitsize-1.  ffs is supposed
     to produce a value in the range 0..bitsize.  */
  temp = expand_binop (mode, add_optab, temp, GEN_INT (1),
		       target, false, OPTAB_DIRECT);
  if (!temp)
    goto fail;

  seq = get_insns ();
  end_sequence ();

  add_equal_note (seq, temp, FFS, op0, 0);
  emit_insn (seq);
  return temp;

 fail:
  end_sequence ();
  return 0;
}

/* Extract the OMODE lowpart from VAL, which has IMODE.  Under certain
   conditions, VAL may already be a SUBREG against which we cannot generate
   a further SUBREG.  In this case, we expect forcing the value into a
   register will work around the situation.  */

static rtx
lowpart_subreg_maybe_copy (enum machine_mode omode, rtx val,
			   enum machine_mode imode)
{
  rtx ret;
  ret = lowpart_subreg (omode, val, imode);
  if (ret == NULL)
    {
      val = force_reg (imode, val);
      ret = lowpart_subreg (omode, val, imode);
      gcc_assert (ret != NULL);
    }
  return ret;
}

/* Expand a floating point absolute value or negation operation via a
   logical operation on the sign bit.  */

static rtx
expand_absneg_bit (enum rtx_code code, enum machine_mode mode,
		   rtx op0, rtx target)
{
  const struct real_format *fmt;
  int bitpos, word, nwords, i;
  enum machine_mode imode;
  HOST_WIDE_INT hi, lo;
  rtx temp, insns;

  /* The format has to have a simple sign bit.  */
  fmt = REAL_MODE_FORMAT (mode);
  if (fmt == NULL)
    return NULL_RTX;

  bitpos = fmt->signbit_rw;
  if (bitpos < 0)
    return NULL_RTX;

  /* Don't create negative zeros if the format doesn't support them.  */
  if (code == NEG && !fmt->has_signed_zero)
    return NULL_RTX;

  if (GET_MODE_SIZE (mode) <= UNITS_PER_WORD)
    {
      imode = int_mode_for_mode (mode);
      if (imode == BLKmode)
	return NULL_RTX;
      word = 0;
      nwords = 1;
    }
  else
    {
      imode = word_mode;

      if (FLOAT_WORDS_BIG_ENDIAN)
	word = (GET_MODE_BITSIZE (mode) - bitpos) / BITS_PER_WORD;
      else
	word = bitpos / BITS_PER_WORD;
      bitpos = bitpos % BITS_PER_WORD;
      nwords = (GET_MODE_BITSIZE (mode) + BITS_PER_WORD - 1) / BITS_PER_WORD;
    }

  if (bitpos < HOST_BITS_PER_WIDE_INT)
    {
      hi = 0;
      lo = (HOST_WIDE_INT) 1 << bitpos;
    }
  else
    {
      hi = (HOST_WIDE_INT) 1 << (bitpos - HOST_BITS_PER_WIDE_INT);
      lo = 0;
    }
  if (code == ABS)
    lo = ~lo, hi = ~hi;

  if (target == 0 || target == op0)
    target = gen_reg_rtx (mode);

  if (nwords > 1)
    {
      start_sequence ();

      for (i = 0; i < nwords; ++i)
	{
	  rtx targ_piece = operand_subword (target, i, 1, mode);
	  rtx op0_piece = operand_subword_force (op0, i, mode);

	  if (i == word)
	    {
	      temp = expand_binop (imode, code == ABS ? and_optab : xor_optab,
				   op0_piece,
				   immed_double_const (lo, hi, imode),
				   targ_piece, 1, OPTAB_LIB_WIDEN);
	      if (temp != targ_piece)
		emit_move_insn (targ_piece, temp);
	    }
	  else
	    emit_move_insn (targ_piece, op0_piece);
	}

      insns = get_insns ();
      end_sequence ();

      temp = gen_rtx_fmt_e (code, mode, copy_rtx (op0));
      emit_no_conflict_block (insns, target, op0, NULL_RTX, temp);
    }
  else
    {
      temp = expand_binop (imode, code == ABS ? and_optab : xor_optab,
			   gen_lowpart (imode, op0),
			   immed_double_const (lo, hi, imode),
		           gen_lowpart (imode, target), 1, OPTAB_LIB_WIDEN);
      target = lowpart_subreg_maybe_copy (mode, temp, imode);

      set_unique_reg_note (get_last_insn (), REG_EQUAL,
			   gen_rtx_fmt_e (code, mode, copy_rtx (op0)));
    }

  return target;
}

/* As expand_unop, but will fail rather than attempt the operation in a
   different mode or with a libcall.  */
static rtx
expand_unop_direct (enum machine_mode mode, optab unoptab, rtx op0, rtx target,
	     int unsignedp)
{
  if (optab_handler (unoptab, mode)->insn_code != CODE_FOR_nothing)
    {
      int icode = (int) optab_handler (unoptab, mode)->insn_code;
      enum machine_mode mode0 = insn_data[icode].operand[1].mode;
      rtx xop0 = op0;
      rtx last = get_last_insn ();
      rtx pat, temp;

      if (target)
	temp = target;
      else
	temp = gen_reg_rtx (mode);

      if (GET_MODE (xop0) != VOIDmode
	  && GET_MODE (xop0) != mode0)
	xop0 = convert_to_mode (mode0, xop0, unsignedp);

      /* Now, if insn doesn't accept our operand, put it into a pseudo.  */

      if (!insn_data[icode].operand[1].predicate (xop0, mode0))
	xop0 = copy_to_mode_reg (mode0, xop0);

      if (!insn_data[icode].operand[0].predicate (temp, mode))
	temp = gen_reg_rtx (mode);

      pat = GEN_FCN (icode) (temp, xop0);
      if (pat)
	{
	  if (INSN_P (pat) && NEXT_INSN (pat) != NULL_RTX
	      && ! add_equal_note (pat, temp, unoptab->code, xop0, NULL_RTX))
	    {
	      delete_insns_since (last);
	      return expand_unop (mode, unoptab, op0, NULL_RTX, unsignedp);
	    }

	  emit_insn (pat);

	  return temp;
	}
      else
	delete_insns_since (last);
    }
  return 0;
}

/* Generate code to perform an operation specified by UNOPTAB
   on operand OP0, with result having machine-mode MODE.

   UNSIGNEDP is for the case where we have to widen the operands
   to perform the operation.  It says to use zero-extension.

   If TARGET is nonzero, the value
   is generated there, if it is convenient to do so.
   In all cases an rtx is returned for the locus of the value;
   this may or may not be TARGET.  */

rtx
expand_unop (enum machine_mode mode, optab unoptab, rtx op0, rtx target,
	     int unsignedp)
{
  enum mode_class class = GET_MODE_CLASS (mode);
  enum machine_mode wider_mode;
  rtx temp;
  rtx libfunc;

  temp = expand_unop_direct (mode, unoptab, op0, target, unsignedp);
  if (temp)
    return temp;

  /* It can't be done in this mode.  Can we open-code it in a wider mode?  */

  /* Widening (or narrowing) clz needs special treatment.  */
  if (unoptab == clz_optab)
    {
      temp = widen_clz (mode, op0, target);
      if (temp)
	return temp;

      if (GET_MODE_SIZE (mode) == 2 * UNITS_PER_WORD
	  && optab_handler (unoptab, word_mode)->insn_code != CODE_FOR_nothing)
	{
	  temp = expand_doubleword_clz (mode, op0, target);
	  if (temp)
	    return temp;
	}

	goto try_libcall;
    }

  /* Widening (or narrowing) bswap needs special treatment.  */
  if (unoptab == bswap_optab)
    {
      temp = widen_bswap (mode, op0, target);
      if (temp)
	return temp;

      if (GET_MODE_SIZE (mode) == 2 * UNITS_PER_WORD
	  && optab_handler (unoptab, word_mode)->insn_code != CODE_FOR_nothing)
	{
	  temp = expand_doubleword_bswap (mode, op0, target);
	  if (temp)
	    return temp;
	}

      goto try_libcall;
    }

  if (CLASS_HAS_WIDER_MODES_P (class))
    for (wider_mode = GET_MODE_WIDER_MODE (mode);
	 wider_mode != VOIDmode;
	 wider_mode = GET_MODE_WIDER_MODE (wider_mode))
      {
	if (optab_handler (unoptab, wider_mode)->insn_code != CODE_FOR_nothing)
	  {
	    rtx xop0 = op0;
	    rtx last = get_last_insn ();

	    /* For certain operations, we need not actually extend
	       the narrow operand, as long as we will truncate the
	       results to the same narrowness.  */

	    xop0 = widen_operand (xop0, wider_mode, mode, unsignedp,
				  (unoptab == neg_optab
				   || unoptab == one_cmpl_optab)
				  && class == MODE_INT);

	    temp = expand_unop (wider_mode, unoptab, xop0, NULL_RTX,
				unsignedp);

	    if (temp)
	      {
		if (class != MODE_INT
		    || !TRULY_NOOP_TRUNCATION (GET_MODE_BITSIZE (mode),
					       GET_MODE_BITSIZE (wider_mode)))
		  {
		    if (target == 0)
		      target = gen_reg_rtx (mode);
		    convert_move (target, temp, 0);
		    return target;
		  }
		else
		  return gen_lowpart (mode, temp);
	      }
	    else
	      delete_insns_since (last);
	  }
      }

  /* These can be done a word at a time.  */
  if (unoptab == one_cmpl_optab
      && class == MODE_INT
      && GET_MODE_SIZE (mode) > UNITS_PER_WORD
      && optab_handler (unoptab, word_mode)->insn_code != CODE_FOR_nothing)
    {
      int i;
      rtx insns;

      if (target == 0 || target == op0)
	target = gen_reg_rtx (mode);

      start_sequence ();

      /* Do the actual arithmetic.  */
      for (i = 0; i < GET_MODE_BITSIZE (mode) / BITS_PER_WORD; i++)
	{
	  rtx target_piece = operand_subword (target, i, 1, mode);
	  rtx x = expand_unop (word_mode, unoptab,
			       operand_subword_force (op0, i, mode),
			       target_piece, unsignedp);

	  if (target_piece != x)
	    emit_move_insn (target_piece, x);
	}

      insns = get_insns ();
      end_sequence ();

      emit_no_conflict_block (insns, target, op0, NULL_RTX,
			      gen_rtx_fmt_e (unoptab->code, mode,
					     copy_rtx (op0)));
      return target;
    }

  if (unoptab->code == NEG)
    {
      /* Try negating floating point values by flipping the sign bit.  */
      if (SCALAR_FLOAT_MODE_P (mode))
	{
	  temp = expand_absneg_bit (NEG, mode, op0, target);
	  if (temp)
	    return temp;
	}

      /* If there is no negation pattern, and we have no negative zero,
	 try subtracting from zero.  */
      if (!HONOR_SIGNED_ZEROS (mode))
	{
	  temp = expand_binop (mode, (unoptab == negv_optab
				      ? subv_optab : sub_optab),
			       CONST0_RTX (mode), op0, target,
			       unsignedp, OPTAB_DIRECT);
	  if (temp)
	    return temp;
	}
    }

  /* Try calculating parity (x) as popcount (x) % 2.  */
  if (unoptab == parity_optab)
    {
      temp = expand_parity (mode, op0, target);
      if (temp)
	return temp;
    }

  /* Try implementing ffs (x) in terms of clz (x).  */
  if (unoptab == ffs_optab)
    {
      temp = expand_ffs (mode, op0, target);
      if (temp)
	return temp;
    }

  /* Try implementing ctz (x) in terms of clz (x).  */
  if (unoptab == ctz_optab)
    {
      temp = expand_ctz (mode, op0, target);
      if (temp)
	return temp;
    }

 try_libcall:
  /* Now try a library call in this mode.  */
  libfunc = optab_libfunc (unoptab, mode);
  if (libfunc)
    {
      rtx insns;
      rtx value;
      rtx eq_value;
      enum machine_mode outmode = mode;

      /* All of these functions return small values.  Thus we choose to
	 have them return something that isn't a double-word.  */
      if (unoptab == ffs_optab || unoptab == clz_optab || unoptab == ctz_optab
	  || unoptab == popcount_optab || unoptab == parity_optab)
	outmode
	    = GET_MODE (hard_libcall_value (TYPE_MODE (integer_type_node)));

      start_sequence ();

      /* Pass 1 for NO_QUEUE so we don't lose any increments
	 if the libcall is cse'd or moved.  */
      value = emit_library_call_value (libfunc, NULL_RTX, LCT_CONST, outmode,
				       1, op0, mode);
      insns = get_insns ();
      end_sequence ();

      target = gen_reg_rtx (outmode);
      eq_value = gen_rtx_fmt_e (unoptab->code, mode, op0);
      if (GET_MODE_SIZE (outmode) < GET_MODE_SIZE (mode))
	eq_value = simplify_gen_unary (TRUNCATE, outmode, eq_value, mode);
      else if (GET_MODE_SIZE (outmode) > GET_MODE_SIZE (mode))
	eq_value = simplify_gen_unary (ZERO_EXTEND, outmode, eq_value, mode);
      emit_libcall_block (insns, target, value, eq_value);

      return target;
    }

  /* It can't be done in this mode.  Can we do it in a wider mode?  */

  if (CLASS_HAS_WIDER_MODES_P (class))
    {
      for (wider_mode = GET_MODE_WIDER_MODE (mode);
	   wider_mode != VOIDmode;
	   wider_mode = GET_MODE_WIDER_MODE (wider_mode))
	{
	  if ((optab_handler (unoptab, wider_mode)->insn_code
	       != CODE_FOR_nothing)
	      || optab_libfunc (unoptab, wider_mode))
	    {
	      rtx xop0 = op0;
	      rtx last = get_last_insn ();

	      /* For certain operations, we need not actually extend
		 the narrow operand, as long as we will truncate the
		 results to the same narrowness.  */

	      xop0 = widen_operand (xop0, wider_mode, mode, unsignedp,
				    (unoptab == neg_optab
				     || unoptab == one_cmpl_optab)
				    && class == MODE_INT);

	      temp = expand_unop (wider_mode, unoptab, xop0, NULL_RTX,
				  unsignedp);

	      /* If we are generating clz using wider mode, adjust the
		 result.  */
	      if (unoptab == clz_optab && temp != 0)
		temp = expand_binop (wider_mode, sub_optab, temp,
				     GEN_INT (GET_MODE_BITSIZE (wider_mode)
					      - GET_MODE_BITSIZE (mode)),
				     target, true, OPTAB_DIRECT);

	      if (temp)
		{
		  if (class != MODE_INT)
		    {
		      if (target == 0)
			target = gen_reg_rtx (mode);
		      convert_move (target, temp, 0);
		      return target;
		    }
		  else
		    return gen_lowpart (mode, temp);
		}
	      else
		delete_insns_since (last);
	    }
	}
    }

  /* One final attempt at implementing negation via subtraction,
     this time allowing widening of the operand.  */
  if (unoptab->code == NEG && !HONOR_SIGNED_ZEROS (mode))
    {
      rtx temp;
      temp = expand_binop (mode,
                           unoptab == negv_optab ? subv_optab : sub_optab,
                           CONST0_RTX (mode), op0,
                           target, unsignedp, OPTAB_LIB_WIDEN);
      if (temp)
        return temp;
    }

  return 0;
}

/* Emit code to compute the absolute value of OP0, with result to
   TARGET if convenient.  (TARGET may be 0.)  The return value says
   where the result actually is to be found.

   MODE is the mode of the operand; the mode of the result is
   different but can be deduced from MODE.

 */

rtx
expand_abs_nojump (enum machine_mode mode, rtx op0, rtx target,
		   int result_unsignedp)
{
  rtx temp;

  if (! flag_trapv)
    result_unsignedp = 1;

  /* First try to do it with a special abs instruction.  */
  temp = expand_unop (mode, result_unsignedp ? abs_optab : absv_optab,
                      op0, target, 0);
  if (temp != 0)
    return temp;

  /* For floating point modes, try clearing the sign bit.  */
  if (SCALAR_FLOAT_MODE_P (mode))
    {
      temp = expand_absneg_bit (ABS, mode, op0, target);
      if (temp)
	return temp;
    }

  /* If we have a MAX insn, we can do this as MAX (x, -x).  */
  if (optab_handler (smax_optab, mode)->insn_code != CODE_FOR_nothing
      && !HONOR_SIGNED_ZEROS (mode))
    {
      rtx last = get_last_insn ();

      temp = expand_unop (mode, neg_optab, op0, NULL_RTX, 0);
      if (temp != 0)
	temp = expand_binop (mode, smax_optab, op0, temp, target, 0,
			     OPTAB_WIDEN);

      if (temp != 0)
	return temp;

      delete_insns_since (last);
    }

  /* If this machine has expensive jumps, we can do integer absolute
     value of X as (((signed) x >> (W-1)) ^ x) - ((signed) x >> (W-1)),
     where W is the width of MODE.  */

  if (GET_MODE_CLASS (mode) == MODE_INT && BRANCH_COST >= 2)
    {
      rtx extended = expand_shift (RSHIFT_EXPR, mode, op0,
				   size_int (GET_MODE_BITSIZE (mode) - 1),
				   NULL_RTX, 0);

      temp = expand_binop (mode, xor_optab, extended, op0, target, 0,
			   OPTAB_LIB_WIDEN);
      if (temp != 0)
	temp = expand_binop (mode, result_unsignedp ? sub_optab : subv_optab,
                             temp, extended, target, 0, OPTAB_LIB_WIDEN);

      if (temp != 0)
	return temp;
    }

  return NULL_RTX;
}

rtx
expand_abs (enum machine_mode mode, rtx op0, rtx target,
	    int result_unsignedp, int safe)
{
  rtx temp, op1;

  if (! flag_trapv)
    result_unsignedp = 1;

  temp = expand_abs_nojump (mode, op0, target, result_unsignedp);
  if (temp != 0)
    return temp;

  /* If that does not win, use conditional jump and negate.  */

  /* It is safe to use the target if it is the same
     as the source if this is also a pseudo register */
  if (op0 == target && REG_P (op0)
      && REGNO (op0) >= FIRST_PSEUDO_REGISTER)
    safe = 1;

  op1 = gen_label_rtx ();
  if (target == 0 || ! safe
      || GET_MODE (target) != mode
      || (MEM_P (target) && MEM_VOLATILE_P (target))
      || (REG_P (target)
	  && REGNO (target) < FIRST_PSEUDO_REGISTER))
    target = gen_reg_rtx (mode);

  emit_move_insn (target, op0);
  NO_DEFER_POP;

  do_compare_rtx_and_jump (target, CONST0_RTX (mode), GE, 0, mode,
			   NULL_RTX, NULL_RTX, op1);

  op0 = expand_unop (mode, result_unsignedp ? neg_optab : negv_optab,
                     target, target, 0);
  if (op0 != target)
    emit_move_insn (target, op0);
  emit_label (op1);
  OK_DEFER_POP;
  return target;
}

/* A subroutine of expand_copysign, perform the copysign operation using the
   abs and neg primitives advertised to exist on the target.  The assumption
   is that we have a split register file, and leaving op0 in fp registers,
   and not playing with subregs so much, will help the register allocator.  */

static rtx
expand_copysign_absneg (enum machine_mode mode, rtx op0, rtx op1, rtx target,
		        int bitpos, bool op0_is_abs)
{
  enum machine_mode imode;
  int icode;
  rtx sign, label;

  if (target == op1)
    target = NULL_RTX;

  /* Check if the back end provides an insn that handles signbit for the
     argument's mode. */
  icode = (int) signbit_optab->handlers [(int) mode].insn_code;
  if (icode != CODE_FOR_nothing)
    {
      imode = insn_data[icode].operand[0].mode;
      sign = gen_reg_rtx (imode);
      emit_unop_insn (icode, sign, op1, UNKNOWN);
    }
  else
    {
      HOST_WIDE_INT hi, lo;

      if (GET_MODE_SIZE (mode) <= UNITS_PER_WORD)
	{
	  imode = int_mode_for_mode (mode);
	  if (imode == BLKmode)
	    return NULL_RTX;
	  op1 = gen_lowpart (imode, op1);
	}
      else
	{
	  int word;

	  imode = word_mode;
	  if (FLOAT_WORDS_BIG_ENDIAN)
	    word = (GET_MODE_BITSIZE (mode) - bitpos) / BITS_PER_WORD;
	  else
	    word = bitpos / BITS_PER_WORD;
	  bitpos = bitpos % BITS_PER_WORD;
	  op1 = operand_subword_force (op1, word, mode);
	}

      if (bitpos < HOST_BITS_PER_WIDE_INT)
	{
	  hi = 0;
	  lo = (HOST_WIDE_INT) 1 << bitpos;
	}
      else
	{
	  hi = (HOST_WIDE_INT) 1 << (bitpos - HOST_BITS_PER_WIDE_INT);
	  lo = 0;
	}

      sign = gen_reg_rtx (imode);
      sign = expand_binop (imode, and_optab, op1,
			   immed_double_const (lo, hi, imode),
			   NULL_RTX, 1, OPTAB_LIB_WIDEN);
    }

  if (!op0_is_abs)
    {
      op0 = expand_unop (mode, abs_optab, op0, target, 0);
      if (op0 == NULL)
	return NULL_RTX;
      target = op0;
    }
  else
    {
      if (target == NULL_RTX)
        target = copy_to_reg (op0);
      else
	emit_move_insn (target, op0);
    }

  label = gen_label_rtx ();
  emit_cmp_and_jump_insns (sign, const0_rtx, EQ, NULL_RTX, imode, 1, label);

  if (GET_CODE (op0) == CONST_DOUBLE)
    op0 = simplify_unary_operation (NEG, mode, op0, mode);
  else
    op0 = expand_unop (mode, neg_optab, op0, target, 0);
  if (op0 != target)
    emit_move_insn (target, op0);

  emit_label (label);

  return target;
}


/* A subroutine of expand_copysign, perform the entire copysign operation
   with integer bitmasks.  BITPOS is the position of the sign bit; OP0_IS_ABS
   is true if op0 is known to have its sign bit clear.  */

static rtx
expand_copysign_bit (enum machine_mode mode, rtx op0, rtx op1, rtx target,
		     int bitpos, bool op0_is_abs)
{
  enum machine_mode imode;
  HOST_WIDE_INT hi, lo;
  int word, nwords, i;
  rtx temp, insns;

  if (GET_MODE_SIZE (mode) <= UNITS_PER_WORD)
    {
      imode = int_mode_for_mode (mode);
      if (imode == BLKmode)
	return NULL_RTX;
      word = 0;
      nwords = 1;
    }
  else
    {
      imode = word_mode;

      if (FLOAT_WORDS_BIG_ENDIAN)
	word = (GET_MODE_BITSIZE (mode) - bitpos) / BITS_PER_WORD;
      else
	word = bitpos / BITS_PER_WORD;
      bitpos = bitpos % BITS_PER_WORD;
      nwords = (GET_MODE_BITSIZE (mode) + BITS_PER_WORD - 1) / BITS_PER_WORD;
    }

  if (bitpos < HOST_BITS_PER_WIDE_INT)
    {
      hi = 0;
      lo = (HOST_WIDE_INT) 1 << bitpos;
    }
  else
    {
      hi = (HOST_WIDE_INT) 1 << (bitpos - HOST_BITS_PER_WIDE_INT);
      lo = 0;
    }

  if (target == 0 || target == op0 || target == op1)
    target = gen_reg_rtx (mode);

  if (nwords > 1)
    {
      start_sequence ();

      for (i = 0; i < nwords; ++i)
	{
	  rtx targ_piece = operand_subword (target, i, 1, mode);
	  rtx op0_piece = operand_subword_force (op0, i, mode);

	  if (i == word)
	    {
	      if (!op0_is_abs)
		op0_piece = expand_binop (imode, and_optab, op0_piece,
					  immed_double_const (~lo, ~hi, imode),
					  NULL_RTX, 1, OPTAB_LIB_WIDEN);

	      op1 = expand_binop (imode, and_optab,
				  operand_subword_force (op1, i, mode),
				  immed_double_const (lo, hi, imode),
				  NULL_RTX, 1, OPTAB_LIB_WIDEN);

	      temp = expand_binop (imode, ior_optab, op0_piece, op1,
				   targ_piece, 1, OPTAB_LIB_WIDEN);
	      if (temp != targ_piece)
		emit_move_insn (targ_piece, temp);
	    }
	  else
	    emit_move_insn (targ_piece, op0_piece);
	}

      insns = get_insns ();
      end_sequence ();

      emit_no_conflict_block (insns, target, op0, op1, NULL_RTX);
    }
  else
    {
      op1 = expand_binop (imode, and_optab, gen_lowpart (imode, op1),
		          immed_double_const (lo, hi, imode),
		          NULL_RTX, 1, OPTAB_LIB_WIDEN);

      op0 = gen_lowpart (imode, op0);
      if (!op0_is_abs)
	op0 = expand_binop (imode, and_optab, op0,
			    immed_double_const (~lo, ~hi, imode),
			    NULL_RTX, 1, OPTAB_LIB_WIDEN);

      temp = expand_binop (imode, ior_optab, op0, op1,
			   gen_lowpart (imode, target), 1, OPTAB_LIB_WIDEN);
      target = lowpart_subreg_maybe_copy (mode, temp, imode);
    }

  return target;
}

/* Expand the C99 copysign operation.  OP0 and OP1 must be the same
   scalar floating point mode.  Return NULL if we do not know how to
   expand the operation inline.  */

rtx
expand_copysign (rtx op0, rtx op1, rtx target)
{
  enum machine_mode mode = GET_MODE (op0);
  const struct real_format *fmt;
  bool op0_is_abs;
  rtx temp;

  gcc_assert (SCALAR_FLOAT_MODE_P (mode));
  gcc_assert (GET_MODE (op1) == mode);

  /* First try to do it with a special instruction.  */
  temp = expand_binop (mode, copysign_optab, op0, op1,
		       target, 0, OPTAB_DIRECT);
  if (temp)
    return temp;

  fmt = REAL_MODE_FORMAT (mode);
  if (fmt == NULL || !fmt->has_signed_zero)
    return NULL_RTX;

  op0_is_abs = false;
  if (GET_CODE (op0) == CONST_DOUBLE)
    {
      if (real_isneg (CONST_DOUBLE_REAL_VALUE (op0)))
	op0 = simplify_unary_operation (ABS, mode, op0, mode);
      op0_is_abs = true;
    }

  if (fmt->signbit_ro >= 0
      && (GET_CODE (op0) == CONST_DOUBLE
	  || (optab_handler (neg_optab, mode)->insn_code != CODE_FOR_nothing
	      && optab_handler (abs_optab, mode)->insn_code != CODE_FOR_nothing)))
    {
      temp = expand_copysign_absneg (mode, op0, op1, target,
				     fmt->signbit_ro, op0_is_abs);
      if (temp)
	return temp;
    }

  if (fmt->signbit_rw < 0)
    return NULL_RTX;
  return expand_copysign_bit (mode, op0, op1, target,
			      fmt->signbit_rw, op0_is_abs);
}

/* Generate an instruction whose insn-code is INSN_CODE,
   with two operands: an output TARGET and an input OP0.
   TARGET *must* be nonzero, and the output is always stored there.
   CODE is an rtx code such that (CODE OP0) is an rtx that describes
   the value that is stored into TARGET.  */

void
emit_unop_insn (int icode, rtx target, rtx op0, enum rtx_code code)
{
  rtx temp;
  enum machine_mode mode0 = insn_data[icode].operand[1].mode;
  rtx pat;

  temp = target;

  /* Now, if insn does not accept our operands, put them into pseudos.  */

  if (!insn_data[icode].operand[1].predicate (op0, mode0))
    op0 = copy_to_mode_reg (mode0, op0);

  if (!insn_data[icode].operand[0].predicate (temp, GET_MODE (temp)))
    temp = gen_reg_rtx (GET_MODE (temp));

  pat = GEN_FCN (icode) (temp, op0);

  if (INSN_P (pat) && NEXT_INSN (pat) != NULL_RTX && code != UNKNOWN)
    add_equal_note (pat, temp, code, op0, NULL_RTX);

  emit_insn (pat);

  if (temp != target)
    emit_move_insn (target, temp);
}

struct no_conflict_data
{
  rtx target, first, insn;
  bool must_stay;
};

/* Called via note_stores by emit_no_conflict_block and emit_libcall_block.
   Set P->must_stay if the currently examined clobber / store has to stay
   in the list of insns that constitute the actual no_conflict block /
   libcall block.  */
static void
no_conflict_move_test (rtx dest, const_rtx set, void *p0)
{
  struct no_conflict_data *p= p0;

  /* If this inns directly contributes to setting the target, it must stay.  */
  if (reg_overlap_mentioned_p (p->target, dest))
    p->must_stay = true;
  /* If we haven't committed to keeping any other insns in the list yet,
     there is nothing more to check.  */
  else if (p->insn == p->first)
    return;
  /* If this insn sets / clobbers a register that feeds one of the insns
     already in the list, this insn has to stay too.  */
  else if (reg_overlap_mentioned_p (dest, PATTERN (p->first))
	   || (CALL_P (p->first) && (find_reg_fusage (p->first, USE, dest)))
	   || reg_used_between_p (dest, p->first, p->insn)
	   /* Likewise if this insn depends on a register set by a previous
	      insn in the list, or if it sets a result (presumably a hard
	      register) that is set or clobbered by a previous insn.
	      N.B. the modified_*_p (SET_DEST...) tests applied to a MEM
	      SET_DEST perform the former check on the address, and the latter
	      check on the MEM.  */
	   || (GET_CODE (set) == SET
	       && (modified_in_p (SET_SRC (set), p->first)
		   || modified_in_p (SET_DEST (set), p->first)
		   || modified_between_p (SET_SRC (set), p->first, p->insn)
		   || modified_between_p (SET_DEST (set), p->first, p->insn))))
    p->must_stay = true;
}

/* Encapsulate the block starting at FIRST and ending with LAST, which is
   logically equivalent to EQUIV, so it gets manipulated as a unit if it
   is possible to do so.  */

void
maybe_encapsulate_block (rtx first, rtx last, rtx equiv)
{
  if (!flag_non_call_exceptions || !may_trap_p (equiv))
    {
      /* We can't attach the REG_LIBCALL and REG_RETVAL notes when the
	 encapsulated region would not be in one basic block, i.e. when
	 there is a control_flow_insn_p insn between FIRST and LAST.  */
      bool attach_libcall_retval_notes = true;
      rtx insn, next = NEXT_INSN (last);

      for (insn = first; insn != next; insn = NEXT_INSN (insn))
	if (control_flow_insn_p (insn))
	  {
	    attach_libcall_retval_notes = false;
	    break;
	  }

      if (attach_libcall_retval_notes)
	{
	  REG_NOTES (first) = gen_rtx_INSN_LIST (REG_LIBCALL, last,
						 REG_NOTES (first));
	  REG_NOTES (last) = gen_rtx_INSN_LIST (REG_RETVAL, first,
						REG_NOTES (last));
	}
    }
}

/* Emit code to perform a series of operations on a multi-word quantity, one
   word at a time.

   Such a block is preceded by a CLOBBER of the output, consists of multiple
   insns, each setting one word of the output, and followed by a SET copying
   the output to itself.

   Each of the insns setting words of the output receives a REG_NO_CONFLICT
   note indicating that it doesn't conflict with the (also multi-word)
   inputs.  The entire block is surrounded by REG_LIBCALL and REG_RETVAL
   notes.

   INSNS is a block of code generated to perform the operation, not including
   the CLOBBER and final copy.  All insns that compute intermediate values
   are first emitted, followed by the block as described above.

   TARGET, OP0, and OP1 are the output and inputs of the operations,
   respectively.  OP1 may be zero for a unary operation.

   EQUIV, if nonzero, is an expression to be placed into a REG_EQUAL note
   on the last insn.

   If TARGET is not a register, INSNS is simply emitted with no special
   processing.  Likewise if anything in INSNS is not an INSN or if
   there is a libcall block inside INSNS.

   The final insn emitted is returned.  */

rtx
emit_no_conflict_block (rtx insns, rtx target, rtx op0, rtx op1, rtx equiv)
{
  rtx prev, next, first, last, insn;

  if (!REG_P (target) || reload_in_progress)
    return emit_insn (insns);
  else
    for (insn = insns; insn; insn = NEXT_INSN (insn))
      if (!NONJUMP_INSN_P (insn)
	  || find_reg_note (insn, REG_LIBCALL, NULL_RTX))
	return emit_insn (insns);

  /* First emit all insns that do not store into words of the output and remove
     these from the list.  */
  for (insn = insns; insn; insn = next)
    {
      rtx note;
      struct no_conflict_data data;

      next = NEXT_INSN (insn);

      /* Some ports (cris) create a libcall regions at their own.  We must
	 avoid any potential nesting of LIBCALLs.  */
      if ((note = find_reg_note (insn, REG_LIBCALL, NULL)) != NULL)
	remove_note (insn, note);
      if ((note = find_reg_note (insn, REG_RETVAL, NULL)) != NULL)
	remove_note (insn, note);

      data.target = target;
      data.first = insns;
      data.insn = insn;
      data.must_stay = 0;
      note_stores (PATTERN (insn), no_conflict_move_test, &data);
      if (! data.must_stay)
	{
	  if (PREV_INSN (insn))
	    NEXT_INSN (PREV_INSN (insn)) = next;
	  else
	    insns = next;

	  if (next)
	    PREV_INSN (next) = PREV_INSN (insn);

	  add_insn (insn);
	}
    }

  prev = get_last_insn ();

  /* Now write the CLOBBER of the output, followed by the setting of each
     of the words, followed by the final copy.  */
  if (target != op0 && target != op1)
    emit_insn (gen_rtx_CLOBBER (VOIDmode, target));

  for (insn = insns; insn; insn = next)
    {
      next = NEXT_INSN (insn);
      add_insn (insn);

      if (op1 && REG_P (op1))
	REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_NO_CONFLICT, op1,
					      REG_NOTES (insn));

      if (op0 && REG_P (op0))
	REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_NO_CONFLICT, op0,
					      REG_NOTES (insn));
    }

  if (optab_handler (mov_optab, GET_MODE (target))->insn_code
      != CODE_FOR_nothing)
    {
      last = emit_move_insn (target, target);
      if (equiv)
	set_unique_reg_note (last, REG_EQUAL, equiv);
    }
  else
    {
      last = get_last_insn ();

      /* Remove any existing REG_EQUAL note from "last", or else it will
	 be mistaken for a note referring to the full contents of the
	 alleged libcall value when found together with the REG_RETVAL
	 note added below.  An existing note can come from an insn
	 expansion at "last".  */
      remove_note (last, find_reg_note (last, REG_EQUAL, NULL_RTX));
    }

  if (prev == 0)
    first = get_insns ();
  else
    first = NEXT_INSN (prev);

  maybe_encapsulate_block (first, last, equiv);

  return last;
}

/* Emit code to make a call to a constant function or a library call.

   INSNS is a list containing all insns emitted in the call.
   These insns leave the result in RESULT.  Our block is to copy RESULT
   to TARGET, which is logically equivalent to EQUIV.

   We first emit any insns that set a pseudo on the assumption that these are
   loading constants into registers; doing so allows them to be safely cse'ed
   between blocks.  Then we emit all the other insns in the block, followed by
   an insn to move RESULT to TARGET.  This last insn will have a REQ_EQUAL
   note with an operand of EQUIV.

   Moving assignments to pseudos outside of the block is done to improve
   the generated code, but is not required to generate correct code,
   hence being unable to move an assignment is not grounds for not making
   a libcall block.  There are two reasons why it is safe to leave these
   insns inside the block: First, we know that these pseudos cannot be
   used in generated RTL outside the block since they are created for
   temporary purposes within the block.  Second, CSE will not record the
   values of anything set inside a libcall block, so we know they must
   be dead at the end of the block.

   Except for the first group of insns (the ones setting pseudos), the
   block is delimited by REG_RETVAL and REG_LIBCALL notes.  */
void
emit_libcall_block (rtx insns, rtx target, rtx result, rtx equiv)
{
  rtx final_dest = target;
  rtx prev, next, first, last, insn;

  /* If this is a reg with REG_USERVAR_P set, then it could possibly turn
     into a MEM later.  Protect the libcall block from this change.  */
  if (! REG_P (target) || REG_USERVAR_P (target))
    target = gen_reg_rtx (GET_MODE (target));

  /* If we're using non-call exceptions, a libcall corresponding to an
     operation that may trap may also trap.  */
  if (flag_non_call_exceptions && may_trap_p (equiv))
    {
      for (insn = insns; insn; insn = NEXT_INSN (insn))
	if (CALL_P (insn))
	  {
	    rtx note = find_reg_note (insn, REG_EH_REGION, NULL_RTX);

	    if (note != 0 && INTVAL (XEXP (note, 0)) <= 0)
	      remove_note (insn, note);
	  }
    }
  else
  /* look for any CALL_INSNs in this sequence, and attach a REG_EH_REGION
     reg note to indicate that this call cannot throw or execute a nonlocal
     goto (unless there is already a REG_EH_REGION note, in which case
     we update it).  */
    for (insn = insns; insn; insn = NEXT_INSN (insn))
      if (CALL_P (insn))
	{
	  rtx note = find_reg_note (insn, REG_EH_REGION, NULL_RTX);

	  if (note != 0)
	    XEXP (note, 0) = constm1_rtx;
	  else
	    REG_NOTES (insn) = gen_rtx_EXPR_LIST (REG_EH_REGION, constm1_rtx,
						  REG_NOTES (insn));
	}

  /* First emit all insns that set pseudos.  Remove them from the list as
     we go.  Avoid insns that set pseudos which were referenced in previous
     insns.  These can be generated by move_by_pieces, for example,
     to update an address.  Similarly, avoid insns that reference things
     set in previous insns.  */

  for (insn = insns; insn; insn = next)
    {
      rtx set = single_set (insn);
      rtx note;

      /* Some ports (cris) create a libcall regions at their own.  We must
	 avoid any potential nesting of LIBCALLs.  */
      if ((note = find_reg_note (insn, REG_LIBCALL, NULL)) != NULL)
	remove_note (insn, note);
      if ((note = find_reg_note (insn, REG_RETVAL, NULL)) != NULL)
	remove_note (insn, note);

      next = NEXT_INSN (insn);

      if (set != 0 && REG_P (SET_DEST (set))
	  && REGNO (SET_DEST (set)) >= FIRST_PSEUDO_REGISTER)
	{
	  struct no_conflict_data data;

	  data.target = const0_rtx;
	  data.first = insns;
	  data.insn = insn;
	  data.must_stay = 0;
	  note_stores (PATTERN (insn), no_conflict_move_test, &data);
	  if (! data.must_stay)
	    {
	      if (PREV_INSN (insn))
		NEXT_INSN (PREV_INSN (insn)) = next;
	      else
		insns = next;

	      if (next)
		PREV_INSN (next) = PREV_INSN (insn);

	      add_insn (insn);
	    }
	}

      /* Some ports use a loop to copy large arguments onto the stack.
	 Don't move anything outside such a loop.  */
      if (LABEL_P (insn))
	break;
    }

  prev = get_last_insn ();

  /* Write the remaining insns followed by the final copy.  */

  for (insn = insns; insn; insn = next)
    {
      next = NEXT_INSN (insn);

      add_insn (insn);
    }

  last = emit_move_insn (target, result);
  if (optab_handler (mov_optab, GET_MODE (target))->insn_code
      != CODE_FOR_nothing)
    set_unique_reg_note (last, REG_EQUAL, copy_rtx (equiv));
  else
    {
      /* Remove any existing REG_EQUAL note from "last", or else it will
	 be mistaken for a note referring to the full contents of the
	 libcall value when found together with the REG_RETVAL note added
	 below.  An existing note can come from an insn expansion at
	 "last".  */
      remove_note (last, find_reg_note (last, REG_EQUAL, NULL_RTX));
    }

  if (final_dest != target)
    emit_move_insn (final_dest, target);

  if (prev == 0)
    first = get_insns ();
  else
    first = NEXT_INSN (prev);

  maybe_encapsulate_block (first, last, equiv);
}

/* Nonzero if we can perform a comparison of mode MODE straightforwardly.
   PURPOSE describes how this comparison will be used.  CODE is the rtx
   comparison code we will be using.

   ??? Actually, CODE is slightly weaker than that.  A target is still
   required to implement all of the normal bcc operations, but not
   required to implement all (or any) of the unordered bcc operations.  */

int
can_compare_p (enum rtx_code code, enum machine_mode mode,
	       enum can_compare_purpose purpose)
{
  do
    {
      if (optab_handler (cmp_optab, mode)->insn_code != CODE_FOR_nothing)
	{
	  if (purpose == ccp_jump)
	    return bcc_gen_fctn[(int) code] != NULL;
	  else if (purpose == ccp_store_flag)
	    return setcc_gen_code[(int) code] != CODE_FOR_nothing;
	  else
	    /* There's only one cmov entry point, and it's allowed to fail.  */
	    return 1;
	}
      if (purpose == ccp_jump
	  && optab_handler (cbranch_optab, mode)->insn_code != CODE_FOR_nothing)
	return 1;
      if (purpose == ccp_cmov
	  && optab_handler (cmov_optab, mode)->insn_code != CODE_FOR_nothing)
	return 1;
      if (purpose == ccp_store_flag
	  && optab_handler (cstore_optab, mode)->insn_code != CODE_FOR_nothing)
	return 1;
      mode = GET_MODE_WIDER_MODE (mode);
    }
  while (mode != VOIDmode);

  return 0;
}

/* This function is called when we are going to emit a compare instruction that
   compares the values found in *PX and *PY, using the rtl operator COMPARISON.

   *PMODE is the mode of the inputs (in case they are const_int).
   *PUNSIGNEDP nonzero says that the operands are unsigned;
   this matters if they need to be widened.

   If they have mode BLKmode, then SIZE specifies the size of both operands.

   This function performs all the setup necessary so that the caller only has
   to emit a single comparison insn.  This setup can involve doing a BLKmode
   comparison or emitting a library call to perform the comparison if no insn
   is available to handle it.
   The values which are passed in through pointers can be modified; the caller
   should perform the comparison on the modified values.  Constant
   comparisons must have already been folded.  */

static void
prepare_cmp_insn (rtx *px, rtx *py, enum rtx_code *pcomparison, rtx size,
		  enum machine_mode *pmode, int *punsignedp,
		  enum can_compare_purpose purpose)
{
  enum machine_mode mode = *pmode;
  rtx x = *px, y = *py;
  int unsignedp = *punsignedp;
  rtx libfunc;

  /* If we are inside an appropriately-short loop and we are optimizing,
     force expensive constants into a register.  */
  if (CONSTANT_P (x) && optimize
      && rtx_cost (x, COMPARE) > COSTS_N_INSNS (1))
    x = force_reg (mode, x);

  if (CONSTANT_P (y) && optimize
      && rtx_cost (y, COMPARE) > COSTS_N_INSNS (1))
    y = force_reg (mode, y);

#ifdef HAVE_cc0
  /* Make sure if we have a canonical comparison.  The RTL
     documentation states that canonical comparisons are required only
     for targets which have cc0.  */
  gcc_assert (!CONSTANT_P (x) || CONSTANT_P (y));
#endif

  /* Don't let both operands fail to indicate the mode.  */
  if (GET_MODE (x) == VOIDmode && GET_MODE (y) == VOIDmode)
    x = force_reg (mode, x);

  /* Handle all BLKmode compares.  */

  if (mode == BLKmode)
    {
      enum machine_mode cmp_mode, result_mode;
      enum insn_code cmp_code;
      tree length_type;
      rtx libfunc;
      rtx result;
      rtx opalign
	= GEN_INT (MIN (MEM_ALIGN (x), MEM_ALIGN (y)) / BITS_PER_UNIT);

      gcc_assert (size);

      /* Try to use a memory block compare insn - either cmpstr
	 or cmpmem will do.  */
      for (cmp_mode = GET_CLASS_NARROWEST_MODE (MODE_INT);
	   cmp_mode != VOIDmode;
	   cmp_mode = GET_MODE_WIDER_MODE (cmp_mode))
	{
	  cmp_code = cmpmem_optab[cmp_mode];
	  if (cmp_code == CODE_FOR_nothing)
	    cmp_code = cmpstr_optab[cmp_mode];
	  if (cmp_code == CODE_FOR_nothing)
	    cmp_code = cmpstrn_optab[cmp_mode];
	  if (cmp_code == CODE_FOR_nothing)
	    continue;

	  /* Must make sure the size fits the insn's mode.  */
	  if ((GET_CODE (size) == CONST_INT
	       && INTVAL (size) >= (1 << GET_MODE_BITSIZE (cmp_mode)))
	      || (GET_MODE_BITSIZE (GET_MODE (size))
		  > GET_MODE_BITSIZE (cmp_mode)))
	    continue;

	  result_mode = insn_data[cmp_code].operand[0].mode;
	  result = gen_reg_rtx (result_mode);
	  size = convert_to_mode (cmp_mode, size, 1);
	  emit_insn (GEN_FCN (cmp_code) (result, x, y, size, opalign));

	  *px = result;
	  *py = const0_rtx;
	  *pmode = result_mode;
	  return;
	}

      /* Otherwise call a library function, memcmp.  */
      libfunc = memcmp_libfunc;
      length_type = sizetype;
      result_mode = TYPE_MODE (integer_type_node);
      cmp_mode = TYPE_MODE (length_type);
      size = convert_to_mode (TYPE_MODE (length_type), size,
			      TYPE_UNSIGNED (length_type));

      result = emit_library_call_value (libfunc, 0, LCT_PURE_MAKE_BLOCK,
					result_mode, 3,
					XEXP (x, 0), Pmode,
					XEXP (y, 0), Pmode,
					size, cmp_mode);
      *px = result;
      *py = const0_rtx;
      *pmode = result_mode;
      return;
    }

  /* Don't allow operands to the compare to trap, as that can put the
     compare and branch in different basic blocks.  */
  if (flag_non_call_exceptions)
    {
      if (may_trap_p (x))
	x = force_reg (mode, x);
      if (may_trap_p (y))
	y = force_reg (mode, y);
    }

  *px = x;
  *py = y;
  if (can_compare_p (*pcomparison, mode, purpose))
    return;

  /* Handle a lib call just for the mode we are using.  */

  libfunc = optab_libfunc (cmp_optab, mode);
  if (libfunc && !SCALAR_FLOAT_MODE_P (mode))
    {
      rtx result;

      /* If we want unsigned, and this mode has a distinct unsigned
	 comparison routine, use that.  */
      if (unsignedp)
	{
	  rtx ulibfunc = optab_libfunc (ucmp_optab, mode);
	  if (ulibfunc)
	    libfunc = ulibfunc;
	}

      result = emit_library_call_value (libfunc, NULL_RTX, LCT_CONST_MAKE_BLOCK,
					targetm.libgcc_cmp_return_mode (),
					2, x, mode, y, mode);

      /* There are two kinds of comparison routines. Biased routines
	 return 0/1/2, and unbiased routines return -1/0/1. Other parts
	 of gcc expect that the comparison operation is equivalent
	 to the modified comparison. For signed comparisons compare the 
	 result against 1 in the biased case, and zero in the unbiased
	 case. For unsigned comparisons always compare against 1 after
	 biasing the unbiased result by adding 1. This gives us a way to
	 represent LTU. */
      *px = result;
      *pmode = word_mode;
      *py = const1_rtx;

      if (!TARGET_LIB_INT_CMP_BIASED)
	{
	  if (*punsignedp)
	    *px = plus_constant (result, 1);  
	  else
	    *py = const0_rtx;
	}
      return;
    }

  gcc_assert (SCALAR_FLOAT_MODE_P (mode));
  prepare_float_lib_cmp (px, py, pcomparison, pmode, punsignedp);
}

/* Before emitting an insn with code ICODE, make sure that X, which is going
   to be used for operand OPNUM of the insn, is converted from mode MODE to
   WIDER_MODE (UNSIGNEDP determines whether it is an unsigned conversion), and
   that it is accepted by the operand predicate.  Return the new value.  */

static rtx
prepare_operand (int icode, rtx x, int opnum, enum machine_mode mode,
		 enum machine_mode wider_mode, int unsignedp)
{
  if (mode != wider_mode)
    x = convert_modes (wider_mode, mode, x, unsignedp);

  if (!insn_data[icode].operand[opnum].predicate
      (x, insn_data[icode].operand[opnum].mode))
    {
      if (reload_completed)
	return NULL_RTX;
      x = copy_to_mode_reg (insn_data[icode].operand[opnum].mode, x);
    }

  return x;
}

/* Subroutine of emit_cmp_and_jump_insns; this function is called when we know
   we can do the comparison.
   The arguments are the same as for emit_cmp_and_jump_insns; but LABEL may
   be NULL_RTX which indicates that only a comparison is to be generated.  */

static void
emit_cmp_and_jump_insn_1 (rtx x, rtx y, enum machine_mode mode,
			  enum rtx_code comparison, int unsignedp, rtx label)
{
  rtx test = gen_rtx_fmt_ee (comparison, mode, x, y);
  enum mode_class class = GET_MODE_CLASS (mode);
  enum machine_mode wider_mode = mode;

  /* Try combined insns first.  */
  do
    {
      enum insn_code icode;
      PUT_MODE (test, wider_mode);

      if (label)
	{
	  icode = optab_handler (cbranch_optab, wider_mode)->insn_code;

	  if (icode != CODE_FOR_nothing
	      && insn_data[icode].operand[0].predicate (test, wider_mode))
	    {
	      x = prepare_operand (icode, x, 1, mode, wider_mode, unsignedp);
	      y = prepare_operand (icode, y, 2, mode, wider_mode, unsignedp);
	      emit_jump_insn (GEN_FCN (icode) (test, x, y, label));
	      return;
	    }
	}

      /* Handle some compares against zero.  */
      icode = (int) optab_handler (tst_optab, wider_mode)->insn_code;
      if (y == CONST0_RTX (mode) && icode != CODE_FOR_nothing)
	{
	  x = prepare_operand (icode, x, 0, mode, wider_mode, unsignedp);
	  emit_insn (GEN_FCN (icode) (x));
	  if (label)
	    emit_jump_insn (bcc_gen_fctn[(int) comparison] (label));
	  return;
	}

      /* Handle compares for which there is a directly suitable insn.  */

      icode = (int) optab_handler (cmp_optab, wider_mode)->insn_code;
      if (icode != CODE_FOR_nothing)
	{
	  x = prepare_operand (icode, x, 0, mode, wider_mode, unsignedp);
	  y = prepare_operand (icode, y, 1, mode, wider_mode, unsignedp);
	  emit_insn (GEN_FCN (icode) (x, y));
	  if (label)
	    emit_jump_insn (bcc_gen_fctn[(int) comparison] (label));
	  return;
	}

      if (!CLASS_HAS_WIDER_MODES_P (class))
	break;

      wider_mode = GET_MODE_WIDER_MODE (wider_mode);
    }
  while (wider_mode != VOIDmode);

  gcc_unreachable ();
}

/* Generate code to compare X with Y so that the condition codes are
   set and to jump to LABEL if the condition is true.  If X is a
   constant and Y is not a constant, then the comparison is swapped to
   ensure that the comparison RTL has the canonical form.

   UNSIGNEDP nonzero says that X and Y are unsigned; this matters if they
   need to be widened by emit_cmp_insn.  UNSIGNEDP is also used to select
   the proper branch condition code.

   If X and Y have mode BLKmode, then SIZE specifies the size of both X and Y.

   MODE is the mode of the inputs (in case they are const_int).

   COMPARISON is the rtl operator to compare with (EQ, NE, GT, etc.).  It will
   be passed unchanged to emit_cmp_insn, then potentially converted into an
   unsigned variant based on UNSIGNEDP to select a proper jump instruction.  */

void
emit_cmp_and_jump_insns (rtx x, rtx y, enum rtx_code comparison, rtx size,
			 enum machine_mode mode, int unsignedp, rtx label)
{
  rtx op0 = x, op1 = y;

  /* Swap operands and condition to ensure canonical RTL.  */
  if (swap_commutative_operands_p (x, y))
    {
      /* If we're not emitting a branch, callers are required to pass
	 operands in an order conforming to canonical RTL.  We relax this
	 for commutative comparisons so callers using EQ don't need to do
	 swapping by hand.  */
      gcc_assert (label || (comparison == swap_condition (comparison)));

      op0 = y, op1 = x;
      comparison = swap_condition (comparison);
    }

#ifdef HAVE_cc0
  /* If OP0 is still a constant, then both X and Y must be constants.
     Force X into a register to create canonical RTL.  */
  if (CONSTANT_P (op0))
    op0 = force_reg (mode, op0);
#endif

  if (unsignedp)
    comparison = unsigned_condition (comparison);

  prepare_cmp_insn (&op0, &op1, &comparison, size, &mode, &unsignedp,
		    ccp_jump);
  emit_cmp_and_jump_insn_1 (op0, op1, mode, comparison, unsignedp, label);
}

/* Like emit_cmp_and_jump_insns, but generate only the comparison.  */

void
emit_cmp_insn (rtx x, rtx y, enum rtx_code comparison, rtx size,
	       enum machine_mode mode, int unsignedp)
{
  emit_cmp_and_jump_insns (x, y, comparison, size, mode, unsignedp, 0);
}

/* Emit a library call comparison between floating point X and Y.
   COMPARISON is the rtl operator to compare with (EQ, NE, GT, etc.).  */

static void
prepare_float_lib_cmp (rtx *px, rtx *py, enum rtx_code *pcomparison,
		       enum machine_mode *pmode, int *punsignedp)
{
  enum rtx_code comparison = *pcomparison;
  enum rtx_code swapped = swap_condition (comparison);
  enum rtx_code reversed = reverse_condition_maybe_unordered (comparison);
  rtx x = *px;
  rtx y = *py;
  enum machine_mode orig_mode = GET_MODE (x);
  enum machine_mode mode, cmp_mode;
  rtx value, target, insns, equiv;
  rtx libfunc = 0;
  bool reversed_p = false;
  cmp_mode = targetm.libgcc_cmp_return_mode ();

  for (mode = orig_mode;
       mode != VOIDmode;
       mode = GET_MODE_WIDER_MODE (mode))
    {
      if ((libfunc = optab_libfunc (code_to_optab[comparison], mode)))
	break;

      if ((libfunc = optab_libfunc (code_to_optab[swapped] , mode)))
	{
	  rtx tmp;
	  tmp = x; x = y; y = tmp;
	  comparison = swapped;
	  break;
	}

      if ((libfunc = optab_libfunc (code_to_optab[reversed], mode))
	  && FLOAT_LIB_COMPARE_RETURNS_BOOL (mode, reversed))
	{
	  comparison = reversed;
	  reversed_p = true;
	  break;
	}
    }

  gcc_assert (mode != VOIDmode);

  if (mode != orig_mode)
    {
      x = convert_to_mode (mode, x, 0);
      y = convert_to_mode (mode, y, 0);
    }

  /* Attach a REG_EQUAL note describing the semantics of the libcall to
     the RTL.  The allows the RTL optimizers to delete the libcall if the
     condition can be determined at compile-time.  */
  if (comparison == UNORDERED)
    {
      rtx temp = simplify_gen_relational (NE, cmp_mode, mode, x, x);
      equiv = simplify_gen_relational (NE, cmp_mode, mode, y, y);
      equiv = simplify_gen_ternary (IF_THEN_ELSE, cmp_mode, cmp_mode,
				    temp, const_true_rtx, equiv);
    }
  else
    {
      equiv = simplify_gen_relational (comparison, cmp_mode, mode, x, y);
      if (! FLOAT_LIB_COMPARE_RETURNS_BOOL (mode, comparison))
	{
	  rtx true_rtx, false_rtx;

	  switch (comparison)
	    {
	    case EQ:
	      true_rtx = const0_rtx;
	      false_rtx = const_true_rtx;
	      break;

	    case NE:
	      true_rtx = const_true_rtx;
	      false_rtx = const0_rtx;
	      break;

	    case GT:
	      true_rtx = const1_rtx;
	      false_rtx = const0_rtx;
	      break;

	    case GE:
	      true_rtx = const0_rtx;
	      false_rtx = constm1_rtx;
	      break;

	    case LT:
	      true_rtx = constm1_rtx;
	      false_rtx = const0_rtx;
	      break;

	    case LE:
	      true_rtx = const0_rtx;
	      false_rtx = const1_rtx;
	      break;

	    default:
	      gcc_unreachable ();
	    }
	  equiv = simplify_gen_ternary (IF_THEN_ELSE, cmp_mode, cmp_mode,
					equiv, true_rtx, false_rtx);
	}
    }

  start_sequence ();
  value = emit_library_call_value (libfunc, NULL_RTX, LCT_CONST,
				   cmp_mode, 2, x, mode, y, mode);
  insns = get_insns ();
  end_sequence ();

  target = gen_reg_rtx (cmp_mode);
  emit_libcall_block (insns, target, value, equiv);

  if (comparison == UNORDERED
      || FLOAT_LIB_COMPARE_RETURNS_BOOL (mode, comparison))
    comparison = reversed_p ? EQ : NE;

  *px = target;
  *py = const0_rtx;
  *pmode = cmp_mode;
  *pcomparison = comparison;
  *punsignedp = 0;
}

/* Generate code to indirectly jump to a location given in the rtx LOC.  */

void
emit_indirect_jump (rtx loc)
{
  if (!insn_data[(int) CODE_FOR_indirect_jump].operand[0].predicate
      (loc, Pmode))
    loc = copy_to_mode_reg (Pmode, loc);

  emit_jump_insn (gen_indirect_jump (loc));
  emit_barrier ();
}

#ifdef HAVE_conditional_move

/* Emit a conditional move instruction if the machine supports one for that
   condition and machine mode.

   OP0 and OP1 are the operands that should be compared using CODE.  CMODE is
   the mode to use should they be constants.  If it is VOIDmode, they cannot
   both be constants.

   OP2 should be stored in TARGET if the comparison is true, otherwise OP3
   should be stored there.  MODE is the mode to use should they be constants.
   If it is VOIDmode, they cannot both be constants.

   The result is either TARGET (perhaps modified) or NULL_RTX if the operation
   is not supported.  */

rtx
emit_conditional_move (rtx target, enum rtx_code code, rtx op0, rtx op1,
		       enum machine_mode cmode, rtx op2, rtx op3,
		       enum machine_mode mode, int unsignedp)
{
  rtx tem, subtarget, comparison, insn;
  enum insn_code icode;
  enum rtx_code reversed;

  /* If one operand is constant, make it the second one.  Only do this
     if the other operand is not constant as well.  */

  if (swap_commutative_operands_p (op0, op1))
    {
      tem = op0;
      op0 = op1;
      op1 = tem;
      code = swap_condition (code);
    }

  /* get_condition will prefer to generate LT and GT even if the old
     comparison was against zero, so undo that canonicalization here since
     comparisons against zero are cheaper.  */
  if (code == LT && op1 == const1_rtx)
    code = LE, op1 = const0_rtx;
  else if (code == GT && op1 == constm1_rtx)
    code = GE, op1 = const0_rtx;

  if (cmode == VOIDmode)
    cmode = GET_MODE (op0);

  if (swap_commutative_operands_p (op2, op3)
      && ((reversed = reversed_comparison_code_parts (code, op0, op1, NULL))
          != UNKNOWN))
    {
      tem = op2;
      op2 = op3;
      op3 = tem;
      code = reversed;
    }

  if (mode == VOIDmode)
    mode = GET_MODE (op2);

  icode = movcc_gen_code[mode];

  if (icode == CODE_FOR_nothing)
    return 0;

  if (!target)
    target = gen_reg_rtx (mode);

  subtarget = target;

  /* If the insn doesn't accept these operands, put them in pseudos.  */

  if (!insn_data[icode].operand[0].predicate
      (subtarget, insn_data[icode].operand[0].mode))
    subtarget = gen_reg_rtx (insn_data[icode].operand[0].mode);

  if (!insn_data[icode].operand[2].predicate
      (op2, insn_data[icode].operand[2].mode))
    op2 = copy_to_mode_reg (insn_data[icode].operand[2].mode, op2);

  if (!insn_data[icode].operand[3].predicate
      (op3, insn_data[icode].operand[3].mode))
    op3 = copy_to_mode_reg (insn_data[icode].operand[3].mode, op3);

  /* Everything should now be in the suitable form, so emit the compare insn
     and then the conditional move.  */

  comparison
    = compare_from_rtx (op0, op1, code, unsignedp, cmode, NULL_RTX);

  /* ??? Watch for const0_rtx (nop) and const_true_rtx (unconditional)?  */
  /* We can get const0_rtx or const_true_rtx in some circumstances.  Just
     return NULL and let the caller figure out how best to deal with this
     situation.  */
  if (GET_CODE (comparison) != code)
    return NULL_RTX;

  insn = GEN_FCN (icode) (subtarget, comparison, op2, op3);

  /* If that failed, then give up.  */
  if (insn == 0)
    return 0;

  emit_insn (insn);

  if (subtarget != target)
    convert_move (target, subtarget, 0);

  return target;
}

/* Return nonzero if a conditional move of mode MODE is supported.

   This function is for combine so it can tell whether an insn that looks
   like a conditional move is actually supported by the hardware.  If we
   guess wrong we lose a bit on optimization, but that's it.  */
/* ??? sparc64 supports conditionally moving integers values based on fp
   comparisons, and vice versa.  How do we handle them?  */

int
can_conditionally_move_p (enum machine_mode mode)
{
  if (movcc_gen_code[mode] != CODE_FOR_nothing)
    return 1;

  return 0;
}

#endif /* HAVE_conditional_move */

/* Emit a conditional addition instruction if the machine supports one for that
   condition and machine mode.

   OP0 and OP1 are the operands that should be compared using CODE.  CMODE is
   the mode to use should they be constants.  If it is VOIDmode, they cannot
   both be constants.

   OP2 should be stored in TARGET if the comparison is true, otherwise OP2+OP3
   should be stored there.  MODE is the mode to use should they be constants.
   If it is VOIDmode, they cannot both be constants.

   The result is either TARGET (perhaps modified) or NULL_RTX if the operation
   is not supported.  */

rtx
emit_conditional_add (rtx target, enum rtx_code code, rtx op0, rtx op1,
		      enum machine_mode cmode, rtx op2, rtx op3,
		      enum machine_mode mode, int unsignedp)
{
  rtx tem, subtarget, comparison, insn;
  enum insn_code icode;
  enum rtx_code reversed;

  /* If one operand is constant, make it the second one.  Only do this
     if the other operand is not constant as well.  */

  if (swap_commutative_operands_p (op0, op1))
    {
      tem = op0;
      op0 = op1;
      op1 = tem;
      code = swap_condition (code);
    }

  /* get_condition will prefer to generate LT and GT even if the old
     comparison was against zero, so undo that canonicalization here since
     comparisons against zero are cheaper.  */
  if (code == LT && op1 == const1_rtx)
    code = LE, op1 = const0_rtx;
  else if (code == GT && op1 == constm1_rtx)
    code = GE, op1 = const0_rtx;

  if (cmode == VOIDmode)
    cmode = GET_MODE (op0);

  if (swap_commutative_operands_p (op2, op3)
      && ((reversed = reversed_comparison_code_parts (code, op0, op1, NULL))
          != UNKNOWN))
    {
      tem = op2;
      op2 = op3;
      op3 = tem;
      code = reversed;
    }

  if (mode == VOIDmode)
    mode = GET_MODE (op2);

  icode = optab_handler (addcc_optab, mode)->insn_code;

  if (icode == CODE_FOR_nothing)
    return 0;

  if (!target)
    target = gen_reg_rtx (mode);

  /* If the insn doesn't accept these operands, put them in pseudos.  */

  if (!insn_data[icode].operand[0].predicate
      (target, insn_data[icode].operand[0].mode))
    subtarget = gen_reg_rtx (insn_data[icode].operand[0].mode);
  else
    subtarget = target;

  if (!insn_data[icode].operand[2].predicate
      (op2, insn_data[icode].operand[2].mode))
    op2 = copy_to_mode_reg (insn_data[icode].operand[2].mode, op2);

  if (!insn_data[icode].operand[3].predicate
      (op3, insn_data[icode].operand[3].mode))
    op3 = copy_to_mode_reg (insn_data[icode].operand[3].mode, op3);

  /* Everything should now be in the suitable form, so emit the compare insn
     and then the conditional move.  */

  comparison
    = compare_from_rtx (op0, op1, code, unsignedp, cmode, NULL_RTX);

  /* ??? Watch for const0_rtx (nop) and const_true_rtx (unconditional)?  */
  /* We can get const0_rtx or const_true_rtx in some circumstances.  Just
     return NULL and let the caller figure out how best to deal with this
     situation.  */
  if (GET_CODE (comparison) != code)
    return NULL_RTX;

  insn = GEN_FCN (icode) (subtarget, comparison, op2, op3);

  /* If that failed, then give up.  */
  if (insn == 0)
    return 0;

  emit_insn (insn);

  if (subtarget != target)
    convert_move (target, subtarget, 0);

  return target;
}

/* These functions attempt to generate an insn body, rather than
   emitting the insn, but if the gen function already emits them, we
   make no attempt to turn them back into naked patterns.  */

/* Generate and return an insn body to add Y to X.  */

rtx
gen_add2_insn (rtx x, rtx y)
{
  int icode = (int) optab_handler (add_optab, GET_MODE (x))->insn_code;

  gcc_assert (insn_data[icode].operand[0].predicate
	      (x, insn_data[icode].operand[0].mode));
  gcc_assert (insn_data[icode].operand[1].predicate
	      (x, insn_data[icode].operand[1].mode));
  gcc_assert (insn_data[icode].operand[2].predicate
	      (y, insn_data[icode].operand[2].mode));

  return GEN_FCN (icode) (x, x, y);
}

/* Generate and return an insn body to add r1 and c,
   storing the result in r0.  */

rtx
gen_add3_insn (rtx r0, rtx r1, rtx c)
{
  int icode = (int) optab_handler (add_optab, GET_MODE (r0))->insn_code;

  if (icode == CODE_FOR_nothing
      || !(insn_data[icode].operand[0].predicate
	   (r0, insn_data[icode].operand[0].mode))
      || !(insn_data[icode].operand[1].predicate
	   (r1, insn_data[icode].operand[1].mode))
      || !(insn_data[icode].operand[2].predicate
	   (c, insn_data[icode].operand[2].mode)))
    return NULL_RTX;

  return GEN_FCN (icode) (r0, r1, c);
}

int
have_add2_insn (rtx x, rtx y)
{
  int icode;

  gcc_assert (GET_MODE (x) != VOIDmode);

  icode = (int) optab_handler (add_optab, GET_MODE (x))->insn_code;

  if (icode == CODE_FOR_nothing)
    return 0;

  if (!(insn_data[icode].operand[0].predicate
	(x, insn_data[icode].operand[0].mode))
      || !(insn_data[icode].operand[1].predicate
	   (x, insn_data[icode].operand[1].mode))
      || !(insn_data[icode].operand[2].predicate
	   (y, insn_data[icode].operand[2].mode)))
    return 0;

  return 1;
}

/* Generate and return an insn body to subtract Y from X.  */

rtx
gen_sub2_insn (rtx x, rtx y)
{
  int icode = (int) optab_handler (sub_optab, GET_MODE (x))->insn_code;

  gcc_assert (insn_data[icode].operand[0].predicate
	      (x, insn_data[icode].operand[0].mode));
  gcc_assert (insn_data[icode].operand[1].predicate
	      (x, insn_data[icode].operand[1].mode));
  gcc_assert  (insn_data[icode].operand[2].predicate
	       (y, insn_data[icode].operand[2].mode));

  return GEN_FCN (icode) (x, x, y);
}

/* Generate and return an insn body to subtract r1 and c,
   storing the result in r0.  */

rtx
gen_sub3_insn (rtx r0, rtx r1, rtx c)
{
  int icode = (int) optab_handler (sub_optab, GET_MODE (r0))->insn_code;

  if (icode == CODE_FOR_nothing
      || !(insn_data[icode].operand[0].predicate
	   (r0, insn_data[icode].operand[0].mode))
      || !(insn_data[icode].operand[1].predicate
	   (r1, insn_data[icode].operand[1].mode))
      || !(insn_data[icode].operand[2].predicate
	   (c, insn_data[icode].operand[2].mode)))
    return NULL_RTX;

  return GEN_FCN (icode) (r0, r1, c);
}

int
have_sub2_insn (rtx x, rtx y)
{
  int icode;

  gcc_assert (GET_MODE (x) != VOIDmode);

  icode = (int) optab_handler (sub_optab, GET_MODE (x))->insn_code;

  if (icode == CODE_FOR_nothing)
    return 0;

  if (!(insn_data[icode].operand[0].predicate
	(x, insn_data[icode].operand[0].mode))
      || !(insn_data[icode].operand[1].predicate
	   (x, insn_data[icode].operand[1].mode))
      || !(insn_data[icode].operand[2].predicate
	   (y, insn_data[icode].operand[2].mode)))
    return 0;

  return 1;
}

/* Generate the body of an instruction to copy Y into X.
   It may be a list of insns, if one insn isn't enough.  */

rtx
gen_move_insn (rtx x, rtx y)
{
  rtx seq;

  start_sequence ();
  emit_move_insn_1 (x, y);
  seq = get_insns ();
  end_sequence ();
  return seq;
}

/* Return the insn code used to extend FROM_MODE to TO_MODE.
   UNSIGNEDP specifies zero-extension instead of sign-extension.  If
   no such operation exists, CODE_FOR_nothing will be returned.  */

enum insn_code
can_extend_p (enum machine_mode to_mode, enum machine_mode from_mode,
	      int unsignedp)
{
  convert_optab tab;
#ifdef HAVE_ptr_extend
  if (unsignedp < 0)
    return CODE_FOR_ptr_extend;
#endif

  tab = unsignedp ? zext_optab : sext_optab;
  return convert_optab_handler (tab, to_mode, from_mode)->insn_code;
}

/* Generate the body of an insn to extend Y (with mode MFROM)
   into X (with mode MTO).  Do zero-extension if UNSIGNEDP is nonzero.  */

rtx
gen_extend_insn (rtx x, rtx y, enum machine_mode mto,
		 enum machine_mode mfrom, int unsignedp)
{
  enum insn_code icode = can_extend_p (mto, mfrom, unsignedp);
  return GEN_FCN (icode) (x, y);
}

/* can_fix_p and can_float_p say whether the target machine
   can directly convert a given fixed point type to
   a given floating point type, or vice versa.
   The returned value is the CODE_FOR_... value to use,
   or CODE_FOR_nothing if these modes cannot be directly converted.

   *TRUNCP_PTR is set to 1 if it is necessary to output
   an explicit FTRUNC insn before the fix insn; otherwise 0.  */

static enum insn_code
can_fix_p (enum machine_mode fixmode, enum machine_mode fltmode,
	   int unsignedp, int *truncp_ptr)
{
  convert_optab tab;
  enum insn_code icode;

  tab = unsignedp ? ufixtrunc_optab : sfixtrunc_optab;
  icode = convert_optab_handler (tab, fixmode, fltmode)->insn_code;
  if (icode != CODE_FOR_nothing)
    {
      *truncp_ptr = 0;
      return icode;
    }

  /* FIXME: This requires a port to define both FIX and FTRUNC pattern
     for this to work. We need to rework the fix* and ftrunc* patterns
     and documentation.  */
  tab = unsignedp ? ufix_optab : sfix_optab;
  icode = convert_optab_handler (tab, fixmode, fltmode)->insn_code;
  if (icode != CODE_FOR_nothing
      && optab_handler (ftrunc_optab, fltmode)->insn_code != CODE_FOR_nothing)
    {
      *truncp_ptr = 1;
      return icode;
    }

  *truncp_ptr = 0;
  return CODE_FOR_nothing;
}

static enum insn_code
can_float_p (enum machine_mode fltmode, enum machine_mode fixmode,
	     int unsignedp)
{
  convert_optab tab;

  tab = unsignedp ? ufloat_optab : sfloat_optab;
  return convert_optab_handler (tab, fltmode, fixmode)->insn_code;
}

/* Generate code to convert FROM to floating point
   and store in TO.  FROM must be fixed point and not VOIDmode.
   UNSIGNEDP nonzero means regard FROM as unsigned.
   Normally this is done by correcting the final value
   if it is negative.  */

void
expand_float (rtx to, rtx from, int unsignedp)
{
  enum insn_code icode;
  rtx target = to;
  enum machine_mode fmode, imode;
  bool can_do_signed = false;

  /* Crash now, because we won't be able to decide which mode to use.  */
  gcc_assert (GET_MODE (from) != VOIDmode);

  /* Look for an insn to do the conversion.  Do it in the specified
     modes if possible; otherwise convert either input, output or both to
     wider mode.  If the integer mode is wider than the mode of FROM,
     we can do the conversion signed even if the input is unsigned.  */

  for (fmode = GET_MODE (to); fmode != VOIDmode;
       fmode = GET_MODE_WIDER_MODE (fmode))
    for (imode = GET_MODE (from); imode != VOIDmode;
	 imode = GET_MODE_WIDER_MODE (imode))
      {
	int doing_unsigned = unsignedp;

	if (fmode != GET_MODE (to)
	    && significand_size (fmode) < GET_MODE_BITSIZE (GET_MODE (from)))
	  continue;

	icode = can_float_p (fmode, imode, unsignedp);
	if (icode == CODE_FOR_nothing && unsignedp)
	  {
	    enum insn_code scode = can_float_p (fmode, imode, 0);
	    if (scode != CODE_FOR_nothing)
	      can_do_signed = true;
	    if (imode != GET_MODE (from))
	      icode = scode, doing_unsigned = 0;
	  }

	if (icode != CODE_FOR_nothing)
	  {
	    if (imode != GET_MODE (from))
	      from = convert_to_mode (imode, from, unsignedp);

	    if (fmode != GET_MODE (to))
	      target = gen_reg_rtx (fmode);

	    emit_unop_insn (icode, target, from,
			    doing_unsigned ? UNSIGNED_FLOAT : FLOAT);

	    if (target != to)
	      convert_move (to, target, 0);
	    return;
	  }
      }

  /* Unsigned integer, and no way to convert directly.  Convert as signed,
     then unconditionally adjust the result.  */
  if (unsignedp && can_do_signed)
    {
      rtx label = gen_label_rtx ();
      rtx temp;
      REAL_VALUE_TYPE offset;

      /* Look for a usable floating mode FMODE wider than the source and at
	 least as wide as the target.  Using FMODE will avoid rounding woes
	 with unsigned values greater than the signed maximum value.  */

      for (fmode = GET_MODE (to);  fmode != VOIDmode;
	   fmode = GET_MODE_WIDER_MODE (fmode))
	if (GET_MODE_BITSIZE (GET_MODE (from)) < GET_MODE_BITSIZE (fmode)
	    && can_float_p (fmode, GET_MODE (from), 0) != CODE_FOR_nothing)
	  break;

      if (fmode == VOIDmode)
	{
	  /* There is no such mode.  Pretend the target is wide enough.  */
	  fmode = GET_MODE (to);

	  /* Avoid double-rounding when TO is narrower than FROM.  */
	  if ((significand_size (fmode) + 1)
	      < GET_MODE_BITSIZE (GET_MODE (from)))
	    {
	      rtx temp1;
	      rtx neglabel = gen_label_rtx ();

	      /* Don't use TARGET if it isn't a register, is a hard register,
		 or is the wrong mode.  */
	      if (!REG_P (target)
		  || REGNO (target) < FIRST_PSEUDO_REGISTER
		  || GET_MODE (target) != fmode)
		target = gen_reg_rtx (fmode);

	      imode = GET_MODE (from);
	      do_pending_stack_adjust ();

	      /* Test whether the sign bit is set.  */
	      emit_cmp_and_jump_insns (from, const0_rtx, LT, NULL_RTX, imode,
				       0, neglabel);

	      /* The sign bit is not set.  Convert as signed.  */
	      expand_float (target, from, 0);
	      emit_jump_insn (gen_jump (label));
	      emit_barrier ();

	      /* The sign bit is set.
		 Convert to a usable (positive signed) value by shifting right
		 one bit, while remembering if a nonzero bit was shifted
		 out; i.e., compute  (from & 1) | (from >> 1).  */

	      emit_label (neglabel);
	      temp = expand_binop (imode, and_optab, from, const1_rtx,
				   NULL_RTX, 1, OPTAB_LIB_WIDEN);
	      temp1 = expand_shift (RSHIFT_EXPR, imode, from, integer_one_node,
				    NULL_RTX, 1);
	      temp = expand_binop (imode, ior_optab, temp, temp1, temp, 1,
				   OPTAB_LIB_WIDEN);
	      expand_float (target, temp, 0);

	      /* Multiply by 2 to undo the shift above.  */
	      temp = expand_binop (fmode, add_optab, target, target,
				   target, 0, OPTAB_LIB_WIDEN);
	      if (temp != target)
		emit_move_insn (target, temp);

	      do_pending_stack_adjust ();
	      emit_label (label);
	      goto done;
	    }
	}

      /* If we are about to do some arithmetic to correct for an
	 unsigned operand, do it in a pseudo-register.  */

      if (GET_MODE (to) != fmode
	  || !REG_P (to) || REGNO (to) < FIRST_PSEUDO_REGISTER)
	target = gen_reg_rtx (fmode);

      /* Convert as signed integer to floating.  */
      expand_float (target, from, 0);

      /* If FROM is negative (and therefore TO is negative),
	 correct its value by 2**bitwidth.  */

      do_pending_stack_adjust ();
      emit_cmp_and_jump_insns (from, const0_rtx, GE, NULL_RTX, GET_MODE (from),
			       0, label);


      real_2expN (&offset, GET_MODE_BITSIZE (GET_MODE (from)), fmode);
      temp = expand_binop (fmode, add_optab, target,
			   CONST_DOUBLE_FROM_REAL_VALUE (offset, fmode),
			   target, 0, OPTAB_LIB_WIDEN);
      if (temp != target)
	emit_move_insn (target, temp);

      do_pending_stack_adjust ();
      emit_label (label);
      goto done;
    }

  /* No hardware instruction available; call a library routine.  */
    {
      rtx libfunc;
      rtx insns;
      rtx value;
      convert_optab tab = unsignedp ? ufloat_optab : sfloat_optab;

      if (GET_MODE_SIZE (GET_MODE (from)) < GET_MODE_SIZE (SImode))
	from = convert_to_mode (SImode, from, unsignedp);

      libfunc = convert_optab_libfunc (tab, GET_MODE (to), GET_MODE (from));
      gcc_assert (libfunc);

      start_sequence ();

      value = emit_library_call_value (libfunc, NULL_RTX, LCT_CONST,
				       GET_MODE (to), 1, from,
				       GET_MODE (from));
      insns = get_insns ();
      end_sequence ();

      emit_libcall_block (insns, target, value,
			  gen_rtx_FLOAT (GET_MODE (to), from));
    }

 done:

  /* Copy result to requested destination
     if we have been computing in a temp location.  */

  if (target != to)
    {
      if (GET_MODE (target) == GET_MODE (to))
	emit_move_insn (to, target);
      else
	convert_move (to, target, 0);
    }
}

/* Generate code to convert FROM to fixed point and store in TO.  FROM
   must be floating point.  */

void
expand_fix (rtx to, rtx from, int unsignedp)
{
  enum insn_code icode;
  rtx target = to;
  enum machine_mode fmode, imode;
  int must_trunc = 0;

  /* We first try to find a pair of modes, one real and one integer, at
     least as wide as FROM and TO, respectively, in which we can open-code
     this conversion.  If the integer mode is wider than the mode of TO,
     we can do the conversion either signed or unsigned.  */

  for (fmode = GET_MODE (from); fmode != VOIDmode;
       fmode = GET_MODE_WIDER_MODE (fmode))
    for (imode = GET_MODE (to); imode != VOIDmode;
	 imode = GET_MODE_WIDER_MODE (imode))
      {
	int doing_unsigned = unsignedp;

	icode = can_fix_p (imode, fmode, unsignedp, &must_trunc);
	if (icode == CODE_FOR_nothing && imode != GET_MODE (to) && unsignedp)
	  icode = can_fix_p (imode, fmode, 0, &must_trunc), doing_unsigned = 0;

	if (icode != CODE_FOR_nothing)
	  {
	    if (fmode != GET_MODE (from))
	      from = convert_to_mode (fmode, from, 0);

	    if (must_trunc)
	      {
		rtx temp = gen_reg_rtx (GET_MODE (from));
		from = expand_unop (GET_MODE (from), ftrunc_optab, from,
				    temp, 0);
	      }

	    if (imode != GET_MODE (to))
	      target = gen_reg_rtx (imode);

	    emit_unop_insn (icode, target, from,
			    doing_unsigned ? UNSIGNED_FIX : FIX);
	    if (target != to)
	      convert_move (to, target, unsignedp);
	    return;
	  }
      }

  /* For an unsigned conversion, there is one more way to do it.
     If we have a signed conversion, we generate code that compares
     the real value to the largest representable positive number.  If if
     is smaller, the conversion is done normally.  Otherwise, subtract
     one plus the highest signed number, convert, and add it back.

     We only need to check all real modes, since we know we didn't find
     anything with a wider integer mode.

     This code used to extend FP value into mode wider than the destination.
     This is needed for decimal float modes which cannot accurately
     represent one plus the highest signed number of the same size, but
     not for binary modes.  Consider, for instance conversion from SFmode
     into DImode.

     The hot path through the code is dealing with inputs smaller than 2^63
     and doing just the conversion, so there is no bits to lose.

     In the other path we know the value is positive in the range 2^63..2^64-1
     inclusive.  (as for other input overflow happens and result is undefined)
     So we know that the most important bit set in mantissa corresponds to
     2^63.  The subtraction of 2^63 should not generate any rounding as it
     simply clears out that bit.  The rest is trivial.  */

  if (unsignedp && GET_MODE_BITSIZE (GET_MODE (to)) <= HOST_BITS_PER_WIDE_INT)
    for (fmode = GET_MODE (from); fmode != VOIDmode;
	 fmode = GET_MODE_WIDER_MODE (fmode))
      if (CODE_FOR_nothing != can_fix_p (GET_MODE (to), fmode, 0, &must_trunc)
	  && (!DECIMAL_FLOAT_MODE_P (fmode)
	      || GET_MODE_BITSIZE (fmode) > GET_MODE_BITSIZE (GET_MODE (to))))
	{
	  int bitsize;
	  REAL_VALUE_TYPE offset;
	  rtx limit, lab1, lab2, insn;

	  bitsize = GET_MODE_BITSIZE (GET_MODE (to));
	  real_2expN (&offset, bitsize - 1, fmode);
	  limit = CONST_DOUBLE_FROM_REAL_VALUE (offset, fmode);
	  lab1 = gen_label_rtx ();
	  lab2 = gen_label_rtx ();

	  if (fmode != GET_MODE (from))
	    from = convert_to_mode (fmode, from, 0);

	  /* See if we need to do the subtraction.  */
	  do_pending_stack_adjust ();
	  emit_cmp_and_jump_insns (from, limit, GE, NULL_RTX, GET_MODE (from),
				   0, lab1);

	  /* If not, do the signed "fix" and branch around fixup code.  */
	  expand_fix (to, from, 0);
	  emit_jump_insn (gen_jump (lab2));
	  emit_barrier ();

	  /* Otherwise, subtract 2**(N-1), convert to signed number,
	     then add 2**(N-1).  Do the addition using XOR since this
	     will often generate better code.  */
	  emit_label (lab1);
	  target = expand_binop (GET_MODE (from), sub_optab, from, limit,
				 NULL_RTX, 0, OPTAB_LIB_WIDEN);
	  expand_fix (to, target, 0);
	  target = expand_binop (GET_MODE (to), xor_optab, to,
				 gen_int_mode
				 ((HOST_WIDE_INT) 1 << (bitsize - 1),
				  GET_MODE (to)),
				 to, 1, OPTAB_LIB_WIDEN);

	  if (target != to)
	    emit_move_insn (to, target);

	  emit_label (lab2);

	  if (optab_handler (mov_optab, GET_MODE (to))->insn_code
	      != CODE_FOR_nothing)
	    {
	      /* Make a place for a REG_NOTE and add it.  */
	      insn = emit_move_insn (to, to);
	      set_unique_reg_note (insn,
	                           REG_EQUAL,
				   gen_rtx_fmt_e (UNSIGNED_FIX,
						  GET_MODE (to),
						  copy_rtx (from)));
	    }

	  return;
	}

  /* We can't do it with an insn, so use a library call.  But first ensure
     that the mode of TO is at least as wide as SImode, since those are the
     only library calls we know about.  */

  if (GET_MODE_SIZE (GET_MODE (to)) < GET_MODE_SIZE (SImode))
    {
      target = gen_reg_rtx (SImode);

      expand_fix (target, from, unsignedp);
    }
  else
    {
      rtx insns;
      rtx value;
      rtx libfunc;

      convert_optab tab = unsignedp ? ufix_optab : sfix_optab;
      libfunc = convert_optab_libfunc (tab, GET_MODE (to), GET_MODE (from));
      gcc_assert (libfunc);

      start_sequence ();

      value = emit_library_call_value (libfunc, NULL_RTX, LCT_CONST,
				       GET_MODE (to), 1, from,
				       GET_MODE (from));
      insns = get_insns ();
      end_sequence ();

      emit_libcall_block (insns, target, value,
			  gen_rtx_fmt_e (unsignedp ? UNSIGNED_FIX : FIX,
					 GET_MODE (to), from));
    }

  if (target != to)
    {
      if (GET_MODE (to) == GET_MODE (target))
        emit_move_insn (to, target);
      else
        convert_move (to, target, 0);
    }
}

/* Generate code to convert FROM or TO a fixed-point.
   If UINTP is true, either TO or FROM is an unsigned integer.
   If SATP is true, we need to saturate the result.  */

void
expand_fixed_convert (rtx to, rtx from, int uintp, int satp)
{
  enum machine_mode to_mode = GET_MODE (to);
  enum machine_mode from_mode = GET_MODE (from);
  convert_optab tab;
  enum rtx_code this_code;
  enum insn_code code;
  rtx insns, value;
  rtx libfunc;

  if (to_mode == from_mode)
    {
      emit_move_insn (to, from);
      return;
    }

  if (uintp)
    {
      tab = satp ? satfractuns_optab : fractuns_optab;
      this_code = satp ? UNSIGNED_SAT_FRACT : UNSIGNED_FRACT_CONVERT;
    }
  else
    {
      tab = satp ? satfract_optab : fract_optab;
      this_code = satp ? SAT_FRACT : FRACT_CONVERT;
    }
  code = tab->handlers[to_mode][from_mode].insn_code;
  if (code != CODE_FOR_nothing)
    {
      emit_unop_insn (code, to, from, this_code);
      return;
    }

  libfunc = convert_optab_libfunc (tab, to_mode, from_mode);
  gcc_assert (libfunc);

  start_sequence ();
  value = emit_library_call_value (libfunc, NULL_RTX, LCT_CONST, to_mode,
				   1, from, from_mode);
  insns = get_insns ();
  end_sequence ();

  emit_libcall_block (insns, to, value,
		      gen_rtx_fmt_e (tab->code, to_mode, from));
}

/* Generate code to convert FROM to fixed point and store in TO.  FROM
   must be floating point, TO must be signed.  Use the conversion optab
   TAB to do the conversion.  */

bool
expand_sfix_optab (rtx to, rtx from, convert_optab tab)
{
  enum insn_code icode;
  rtx target = to;
  enum machine_mode fmode, imode;

  /* We first try to find a pair of modes, one real and one integer, at
     least as wide as FROM and TO, respectively, in which we can open-code
     this conversion.  If the integer mode is wider than the mode of TO,
     we can do the conversion either signed or unsigned.  */

  for (fmode = GET_MODE (from); fmode != VOIDmode;
       fmode = GET_MODE_WIDER_MODE (fmode))
    for (imode = GET_MODE (to); imode != VOIDmode;
	 imode = GET_MODE_WIDER_MODE (imode))
      {
	icode = convert_optab_handler (tab, imode, fmode)->insn_code;
	if (icode != CODE_FOR_nothing)
	  {
	    if (fmode != GET_MODE (from))
	      from = convert_to_mode (fmode, from, 0);

	    if (imode != GET_MODE (to))
	      target = gen_reg_rtx (imode);

	    emit_unop_insn (icode, target, from, UNKNOWN);
	    if (target != to)
	      convert_move (to, target, 0);
	    return true;
	  }
      }

  return false;
}

/* Report whether we have an instruction to perform the operation
   specified by CODE on operands of mode MODE.  */
int
have_insn_for (enum rtx_code code, enum machine_mode mode)
{
  return (code_to_optab[(int) code] != 0
	  && (optab_handler (code_to_optab[(int) code], mode)->insn_code
	      != CODE_FOR_nothing));
}

/* Set all insn_code fields to CODE_FOR_nothing.  */

static void
init_insn_codes (void)
{
  unsigned int i;

  for (i = 0; i < (unsigned int) OTI_MAX; i++)
    {
      unsigned int j;
      optab op;

      op = &optab_table[i];
      for (j = 0; j < NUM_MACHINE_MODES; j++)
	optab_handler (op, j)->insn_code = CODE_FOR_nothing;
    }
  for (i = 0; i < (unsigned int) COI_MAX; i++)
    {
      unsigned int j, k;
      convert_optab op;

      op = &convert_optab_table[i];
      for (j = 0; j < NUM_MACHINE_MODES; j++)
	for (k = 0; k < NUM_MACHINE_MODES; k++)
	  convert_optab_handler (op, j, k)->insn_code = CODE_FOR_nothing;
    }
}

/* Initialize OP's code to CODE, and write it into the code_to_optab table.  */
static inline void
init_optab (optab op, enum rtx_code code)
{
  op->code = code;
  code_to_optab[(int) code] = op;
}

/* Same, but fill in its code as CODE, and do _not_ write it into
   the code_to_optab table.  */
static inline void
init_optabv (optab op, enum rtx_code code)
{
  op->code = code;
}

/* Conversion optabs never go in the code_to_optab table.  */
static void
init_convert_optab (convert_optab op, enum rtx_code code)
{
  op->code = code;
}

/* Initialize the libfunc fields of an entire group of entries in some
   optab.  Each entry is set equal to a string consisting of a leading
   pair of underscores followed by a generic operation name followed by
   a mode name (downshifted to lowercase) followed by a single character
   representing the number of operands for the given operation (which is
   usually one of the characters '2', '3', or '4').

   OPTABLE is the table in which libfunc fields are to be initialized.
   OPNAME is the generic (string) name of the operation.
   SUFFIX is the character which specifies the number of operands for
     the given generic operation.
   MODE is the mode to generate for.
*/

static void
gen_libfunc (optab optable, const char *opname, int suffix, enum machine_mode mode)
{
  unsigned opname_len = strlen (opname);
  const char *mname = GET_MODE_NAME (mode);
  unsigned mname_len = strlen (mname);
  char *libfunc_name = alloca (2 + opname_len + mname_len + 1 + 1);
  char *p;
  const char *q;

  p = libfunc_name;
  *p++ = '_';
  *p++ = '_';
  for (q = opname; *q; )
    *p++ = *q++;
  for (q = mname; *q; q++)
    *p++ = TOLOWER (*q);
  *p++ = suffix;
  *p = '\0';

  set_optab_libfunc (optable, mode,
		     ggc_alloc_string (libfunc_name, p - libfunc_name));
}

/* Like gen_libfunc, but verify that integer operation is involved.  */

static void
gen_int_libfunc (optab optable, const char *opname, char suffix,
		 enum machine_mode mode)
{
  int maxsize = 2 * BITS_PER_WORD;

  if (GET_MODE_CLASS (mode) != MODE_INT)
    return;
  if (maxsize < LONG_LONG_TYPE_SIZE)
    maxsize = LONG_LONG_TYPE_SIZE;
  if (GET_MODE_CLASS (mode) != MODE_INT
      || mode < word_mode || GET_MODE_BITSIZE (mode) > maxsize)
    return;
  gen_libfunc (optable, opname, suffix, mode);
}

/* Like gen_libfunc, but verify that FP and set decimal prefix if needed.  */

static void
gen_fp_libfunc (optab optable, const char *opname, char suffix,
		enum machine_mode mode)
{
  char *dec_opname;

  if (GET_MODE_CLASS (mode) == MODE_FLOAT)
    gen_libfunc (optable, opname, suffix, mode);
  if (DECIMAL_FLOAT_MODE_P (mode))
    {
      dec_opname = alloca (sizeof (DECIMAL_PREFIX) + strlen (opname));
      /* For BID support, change the name to have either a bid_ or dpd_ prefix
	 depending on the low level floating format used.  */
      memcpy (dec_opname, DECIMAL_PREFIX, sizeof (DECIMAL_PREFIX) - 1);
      strcpy (dec_opname + sizeof (DECIMAL_PREFIX) - 1, opname);
      gen_libfunc (optable, dec_opname, suffix, mode);
    }
}

/* Like gen_libfunc, but verify that fixed-point operation is involved.  */

static void
gen_fixed_libfunc (optab optable, const char *opname, char suffix,
		   enum machine_mode mode)
{
  if (!ALL_FIXED_POINT_MODE_P (mode))
    return;
  gen_libfunc (optable, opname, suffix, mode);
}

/* Like gen_libfunc, but verify that signed fixed-point operation is
   involved.  */

static void
gen_signed_fixed_libfunc (optab optable, const char *opname, char suffix,
			  enum machine_mode mode)
{
  if (!SIGNED_FIXED_POINT_MODE_P (mode))
    return;
  gen_libfunc (optable, opname, suffix, mode);
}

/* Like gen_libfunc, but verify that unsigned fixed-point operation is
   involved.  */

static void
gen_unsigned_fixed_libfunc (optab optable, const char *opname, char suffix,
			    enum machine_mode mode)
{
  if (!UNSIGNED_FIXED_POINT_MODE_P (mode))
    return;
  gen_libfunc (optable, opname, suffix, mode);
}

/* Like gen_libfunc, but verify that FP or INT operation is involved.  */

static void
gen_int_fp_libfunc (optab optable, const char *name, char suffix,
		    enum machine_mode mode)
{
  if (DECIMAL_FLOAT_MODE_P (mode) || GET_MODE_CLASS (mode) == MODE_FLOAT)
    gen_fp_libfunc (optable, name, suffix, mode);
  if (INTEGRAL_MODE_P (mode))
    gen_int_libfunc (optable, name, suffix, mode);
}

/* Like gen_libfunc, but verify that FP or INT operation is involved
   and add 'v' suffix for integer operation.  */

static void
gen_intv_fp_libfunc (optab optable, const char *name, char suffix,
		     enum machine_mode mode)
{
  if (DECIMAL_FLOAT_MODE_P (mode) || GET_MODE_CLASS (mode) == MODE_FLOAT)
    gen_fp_libfunc (optable, name, suffix, mode);
  if (GET_MODE_CLASS (mode) == MODE_INT)
    {
      int len = strlen (name);
      char *v_name = alloca (len + 2);
      strcpy (v_name, name);
      v_name[len] = 'v';
      v_name[len + 1] = 0;
      gen_int_libfunc (optable, v_name, suffix, mode);
    }
}

/* Like gen_libfunc, but verify that FP or INT or FIXED operation is
   involved.  */

static void
gen_int_fp_fixed_libfunc (optab optable, const char *name, char suffix,
			  enum machine_mode mode)
{
  if (DECIMAL_FLOAT_MODE_P (mode) || GET_MODE_CLASS (mode) == MODE_FLOAT)
    gen_fp_libfunc (optable, name, suffix, mode);
  if (INTEGRAL_MODE_P (mode))
    gen_int_libfunc (optable, name, suffix, mode);
  if (ALL_FIXED_POINT_MODE_P (mode))
    gen_fixed_libfunc (optable, name, suffix, mode);
}

/* Like gen_libfunc, but verify that FP or INT or signed FIXED operation is
   involved.  */

static void
gen_int_fp_signed_fixed_libfunc (optab optable, const char *name, char suffix,
				 enum machine_mode mode)
{
  if (DECIMAL_FLOAT_MODE_P (mode) || GET_MODE_CLASS (mode) == MODE_FLOAT)
    gen_fp_libfunc (optable, name, suffix, mode);
  if (INTEGRAL_MODE_P (mode))
    gen_int_libfunc (optable, name, suffix, mode);
  if (SIGNED_FIXED_POINT_MODE_P (mode))
    gen_signed_fixed_libfunc (optable, name, suffix, mode);
}

/* Like gen_libfunc, but verify that INT or FIXED operation is
   involved.  */

static void
gen_int_fixed_libfunc (optab optable, const char *name, char suffix,
		       enum machine_mode mode)
{
  if (INTEGRAL_MODE_P (mode))
    gen_int_libfunc (optable, name, suffix, mode);
  if (ALL_FIXED_POINT_MODE_P (mode))
    gen_fixed_libfunc (optable, name, suffix, mode);
}

/* Like gen_libfunc, but verify that INT or signed FIXED operation is
   involved.  */

static void
gen_int_signed_fixed_libfunc (optab optable, const char *name, char suffix,
			      enum machine_mode mode)
{
  if (INTEGRAL_MODE_P (mode))
    gen_int_libfunc (optable, name, suffix, mode);
  if (SIGNED_FIXED_POINT_MODE_P (mode))
    gen_signed_fixed_libfunc (optable, name, suffix, mode);
}

/* Like gen_libfunc, but verify that INT or unsigned FIXED operation is
   involved.  */

static void
gen_int_unsigned_fixed_libfunc (optab optable, const char *name, char suffix,
				enum machine_mode mode)
{
  if (INTEGRAL_MODE_P (mode))
    gen_int_libfunc (optable, name, suffix, mode);
  if (UNSIGNED_FIXED_POINT_MODE_P (mode))
    gen_unsigned_fixed_libfunc (optable, name, suffix, mode);
}

/* Initialize the libfunc fields of an entire group of entries of an
   inter-mode-class conversion optab.  The string formation rules are
   similar to the ones for init_libfuncs, above, but instead of having
   a mode name and an operand count these functions have two mode names
   and no operand count.  */

static void
gen_interclass_conv_libfunc (convert_optab tab,
			     const char *opname,
			     enum machine_mode tmode,
			     enum machine_mode fmode)
{
  size_t opname_len = strlen (opname);
  size_t mname_len = 0;

  const char *fname, *tname;
  const char *q;
  char *libfunc_name, *suffix;
  char *nondec_name, *dec_name, *nondec_suffix, *dec_suffix;
  char *p;

  /* If this is a decimal conversion, add the current BID vs. DPD prefix that
     depends on which underlying decimal floating point format is used.  */
  const size_t dec_len = sizeof (DECIMAL_PREFIX) - 1;

  mname_len = strlen (GET_MODE_NAME (tmode)) + strlen (GET_MODE_NAME (fmode));

  nondec_name = alloca (2 + opname_len + mname_len + 1 + 1);
  nondec_name[0] = '_';
  nondec_name[1] = '_';
  memcpy (&nondec_name[2], opname, opname_len);
  nondec_suffix = nondec_name + opname_len + 2;

  dec_name = alloca (2 + dec_len + opname_len + mname_len + 1 + 1);
  dec_name[0] = '_';
  dec_name[1] = '_';
  memcpy (&dec_name[2], DECIMAL_PREFIX, dec_len);
  memcpy (&dec_name[2+dec_len], opname, opname_len);
  dec_suffix = dec_name + dec_len + opname_len + 2;

  fname = GET_MODE_NAME (fmode);
  tname = GET_MODE_NAME (tmode);

  if (DECIMAL_FLOAT_MODE_P(fmode) || DECIMAL_FLOAT_MODE_P(tmode))
    {
      libfunc_name = dec_name;
      suffix = dec_suffix;
    }
  else
    {
      libfunc_name = nondec_name;
      suffix = nondec_suffix;
    }

  p = suffix;
  for (q = fname; *q; p++, q++)
    *p = TOLOWER (*q);
  for (q = tname; *q; p++, q++)
    *p = TOLOWER (*q);

  *p = '\0';

  set_conv_libfunc (tab, tmode, fmode,
		    ggc_alloc_string (libfunc_name, p - libfunc_name));
}

/* Same as gen_interclass_conv_libfunc but verify that we are producing
   int->fp conversion.  */

static void
gen_int_to_fp_conv_libfunc (convert_optab tab,
			    const char *opname,
			    enum machine_mode tmode,
			    enum machine_mode fmode)
{
  if (GET_MODE_CLASS (fmode) != MODE_INT)
    return;
  if (GET_MODE_CLASS (tmode) != MODE_FLOAT && !DECIMAL_FLOAT_MODE_P (tmode))
    return;
  gen_interclass_conv_libfunc (tab, opname, tmode, fmode);
}

/* ufloat_optab is special by using floatun for FP and floatuns decimal fp
   naming scheme.  */

static void
gen_ufloat_conv_libfunc (convert_optab tab,
			 const char *opname ATTRIBUTE_UNUSED,
			 enum machine_mode tmode,
			 enum machine_mode fmode)
{
  if (DECIMAL_FLOAT_MODE_P (tmode))
    gen_int_to_fp_conv_libfunc (tab, "floatuns", tmode, fmode);
  else
    gen_int_to_fp_conv_libfunc (tab, "floatun", tmode, fmode);
}

/* Same as gen_interclass_conv_libfunc but verify that we are producing
   fp->int conversion.  */

static void
gen_int_to_fp_nondecimal_conv_libfunc (convert_optab tab,
			               const char *opname,
			               enum machine_mode tmode,
			               enum machine_mode fmode)
{
  if (GET_MODE_CLASS (fmode) != MODE_INT)
    return;
  if (GET_MODE_CLASS (tmode) != MODE_FLOAT)
    return;
  gen_interclass_conv_libfunc (tab, opname, tmode, fmode);
}

/* Same as gen_interclass_conv_libfunc but verify that we are producing
   fp->int conversion with no decimal floating point involved.  */

static void
gen_fp_to_int_conv_libfunc (convert_optab tab,
			    const char *opname,
			    enum machine_mode tmode,
			    enum machine_mode fmode)
{
  if (GET_MODE_CLASS (fmode) != MODE_FLOAT && !DECIMAL_FLOAT_MODE_P (fmode))
    return;
  if (GET_MODE_CLASS (tmode) != MODE_INT)
    return;
  gen_interclass_conv_libfunc (tab, opname, tmode, fmode);
}

/* Initialize the libfunc fiels of an of an intra-mode-class conversion optab.
   The string formation rules are
   similar to the ones for init_libfunc, above.  */

static void
gen_intraclass_conv_libfunc (convert_optab tab, const char *opname,
			     enum machine_mode tmode, enum machine_mode fmode)
{
  size_t opname_len = strlen (opname);
  size_t mname_len = 0;

  const char *fname, *tname;
  const char *q;
  char *nondec_name, *dec_name, *nondec_suffix, *dec_suffix;
  char *libfunc_name, *suffix;
  char *p;

  /* If this is a decimal conversion, add the current BID vs. DPD prefix that
     depends on which underlying decimal floating point format is used.  */
  const size_t dec_len = sizeof (DECIMAL_PREFIX) - 1;

  mname_len = strlen (GET_MODE_NAME (tmode)) + strlen (GET_MODE_NAME (fmode));

  nondec_name = alloca (2 + opname_len + mname_len + 1 + 1);
  nondec_name[0] = '_';
  nondec_name[1] = '_';
  memcpy (&nondec_name[2], opname, opname_len);
  nondec_suffix = nondec_name + opname_len + 2;

  dec_name = alloca (2 + dec_len + opname_len + mname_len + 1 + 1);
  dec_name[0] = '_';
  dec_name[1] = '_';
  memcpy (&dec_name[2], DECIMAL_PREFIX, dec_len);
  memcpy (&dec_name[2 + dec_len], opname, opname_len);
  dec_suffix = dec_name + dec_len + opname_len + 2;

  fname = GET_MODE_NAME (fmode);
  tname = GET_MODE_NAME (tmode);

  if (DECIMAL_FLOAT_MODE_P(fmode) || DECIMAL_FLOAT_MODE_P(tmode))
    {
      libfunc_name = dec_name;
      suffix = dec_suffix;
    }
  else
    {
      libfunc_name = nondec_name;
      suffix = nondec_suffix;
    }

  p = suffix;
  for (q = fname; *q; p++, q++)
    *p = TOLOWER (*q);
  for (q = tname; *q; p++, q++)
    *p = TOLOWER (*q);

  *p++ = '2';
  *p = '\0';

  set_conv_libfunc (tab, tmode, fmode,
		    ggc_alloc_string (libfunc_name, p - libfunc_name));
}

/* Pick proper libcall for trunc_optab.  We need to chose if we do
   truncation or extension and interclass or intraclass.  */

static void
gen_trunc_conv_libfunc (convert_optab tab,
			 const char *opname,
			 enum machine_mode tmode,
			 enum machine_mode fmode)
{
  if (GET_MODE_CLASS (tmode) != MODE_FLOAT && !DECIMAL_FLOAT_MODE_P (tmode))
    return;
  if (GET_MODE_CLASS (fmode) != MODE_FLOAT && !DECIMAL_FLOAT_MODE_P (fmode))
    return;
  if (tmode == fmode)
    return;

  if ((GET_MODE_CLASS (tmode) == MODE_FLOAT && DECIMAL_FLOAT_MODE_P (fmode))
      || (GET_MODE_CLASS (fmode) == MODE_FLOAT && DECIMAL_FLOAT_MODE_P (tmode)))
     gen_interclass_conv_libfunc (tab, opname, tmode, fmode);
  
  if (GET_MODE_PRECISION (fmode) <= GET_MODE_PRECISION (tmode))
    return;

  if ((GET_MODE_CLASS (tmode) == MODE_FLOAT
       && GET_MODE_CLASS (fmode) == MODE_FLOAT)
      || (DECIMAL_FLOAT_MODE_P (fmode) && DECIMAL_FLOAT_MODE_P (tmode)))
    gen_intraclass_conv_libfunc (tab, opname, tmode, fmode);
}

/* Pick proper libcall for extend_optab.  We need to chose if we do
   truncation or extension and interclass or intraclass.  */

static void
gen_extend_conv_libfunc (convert_optab tab,
			 const char *opname ATTRIBUTE_UNUSED,
			 enum machine_mode tmode,
			 enum machine_mode fmode)
{
  if (GET_MODE_CLASS (tmode) != MODE_FLOAT && !DECIMAL_FLOAT_MODE_P (tmode))
    return;
  if (GET_MODE_CLASS (fmode) != MODE_FLOAT && !DECIMAL_FLOAT_MODE_P (fmode))
    return;
  if (tmode == fmode)
    return;

  if ((GET_MODE_CLASS (tmode) == MODE_FLOAT && DECIMAL_FLOAT_MODE_P (fmode))
      || (GET_MODE_CLASS (fmode) == MODE_FLOAT && DECIMAL_FLOAT_MODE_P (tmode)))
     gen_interclass_conv_libfunc (tab, opname, tmode, fmode);
  
  if (GET_MODE_PRECISION (fmode) > GET_MODE_PRECISION (tmode))
    return;

  if ((GET_MODE_CLASS (tmode) == MODE_FLOAT
       && GET_MODE_CLASS (fmode) == MODE_FLOAT)
      || (DECIMAL_FLOAT_MODE_P (fmode) && DECIMAL_FLOAT_MODE_P (tmode)))
    gen_intraclass_conv_libfunc (tab, opname, tmode, fmode);
}

/* Pick proper libcall for fract_optab.  We need to chose if we do
   interclass or intraclass.  */

static void
gen_fract_conv_libfunc (convert_optab tab,
			const char *opname,
			enum machine_mode tmode,
			enum machine_mode fmode)
{
  if (tmode == fmode)
    return;
  if (!(ALL_FIXED_POINT_MODE_P (tmode) || ALL_FIXED_POINT_MODE_P (fmode)))
    return;

  if (GET_MODE_CLASS (tmode) == GET_MODE_CLASS (fmode))
    gen_intraclass_conv_libfunc (tab, opname, tmode, fmode);
  else
    gen_interclass_conv_libfunc (tab, opname, tmode, fmode);
}

/* Pick proper libcall for fractuns_optab.  */

static void
gen_fractuns_conv_libfunc (convert_optab tab,
			   const char *opname,
			   enum machine_mode tmode,
			   enum machine_mode fmode)
{
  if (tmode == fmode)
    return;
  /* One mode must be a fixed-point mode, and the other must be an integer
     mode. */
  if (!((ALL_FIXED_POINT_MODE_P (tmode) && GET_MODE_CLASS (fmode) == MODE_INT)
	|| (ALL_FIXED_POINT_MODE_P (fmode)
	    && GET_MODE_CLASS (tmode) == MODE_INT)))
    return;

  gen_interclass_conv_libfunc (tab, opname, tmode, fmode);
}

/* Pick proper libcall for satfract_optab.  We need to chose if we do
   interclass or intraclass.  */

static void
gen_satfract_conv_libfunc (convert_optab tab,
			   const char *opname,
			   enum machine_mode tmode,
			   enum machine_mode fmode)
{
  if (tmode == fmode)
    return;
  /* TMODE must be a fixed-point mode.  */
  if (!ALL_FIXED_POINT_MODE_P (tmode))
    return;

  if (GET_MODE_CLASS (tmode) == GET_MODE_CLASS (fmode))
    gen_intraclass_conv_libfunc (tab, opname, tmode, fmode);
  else
    gen_interclass_conv_libfunc (tab, opname, tmode, fmode);
}

/* Pick proper libcall for satfractuns_optab.  */

static void
gen_satfractuns_conv_libfunc (convert_optab tab,
			      const char *opname,
			      enum machine_mode tmode,
			      enum machine_mode fmode)
{
  if (tmode == fmode)
    return;
  /* TMODE must be a fixed-point mode, and FMODE must be an integer mode. */
  if (!(ALL_FIXED_POINT_MODE_P (tmode) && GET_MODE_CLASS (fmode) == MODE_INT))
    return;

  gen_interclass_conv_libfunc (tab, opname, tmode, fmode);
}

rtx
init_one_libfunc (const char *name)
{
  rtx symbol;

  /* Create a FUNCTION_DECL that can be passed to
     targetm.encode_section_info.  */
  /* ??? We don't have any type information except for this is
     a function.  Pretend this is "int foo()".  */
  tree decl = build_decl (FUNCTION_DECL, get_identifier (name),
			  build_function_type (integer_type_node, NULL_TREE));
  DECL_ARTIFICIAL (decl) = 1;
  DECL_EXTERNAL (decl) = 1;
  TREE_PUBLIC (decl) = 1;

  symbol = XEXP (DECL_RTL (decl), 0);

  /* Zap the nonsensical SYMBOL_REF_DECL for this.  What we're left with
     are the flags assigned by targetm.encode_section_info.  */
  SET_SYMBOL_REF_DECL (symbol, 0);

  return symbol;
}

/* Call this to reset the function entry for one optab (OPTABLE) in mode
   MODE to NAME, which should be either 0 or a string constant.  */
void
set_optab_libfunc (optab optable, enum machine_mode mode, const char *name)
{
  rtx val;
  struct libfunc_entry e;
  struct libfunc_entry **slot;
  e.optab = (size_t) (optable - &optab_table[0]);
  e.mode1 = mode;
  e.mode2 = VOIDmode;

  if (name)
    val = init_one_libfunc (name);
  else
    val = 0;
  slot = (struct libfunc_entry **) htab_find_slot (libfunc_hash, &e, INSERT);
  if (*slot == NULL)
    *slot = ggc_alloc (sizeof (struct libfunc_entry));
  (*slot)->optab = (size_t) (optable - &optab_table[0]);
  (*slot)->mode1 = mode;
  (*slot)->mode2 = VOIDmode;
  (*slot)->libfunc = val;
}

/* Call this to reset the function entry for one conversion optab
   (OPTABLE) from mode FMODE to mode TMODE to NAME, which should be
   either 0 or a string constant.  */
void
set_conv_libfunc (convert_optab optable, enum machine_mode tmode,
		  enum machine_mode fmode, const char *name)
{
  rtx val;
  struct libfunc_entry e;
  struct libfunc_entry **slot;
  e.optab = (size_t) (optable - &convert_optab_table[0]);
  e.mode1 = tmode;
  e.mode2 = fmode;

  if (name)
    val = init_one_libfunc (name);
  else
    val = 0;
  slot = (struct libfunc_entry **) htab_find_slot (libfunc_hash, &e, INSERT);
  if (*slot == NULL)
    *slot = ggc_alloc (sizeof (struct libfunc_entry));
  (*slot)->optab = (size_t) (optable - &convert_optab_table[0]);
  (*slot)->mode1 = tmode;
  (*slot)->mode2 = fmode;
  (*slot)->libfunc = val;
}

/* Call this to initialize the contents of the optabs
   appropriately for the current target machine.  */

void
init_optabs (void)
{
  unsigned int i;
  enum machine_mode int_mode;
  static bool reinit;

  libfunc_hash = htab_create_ggc (10, hash_libfunc, eq_libfunc, NULL);
  /* Start by initializing all tables to contain CODE_FOR_nothing.  */

  for (i = 0; i < NUM_RTX_CODE; i++)
    setcc_gen_code[i] = CODE_FOR_nothing;

#ifdef HAVE_conditional_move
  for (i = 0; i < NUM_MACHINE_MODES; i++)
    movcc_gen_code[i] = CODE_FOR_nothing;
#endif

  for (i = 0; i < NUM_MACHINE_MODES; i++)
    {
      vcond_gen_code[i] = CODE_FOR_nothing;
      vcondu_gen_code[i] = CODE_FOR_nothing;
    }

#if GCC_VERSION >= 4000
  /* We statically initialize the insn_codes with CODE_FOR_nothing.  */
  if (reinit)
    init_insn_codes ();
#else
  init_insn_codes ();
#endif

  init_optab (add_optab, PLUS);
  init_optabv (addv_optab, PLUS);
  init_optab (sub_optab, MINUS);
  init_optabv (subv_optab, MINUS);
  init_optab (ssadd_optab, SS_PLUS);
  init_optab (usadd_optab, US_PLUS);
  init_optab (sssub_optab, SS_MINUS);
  init_optab (ussub_optab, US_MINUS);
  init_optab (smul_optab, MULT);
  init_optab (ssmul_optab, SS_MULT);
  init_optab (usmul_optab, US_MULT);
  init_optabv (smulv_optab, MULT);
  init_optab (smul_highpart_optab, UNKNOWN);
  init_optab (umul_highpart_optab, UNKNOWN);
  init_optab (smul_widen_optab, UNKNOWN);
  init_optab (umul_widen_optab, UNKNOWN);
  init_optab (usmul_widen_optab, UNKNOWN);
  init_optab (smadd_widen_optab, UNKNOWN);
  init_optab (umadd_widen_optab, UNKNOWN);
  init_optab (ssmadd_widen_optab, UNKNOWN);
  init_optab (usmadd_widen_optab, UNKNOWN);
  init_optab (smsub_widen_optab, UNKNOWN);
  init_optab (umsub_widen_optab, UNKNOWN);
  init_optab (ssmsub_widen_optab, UNKNOWN);
  init_optab (usmsub_widen_optab, UNKNOWN);
  init_optab (sdiv_optab, DIV);
  init_optab (ssdiv_optab, SS_DIV);
  init_optab (usdiv_optab, US_DIV);
  init_optabv (sdivv_optab, DIV);
  init_optab (sdivmod_optab, UNKNOWN);
  init_optab (udiv_optab, UDIV);
  init_optab (udivmod_optab, UNKNOWN);
  init_optab (smod_optab, MOD);
  init_optab (umod_optab, UMOD);
  init_optab (fmod_optab, UNKNOWN);
  init_optab (remainder_optab, UNKNOWN);
  init_optab (ftrunc_optab, UNKNOWN);
  init_optab (and_optab, AND);
  init_optab (ior_optab, IOR);
  init_optab (xor_optab, XOR);
  init_optab (ashl_optab, ASHIFT);
  init_optab (ssashl_optab, SS_ASHIFT);
  init_optab (usashl_optab, US_ASHIFT);
  init_optab (ashr_optab, ASHIFTRT);
  init_optab (lshr_optab, LSHIFTRT);
  init_optab (rotl_optab, ROTATE);
  init_optab (rotr_optab, ROTATERT);
  init_optab (smin_optab, SMIN);
  init_optab (smax_optab, SMAX);
  init_optab (umin_optab, UMIN);
  init_optab (umax_optab, UMAX);
  init_optab (pow_optab, UNKNOWN);
  init_optab (atan2_optab, UNKNOWN);

  /* These three have codes assigned exclusively for the sake of
     have_insn_for.  */
  init_optab (mov_optab, SET);
  init_optab (movstrict_optab, STRICT_LOW_PART);
  init_optab (cmp_optab, COMPARE);

  init_optab (storent_optab, UNKNOWN);

  init_optab (ucmp_optab, UNKNOWN);
  init_optab (tst_optab, UNKNOWN);

  init_optab (eq_optab, EQ);
  init_optab (ne_optab, NE);
  init_optab (gt_optab, GT);
  init_optab (ge_optab, GE);
  init_optab (lt_optab, LT);
  init_optab (le_optab, LE);
  init_optab (unord_optab, UNORDERED);

  init_optab (neg_optab, NEG);
  init_optab (ssneg_optab, SS_NEG);
  init_optab (usneg_optab, US_NEG);
  init_optabv (negv_optab, NEG);
  init_optab (abs_optab, ABS);
  init_optabv (absv_optab, ABS);
  init_optab (addcc_optab, UNKNOWN);
  init_optab (one_cmpl_optab, NOT);
  init_optab (bswap_optab, BSWAP);
  init_optab (ffs_optab, FFS);
  init_optab (clz_optab, CLZ);
  init_optab (ctz_optab, CTZ);
  init_optab (popcount_optab, POPCOUNT);
  init_optab (parity_optab, PARITY);
  init_optab (sqrt_optab, SQRT);
  init_optab (floor_optab, UNKNOWN);
  init_optab (ceil_optab, UNKNOWN);
  init_optab (round_optab, UNKNOWN);
  init_optab (btrunc_optab, UNKNOWN);
  init_optab (nearbyint_optab, UNKNOWN);
  init_optab (rint_optab, UNKNOWN);
  init_optab (sincos_optab, UNKNOWN);
  init_optab (sin_optab, UNKNOWN);
  init_optab (asin_optab, UNKNOWN);
  init_optab (cos_optab, UNKNOWN);
  init_optab (acos_optab, UNKNOWN);
  init_optab (exp_optab, UNKNOWN);
  init_optab (exp10_optab, UNKNOWN);
  init_optab (exp2_optab, UNKNOWN);
  init_optab (expm1_optab, UNKNOWN);
  init_optab (ldexp_optab, UNKNOWN);
  init_optab (scalb_optab, UNKNOWN);
  init_optab (logb_optab, UNKNOWN);
  init_optab (ilogb_optab, UNKNOWN);
  init_optab (log_optab, UNKNOWN);
  init_optab (log10_optab, UNKNOWN);
  init_optab (log2_optab, UNKNOWN);
  init_optab (log1p_optab, UNKNOWN);
  init_optab (tan_optab, UNKNOWN);
  init_optab (atan_optab, UNKNOWN);
  init_optab (copysign_optab, UNKNOWN);
  init_optab (signbit_optab, UNKNOWN);

  init_optab (isinf_optab, UNKNOWN);

  init_optab (strlen_optab, UNKNOWN);
  init_optab (cbranch_optab, UNKNOWN);
  init_optab (cmov_optab, UNKNOWN);
  init_optab (cstore_optab, UNKNOWN);
  init_optab (push_optab, UNKNOWN);

  init_optab (reduc_smax_optab, UNKNOWN);
  init_optab (reduc_umax_optab, UNKNOWN);
  init_optab (reduc_smin_optab, UNKNOWN);
  init_optab (reduc_umin_optab, UNKNOWN);
  init_optab (reduc_splus_optab, UNKNOWN);
  init_optab (reduc_uplus_optab, UNKNOWN);

  init_optab (ssum_widen_optab, UNKNOWN);
  init_optab (usum_widen_optab, UNKNOWN);
  init_optab (sdot_prod_optab, UNKNOWN); 
  init_optab (udot_prod_optab, UNKNOWN);

  init_optab (vec_extract_optab, UNKNOWN);
  init_optab (vec_extract_even_optab, UNKNOWN);
  init_optab (vec_extract_odd_optab, UNKNOWN);
  init_optab (vec_interleave_high_optab, UNKNOWN);
  init_optab (vec_interleave_low_optab, UNKNOWN);
  init_optab (vec_set_optab, UNKNOWN);
  init_optab (vec_init_optab, UNKNOWN);
  init_optab (vec_shl_optab, UNKNOWN);
  init_optab (vec_shr_optab, UNKNOWN);
  init_optab (vec_realign_load_optab, UNKNOWN);
  init_optab (movmisalign_optab, UNKNOWN);
  init_optab (vec_widen_umult_hi_optab, UNKNOWN);
  init_optab (vec_widen_umult_lo_optab, UNKNOWN);
  init_optab (vec_widen_smult_hi_optab, UNKNOWN);
  init_optab (vec_widen_smult_lo_optab, UNKNOWN);
  init_optab (vec_unpacks_hi_optab, UNKNOWN);
  init_optab (vec_unpacks_lo_optab, UNKNOWN);
  init_optab (vec_unpacku_hi_optab, UNKNOWN);
  init_optab (vec_unpacku_lo_optab, UNKNOWN);
  init_optab (vec_unpacks_float_hi_optab, UNKNOWN);
  init_optab (vec_unpacks_float_lo_optab, UNKNOWN);
  init_optab (vec_unpacku_float_hi_optab, UNKNOWN);
  init_optab (vec_unpacku_float_lo_optab, UNKNOWN);
  init_optab (vec_pack_trunc_optab, UNKNOWN);
  init_optab (vec_pack_usat_optab, UNKNOWN);
  init_optab (vec_pack_ssat_optab, UNKNOWN);
  init_optab (vec_pack_ufix_trunc_optab, UNKNOWN);
  init_optab (vec_pack_sfix_trunc_optab, UNKNOWN);

  init_optab (powi_optab, UNKNOWN);

  /* Conversions.  */
  init_convert_optab (sext_optab, SIGN_EXTEND);
  init_convert_optab (zext_optab, ZERO_EXTEND);
  init_convert_optab (trunc_optab, TRUNCATE);
  init_convert_optab (sfix_optab, FIX);
  init_convert_optab (ufix_optab, UNSIGNED_FIX);
  init_convert_optab (sfixtrunc_optab, UNKNOWN);
  init_convert_optab (ufixtrunc_optab, UNKNOWN);
  init_convert_optab (sfloat_optab, FLOAT);
  init_convert_optab (ufloat_optab, UNSIGNED_FLOAT);
  init_convert_optab (lrint_optab, UNKNOWN);
  init_convert_optab (lround_optab, UNKNOWN);
  init_convert_optab (lfloor_optab, UNKNOWN);
  init_convert_optab (lceil_optab, UNKNOWN);

  init_convert_optab (fract_optab, FRACT_CONVERT);
  init_convert_optab (fractuns_optab, UNSIGNED_FRACT_CONVERT);
  init_convert_optab (satfract_optab, SAT_FRACT);
  init_convert_optab (satfractuns_optab, UNSIGNED_SAT_FRACT);

  for (i = 0; i < NUM_MACHINE_MODES; i++)
    {
      movmem_optab[i] = CODE_FOR_nothing;
      cmpstr_optab[i] = CODE_FOR_nothing;
      cmpstrn_optab[i] = CODE_FOR_nothing;
      cmpmem_optab[i] = CODE_FOR_nothing;
      setmem_optab[i] = CODE_FOR_nothing;

      sync_add_optab[i] = CODE_FOR_nothing;
      sync_sub_optab[i] = CODE_FOR_nothing;
      sync_ior_optab[i] = CODE_FOR_nothing;
      sync_and_optab[i] = CODE_FOR_nothing;
      sync_xor_optab[i] = CODE_FOR_nothing;
      sync_nand_optab[i] = CODE_FOR_nothing;
      sync_old_add_optab[i] = CODE_FOR_nothing;
      sync_old_sub_optab[i] = CODE_FOR_nothing;
      sync_old_ior_optab[i] = CODE_FOR_nothing;
      sync_old_and_optab[i] = CODE_FOR_nothing;
      sync_old_xor_optab[i] = CODE_FOR_nothing;
      sync_old_nand_optab[i] = CODE_FOR_nothing;
      sync_new_add_optab[i] = CODE_FOR_nothing;
      sync_new_sub_optab[i] = CODE_FOR_nothing;
      sync_new_ior_optab[i] = CODE_FOR_nothing;
      sync_new_and_optab[i] = CODE_FOR_nothing;
      sync_new_xor_optab[i] = CODE_FOR_nothing;
      sync_new_nand_optab[i] = CODE_FOR_nothing;
      sync_compare_and_swap[i] = CODE_FOR_nothing;
      sync_compare_and_swap_cc[i] = CODE_FOR_nothing;
      sync_lock_test_and_set[i] = CODE_FOR_nothing;
      sync_lock_release[i] = CODE_FOR_nothing;

      reload_in_optab[i] = reload_out_optab[i] = CODE_FOR_nothing;
    }

  /* Fill in the optabs with the insns we support.  */
  init_all_optabs ();

  /* Initialize the optabs with the names of the library functions.  */
  add_optab->libcall_basename = "add";
  add_optab->libcall_suffix = '3';
  add_optab->libcall_gen = gen_int_fp_fixed_libfunc;
  addv_optab->libcall_basename = "add";
  addv_optab->libcall_suffix = '3';
  addv_optab->libcall_gen = gen_intv_fp_libfunc;
  ssadd_optab->libcall_basename = "ssadd";
  ssadd_optab->libcall_suffix = '3';
  ssadd_optab->libcall_gen = gen_signed_fixed_libfunc;
  usadd_optab->libcall_basename = "usadd";
  usadd_optab->libcall_suffix = '3';
  usadd_optab->libcall_gen = gen_unsigned_fixed_libfunc;
  sub_optab->libcall_basename = "sub";
  sub_optab->libcall_suffix = '3';
  sub_optab->libcall_gen = gen_int_fp_fixed_libfunc;
  subv_optab->libcall_basename = "sub";
  subv_optab->libcall_suffix = '3';
  subv_optab->libcall_gen = gen_intv_fp_libfunc;
  sssub_optab->libcall_basename = "sssub";
  sssub_optab->libcall_suffix = '3';
  sssub_optab->libcall_gen = gen_signed_fixed_libfunc;
  ussub_optab->libcall_basename = "ussub";
  ussub_optab->libcall_suffix = '3';
  ussub_optab->libcall_gen = gen_unsigned_fixed_libfunc;
  smul_optab->libcall_basename = "mul";
  smul_optab->libcall_suffix = '3';
  smul_optab->libcall_gen = gen_int_fp_fixed_libfunc;
  smulv_optab->libcall_basename = "mul";
  smulv_optab->libcall_suffix = '3';
  smulv_optab->libcall_gen = gen_intv_fp_libfunc;
  ssmul_optab->libcall_basename = "ssmul";
  ssmul_optab->libcall_suffix = '3';
  ssmul_optab->libcall_gen = gen_signed_fixed_libfunc;
  usmul_optab->libcall_basename = "usmul";
  usmul_optab->libcall_suffix = '3';
  usmul_optab->libcall_gen = gen_unsigned_fixed_libfunc;
  sdiv_optab->libcall_basename = "div";
  sdiv_optab->libcall_suffix = '3';
  sdiv_optab->libcall_gen = gen_int_fp_signed_fixed_libfunc;
  sdivv_optab->libcall_basename = "divv";
  sdivv_optab->libcall_suffix = '3';
  sdivv_optab->libcall_gen = gen_int_libfunc;
  ssdiv_optab->libcall_basename = "ssdiv";
  ssdiv_optab->libcall_suffix = '3';
  ssdiv_optab->libcall_gen = gen_signed_fixed_libfunc;
  udiv_optab->libcall_basename = "udiv";
  udiv_optab->libcall_suffix = '3';
  udiv_optab->libcall_gen = gen_int_unsigned_fixed_libfunc;
  usdiv_optab->libcall_basename = "usdiv";
  usdiv_optab->libcall_suffix = '3';
  usdiv_optab->libcall_gen = gen_unsigned_fixed_libfunc;
  sdivmod_optab->libcall_basename = "divmod";
  sdivmod_optab->libcall_suffix = '4';
  sdivmod_optab->libcall_gen = gen_int_libfunc;
  udivmod_optab->libcall_basename = "udivmod";
  udivmod_optab->libcall_suffix = '4';
  udivmod_optab->libcall_gen = gen_int_libfunc;
  smod_optab->libcall_basename = "mod";
  smod_optab->libcall_suffix = '3';
  smod_optab->libcall_gen = gen_int_libfunc;
  umod_optab->libcall_basename = "umod";
  umod_optab->libcall_suffix = '3';
  umod_optab->libcall_gen = gen_int_libfunc;
  ftrunc_optab->libcall_basename = "ftrunc";
  ftrunc_optab->libcall_suffix = '2';
  ftrunc_optab->libcall_gen = gen_fp_libfunc;
  and_optab->libcall_basename = "and";
  and_optab->libcall_suffix = '3';
  and_optab->libcall_gen = gen_int_libfunc;
  ior_optab->libcall_basename = "ior";
  ior_optab->libcall_suffix = '3';
  ior_optab->libcall_gen = gen_int_libfunc;
  xor_optab->libcall_basename = "xor";
  xor_optab->libcall_suffix = '3';
  xor_optab->libcall_gen = gen_int_libfunc;
  ashl_optab->libcall_basename = "ashl";
  ashl_optab->libcall_suffix = '3';
  ashl_optab->libcall_gen = gen_int_fixed_libfunc;
  ssashl_optab->libcall_basename = "ssashl";
  ssashl_optab->libcall_suffix = '3';
  ssashl_optab->libcall_gen = gen_signed_fixed_libfunc;
  usashl_optab->libcall_basename = "usashl";
  usashl_optab->libcall_suffix = '3';
  usashl_optab->libcall_gen = gen_unsigned_fixed_libfunc;
  ashr_optab->libcall_basename = "ashr";
  ashr_optab->libcall_suffix = '3';
  ashr_optab->libcall_gen = gen_int_signed_fixed_libfunc;
  lshr_optab->libcall_basename = "lshr";
  lshr_optab->libcall_suffix = '3';
  lshr_optab->libcall_gen = gen_int_unsigned_fixed_libfunc;
  smin_optab->libcall_basename = "min";
  smin_optab->libcall_suffix = '3';
  smin_optab->libcall_gen = gen_int_fp_libfunc;
  smax_optab->libcall_basename = "max";
  smax_optab->libcall_suffix = '3';
  smax_optab->libcall_gen = gen_int_fp_libfunc;
  umin_optab->libcall_basename = "umin";
  umin_optab->libcall_suffix = '3';
  umin_optab->libcall_gen = gen_int_libfunc;
  umax_optab->libcall_basename = "umax";
  umax_optab->libcall_suffix = '3';
  umax_optab->libcall_gen = gen_int_libfunc;
  neg_optab->libcall_basename = "neg";
  neg_optab->libcall_suffix = '2';
  neg_optab->libcall_gen = gen_int_fp_fixed_libfunc;
  ssneg_optab->libcall_basename = "ssneg";
  ssneg_optab->libcall_suffix = '2';
  ssneg_optab->libcall_gen = gen_signed_fixed_libfunc;
  usneg_optab->libcall_basename = "usneg";
  usneg_optab->libcall_suffix = '2';
  usneg_optab->libcall_gen = gen_unsigned_fixed_libfunc;
  negv_optab->libcall_basename = "neg";
  negv_optab->libcall_suffix = '2';
  negv_optab->libcall_gen = gen_intv_fp_libfunc;
  one_cmpl_optab->libcall_basename = "one_cmpl";
  one_cmpl_optab->libcall_suffix = '2';
  one_cmpl_optab->libcall_gen = gen_int_libfunc;
  ffs_optab->libcall_basename = "ffs";
 /* MUTANT (rep_const) */  ffs_optab->libcall_suffix = '0';
  ffs_optab->libcall_gen = gen_int_libfunc;
  clz_optab->libcall_basename = "clz";
  clz_optab->libcall_suffix = '2';
  clz_optab->libcall_gen = gen_int_libfunc;
  ctz_optab->libcall_basename = "ctz";
  ctz_optab->libcall_suffix = '2';
  ctz_optab->libcall_gen = gen_int_libfunc;
  popcount_optab->libcall_basename = "popcount";
  popcount_optab->libcall_suffix = '2';
  popcount_optab->libcall_gen = gen_int_libfunc;
  parity_optab->libcall_basename = "parity";
  parity_optab->libcall_suffix = '2';
  parity_optab->libcall_gen = gen_int_libfunc;

  /* Comparison libcalls for integers MUST come in pairs,
     signed/unsigned.  */
  cmp_optab->libcall_basename = "cmp";
  cmp_optab->libcall_suffix = '2';
  cmp_optab->libcall_gen = gen_int_fp_fixed_libfunc;
  ucmp_optab->libcall_basename = "ucmp";
  ucmp_optab->libcall_suffix = '2';
  ucmp_optab->libcall_gen = gen_int_libfunc;

  /* EQ etc are floating point only.  */
  eq_optab->libcall_basename = "eq";
  eq_optab->libcall_suffix = '2';
  eq_optab->libcall_gen = gen_fp_libfunc;
  ne_optab->libcall_basename = "ne";
  ne_optab->libcall_suffix = '2';
  ne_optab->libcall_gen = gen_fp_libfunc;
  gt_optab->libcall_basename = "gt";
  gt_optab->libcall_suffix = '2';
  gt_optab->libcall_gen = gen_fp_libfunc;
  ge_optab->libcall_basename = "ge";
  ge_optab->libcall_suffix = '2';
  ge_optab->libcall_gen = gen_fp_libfunc;
  lt_optab->libcall_basename = "lt";
  lt_optab->libcall_suffix = '2';
  lt_optab->libcall_gen = gen_fp_libfunc;
  le_optab->libcall_basename = "le";
  le_optab->libcall_suffix = '2';
  le_optab->libcall_gen = gen_fp_libfunc;
  unord_optab->libcall_basename = "unord";
  unord_optab->libcall_suffix = '2';
  unord_optab->libcall_gen = gen_fp_libfunc;

  powi_optab->libcall_basename = "powi";
  powi_optab->libcall_suffix = '2';
  powi_optab->libcall_gen = gen_fp_libfunc;

  /* Conversions.  */
  sfloat_optab->libcall_basename = "float";
  sfloat_optab->libcall_gen = gen_int_to_fp_conv_libfunc;
  ufloat_optab->libcall_gen = gen_ufloat_conv_libfunc;
  sfix_optab->libcall_basename = "fix";
  sfix_optab->libcall_gen = gen_fp_to_int_conv_libfunc;
  ufix_optab->libcall_basename = "fixuns";
  ufix_optab->libcall_gen = gen_fp_to_int_conv_libfunc;
  lrint_optab->libcall_basename = "lrint";
  lrint_optab->libcall_gen = gen_int_to_fp_nondecimal_conv_libfunc;
  lround_optab->libcall_basename = "lround";
  lround_optab->libcall_gen = gen_int_to_fp_nondecimal_conv_libfunc;
  lfloor_optab->libcall_basename = "lfloor";
  lfloor_optab->libcall_gen = gen_int_to_fp_nondecimal_conv_libfunc;
  lceil_optab->libcall_basename = "lceil";
  lceil_optab->libcall_gen = gen_int_to_fp_nondecimal_conv_libfunc;

  /* trunc_optab is also used for FLOAT_EXTEND.  */
  sext_optab->libcall_basename = "extend";
  sext_optab->libcall_gen = gen_extend_conv_libfunc;
  trunc_optab->libcall_basename = "trunc";
  trunc_optab->libcall_gen = gen_trunc_conv_libfunc;

  /* Conversions for fixed-point modes and other modes.  */
  fract_optab->libcall_basename = "fract";
  fract_optab->libcall_gen = gen_fract_conv_libfunc;
  satfract_optab->libcall_basename = "satfract";
  satfract_optab->libcall_gen = gen_satfract_conv_libfunc;
  fractuns_optab->libcall_basename = "fractuns";
  fractuns_optab->libcall_gen = gen_fractuns_conv_libfunc;
  satfractuns_optab->libcall_basename = "satfractuns";
  satfractuns_optab->libcall_gen = gen_satfractuns_conv_libfunc;

  /* The ffs function operates on `int'.  Fall back on it if we do not
     have a libgcc2 function for that width.  */
  if (INT_TYPE_SIZE < BITS_PER_WORD)
    {
      int_mode = mode_for_size (INT_TYPE_SIZE, MODE_INT, 0);
      set_optab_libfunc (ffs_optab, mode_for_size (INT_TYPE_SIZE, MODE_INT, 0),
			 "ffs");
    }

  /* Explicitly initialize the bswap libfuncs since we need them to be
     valid for things other than word_mode.  */
  set_optab_libfunc (bswap_optab, SImode, "__bswapsi2");
  set_optab_libfunc (bswap_optab, DImode, "__bswapdi2");

  /* Use cabs for double complex abs, since systems generally have cabs.
     Don't define any libcall for float complex, so that cabs will be used.  */
  if (complex_double_type_node)
    set_optab_libfunc (abs_optab, TYPE_MODE (complex_double_type_node), "cabs");

  abort_libfunc = init_one_libfunc ("abort");
  memcpy_libfunc = init_one_libfunc ("memcpy");
  memmove_libfunc = init_one_libfunc ("memmove");
  memcmp_libfunc = init_one_libfunc ("memcmp");
  memset_libfunc = init_one_libfunc ("memset");
  setbits_libfunc = init_one_libfunc ("__setbits");

#ifndef DONT_USE_BUILTIN_SETJMP
  setjmp_libfunc = init_one_libfunc ("__builtin_setjmp");
  longjmp_libfunc = init_one_libfunc ("__builtin_longjmp");
#else
  setjmp_libfunc = init_one_libfunc ("setjmp");
  longjmp_libfunc = init_one_libfunc ("longjmp");
#endif
  unwind_sjlj_register_libfunc = init_one_libfunc ("_Unwind_SjLj_Register");
  unwind_sjlj_unregister_libfunc
    = init_one_libfunc ("_Unwind_SjLj_Unregister");

  /* For function entry/exit instrumentation.  */
  profile_function_entry_libfunc
    = init_one_libfunc ("__cyg_profile_func_enter");
  profile_function_exit_libfunc
    = init_one_libfunc ("__cyg_profile_func_exit");

  gcov_flush_libfunc = init_one_libfunc ("__gcov_flush");

  if (HAVE_conditional_trap)
    trap_rtx = gen_rtx_fmt_ee (EQ, VOIDmode, NULL_RTX, NULL_RTX);

  /* Allow the target to add more libcalls or rename some, etc.  */
  targetm.init_libfuncs ();

  reinit = true;
}

/* Print information about the current contents of the optabs on
   STDERR.  */

void
debug_optab_libfuncs (void)
{
  int i;
  int j;
  int k;

  /* Dump the arithmetic optabs.  */
  for (i = 0; i != (int) OTI_MAX; i++)
    for (j = 0; j < NUM_MACHINE_MODES; ++j)
      {
	optab o;
	rtx l;

	o = &optab_table[i];
	l = optab_libfunc (o, j);
	if (l)
	  {
	    gcc_assert (GET_CODE (l) == SYMBOL_REF);
	    fprintf (stderr, "%s\t%s:\t%s\n",
		     GET_RTX_NAME (o->code),
		     GET_MODE_NAME (j),
		     XSTR (l, 0));
	  }
      }

  /* Dump the conversion optabs.  */
  for (i = 0; i < (int) COI_MAX; ++i)
    for (j = 0; j < NUM_MACHINE_MODES; ++j)
      for (k = 0; k < NUM_MACHINE_MODES; ++k)
	{
	  convert_optab o;
	  rtx l;

	  o = &convert_optab_table[i];
	  l = convert_optab_libfunc (o, j, k);
	  if (l)
	    {
	      gcc_assert (GET_CODE (l) == SYMBOL_REF);
	      fprintf (stderr, "%s\t%s\t%s:\t%s\n",
		       GET_RTX_NAME (o->code),
		       GET_MODE_NAME (j),
		       GET_MODE_NAME (k),
		       XSTR (l, 0));
	    }
	}
}


/* Generate insns to trap with code TCODE if OP1 and OP2 satisfy condition
   CODE.  Return 0 on failure.  */

rtx
gen_cond_trap (enum rtx_code code ATTRIBUTE_UNUSED, rtx op1,
	       rtx op2 ATTRIBUTE_UNUSED, rtx tcode ATTRIBUTE_UNUSED)
{
  enum machine_mode mode = GET_MODE (op1);
  enum insn_code icode;
  rtx insn;

  if (!HAVE_conditional_trap)
    return 0;

  if (mode == VOIDmode)
    return 0;

  icode = optab_handler (cmp_optab, mode)->insn_code;
  if (icode == CODE_FOR_nothing)
    return 0;

  start_sequence ();
  op1 = prepare_operand (icode, op1, 0, mode, mode, 0);
  op2 = prepare_operand (icode, op2, 1, mode, mode, 0);
  if (!op1 || !op2)
    {
      end_sequence ();
      return 0;
    }
  emit_insn (GEN_FCN (icode) (op1, op2));

  PUT_CODE (trap_rtx, code);
  gcc_assert (HAVE_conditional_trap);
  insn = gen_conditional_trap (trap_rtx, tcode);
  if (insn)
    {
      emit_insn (insn);
      insn = get_insns ();
    }
  end_sequence ();

  return insn;
}

/* Return rtx code for TCODE. Use UNSIGNEDP to select signed
   or unsigned operation code.  */

static enum rtx_code
get_rtx_code (enum tree_code tcode, bool unsignedp)
{
  enum rtx_code code;
  switch (tcode)
    {
    case EQ_EXPR:
      code = EQ;
      break;
    case NE_EXPR:
      code = NE;
      break;
    case LT_EXPR:
      code = unsignedp ? LTU : LT;
      break;
    case LE_EXPR:
      code = unsignedp ? LEU : LE;
      break;
    case GT_EXPR:
      code = unsignedp ? GTU : GT;
      break;
    case GE_EXPR:
      code = unsignedp ? GEU : GE;
      break;

    case UNORDERED_EXPR:
      code = UNORDERED;
      break;
    case ORDERED_EXPR:
      code = ORDERED;
      break;
    case UNLT_EXPR:
      code = UNLT;
      break;
    case UNLE_EXPR:
      code = UNLE;
      break;
    case UNGT_EXPR:
      code = UNGT;
      break;
    case UNGE_EXPR:
      code = UNGE;
      break;
    case UNEQ_EXPR:
      code = UNEQ;
      break;
    case LTGT_EXPR:
      code = LTGT;
      break;

    default:
      gcc_unreachable ();
    }
  return code;
}

/* Return comparison rtx for COND. Use UNSIGNEDP to select signed or
   unsigned operators. Do not generate compare instruction.  */

static rtx
vector_compare_rtx (tree cond, bool unsignedp, enum insn_code icode)
{
  enum rtx_code rcode;
  tree t_op0, t_op1;
  rtx rtx_op0, rtx_op1;

  /* This is unlikely. While generating VEC_COND_EXPR, auto vectorizer
     ensures that condition is a relational operation.  */
  gcc_assert (COMPARISON_CLASS_P (cond));

  rcode = get_rtx_code (TREE_CODE (cond), unsignedp);
  t_op0 = TREE_OPERAND (cond, 0);
  t_op1 = TREE_OPERAND (cond, 1);

  /* Expand operands.  */
  rtx_op0 = expand_expr (t_op0, NULL_RTX, TYPE_MODE (TREE_TYPE (t_op0)),
			 EXPAND_STACK_PARM);
  rtx_op1 = expand_expr (t_op1, NULL_RTX, TYPE_MODE (TREE_TYPE (t_op1)),
			 EXPAND_STACK_PARM);

  if (!insn_data[icode].operand[4].predicate (rtx_op0, GET_MODE (rtx_op0))
      && GET_MODE (rtx_op0) != VOIDmode)
    rtx_op0 = force_reg (GET_MODE (rtx_op0), rtx_op0);

  if (!insn_data[icode].operand[5].predicate (rtx_op1, GET_MODE (rtx_op1))
      && GET_MODE (rtx_op1) != VOIDmode)
    rtx_op1 = force_reg (GET_MODE (rtx_op1), rtx_op1);

  return gen_rtx_fmt_ee (rcode, VOIDmode, rtx_op0, rtx_op1);
}

/* Return insn code for VEC_COND_EXPR EXPR.  */

static inline enum insn_code
get_vcond_icode (tree expr, enum machine_mode mode)
{
  enum insn_code icode = CODE_FOR_nothing;

  if (TYPE_UNSIGNED (TREE_TYPE (expr)))
    icode = vcondu_gen_code[mode];
  else
    icode = vcond_gen_code[mode];
  return icode;
}

/* Return TRUE iff, appropriate vector insns are available
   for vector cond expr expr in VMODE mode.  */

bool
expand_vec_cond_expr_p (tree expr, enum machine_mode vmode)
{
  if (get_vcond_icode (expr, vmode) == CODE_FOR_nothing)
    return false;
  return true;
}

/* Generate insns for VEC_COND_EXPR.  */

rtx
expand_vec_cond_expr (tree vec_cond_expr, rtx target)
{
  enum insn_code icode;
  rtx comparison, rtx_op1, rtx_op2, cc_op0, cc_op1;
  enum machine_mode mode = TYPE_MODE (TREE_TYPE (vec_cond_expr));
  bool unsignedp = TYPE_UNSIGNED (TREE_TYPE (vec_cond_expr));

  icode = get_vcond_icode (vec_cond_expr, mode);
  if (icode == CODE_FOR_nothing)
    return 0;

  if (!target || !insn_data[icode].operand[0].predicate (target, mode))
    target = gen_reg_rtx (mode);

  /* Get comparison rtx.  First expand both cond expr operands.  */
  comparison = vector_compare_rtx (TREE_OPERAND (vec_cond_expr, 0),
				   unsignedp, icode);
  cc_op0 = XEXP (comparison, 0);
  cc_op1 = XEXP (comparison, 1);
  /* Expand both operands and force them in reg, if required.  */
  rtx_op1 = expand_normal (TREE_OPERAND (vec_cond_expr, 1));
  if (!insn_data[icode].operand[1].predicate (rtx_op1, mode)
      && mode != VOIDmode)
    rtx_op1 = force_reg (mode, rtx_op1);

  rtx_op2 = expand_normal (TREE_OPERAND (vec_cond_expr, 2));
  if (!insn_data[icode].operand[2].predicate (rtx_op2, mode)
      && mode != VOIDmode)
    rtx_op2 = force_reg (mode, rtx_op2);

  /* Emit instruction! */
  emit_insn (GEN_FCN (icode) (target, rtx_op1, rtx_op2,
			      comparison, cc_op0,  cc_op1));

  return target;
}


/* This is an internal subroutine of the other compare_and_swap expanders.
   MEM, OLD_VAL and NEW_VAL are as you'd expect for a compare-and-swap
   operation.  TARGET is an optional place to store the value result of
   the operation.  ICODE is the particular instruction to expand.  Return
   the result of the operation.  */

static rtx
expand_val_compare_and_swap_1 (rtx mem, rtx old_val, rtx new_val,
			       rtx target, enum insn_code icode)
{
  enum machine_mode mode = GET_MODE (mem);
  rtx insn;

  if (!target || !insn_data[icode].operand[0].predicate (target, mode))
    target = gen_reg_rtx (mode);

  if (GET_MODE (old_val) != VOIDmode && GET_MODE (old_val) != mode)
    old_val = convert_modes (mode, GET_MODE (old_val), old_val, 1);
  if (!insn_data[icode].operand[2].predicate (old_val, mode))
    old_val = force_reg (mode, old_val);

  if (GET_MODE (new_val) != VOIDmode && GET_MODE (new_val) != mode)
    new_val = convert_modes (mode, GET_MODE (new_val), new_val, 1);
  if (!insn_data[icode].operand[3].predicate (new_val, mode))
    new_val = force_reg (mode, new_val);

  insn = GEN_FCN (icode) (target, mem, old_val, new_val);
  if (insn == NULL_RTX)
    return NULL_RTX;
  emit_insn (insn);

  return target;
}

/* Expand a compare-and-swap operation and return its value.  */

rtx
expand_val_compare_and_swap (rtx mem, rtx old_val, rtx new_val, rtx target)
{
  enum machine_mode mode = GET_MODE (mem);
  enum insn_code icode = sync_compare_and_swap[mode];

  if (icode == CODE_FOR_nothing)
    return NULL_RTX;

  return expand_val_compare_and_swap_1 (mem, old_val, new_val, target, icode);
}

/* Expand a compare-and-swap operation and store true into the result if
   the operation was successful and false otherwise.  Return the result.
   Unlike other routines, TARGET is not optional.  */

rtx
expand_bool_compare_and_swap (rtx mem, rtx old_val, rtx new_val, rtx target)
{
  enum machine_mode mode = GET_MODE (mem);
  enum insn_code icode;
  rtx subtarget, label0, label1;

  /* If the target supports a compare-and-swap pattern that simultaneously
     sets some flag for success, then use it.  Otherwise use the regular
     compare-and-swap and follow that immediately with a compare insn.  */
  icode = sync_compare_and_swap_cc[mode];
  switch (icode)
    {
    default:
      subtarget = expand_val_compare_and_swap_1 (mem, old_val, new_val,
						 NULL_RTX, icode);
      if (subtarget != NULL_RTX)
	break;

      /* FALLTHRU */
    case CODE_FOR_nothing:
      icode = sync_compare_and_swap[mode];
      if (icode == CODE_FOR_nothing)
	return NULL_RTX;

      /* Ensure that if old_val == mem, that we're not comparing
	 against an old value.  */
      if (MEM_P (old_val))
	old_val = force_reg (mode, old_val);

      subtarget = expand_val_compare_and_swap_1 (mem, old_val, new_val,
						 NULL_RTX, icode);
      if (subtarget == NULL_RTX)
	return NULL_RTX;

      emit_cmp_insn (subtarget, old_val, EQ, const0_rtx, mode, true);
    }

  /* If the target has a sane STORE_FLAG_VALUE, then go ahead and use a
     setcc instruction from the beginning.  We don't work too hard here,
     but it's nice to not be stupid about initial code gen either.  */
  if (STORE_FLAG_VALUE == 1)
    {
      icode = setcc_gen_code[EQ];
      if (icode != CODE_FOR_nothing)
	{
	  enum machine_mode cmode = insn_data[icode].operand[0].mode;
	  rtx insn;

	  subtarget = target;
	  if (!insn_data[icode].operand[0].predicate (target, cmode))
	    subtarget = gen_reg_rtx (cmode);

	  insn = GEN_FCN (icode) (subtarget);
	  if (insn)
	    {
	      emit_insn (insn);
	      if (GET_MODE (target) != GET_MODE (subtarget))
		{
	          convert_move (target, subtarget, 1);
		  subtarget = target;
		}
	      return subtarget;
	    }
	}
    }

  /* Without an appropriate setcc instruction, use a set of branches to
     get 1 and 0 stored into target.  Presumably if the target has a
     STORE_FLAG_VALUE that isn't 1, then this will get cleaned up by ifcvt.  */

  label0 = gen_label_rtx ();
  label1 = gen_label_rtx ();

  emit_jump_insn (bcc_gen_fctn[EQ] (label0));
  emit_move_insn (target, const0_rtx);
  emit_jump_insn (gen_jump (label1));
  emit_barrier ();
  emit_label (label0);
  emit_move_insn (target, const1_rtx);
  emit_label (label1);

  return target;
}

/* This is a helper function for the other atomic operations.  This function
   emits a loop that contains SEQ that iterates until a compare-and-swap
   operation at the end succeeds.  MEM is the memory to be modified.  SEQ is
   a set of instructions that takes a value from OLD_REG as an input and
   produces a value in NEW_REG as an output.  Before SEQ, OLD_REG will be
   set to the current contents of MEM.  After SEQ, a compare-and-swap will
   attempt to update MEM with NEW_REG.  The function returns true when the
   loop was generated successfully.  */

static bool
expand_compare_and_swap_loop (rtx mem, rtx old_reg, rtx new_reg, rtx seq)
{
  enum machine_mode mode = GET_MODE (mem);
  enum insn_code icode;
  rtx label, cmp_reg, subtarget;

  /* The loop we want to generate looks like

	cmp_reg = mem;
      label:
        old_reg = cmp_reg;
	seq;
	cmp_reg = compare-and-swap(mem, old_reg, new_reg)
	if (cmp_reg != old_reg)
	  goto label;

     Note that we only do the plain load from memory once.  Subsequent
     iterations use the value loaded by the compare-and-swap pattern.  */

  label = gen_label_rtx ();
  cmp_reg = gen_reg_rtx (mode);

  emit_move_insn (cmp_reg, mem);
  emit_label (label);
  emit_move_insn (old_reg, cmp_reg);
  if (seq)
    emit_insn (seq);

  /* If the target supports a compare-and-swap pattern that simultaneously
     sets some flag for success, then use it.  Otherwise use the regular
     compare-and-swap and follow that immediately with a compare insn.  */
  icode = sync_compare_and_swap_cc[mode];
  switch (icode)
    {
    default:
      subtarget = expand_val_compare_and_swap_1 (mem, old_reg, new_reg,
						 cmp_reg, icode);
      if (subtarget != NULL_RTX)
	{
	  gcc_assert (subtarget == cmp_reg);
	  break;
	}

      /* FALLTHRU */
    case CODE_FOR_nothing:
      icode = sync_compare_and_swap[mode];
      if (icode == CODE_FOR_nothing)
	return false;

      subtarget = expand_val_compare_and_swap_1 (mem, old_reg, new_reg,
						 cmp_reg, icode);
      if (subtarget == NULL_RTX)
	return false;
      if (subtarget != cmp_reg)
	emit_move_insn (cmp_reg, subtarget);

      emit_cmp_insn (cmp_reg, old_reg, EQ, const0_rtx, mode, true);
    }

  /* ??? Mark this jump predicted not taken?  */
  emit_jump_insn (bcc_gen_fctn[NE] (label));

  return true;
}

/* This function generates the atomic operation MEM CODE= VAL.  In this
   case, we do not care about any resulting value.  Returns NULL if we
   cannot generate the operation.  */

rtx
expand_sync_operation (rtx mem, rtx val, enum rtx_code code)
{
  enum machine_mode mode = GET_MODE (mem);
  enum insn_code icode;
  rtx insn;

  /* Look to see if the target supports the operation directly.  */
  switch (code)
    {
    case PLUS:
      icode = sync_add_optab[mode];
      break;
    case IOR:
      icode = sync_ior_optab[mode];
      break;
    case XOR:
      icode = sync_xor_optab[mode];
      break;
    case AND:
      icode = sync_and_optab[mode];
      break;
    case NOT:
      icode = sync_nand_optab[mode];
      break;

    case MINUS:
      icode = sync_sub_optab[mode];
      if (icode == CODE_FOR_nothing || CONST_INT_P (val))
	{
	  icode = sync_add_optab[mode];
	  if (icode != CODE_FOR_nothing)
	    {
	      val = expand_simple_unop (mode, NEG, val, NULL_RTX, 1);
	      code = PLUS;
	    }
	}
      break;

    default:
      gcc_unreachable ();
    }

  /* Generate the direct operation, if present.  */
  if (icode != CODE_FOR_nothing)
    {
      if (GET_MODE (val) != VOIDmode && GET_MODE (val) != mode)
	val = convert_modes (mode, GET_MODE (val), val, 1);
      if (!insn_data[icode].operand[1].predicate (val, mode))
	val = force_reg (mode, val);

      insn = GEN_FCN (icode) (mem, val);
      if (insn)
	{
	  emit_insn (insn);
	  return const0_rtx;
	}
    }

  /* Failing that, generate a compare-and-swap loop in which we perform the
     operation with normal arithmetic instructions.  */
  if (sync_compare_and_swap[mode] != CODE_FOR_nothing)
    {
      rtx t0 = gen_reg_rtx (mode), t1;

      start_sequence ();

      t1 = t0;
      if (code == NOT)
	{
	  t1 = expand_simple_unop (mode, NOT, t1, NULL_RTX, true);
	  code = AND;
	}
      t1 = expand_simple_binop (mode, code, t1, val, NULL_RTX,
				true, OPTAB_LIB_WIDEN);

      insn = get_insns ();
      end_sequence ();

      if (t1 != NULL && expand_compare_and_swap_loop (mem, t0, t1, insn))
	return const0_rtx;
    }

  return NULL_RTX;
}

/* This function generates the atomic operation MEM CODE= VAL.  In this
   case, we do care about the resulting value: if AFTER is true then
   return the value MEM holds after the operation, if AFTER is false
   then return the value MEM holds before the operation.  TARGET is an
   optional place for the result value to be stored.  */

rtx
expand_sync_fetch_operation (rtx mem, rtx val, enum rtx_code code,
			     bool after, rtx target)
{
  enum machine_mode mode = GET_MODE (mem);
  enum insn_code old_code, new_code, icode;
  bool compensate;
  rtx insn;

  /* Look to see if the target supports the operation directly.  */
  switch (code)
    {
    case PLUS:
      old_code = sync_old_add_optab[mode];
      new_code = sync_new_add_optab[mode];
      break;
    case IOR:
      old_code = sync_old_ior_optab[mode];
      new_code = sync_new_ior_optab[mode];
      break;
    case XOR:
      old_code = sync_old_xor_optab[mode];
      new_code = sync_new_xor_optab[mode];
      break;
    case AND:
      old_code = sync_old_and_optab[mode];
      new_code = sync_new_and_optab[mode];
      break;
    case NOT:
      old_code = sync_old_nand_optab[mode];
      new_code = sync_new_nand_optab[mode];
      break;

    case MINUS:
      old_code = sync_old_sub_optab[mode];
      new_code = sync_new_sub_optab[mode];
      if ((old_code == CODE_FOR_nothing && new_code == CODE_FOR_nothing)
          || CONST_INT_P (val))
	{
	  old_code = sync_old_add_optab[mode];
	  new_code = sync_new_add_optab[mode];
	  if (old_code != CODE_FOR_nothing || new_code != CODE_FOR_nothing)
	    {
	      val = expand_simple_unop (mode, NEG, val, NULL_RTX, 1);
	      code = PLUS;
	    }
	}
      break;

    default:
      gcc_unreachable ();
    }

  /* If the target does supports the proper new/old operation, great.  But
     if we only support the opposite old/new operation, check to see if we
     can compensate.  In the case in which the old value is supported, then
     we can always perform the operation again with normal arithmetic.  In
     the case in which the new value is supported, then we can only handle
     this in the case the operation is reversible.  */
  compensate = false;
  if (after)
    {
      icode = new_code;
      if (icode == CODE_FOR_nothing)
	{
	  icode = old_code;
	  if (icode != CODE_FOR_nothing)
	    compensate = true;
	}
    }
  else
    {
      icode = old_code;
      if (icode == CODE_FOR_nothing
	  && (code == PLUS || code == MINUS || code == XOR))
	{
	  icode = new_code;
	  if (icode != CODE_FOR_nothing)
	    compensate = true;
	}
    }

  /* If we found something supported, great.  */
  if (icode != CODE_FOR_nothing)
    {
      if (!target || !insn_data[icode].operand[0].predicate (target, mode))
	target = gen_reg_rtx (mode);

      if (GET_MODE (val) != VOIDmode && GET_MODE (val) != mode)
	val = convert_modes (mode, GET_MODE (val), val, 1);
      if (!insn_data[icode].operand[2].predicate (val, mode))
	val = force_reg (mode, val);

      insn = GEN_FCN (icode) (target, mem, val);
      if (insn)
	{
	  emit_insn (insn);

	  /* If we need to compensate for using an operation with the
	     wrong return value, do so now.  */
	  if (compensate)
	    {
	      if (!after)
		{
		  if (code == PLUS)
		    code = MINUS;
		  else if (code == MINUS)
		    code = PLUS;
		}

	      if (code == NOT)
		target = expand_simple_unop (mode, NOT, target, NULL_RTX, true);
	      target = expand_simple_binop (mode, code, target, val, NULL_RTX,
					    true, OPTAB_LIB_WIDEN);
	    }

	  return target;
	}
    }

  /* Failing that, generate a compare-and-swap loop in which we perform the
     operation with normal arithmetic instructions.  */
  if (sync_compare_and_swap[mode] != CODE_FOR_nothing)
    {
      rtx t0 = gen_reg_rtx (mode), t1;

      if (!target || !register_operand (target, mode))
	target = gen_reg_rtx (mode);

      start_sequence ();

      if (!after)
	emit_move_insn (target, t0);
      t1 = t0;
      if (code == NOT)
	{
	  t1 = expand_simple_unop (mode, NOT, t1, NULL_RTX, true);
	  code = AND;
	}
      t1 = expand_simple_binop (mode, code, t1, val, NULL_RTX,
				true, OPTAB_LIB_WIDEN);
      if (after)
	emit_move_insn (target, t1);

      insn = get_insns ();
      end_sequence ();

      if (t1 != NULL && expand_compare_and_swap_loop (mem, t0, t1, insn))
	return target;
    }

  return NULL_RTX;
}

/* This function expands a test-and-set operation.  Ideally we atomically
   store VAL in MEM and return the previous value in MEM.  Some targets
   may not support this operation and only support VAL with the constant 1;
   in this case while the return value will be 0/1, but the exact value
   stored in MEM is target defined.  TARGET is an option place to stick
   the return value.  */

rtx
expand_sync_lock_test_and_set (rtx mem, rtx val, rtx target)
{
  enum machine_mode mode = GET_MODE (mem);
  enum insn_code icode;
  rtx insn;

  /* If the target supports the test-and-set directly, great.  */
  icode = sync_lock_test_and_set[mode];
  if (icode != CODE_FOR_nothing)
    {
      if (!target || !insn_data[icode].operand[0].predicate (target, mode))
	target = gen_reg_rtx (mode);

      if (GET_MODE (val) != VOIDmode && GET_MODE (val) != mode)
	val = convert_modes (mode, GET_MODE (val), val, 1);
      if (!insn_data[icode].operand[2].predicate (val, mode))
	val = force_reg (mode, val);

      insn = GEN_FCN (icode) (target, mem, val);
      if (insn)
	{
	  emit_insn (insn);
	  return target;
	}
    }

  /* Otherwise, use a compare-and-swap loop for the exchange.  */
  if (sync_compare_and_swap[mode] != CODE_FOR_nothing)
    {
      if (!target || !register_operand (target, mode))
	target = gen_reg_rtx (mode);
      if (GET_MODE (val) != VOIDmode && GET_MODE (val) != mode)
	val = convert_modes (mode, GET_MODE (val), val, 1);
      if (expand_compare_and_swap_loop (mem, target, val, NULL_RTX))
	return target;
    }

  return NULL_RTX;
}

#include "gt-optabs.h"