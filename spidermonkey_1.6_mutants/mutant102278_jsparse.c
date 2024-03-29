/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/*
 * JS parser.
 *
 * This is a recursive-descent parser for the JavaScript language specified by
 * "The JavaScript 1.5 Language Specification".  It uses lexical and semantic
 * feedback to disambiguate non-LL(1) structures.  It generates trees of nodes
 * induced by the recursive parsing (not precise syntax trees, see jsparse.h).
 * After tree construction, it rewrites trees to fold constants and evaluate
 * compile-time expressions.  Finally, it calls js_EmitTree (see jsemit.h) to
 * generate bytecode.
 *
 * This parser attempts no error recovery.
 */
#include "jsstddef.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "jstypes.h"
#include "jsarena.h" /* Added by JSIFY */
#include "jsutil.h" /* Added by JSIFY */
#include "jsapi.h"
#include "jsatom.h"
#include "jscntxt.h"
#include "jsconfig.h"
#include "jsemit.h"
#include "jsfun.h"
#include "jsinterp.h"
#include "jslock.h"
#include "jsnum.h"
#include "jsobj.h"
#include "jsopcode.h"
#include "jsparse.h"
#include "jsscan.h"
#include "jsscope.h"
#include "jsscript.h"
#include "jsstr.h"

#if JS_HAS_XML_SUPPORT
#include "jsxml.h"
#endif

/*
 * JS parsers, from lowest to highest precedence.
 *
 * Each parser takes a context, a token stream, and a tree context struct.
 * Each returns a parse node tree or null on error.
 */

typedef JSParseNode *
JSParser(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc);

typedef JSParseNode *
JSMemberParser(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc,
               JSBool allowCallSyntax);

static JSParser FunctionStmt;
#if JS_HAS_LEXICAL_CLOSURE
static JSParser FunctionExpr;
#endif
static JSParser Statements;
static JSParser Statement;
static JSParser Variables;
static JSParser Expr;
static JSParser AssignExpr;
static JSParser CondExpr;
static JSParser OrExpr;
static JSParser AndExpr;
static JSParser BitOrExpr;
static JSParser BitXorExpr;
static JSParser BitAndExpr;
static JSParser EqExpr;
static JSParser RelExpr;
static JSParser ShiftExpr;
static JSParser AddExpr;
static JSParser MulExpr;
static JSParser UnaryExpr;
static JSMemberParser MemberExpr;
static JSParser PrimaryExpr;

/*
 * Insist that the next token be of type tt, or report errno and return null.
 * NB: this macro uses cx and ts from its lexical environment.
 */
#define MUST_MATCH_TOKEN(tt, errno)                                           \
    JS_BEGIN_MACRO                                                            \
        if (js_GetToken(cx, ts) != tt) {                                      \
            js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR, \
                            errno);                                           \
            return NULL;                                                      \
        }                                                                     \
    JS_END_MACRO

#define CHECK_RECURSION()                                                     \
    JS_BEGIN_MACRO                                                            \
        int stackDummy;                                                       \
        if (!JS_CHECK_STACK_SIZE(cx, stackDummy)) {                           \
            js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR, \
                                        JSMSG_OVER_RECURSED);                 \
            return NULL;                                                      \
        }                                                                     \
    JS_END_MACRO

#ifdef METER_PARSENODES
static uint32 parsenodes = 0;
static uint32 maxparsenodes = 0;
static uint32 recyclednodes = 0;
#endif

static JSParseNode *
RecycleTree(JSParseNode *pn, JSTreeContext *tc)
{
    JSParseNode *next;

    if (!pn)
        return NULL;
    JS_ASSERT(pn != tc->nodeList);      /* catch back-to-back dup recycles */
    next = pn->pn_next;
    pn->pn_next = tc->nodeList;
    tc->nodeList = pn;
#ifdef METER_PARSENODES
    recyclednodes++;
#endif
    return next;
}

static JSParseNode *
NewOrRecycledNode(JSContext *cx, JSTreeContext *tc)
{
    JSParseNode *pn;

    pn = tc->nodeList;
    if (!pn) {
        JS_ARENA_ALLOCATE_TYPE(pn, JSParseNode, &cx->tempPool);
        if (!pn)
            JS_ReportOutOfMemory(cx);
    } else {
        tc->nodeList = pn->pn_next;

        /* Recycle immediate descendents only, to save work and working set. */
        switch (pn->pn_arity) {
          case PN_FUNC:
            RecycleTree(pn->pn_body, tc);
            break;
          case PN_LIST:
            if (pn->pn_head) {
                /* XXX check for dup recycles in the list */
                *pn->pn_tail = tc->nodeList;
                tc->nodeList = pn->pn_head;
#ifdef METER_PARSENODES
                recyclednodes += pn->pn_count;
#endif
            }
            break;
          case PN_TERNARY:
            RecycleTree(pn->pn_kid1, tc);
            RecycleTree(pn->pn_kid2, tc);
            RecycleTree(pn->pn_kid3, tc);
            break;
          case PN_BINARY:
            RecycleTree(pn->pn_left, tc);
            RecycleTree(pn->pn_right, tc);
            break;
          case PN_UNARY:
            RecycleTree(pn->pn_kid, tc);
            break;
          case PN_NAME:
            RecycleTree(pn->pn_expr, tc);
            break;
          case PN_NULLARY:
            break;
        }
    }
#ifdef METER_PARSENODES
    if (pn) {
        parsenodes++;
        if (parsenodes - recyclednodes > maxparsenodes)
            maxparsenodes = parsenodes - recyclednodes;
    }
#endif
    return pn;
}

/*
 * Allocate a JSParseNode from cx's temporary arena.
 */
static JSParseNode *
NewParseNode(JSContext *cx, JSTokenStream *ts, JSParseNodeArity arity,
             JSTreeContext *tc)
{
    JSParseNode *pn;
    JSToken *tp;

    pn = NewOrRecycledNode(cx, tc);
    if (!pn)
        return NULL;
    tp = &CURRENT_TOKEN(ts);
    pn->pn_type = tp->type;
    pn->pn_pos = tp->pos;
    pn->pn_op = JSOP_NOP;
    pn->pn_arity = arity;
    pn->pn_next = NULL;
#if JS_HAS_XML_SUPPORT
    pn->pn_ts = ts;
#endif
    return pn;
}

static JSParseNode *
NewBinary(JSContext *cx, JSTokenType tt,
          JSOp op, JSParseNode *left, JSParseNode *right,
          JSTreeContext *tc)
{
    JSParseNode *pn, *pn1, *pn2;

    if (!left || !right)
        return NULL;

    /*
     * Flatten a left-associative (left-heavy) tree of a given operator into
     * a list, to reduce js_FoldConstants and js_EmitTree recursion.
     */
    if (left->pn_type == tt &&
        left->pn_op == op &&
        (js_CodeSpec[op].format & JOF_LEFTASSOC)) {
        if (left->pn_arity != PN_LIST) {
            pn1 = left->pn_left, pn2 = left->pn_right;
            left->pn_arity = PN_LIST;
            PN_INIT_LIST_1(left, pn1);
            PN_APPEND(left, pn2);
            if (tt == TOK_PLUS) {
                if (pn1->pn_type == TOK_STRING)
                    left->pn_extra |= PNX_STRCAT;
                else if (pn1->pn_type != TOK_NUMBER)
                    left->pn_extra |= PNX_CANTFOLD;
                if (pn2->pn_type == TOK_STRING)
                    left->pn_extra |= PNX_STRCAT;
                else if (pn2->pn_type != TOK_NUMBER)
                    left->pn_extra |= PNX_CANTFOLD;
            }
        }
        PN_APPEND(left, right);
        left->pn_pos.end = right->pn_pos.end;
        if (tt == TOK_PLUS) {
            if (right->pn_type == TOK_STRING)
                left->pn_extra |= PNX_STRCAT;
            else if (right->pn_type != TOK_NUMBER)
                left->pn_extra |= PNX_CANTFOLD;
        }
        return left;
    }

    /*
     * Fold constant addition immediately, to conserve node space and, what's
     * more, so js_FoldConstants never sees mixed addition and concatenation
     * operations with more than one leading non-string operand in a PN_LIST
     * generated for expressions such as 1 + 2 + "pt" (which should evaluate
     * to "3pt", not "12pt").
     */
    if (tt == TOK_PLUS &&
        left->pn_type == TOK_NUMBER &&
        right->pn_type == TOK_NUMBER) {
        left->pn_dval += right->pn_dval;
        left->pn_pos.end = right->pn_pos.end;
        RecycleTree(right, tc);
        return left;
    }

    pn = NewOrRecycledNode(cx, tc);
    if (!pn)
        return NULL;
    pn->pn_type = tt;
    pn->pn_pos.begin = left->pn_pos.begin;
    pn->pn_pos.end = right->pn_pos.end;
    pn->pn_op = op;
    pn->pn_arity = PN_BINARY;
    pn->pn_left = left;
    pn->pn_right = right;
    pn->pn_next = NULL;
#if JS_HAS_XML_SUPPORT
    pn->pn_ts = NULL;
#endif
    return pn;
}

#if JS_HAS_GETTER_SETTER
static JSTokenType
CheckGetterOrSetter(JSContext *cx, JSTokenStream *ts, JSTokenType tt)
{
    JSAtom *atom;
    JSRuntime *rt;
    JSOp op;
    const char *name;

    JS_ASSERT(CURRENT_TOKEN(ts).type == TOK_NAME);
    atom = CURRENT_TOKEN(ts).t_atom;
    rt = cx->runtime;
    if (atom == rt->atomState.getterAtom)
        op = JSOP_GETTER;
    else if (atom == rt->atomState.setterAtom)
        op = JSOP_SETTER;
    else
        return TOK_NAME;
    if (js_PeekTokenSameLine(cx, ts) != tt)
        return TOK_NAME;
    (void) js_GetToken(cx, ts);
    if (CURRENT_TOKEN(ts).t_op != JSOP_NOP) {
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_BAD_GETTER_OR_SETTER,
                                    (op == JSOP_GETTER)
                                    ? js_getter_str
                                    : js_setter_str);
        return TOK_ERROR;
    }
    CURRENT_TOKEN(ts).t_op = op;
    name = js_AtomToPrintableString(cx, atom);
    if (!name ||
        !js_ReportCompileErrorNumber(cx, ts,
                                     JSREPORT_TS |
                                     JSREPORT_WARNING |
                                     JSREPORT_STRICT,
                                     JSMSG_DEPRECATED_USAGE,
                                     name)) {
        return TOK_ERROR;
    }
    return tt;
}
#endif

/*
 * Parse a top-level JS script.
 */
JS_FRIEND_API(JSParseNode *)
js_ParseTokenStream(JSContext *cx, JSObject *chain, JSTokenStream *ts)
{
    JSStackFrame *fp, frame;
    JSTreeContext tc;
    JSParseNode *pn;

    /*
     * Push a compiler frame if we have no frames, or if the top frame is a
     * lightweight function activation, or if its scope chain doesn't match
     * the one passed to us.
     */
    fp = cx->fp;
    if (!fp || !fp->varobj || fp->scopeChain != chain) {
        memset(&frame, 0, sizeof frame);
        frame.varobj = frame.scopeChain = chain;
        if (cx->options & JSOPTION_VAROBJFIX) {
            while ((chain = JS_GetParent(cx, chain)) != NULL)
                frame.varobj = chain;
        }
        frame.down = fp;
        if (fp) {
            frame.flags = fp->flags & (JSFRAME_SPECIAL | JSFRAME_COMPILE_N_GO |
                                       JSFRAME_SCRIPT_OBJECT);
        }
        cx->fp = &frame;
    }

    /*
     * Protect atoms from being collected by a GC activation, which might
     * - nest on this thread due to out of memory (the so-called "last ditch"
     *   GC attempted within js_NewGCThing), or
     * - run for any reason on another thread if this thread is suspended on
     *   an object lock before it finishes generating bytecode into a script
     *   protected from the GC by a root or a stack frame reference.
     */
    JS_KEEP_ATOMS(cx->runtime);
    TREE_CONTEXT_INIT(&tc);
    pn = Statements(cx, ts, &tc);
    if (pn) {
        if (!js_MatchToken(cx, ts, TOK_EOF)) {
            js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                        JSMSG_SYNTAX_ERROR);
            pn = NULL;
        } else {
            pn->pn_type = TOK_LC;
            if (!js_FoldConstants(cx, pn, &tc))
                pn = NULL;
        }
    }

    TREE_CONTEXT_FINISH(&tc);
    JS_UNKEEP_ATOMS(cx->runtime);
    cx->fp = fp;
    return pn;
}

/*
 * Compile a top-level script.
 */
JS_FRIEND_API(JSBool)
js_CompileTokenStream(JSContext *cx, JSObject *chain, JSTokenStream *ts,
                      JSCodeGenerator *cg)
{
    JSStackFrame *fp, frame;
    uint32 flags;
    JSParseNode *pn;
    JSBool ok;
#ifdef METER_PARSENODES
    void *sbrk(ptrdiff_t), *before = sbrk(0);
#endif

    /*
     * Push a compiler frame if we have no frames, or if the top frame is a
     * lightweight function activation, or if its scope chain doesn't match
     * the one passed to us.
     */
    fp = cx->fp;
    if (!fp || !fp->varobj || fp->scopeChain != chain) {
        memset(&frame, 0, sizeof frame);
        frame.varobj = frame.scopeChain = chain;
        if (cx->options & JSOPTION_VAROBJFIX) {
            while ((chain = JS_GetParent(cx, chain)) != NULL)
                frame.varobj = chain;
        }
        frame.down = fp;
        if (fp) {
            frame.flags = fp->flags & (JSFRAME_SPECIAL | JSFRAME_COMPILE_N_GO |
                                       JSFRAME_SCRIPT_OBJECT);
        }
        cx->fp = &frame;
    }
    flags = cx->fp->flags;
    cx->fp->flags = flags |
                    (JS_HAS_COMPILE_N_GO_OPTION(cx)
                     ? JSFRAME_COMPILING | JSFRAME_COMPILE_N_GO
                     : JSFRAME_COMPILING);

    /* Prevent GC activation while compiling. */
    JS_KEEP_ATOMS(cx->runtime);

    pn = Statements(cx, ts, &cg->treeContext);
    if (!pn) {
        ok = JS_FALSE;
    } else if (!js_MatchToken(cx, ts, TOK_EOF)) {
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_SYNTAX_ERROR);
        ok = JS_FALSE;
    } else {
#ifdef METER_PARSENODES
        printf("Parser growth: %d (%u nodes, %u max, %u unrecycled)\n",
               (char *)sbrk(0) - (char *)before,
               parsenodes,
               maxparsenodes,
               parsenodes - recyclednodes);
        before = sbrk(0);
#endif

        /*
         * No need to emit code here -- Statements already has, for each
         * statement in turn.  Search for TCF_COMPILING in Statements, below.
         * That flag is set for every tc == &cg->treeContext, and it implies
         * that the tc can be downcast to a cg and used to emit code during
         * parsing, rather than at the end of the parse phase.
         */
        JS_ASSERT(cg->treeContext.flags & TCF_COMPILING);
        ok = JS_TRUE;
    }

#ifdef METER_PARSENODES
    printf("Code-gen growth: %d (%u bytecodes, %u srcnotes)\n",
           (char *)sbrk(0) - (char *)before, CG_OFFSET(cg), cg->noteCount);
#endif
#ifdef JS_ARENAMETER
    JS_DumpArenaStats(stdout);
#endif
    JS_UNKEEP_ATOMS(cx->runtime);
    cx->fp->flags = flags;
    cx->fp = fp;
    return ok;
}

/*
 * Insist on a final return before control flows out of pn, but don't be too
 * smart about loops (do {...; return e2;} while(0) at the end of a function
 * that contains an early return e1 will get a strict-option-only warning).
 */
#define ENDS_IN_OTHER   0
#define ENDS_IN_RETURN  1
#define ENDS_IN_BREAK   2

static int
HasFinalReturn(JSParseNode *pn)
{
    uintN rv, rv2, hasDefault;
    JSParseNode *pn2, *pn3;

    switch (pn->pn_type) {
      case TOK_LC:
        if (!pn->pn_head)
            return ENDS_IN_OTHER;
        return HasFinalReturn(PN_LAST(pn));

      case TOK_IF:
        rv = HasFinalReturn(pn->pn_kid2);
        if (pn->pn_kid3)
            rv &= HasFinalReturn(pn->pn_kid3);
        return rv;

#if JS_HAS_SWITCH_STATEMENT
      case TOK_SWITCH:
        rv = ENDS_IN_RETURN;
        hasDefault = ENDS_IN_OTHER;
        for (pn2 = pn->pn_kid2->pn_head; rv && pn2; pn2 = pn2->pn_next) {
            if (pn2->pn_type == TOK_DEFAULT)
                hasDefault = ENDS_IN_RETURN;
            pn3 = pn2->pn_right;
            JS_ASSERT(pn3->pn_type == TOK_LC);
            if (pn3->pn_head) {
                rv2 = HasFinalReturn(PN_LAST(pn3));
                if (rv2 == ENDS_IN_OTHER && pn2->pn_next)
                    /* Falling through to next case or default. */;
                else
                    rv &= rv2;
            }
        }
        /* If a final switch has no default case, we judge it harshly. */
        rv &= hasDefault;
        return rv;
#endif /* JS_HAS_SWITCH_STATEMENT */

      case TOK_BREAK:
        return ENDS_IN_BREAK;

      case TOK_WITH:
        return HasFinalReturn(pn->pn_right);

      case TOK_RETURN:
        return ENDS_IN_RETURN;

      case TOK_COLON:
        return HasFinalReturn(pn->pn_expr);

#if JS_HAS_EXCEPTIONS
      case TOK_THROW:
        return ENDS_IN_RETURN;

      case TOK_TRY:
        /* If we have a finally block that returns, we are done. */
        if (pn->pn_kid3) {
            rv = HasFinalReturn(pn->pn_kid3);
            if (rv == ENDS_IN_RETURN)
                return rv;
        }

        /* Else check the try block and any and all catch statements. */
        rv = HasFinalReturn(pn->pn_kid1);
        if (pn->pn_kid2)
            rv &= HasFinalReturn(pn->pn_kid2);
        return rv;

      case TOK_CATCH:
        /* Check this block's code and iterate over further catch blocks. */
        rv = HasFinalReturn(pn->pn_kid3);
        for (pn2 = pn->pn_kid2; pn2; pn2 = pn2->pn_kid2)
            rv &= HasFinalReturn(pn2->pn_kid3);
        return rv;
#endif

      default:
        return ENDS_IN_OTHER;
    }
}

static JSBool
ReportNoReturnValue(JSContext *cx, JSTokenStream *ts)
{
    JSFunction *fun;
    JSBool ok;

    fun = cx->fp->fun;
    if (fun->atom) {
        char *name = js_GetStringBytes(ATOM_TO_STRING(fun->atom));
        ok = js_ReportCompileErrorNumber(cx, ts,
                                         JSREPORT_TS |
                                         JSREPORT_WARNING |
                                         JSREPORT_STRICT,
                                         JSMSG_NO_RETURN_VALUE, name);
    } else {
        ok = js_ReportCompileErrorNumber(cx, ts,
                                         JSREPORT_TS |
                                         JSREPORT_WARNING |
                                         JSREPORT_STRICT,
                                         JSMSG_ANON_NO_RETURN_VALUE);
    }
    return ok;
}

static JSBool
CheckFinalReturn(JSContext *cx, JSTokenStream *ts, JSParseNode *pn)
{
    return HasFinalReturn(pn) == ENDS_IN_RETURN || ReportNoReturnValue(cx, ts);
}

static JSParseNode *
FunctionBody(JSContext *cx, JSTokenStream *ts, JSFunction *fun,
             JSTreeContext *tc)
{
    JSStackFrame *fp, frame;
    JSObject *funobj;
    uintN oldflags;
    JSParseNode *pn;

    fp = cx->fp;
    funobj = fun->object;
    if (!fp || fp->fun != fun || fp->varobj != funobj ||
        fp->scopeChain != funobj) {
        memset(&frame, 0, sizeof frame);
        frame.fun = fun;
        frame.varobj = frame.scopeChain = funobj;
        frame.down = fp;
        if (fp)
            frame.flags = fp->flags & JSFRAME_COMPILE_N_GO;
        cx->fp = &frame;
    }

    oldflags = tc->flags;
    tc->flags &= ~(TCF_RETURN_EXPR | TCF_RETURN_VOID);
    tc->flags |= TCF_IN_FUNCTION;
    pn = Statements(cx, ts, tc);

    /* Check for falling off the end of a function that returns a value. */
    if (pn && JS_HAS_STRICT_OPTION(cx) && (tc->flags & TCF_RETURN_EXPR)) {
        if (!CheckFinalReturn(cx, ts, pn))
            pn = NULL;
    }

    cx->fp = fp;
    tc->flags = oldflags | (tc->flags & (TCF_FUN_FLAGS | TCF_HAS_DEFXMLNS));
    return pn;
}

/*
 * Compile a JS function body, which might appear as the value of an event
 * handler attribute in an HTML <INPUT> tag.
 */
JSBool
js_CompileFunctionBody(JSContext *cx, JSTokenStream *ts, JSFunction *fun)
{
    JSArenaPool codePool, notePool;
    JSCodeGenerator funcg;
    JSStackFrame *fp, frame;
    JSObject *funobj;
    JSParseNode *pn;
    JSBool ok;

    JS_InitArenaPool(&codePool, "code", 1024, sizeof(jsbytecode));
    JS_InitArenaPool(&notePool, "note", 1024, sizeof(jssrcnote));
    if (!js_InitCodeGenerator(cx, &funcg, &codePool, &notePool,
                              ts->filename, ts->lineno,
                              ts->principals)) {
        return JS_FALSE;
    }

    /* Prevent GC activation while compiling. */
    JS_KEEP_ATOMS(cx->runtime);

    /* Push a JSStackFrame for use by FunctionBody. */
    fp = cx->fp;
    funobj = fun->object;
    JS_ASSERT(!fp || (fp->fun != fun && fp->varobj != funobj &&
                      fp->scopeChain != funobj));
    memset(&frame, 0, sizeof frame);
    frame.fun = fun;
    frame.varobj = frame.scopeChain = funobj;
    frame.down = fp;
    frame.flags = JS_HAS_COMPILE_N_GO_OPTION(cx)
                  ? JSFRAME_COMPILING | JSFRAME_COMPILE_N_GO
                  : JSFRAME_COMPILING;
    cx->fp = &frame;

    /* Ensure that the body looks like a block statement to js_EmitTree. */
    CURRENT_TOKEN(ts).type = TOK_LC;
    pn = FunctionBody(cx, ts, fun, &funcg.treeContext);
    if (!pn) {
        ok = JS_FALSE;
    } else {
        /*
         * No need to emit code here -- Statements (via FunctionBody) already
         * has.  See similar comment in js_CompileTokenStream, and bug 108257.
         */
        fun->u.script = js_NewScriptFromCG(cx, &funcg, fun);
        if (!fun->u.script) {
            ok = JS_FALSE;
        } else {
            fun->interpreted = JS_TRUE;
            if (funcg.treeContext.flags & TCF_FUN_HEAVYWEIGHT)
                fun->flags |= JSFUN_HEAVYWEIGHT;
            ok = JS_TRUE;
        }
    }

    /* Restore saved state and release code generation arenas. */
    cx->fp = fp;
    JS_UNKEEP_ATOMS(cx->runtime);
    js_FinishCodeGenerator(cx, &funcg);
    JS_FinishArenaPool(&codePool);
    JS_FinishArenaPool(&notePool);
    return ok;
}

static JSParseNode *
FunctionDef(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc,
            JSBool lambda)
{
    JSOp op, prevop;
    JSParseNode *pn, *body, *result;
    JSAtom *funAtom, *objAtom, *argAtom;
    JSStackFrame *fp;
    JSObject *varobj, *pobj;
    JSAtomListElement *ale;
    JSProperty *prop;
    JSFunction *fun;
    uintN dupflag;
    JSBool ok;
    JSTreeContext funtc;

    /* Make a TOK_FUNCTION node. */
#if JS_HAS_GETTER_SETTER
    op = CURRENT_TOKEN(ts).t_op;
#endif
    pn = NewParseNode(cx, ts, PN_FUNC, tc);
    if (!pn)
        return NULL;

    /* Scan the optional function name into funAtom. */
    funAtom = js_MatchToken(cx, ts, TOK_NAME) ? CURRENT_TOKEN(ts).t_atom : NULL;
#if !JS_HAS_LEXICAL_CLOSURE
    if (!funAtom && !lambda) {
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_SYNTAX_ERROR);
        return NULL;
    }
#endif

    /* Find the nearest variable-declaring scope and use it as our parent. */
    fp = cx->fp;
    varobj = fp->varobj;

    /*
     * Record names for function statements in tc->decls so we know when to
     * avoid optimizing variable references that might name a function.
     */
    if (!lambda && funAtom) {
        ATOM_LIST_SEARCH(ale, &tc->decls, funAtom);
        if (ale) {
            prevop = ALE_JSOP(ale);
            if (JS_HAS_STRICT_OPTION(cx) || prevop == JSOP_DEFCONST) {
                const char *name = js_AtomToPrintableString(cx, funAtom);
                if (!name ||
                    !js_ReportCompileErrorNumber(cx, ts,
                                                 (prevop != JSOP_DEFCONST)
                                                 ? JSREPORT_TS |
                                                   JSREPORT_WARNING |
                                                   JSREPORT_STRICT
                                                 : JSREPORT_TS | JSREPORT_ERROR,
                                                 JSMSG_REDECLARED_VAR,
                                                 (prevop == JSOP_DEFFUN ||
                                                  prevop == JSOP_CLOSURE)
                                                 ? js_function_str
                                                 : (prevop == JSOP_DEFCONST)
                                                 ? js_const_str
                                                 : js_var_str,
                                                 name)) {
                    return NULL;
                }
            }
            if (tc->topStmt && prevop == JSOP_DEFVAR)
                tc->flags |= TCF_FUN_CLOSURE_VS_VAR;
        } else {
            ale = js_IndexAtom(cx, funAtom, &tc->decls);
            if (!ale)
                return NULL;
        }
        ALE_SET_JSOP(ale, tc->topStmt ? JSOP_CLOSURE : JSOP_DEFFUN);

#if JS_HAS_LEXICAL_CLOSURE
        /*
         * A function nested at top level inside another's body needs only a
         * local variable to bind its name to its value, and not an activation
         * object property (it might also need the activation property, if the
         * outer function contains with statements, e.g., but the stack slot
         * wins when jsemit.c's BindNameToSlot can optimize a JSOP_NAME into a
         * JSOP_GETVAR bytecode).
         */
        if (!tc->topStmt && (tc->flags & TCF_IN_FUNCTION)) {
            JSScopeProperty *sprop;

            /*
             * Define a property on the outer function so that BindNameToSlot
             * can properly optimize accesses.
             */
            JS_ASSERT(OBJ_GET_CLASS(cx, varobj) == &js_FunctionClass);
            JS_ASSERT(fp->fun == (JSFunction *) JS_GetPrivate(cx, varobj));
            if (!js_LookupHiddenProperty(cx, varobj, ATOM_TO_JSID(funAtom),
                                         &pobj, &prop)) {
                return NULL;
            }
            if (prop)
                OBJ_DROP_PROPERTY(cx, pobj, prop);
            sprop = NULL;
            if (!prop ||
                pobj != varobj ||
                (sprop = (JSScopeProperty *)prop,
                 sprop->getter != js_GetLocalVariable)) {
                uintN sflags;

                /*
                 * Use SPROP_IS_DUPLICATE if there is a formal argument of the
                 * same name, so the decompiler can find the parameter name.
                 */
                sflags = (sprop && sprop->getter == js_GetArgument)
                         ? SPROP_IS_DUPLICATE | SPROP_HAS_SHORTID
                         : SPROP_HAS_SHORTID;
                if (!js_AddHiddenProperty(cx, varobj, ATOM_TO_JSID(funAtom),
                                          js_GetLocalVariable,
                                          js_SetLocalVariable,
                                          SPROP_INVALID_SLOT,
                                          JSPROP_PERMANENT | JSPROP_SHARED,
                                          sflags, fp->fun->nvars)) {
                    return NULL;
                }
                if (fp->fun->nvars == JS_BITMASK(16)) {
                    JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                         JSMSG_TOO_MANY_FUN_VARS);
                    return NULL;
                }
                fp->fun->nvars++;
            }
        }
#endif
    }

    fun = js_NewFunction(cx, NULL, NULL, 0, lambda ? JSFUN_LAMBDA : 0, varobj,
                         funAtom);
    if (!fun)
        return NULL;
#if JS_HAS_GETTER_SETTER
    if (op != JSOP_NOP)
        fun->flags |= (op == JSOP_GETTER) ? JSPROP_GETTER : JSPROP_SETTER;
#endif


    /*
     * Set interpreted early so js_EmitTree can test it to decide whether to
     * eliminate useless expressions.
     */
    fun->interpreted = JS_TRUE;

    /*
     * Atomize fun->object early to protect against a last-ditch GC under
     * js_LookupHiddenProperty.
     *
     * Absent use of the new scoped local GC roots API around compiler calls,
     * we need to atomize here to protect against a GC activation.  Atoms are
     * protected from GC during compilation by the JS_FRIEND_API entry points
     * in this file.  There doesn't seem to be any gain in switching from the
     * atom-keeping method to the bulkier, slower scoped local roots method.
     */
    objAtom = js_AtomizeObject(cx, fun->object, 0);
    if (!objAtom)
        return NULL;

    /* Now parse formal argument list and compute fun->nargs. */
    MUST_MATCH_TOKEN(TOK_LP, JSMSG_PAREN_BEFORE_FORMAL);
    if (!js_MatchToken(cx, ts, TOK_RP)) {
        do {
            MUST_MATCH_TOKEN(TOK_NAME, JSMSG_MISSING_FORMAL);
            argAtom = CURRENT_TOKEN(ts).t_atom;
            pobj = NULL;
            if (!js_LookupHiddenProperty(cx, fun->object, ATOM_TO_JSID(argAtom),
                                         &pobj, &prop)) {
                return NULL;
            }
            dupflag = 0;
            if (prop) {
                ok = JS_TRUE;
                if (pobj == fun->object &&
                    ((JSScopeProperty *) prop)->getter == js_GetArgument) {
                    const char *name = js_AtomToPrintableString(cx, argAtom);

                    /*
                     * A duplicate parameter name. We force a duplicate node
                     * on the SCOPE_LAST_PROP(scope) list with the same id,
                     * distinguished by the SPROP_IS_DUPLICATE flag, and not
                     * mapped by an entry in scope.
                     */
                    ok = name &&
                         js_ReportCompileErrorNumber(cx, ts,
                                                     JSREPORT_TS |
                                                     JSREPORT_WARNING |
                                                     JSREPORT_STRICT,
                                                     JSMSG_DUPLICATE_FORMAL,
                                                     name);

                    dupflag = SPROP_IS_DUPLICATE;
                }
                OBJ_DROP_PROPERTY(cx, pobj, prop);
                if (!ok)
                    return NULL;
                prop = NULL;
            }
            if (!js_AddHiddenProperty(cx, fun->object, ATOM_TO_JSID(argAtom),
                                      js_GetArgument, js_SetArgument,
                                      SPROP_INVALID_SLOT,
                                      JSPROP_PERMANENT | JSPROP_SHARED,
                                      dupflag | SPROP_HAS_SHORTID,
                                      fun->nargs)) {
                return NULL;
            }
            if (fun->nargs == JS_BITMASK(16)) {
                JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                     JSMSG_TOO_MANY_FUN_ARGS);
                return NULL;
            }
            fun->nargs++;
        } while (js_MatchToken(cx, ts, TOK_COMMA));

        MUST_MATCH_TOKEN(TOK_RP, JSMSG_PAREN_AFTER_FORMAL);
    }

    MUST_MATCH_TOKEN(TOK_LC, JSMSG_CURLY_BEFORE_BODY);
    pn->pn_pos.begin = CURRENT_TOKEN(ts).pos.begin;

    TREE_CONTEXT_INIT(&funtc);
    body = FunctionBody(cx, ts, fun, &funtc);
    if (!body)
        return NULL;

    MUST_MATCH_TOKEN(TOK_RC, JSMSG_CURLY_AFTER_BODY);
    pn->pn_pos.end = CURRENT_TOKEN(ts).pos.end;

#if JS_HAS_LEXICAL_CLOSURE
    /*
     * If we collected flags that indicate nested heavyweight functions, or
     * this function contains heavyweight-making statements (references to
     * __parent__ or __proto__; use of with, eval, import, or export; and
     * assignment to arguments), flag the function as heavyweight (requiring
     * a call object per invocation).
     */
    if (funtc.flags & TCF_FUN_HEAVYWEIGHT) {
        fun->flags |= JSFUN_HEAVYWEIGHT;
        tc->flags |= TCF_FUN_HEAVYWEIGHT;
    } else {
        /*
         * If this function is a named statement function not at top-level
         * (i.e. a JSOP_CLOSURE), or if it refers to unqualified names that
         * are not local args or vars (TCF_FUN_USES_NONLOCALS), then our
         * enclosing function, if any, must be heavyweight.
         */
        if ((!lambda && funAtom && tc->topStmt) ||
            (funtc.flags & TCF_FUN_USES_NONLOCALS)) {
            tc->flags |= TCF_FUN_HEAVYWEIGHT;
        }
    }
#endif

    result = pn;
#if JS_HAS_LEXICAL_CLOSURE
    if (lambda) {
        /*
         * ECMA ed. 3 standard: function expression, possibly anonymous.
         */
        op = funAtom ? JSOP_NAMEDFUNOBJ : JSOP_ANONFUNOBJ;
    } else if (!funAtom) {
        /*
         * If this anonymous function definition is *not* embedded within a
         * larger expression, we treat it as an expression statement, not as
         * a function declaration -- and not as a syntax error (as ECMA-262
         * Edition 3 would have it).  Backward compatibility trumps all.
         */
        result = NewParseNode(cx, ts, PN_UNARY, tc);
        if (!result)
            return NULL;
        result->pn_type = TOK_SEMI;
        result->pn_pos = pn->pn_pos;
        result->pn_kid = pn;
        op = JSOP_ANONFUNOBJ;
    } else if (tc->topStmt) {
        /*
         * ECMA ed. 3 extension: a function expression statement not at the
         * top level, e.g., in a compound statement such as the "then" part
         * of an "if" statement, binds a closure only if control reaches that
         * sub-statement.
         */
        op = JSOP_CLOSURE;
    } else
#endif
        op = JSOP_NOP;

    pn->pn_funAtom = objAtom;
    pn->pn_op = op;
    pn->pn_body = body;
    pn->pn_flags = funtc.flags & (TCF_FUN_FLAGS | TCF_HAS_DEFXMLNS);
    pn->pn_tryCount = funtc.tryCount;
    TREE_CONTEXT_FINISH(&funtc);
    return result;
}

static JSParseNode *
FunctionStmt(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    return FunctionDef(cx, ts, tc, JS_FALSE);
}

#if JS_HAS_LEXICAL_CLOSURE
static JSParseNode *
FunctionExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    return FunctionDef(cx, ts, tc, JS_TRUE);
}
#endif

/*
 * Parse the statements in a block, creating a TOK_LC node that lists the
 * statements' trees.  If called from block-parsing code, the caller must
 * match { before and } after.
 */
static JSParseNode *
Statements(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn, *pn2;
    JSTokenType tt;

    CHECK_RECURSION();

    pn = NewParseNode(cx, ts, PN_LIST, tc);
    if (!pn)
        return NULL;
    PN_INIT_LIST(pn);

    ts->flags |= TSF_OPERAND;
    while ((tt = js_PeekToken(cx, ts)) > TOK_EOF && tt != TOK_RC) {
        ts->flags &= ~TSF_OPERAND;
        pn2 = Statement(cx, ts, tc);
        if (!pn2) {
            if (ts->flags & TSF_EOF)
                ts->flags |= TSF_UNEXPECTED_EOF;
            return NULL;
        }
        ts->flags |= TSF_OPERAND;

        /* If compiling top-level statements, emit as we go to save space. */
        if (!tc->topStmt && (tc->flags & TCF_COMPILING)) {
            if (cx->fp->fun &&
                JS_HAS_STRICT_OPTION(cx) &&
                (tc->flags & TCF_RETURN_EXPR)) {
                /*
                 * Check pn2 for lack of a final return statement if it is the
                 * last statement in the block.
                 */
                tt = js_PeekToken(cx, ts);
                if ((tt == TOK_EOF || tt == TOK_RC) &&
                    !CheckFinalReturn(cx, ts, pn2)) {
                    tt = TOK_ERROR;
                    break;
                }

                /*
                 * Clear TCF_RETURN_EXPR so FunctionBody doesn't try to
                 * CheckFinalReturn again.
                 */
                tc->flags &= ~TCF_RETURN_EXPR;
            }
            if (!js_FoldConstants(cx, pn2, tc) ||
                !js_AllocTryNotes(cx, (JSCodeGenerator *)tc) ||
                !js_EmitTree(cx, (JSCodeGenerator *)tc, pn2)) {
                tt = TOK_ERROR;
                break;
            }
            RecycleTree(pn2, tc);
        } else {
            PN_APPEND(pn, pn2);
        }
    }
    ts->flags &= ~TSF_OPERAND;
    if (tt == TOK_ERROR)
        return NULL;

    pn->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
    return pn;
}

static JSParseNode *
Condition(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn, *pn2;

    MUST_MATCH_TOKEN(TOK_LP, JSMSG_PAREN_BEFORE_COND);
    pn = Expr(cx, ts, tc);
    if (!pn)
        return NULL;
    MUST_MATCH_TOKEN(TOK_RP, JSMSG_PAREN_AFTER_COND);

    /*
     * Check for (a = b) and "correct" it to (a == b) iff b's operator has
     * greater precedence than ==.
     * XXX not ECMA, but documented in several books -- now a strict warning.
     */
    if (pn->pn_type == TOK_ASSIGN &&
        pn->pn_op == JSOP_NOP &&
        pn->pn_right->pn_type > TOK_EQOP)
    {
        JSBool rewrite = !JS_VERSION_IS_ECMA(cx);
        if (!js_ReportCompileErrorNumber(cx, ts,
                                         JSREPORT_TS |
                                         JSREPORT_WARNING |
                                         JSREPORT_STRICT,
                                         JSMSG_EQUAL_AS_ASSIGN,
                                         rewrite
                                         ? "\nAssuming equality test"
                                         : "")) {
            return NULL;
        }
        if (rewrite) {
            pn->pn_type = TOK_EQOP;
            pn->pn_op = (JSOp)cx->jsop_eq;
            pn2 = pn->pn_left;
            switch (pn2->pn_op) {
              case JSOP_SETNAME:
                pn2->pn_op = JSOP_NAME;
                break;
              case JSOP_SETPROP:
                pn2->pn_op = JSOP_GETPROP;
                break;
              case JSOP_SETELEM:
                pn2->pn_op = JSOP_GETELEM;
                break;
              default:
                JS_ASSERT(0);
            }
        }
    }
    return pn;
}

static JSBool
MatchLabel(JSContext *cx, JSTokenStream *ts, JSParseNode *pn)
{
    JSAtom *label;
#if JS_HAS_LABEL_STATEMENT
    JSTokenType tt;

    tt = js_PeekTokenSameLine(cx, ts);
    if (tt == TOK_ERROR)
        return JS_FALSE;
    if (tt == TOK_NAME) {
        (void) js_GetToken(cx, ts);
        label = CURRENT_TOKEN(ts).t_atom;
    } else {
        label = NULL;
    }
#else
    label = NULL;
#endif
    pn->pn_atom = label;
    return JS_TRUE;
}

#if JS_HAS_EXPORT_IMPORT
static JSParseNode *
ImportExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn, *pn2;
    JSTokenType tt;

    MUST_MATCH_TOKEN(TOK_NAME, JSMSG_NO_IMPORT_NAME);
    pn = NewParseNode(cx, ts, PN_NAME, tc);
    if (!pn)
        return NULL;
    pn->pn_op = JSOP_NAME;
    pn->pn_atom = CURRENT_TOKEN(ts).t_atom;
    pn->pn_expr = NULL;
    pn->pn_slot = -1;
    pn->pn_attrs = 0;

    ts->flags |= TSF_OPERAND;
    while ((tt = js_GetToken(cx, ts)) == TOK_DOT || tt == TOK_LB) {
        ts->flags &= ~TSF_OPERAND;
        if (pn->pn_op == JSOP_IMPORTALL)
            goto bad_import;

        if (tt == TOK_DOT) {
            pn2 = NewParseNode(cx, ts, PN_NAME, tc);
            if (!pn2)
                return NULL;
            if (js_MatchToken(cx, ts, TOK_STAR)) {
                pn2->pn_op = JSOP_IMPORTALL;
                pn2->pn_atom = NULL;
            } else {
                MUST_MATCH_TOKEN(TOK_NAME, JSMSG_NAME_AFTER_DOT);
                pn2->pn_op = JSOP_GETPROP;
                pn2->pn_atom = CURRENT_TOKEN(ts).t_atom;
                pn2->pn_slot = -1;
                pn2->pn_attrs = 0;
            }
            pn2->pn_expr = pn;
            pn2->pn_pos.begin = pn->pn_pos.begin;
            pn2->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
        } else {
            /* Make a TOK_LB binary node. */
            pn2 = NewBinary(cx, tt, JSOP_GETELEM, pn, Expr(cx, ts, tc), tc);
            if (!pn2)
                return NULL;

            MUST_MATCH_TOKEN(TOK_RB, JSMSG_BRACKET_IN_INDEX);
        }

        pn = pn2;
        ts->flags |= TSF_OPERAND;
    }
    ts->flags &= ~TSF_OPERAND;
    if (tt == TOK_ERROR)
        return NULL;
    js_UngetToken(ts);

    switch (pn->pn_op) {
      case JSOP_GETPROP:
        pn->pn_op = JSOP_IMPORTPROP;
        break;
      case JSOP_GETELEM:
        pn->pn_op = JSOP_IMPORTELEM;
        break;
      case JSOP_IMPORTALL:
        break;
      default:
        goto bad_import;
    }
    return pn;

  bad_import:
    js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                JSMSG_BAD_IMPORT);
    return NULL;
}
#endif /* JS_HAS_EXPORT_IMPORT */

extern const char js_with_statement_str[];

static JSParseNode *
Statement(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSTokenType tt;
    JSParseNode *pn, *pn1, *pn2, *pn3, *pn4;
    JSStmtInfo stmtInfo, *stmt, *stmt2;
    JSAtom *label;

    CHECK_RECURSION();

    ts->flags |= TSF_OPERAND;
    tt = js_GetToken(cx, ts);
    ts->flags &= ~TSF_OPERAND;

#if JS_HAS_GETTER_SETTER
    if (tt == TOK_NAME) {
        tt = CheckGetterOrSetter(cx, ts, TOK_FUNCTION);
        if (tt == TOK_ERROR)
            return NULL;
    }
#endif

    switch (tt) {
#if JS_HAS_EXPORT_IMPORT
      case TOK_EXPORT:
        pn = NewParseNode(cx, ts, PN_LIST, tc);
        if (!pn)
            return NULL;
        PN_INIT_LIST(pn);
        if (js_MatchToken(cx, ts, TOK_STAR)) {
            pn2 = NewParseNode(cx, ts, PN_NULLARY, tc);
            if (!pn2)
                return NULL;
            PN_APPEND(pn, pn2);
        } else {
            do {
                MUST_MATCH_TOKEN(TOK_NAME, JSMSG_NO_EXPORT_NAME);
                pn2 = NewParseNode(cx, ts, PN_NAME, tc);
                if (!pn2)
                    return NULL;
                pn2->pn_op = JSOP_NAME;
                pn2->pn_atom = CURRENT_TOKEN(ts).t_atom;
                pn2->pn_expr = NULL;
                pn2->pn_slot = -1;
                pn2->pn_attrs = 0;
                PN_APPEND(pn, pn2);
            } while (js_MatchToken(cx, ts, TOK_COMMA));
        }
        pn->pn_pos.end = PN_LAST(pn)->pn_pos.end;
        tc->flags |= TCF_FUN_HEAVYWEIGHT;
        break;

      case TOK_IMPORT:
        pn = NewParseNode(cx, ts, PN_LIST, tc);
        if (!pn)
            return NULL;
        PN_INIT_LIST(pn);
        do {
            pn2 = ImportExpr(cx, ts, tc);
            if (!pn2)
                return NULL;
            PN_APPEND(pn, pn2);
        } while (js_MatchToken(cx, ts, TOK_COMMA));
        pn->pn_pos.end = PN_LAST(pn)->pn_pos.end;
        tc->flags |= TCF_FUN_HEAVYWEIGHT;
        break;
#endif /* JS_HAS_EXPORT_IMPORT */

      case TOK_FUNCTION:
#if JS_HAS_XML_SUPPORT
        if (js_PeekToken(cx, ts) == TOK_DBLCOLON)
            goto expression;
#endif
        return FunctionStmt(cx, ts, tc);

      case TOK_IF:
        /* An IF node has three kids: condition, then, and optional else. */
        pn = NewParseNode(cx, ts, PN_TERNARY, tc);
        if (!pn)
            return NULL;
        pn1 = Condition(cx, ts, tc);
        if (!pn1)
            return NULL;
        js_PushStatement(tc, &stmtInfo, STMT_IF, -1);
        pn2 = Statement(cx, ts, tc);
        if (!pn2)
            return NULL;
        if (js_MatchToken(cx, ts, TOK_ELSE)) {
            stmtInfo.type = STMT_ELSE;
            pn3 = Statement(cx, ts, tc);
            if (!pn3)
                return NULL;
            pn->pn_pos.end = pn3->pn_pos.end;
        } else {
            pn3 = NULL;
            pn->pn_pos.end = pn2->pn_pos.end;
        }
        js_PopStatement(tc);
        pn->pn_kid1 = pn1;
        pn->pn_kid2 = pn2;
        pn->pn_kid3 = pn3;
        return pn;

#if JS_HAS_SWITCH_STATEMENT
      case TOK_SWITCH:
      {
        JSParseNode *pn5;
        JSBool seenDefault = JS_FALSE;

        pn = NewParseNode(cx, ts, PN_BINARY, tc);
        if (!pn)
            return NULL;
        MUST_MATCH_TOKEN(TOK_LP, JSMSG_PAREN_BEFORE_SWITCH);

        /* pn1 points to the switch's discriminant. */
        pn1 = Expr(cx, ts, tc);
        if (!pn1)
            return NULL;

        MUST_MATCH_TOKEN(TOK_RP, JSMSG_PAREN_AFTER_SWITCH);
        MUST_MATCH_TOKEN(TOK_LC, JSMSG_CURLY_BEFORE_SWITCH);

        /* pn2 is a list of case nodes. The default case has pn_left == NULL */
        pn2 = NewParseNode(cx, ts, PN_LIST, tc);
        if (!pn2)
            return NULL;
        PN_INIT_LIST(pn2);

        js_PushStatement(tc, &stmtInfo, STMT_SWITCH, -1);

        while ((tt = js_GetToken(cx, ts)) != TOK_RC) {
            switch (tt) {
              case TOK_DEFAULT:
                if (seenDefault) {
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_TOO_MANY_DEFAULTS);
                    return NULL;
                }
                seenDefault = JS_TRUE;
                /* fall through */

              case TOK_CASE:
                pn3 = NewParseNode(cx, ts, PN_BINARY, tc);
                if (!pn3)
                    return NULL;
                if (tt == TOK_DEFAULT) {
                    pn3->pn_left = NULL;
                } else {
                    pn3->pn_left = Expr(cx, ts, tc);
                    if (!pn3->pn_left)
                        return NULL;
                }
                PN_APPEND(pn2, pn3);
                if (pn2->pn_count == JS_BIT(16)) {
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_TOO_MANY_CASES);
                    return NULL;
                }
                break;

              case TOK_ERROR:
                return NULL;

              default:
                js_ReportCompileErrorNumber(cx, ts,
                                            JSREPORT_TS | JSREPORT_ERROR,
                                            JSMSG_BAD_SWITCH);
                return NULL;
            }
            MUST_MATCH_TOKEN(TOK_COLON, JSMSG_COLON_AFTER_CASE);

            pn4 = NewParseNode(cx, ts, PN_LIST, tc);
            if (!pn4)
                return NULL;
            pn4->pn_type = TOK_LC;
            PN_INIT_LIST(pn4);
            ts->flags |= TSF_OPERAND;
            while ((tt = js_PeekToken(cx, ts)) != TOK_RC &&
                   tt != TOK_CASE && tt != TOK_DEFAULT) {
                ts->flags &= ~TSF_OPERAND;
                if (tt == TOK_ERROR)
                    return NULL;
                pn5 = Statement(cx, ts, tc);
                if (!pn5)
                    return NULL;
                pn4->pn_pos.end = pn5->pn_pos.end;
                PN_APPEND(pn4, pn5);
                ts->flags |= TSF_OPERAND;
            }
            ts->flags &= ~TSF_OPERAND;

            /* Fix the PN_LIST so it doesn't begin at the TOK_COLON. */
            if (pn4->pn_head)
                pn4->pn_pos.begin = pn4->pn_head->pn_pos.begin;
            pn3->pn_pos.end = pn4->pn_pos.end;
            pn3->pn_right = pn4;
        }

        js_PopStatement(tc);

        pn->pn_pos.end = pn2->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
        pn->pn_kid1 = pn1;
        pn->pn_kid2 = pn2;
        return pn;
      }
#endif /* JS_HAS_SWITCH_STATEMENT */

      case TOK_WHILE:
        pn = NewParseNode(cx, ts, PN_BINARY, tc);
        if (!pn)
            return NULL;
        js_PushStatement(tc, &stmtInfo, STMT_WHILE_LOOP, -1);
        pn2 = Condition(cx, ts, tc);
        if (!pn2)
            return NULL;
        pn->pn_left = pn2;
        pn2 = Statement(cx, ts, tc);
        if (!pn2)
            return NULL;
        js_PopStatement(tc);
        pn->pn_pos.end = pn2->pn_pos.end;
        pn->pn_right = pn2;
        return pn;

#if JS_HAS_DO_WHILE_LOOP
      case TOK_DO:
        pn = NewParseNode(cx, ts, PN_BINARY, tc);
        if (!pn)
            return NULL;
        js_PushStatement(tc, &stmtInfo, STMT_DO_LOOP, -1);
        pn2 = Statement(cx, ts, tc);
        if (!pn2)
            return NULL;
        pn->pn_left = pn2;
        MUST_MATCH_TOKEN(TOK_WHILE, JSMSG_WHILE_AFTER_DO);
        pn2 = Condition(cx, ts, tc);
        if (!pn2)
            return NULL;
        js_PopStatement(tc);
        pn->pn_pos.end = pn2->pn_pos.end;
        pn->pn_right = pn2;
        if ((cx->version & JSVERSION_MASK) != JSVERSION_ECMA_3) {
            /*
             * All legacy and extended versions must do automatic semicolon
             * insertion after do-while.  See the testcase and discussion in
             * http://bugzilla.mozilla.org/show_bug.cgi?id=238945.
             */
            (void) js_MatchToken(cx, ts, TOK_SEMI);
            return pn;
        }
        break;
#endif /* JS_HAS_DO_WHILE_LOOP */

      case TOK_FOR:
        /* A FOR node is binary, left is loop control and right is the body. */
        pn = NewParseNode(cx, ts, PN_BINARY, tc);
        if (!pn)
            return NULL;
        js_PushStatement(tc, &stmtInfo, STMT_FOR_LOOP, -1);

#if JS_HAS_XML_SUPPORT
        pn->pn_op = JSOP_NOP;
        if (js_MatchToken(cx, ts, TOK_NAME)) {
            if (CURRENT_TOKEN(ts).t_atom == cx->runtime->atomState.eachAtom)
                pn->pn_op = JSOP_FOREACH;
            else
                js_UngetToken(ts);
        }
#endif

        MUST_MATCH_TOKEN(TOK_LP, JSMSG_PAREN_AFTER_FOR);
        ts->flags |= TSF_OPERAND;
        tt = js_PeekToken(cx, ts);
        ts->flags &= ~TSF_OPERAND;
        if (tt == TOK_SEMI) {
#if JS_HAS_XML_SUPPORT
            if (pn->pn_op == JSOP_FOREACH)
                goto bad_for_each;
#endif

            /* No initializer -- set first kid of left sub-node to null. */
            pn1 = NULL;
        } else {
            /* Set pn1 to a var list or an initializing expression. */
#if JS_HAS_IN_OPERATOR
            /*
             * Set the TCF_IN_FOR_INIT flag during parsing of the first clause
             * of the for statement.  This flag will be used by the RelExpr
             * production; if it is set, then the 'in' keyword will not be
             * recognized as an operator, leaving it available to be parsed as
             * part of a for/in loop.  A side effect of this restriction is
             * that (unparenthesized) expressions involving an 'in' operator
             * are illegal in the init clause of an ordinary for loop.
             */
            tc->flags |= TCF_IN_FOR_INIT;
#endif /* JS_HAS_IN_OPERATOR */
            if (tt == TOK_VAR) {
                (void) js_GetToken(cx, ts);
                pn1 = Variables(cx, ts, tc);
            } else {
                pn1 = Expr(cx, ts, tc);
            }
#if JS_HAS_IN_OPERATOR
            tc->flags &= ~TCF_IN_FOR_INIT;
#endif /* JS_HAS_IN_OPERATOR */
            if (!pn1)
                return NULL;
        }

        /*
         * We can be sure that it's a for/in loop if there's still an 'in'
         * keyword here, even if JavaScript recognizes 'in' as an operator,
         * as we've excluded 'in' from being parsed in RelExpr by setting
         * the TCF_IN_FOR_INIT flag in our JSTreeContext.
         */
        if (pn1 && js_MatchToken(cx, ts, TOK_IN)) {
            stmtInfo.type = STMT_FOR_IN_LOOP;

            /* Check that the left side of the 'in' is valid. */
            while (pn1->pn_type == TOK_RP)
                pn1 = pn1->pn_kid;
            if ((pn1->pn_type == TOK_VAR)
                ? (pn1->pn_count > 1 || pn1->pn_op == JSOP_DEFCONST)
                : (pn1->pn_type != TOK_NAME &&
                   pn1->pn_type != TOK_DOT &&
#if JS_HAS_LVALUE_RETURN
                   pn1->pn_type != TOK_LP &&
#endif
#if JS_HAS_XML_SUPPORT
                   (pn1->pn_type != TOK_UNARYOP ||
                    pn1->pn_op != JSOP_XMLNAME) &&
#endif
                   pn1->pn_type != TOK_LB)) {
                js_ReportCompileErrorNumber(cx, ts,
                                            JSREPORT_TS | JSREPORT_ERROR,
                                            JSMSG_BAD_FOR_LEFTSIDE);
                return NULL;
            }

            if (pn1->pn_type == TOK_VAR) {
                /* Tell js_EmitTree(TOK_VAR) that pn1 is part of a for/in. */
                pn1->pn_extra |= PNX_FORINVAR;

                /* Generate a final POP only if the var has an initializer. */
                pn2 = pn1->pn_head;
                if (pn2->pn_expr)
                    pn1->pn_extra |= PNX_POPVAR;
            } else {
                pn2 = pn1;
#if JS_HAS_LVALUE_RETURN
                if (pn2->pn_type == TOK_LP)
                    pn2->pn_op = JSOP_SETCALL;
#endif
#if JS_HAS_XML_SUPPORT
                if (pn2->pn_type == TOK_UNARYOP)
                    pn2->pn_op = JSOP_BINDXMLNAME;
#endif
            }

            /* Beware 'for (arguments in ...)' with or without a 'var'. */
            if (pn2->pn_type == TOK_NAME &&
                pn2->pn_atom == cx->runtime->atomState.argumentsAtom) {
                tc->flags |= TCF_FUN_HEAVYWEIGHT;
            }

            /* Parse the object expression as the right operand of 'in'. */
            pn2 = NewBinary(cx, TOK_IN, JSOP_NOP, pn1, Expr(cx, ts, tc), tc);
            if (!pn2)
                return NULL;
            pn->pn_left = pn2;
        } else {
#if JS_HAS_XML_SUPPORT
            if (pn->pn_op == JSOP_FOREACH)
                goto bad_for_each;
#endif

            /* Parse the loop condition or null into pn2. */
            MUST_MATCH_TOKEN(TOK_SEMI, JSMSG_SEMI_AFTER_FOR_INIT);
            ts->flags |= TSF_OPERAND;
            tt = js_PeekToken(cx, ts);
            ts->flags &= ~TSF_OPERAND;
            if (tt == TOK_SEMI) {
                pn2 = NULL;
            } else {
                pn2 = Expr(cx, ts, tc);
                if (!pn2)
                    return NULL;
            }

            /* Parse the update expression or null into pn3. */
            MUST_MATCH_TOKEN(TOK_SEMI, JSMSG_SEMI_AFTER_FOR_COND);
            ts->flags |= TSF_OPERAND;
            tt = js_PeekToken(cx, ts);
            ts->flags &= ~TSF_OPERAND;
            if (tt == TOK_RP) {
                pn3 = NULL;
            } else {
                pn3 = Expr(cx, ts, tc);
                if (!pn3)
                    return NULL;
            }

            /* Build the RESERVED node to use as the left kid of pn. */
            pn4 = NewParseNode(cx, ts, PN_TERNARY, tc);
            if (!pn4)
                return NULL;
            pn4->pn_type = TOK_RESERVED;
            pn4->pn_op = JSOP_NOP;
            pn4->pn_kid1 = pn1;
            pn4->pn_kid2 = pn2;
            pn4->pn_kid3 = pn3;
            pn->pn_left = pn4;
        }

        MUST_MATCH_TOKEN(TOK_RP, JSMSG_PAREN_AFTER_FOR_CTRL);

        /* Parse the loop body into pn->pn_right. */
        pn2 = Statement(cx, ts, tc);
        if (!pn2)
            return NULL;
        pn->pn_right = pn2;
        js_PopStatement(tc);

        /* Record the absolute line number for source note emission. */
        pn->pn_pos.end = pn2->pn_pos.end;
        return pn;

#if JS_HAS_XML_SUPPORT
      bad_for_each:
        js_ReportCompileErrorNumber(cx, pn,
                                    JSREPORT_PN | JSREPORT_ERROR,
                                    JSMSG_BAD_FOR_EACH_LOOP);
        return NULL;
#endif

#if JS_HAS_EXCEPTIONS
      case TOK_TRY: {
        JSParseNode *catchtail = NULL;
        /*
         * try nodes are ternary.
         * kid1 is the try Statement
         * kid2 is the catch node
         * kid3 is the finally Statement
         *
         * catch nodes are ternary.
         * kid1 is the discriminant
         * kid2 is the next catch node, or NULL
         * kid3 is the catch block (on kid3 so that we can always append a
         *                          new catch pn on catchtail->kid2)
         *
         * catch discriminant nodes are binary
         * atom is the receptacle
         * expr is the discriminant code
         *
         * finally nodes are unary (just the finally expression)
         */
        pn = NewParseNode(cx, ts, PN_TERNARY, tc);
        pn->pn_op = JSOP_NOP;

        MUST_MATCH_TOKEN(TOK_LC, JSMSG_CURLY_BEFORE_TRY);
        js_PushStatement(tc, &stmtInfo, STMT_TRY, -1);
        pn->pn_kid1 = Statements(cx, ts, tc);
        if (!pn->pn_kid1)
            return NULL;
        MUST_MATCH_TOKEN(TOK_RC, JSMSG_CURLY_AFTER_TRY);
        js_PopStatement(tc);

        catchtail = pn;
        while (js_PeekToken(cx, ts) == TOK_CATCH) {
            /* check for another catch after unconditional catch */
            if (catchtail != pn && !catchtail->pn_kid1->pn_expr) {
                js_ReportCompileErrorNumber(cx, ts,
                                            JSREPORT_TS | JSREPORT_ERROR,
                                            JSMSG_CATCH_AFTER_GENERAL);
                return NULL;
            }

            /*
             * legal catch forms are:
             * catch (v)
             * catch (v if <boolean_expression>)
             * (the latter is legal only #ifdef JS_HAS_CATCH_GUARD)
             */
            (void) js_GetToken(cx, ts); /* eat `catch' */
            pn2 = NewParseNode(cx, ts, PN_TERNARY, tc);
            if (!pn2)
                return NULL;

            /*
             * We use a PN_NAME for the discriminant (catchguard) node
             * with the actual discriminant code in the initializer spot
             */
            MUST_MATCH_TOKEN(TOK_LP, JSMSG_PAREN_BEFORE_CATCH);
            MUST_MATCH_TOKEN(TOK_NAME, JSMSG_CATCH_IDENTIFIER);
            pn3 = NewParseNode(cx, ts, PN_NAME, tc);
            if (!pn3)
                return NULL;

            pn3->pn_atom = CURRENT_TOKEN(ts).t_atom;
            pn3->pn_expr = NULL;
#if JS_HAS_CATCH_GUARD
            /*
             * We use `catch (x if x === 5)' (not `catch (x : x === 5)') to
             * avoid conflicting with the JS2/ECMA2 proposed catchguard syntax.
             */
            if (js_PeekToken(cx, ts) == TOK_IF) {
                (void)js_GetToken(cx, ts); /* eat `if' */
                pn3->pn_expr = Expr(cx, ts, tc);
                if (!pn3->pn_expr)
                    return NULL;
            }
#endif
            pn2->pn_kid1 = pn3;

            MUST_MATCH_TOKEN(TOK_RP, JSMSG_PAREN_AFTER_CATCH);

            MUST_MATCH_TOKEN(TOK_LC, JSMSG_CURLY_BEFORE_CATCH);
            js_PushStatement(tc, &stmtInfo, STMT_CATCH, -1);
            stmtInfo.label = pn3->pn_atom;
            pn2->pn_kid3 = Statements(cx, ts, tc);
            if (!pn2->pn_kid3)
                return NULL;
            MUST_MATCH_TOKEN(TOK_RC, JSMSG_CURLY_AFTER_CATCH);
            js_PopStatement(tc);

            catchtail = catchtail->pn_kid2 = pn2;
        }
        catchtail->pn_kid2 = NULL;

        if (js_MatchToken(cx, ts, TOK_FINALLY)) {
            tc->tryCount++;
            MUST_MATCH_TOKEN(TOK_LC, JSMSG_CURLY_BEFORE_FINALLY);
            js_PushStatement(tc, &stmtInfo, STMT_FINALLY, -1);
            pn->pn_kid3 = Statements(cx, ts, tc);
            if (!pn->pn_kid3)
                return NULL;
            MUST_MATCH_TOKEN(TOK_RC, JSMSG_CURLY_AFTER_FINALLY);
            js_PopStatement(tc);
        } else {
            pn->pn_kid3 = NULL;
        }
        if (!pn->pn_kid2 && !pn->pn_kid3) {
            js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                        JSMSG_CATCH_OR_FINALLY);
            return NULL;
        }
        tc->tryCount++;
        return pn;
      }

      case TOK_THROW:
        pn = NewParseNode(cx, ts, PN_UNARY, tc);
        if (!pn)
            return NULL;

        /* ECMA-262 Edition 3 says 'throw [no LineTerminator here] Expr'. */
        ts->flags |= TSF_OPERAND;
        tt = js_PeekTokenSameLine(cx, ts);
        ts->flags &= ~TSF_OPERAND;
        if (tt == TOK_ERROR)
            return NULL;
        if (tt == TOK_EOF || tt == TOK_EOL || tt == TOK_SEMI || tt == TOK_RC) {
            js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                        JSMSG_SYNTAX_ERROR);
            return NULL;
        }

        pn2 = Expr(cx, ts, tc);
        if (!pn2)
            return NULL;
        pn->pn_pos.end = pn2->pn_pos.end;
        pn->pn_op = JSOP_THROW;
        pn->pn_kid = pn2;
        break;

      /* TOK_CATCH and TOK_FINALLY are both handled in the TOK_TRY case */
      case TOK_CATCH:
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_CATCH_WITHOUT_TRY);
        return NULL;

      case TOK_FINALLY:
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_FINALLY_WITHOUT_TRY);
        return NULL;

#endif /* JS_HAS_EXCEPTIONS */

      case TOK_BREAK:
        pn = NewParseNode(cx, ts, PN_NULLARY, tc);
        if (!pn)
            return NULL;
        if (!MatchLabel(cx, ts, pn))
            return NULL;
        stmt = tc->topStmt;
        label = pn->pn_atom;
        if (label) {
            for (; ; stmt = stmt->down) {
                if (!stmt) {
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_LABEL_NOT_FOUND);
                    return NULL;
                }
                if (stmt->type == STMT_LABEL && stmt->label == label)
                    break;
            }
        } else {
            for (; ; stmt = stmt->down) {
                if (!stmt) {
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_TOUGH_BREAK);
                    return NULL;
                }
                if (STMT_IS_LOOP(stmt) || stmt->type == STMT_SWITCH)
                    break;
            }
        }
        if (label)
            pn->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
        break;

      case TOK_CONTINUE:
        pn = NewParseNode(cx, ts, PN_NULLARY, tc);
        if (!pn)
            return NULL;
        if (!MatchLabel(cx, ts, pn))
            return NULL;
        stmt = tc->topStmt;
        label = pn->pn_atom;
        if (label) {
            for (stmt2 = NULL; ; stmt = stmt->down) {
                if (!stmt) {
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_LABEL_NOT_FOUND);
                    return NULL;
                }
                if (stmt->type == STMT_LABEL) {
                    if (stmt->label == label) {
                        if (!stmt2 || !STMT_IS_LOOP(stmt2)) {
                            js_ReportCompileErrorNumber(cx, ts,
                                                        JSREPORT_TS |
                                                        JSREPORT_ERROR,
                                                        JSMSG_BAD_CONTINUE);
                            return NULL;
                        }
                        break;
                    }
                } else {
                    stmt2 = stmt;
                }
            }
        } else {
            for (; ; stmt = stmt->down) {
                if (!stmt) {
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_BAD_CONTINUE);
                    return NULL;
                }
                if (STMT_IS_LOOP(stmt))
                    break;
            }
        }
        if (label)
            pn->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
        break;

      case TOK_WITH:
        pn = NewParseNode(cx, ts, PN_BINARY, tc);
        if (!pn)
            return NULL;
        MUST_MATCH_TOKEN(TOK_LP, JSMSG_PAREN_BEFORE_WITH);
        pn2 = Expr(cx, ts, tc);
        if (!pn2)
            return NULL;
        MUST_MATCH_TOKEN(TOK_RP, JSMSG_PAREN_AFTER_WITH);
        pn->pn_left = pn2;

        js_PushStatement(tc, &stmtInfo, STMT_WITH, -1);
        pn2 = Statement(cx, ts, tc);
        if (!pn2)
            return NULL;
        js_PopStatement(tc);

        pn->pn_pos.end = pn2->pn_pos.end;
        pn->pn_right = pn2;
        tc->flags |= TCF_FUN_HEAVYWEIGHT;
        return pn;

      case TOK_VAR:
        pn = Variables(cx, ts, tc);
        if (!pn)
            return NULL;

        /* Tell js_EmitTree to generate a final POP. */
        pn->pn_extra |= PNX_POPVAR;
        break;

      case TOK_RETURN:
        if (!(tc->flags & TCF_IN_FUNCTION)) {
            js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                        JSMSG_BAD_RETURN);
            return NULL;
        }
        pn = NewParseNode(cx, ts, PN_UNARY, tc);
        if (!pn)
            return NULL;

        /* This is ugly, but we don't want to require a semicolon. */
        ts->flags |= TSF_OPERAND;
        tt = js_PeekTokenSameLine(cx, ts);
        ts->flags &= ~TSF_OPERAND;
        if (tt == TOK_ERROR)
            return NULL;

        if (tt != TOK_EOF && tt != TOK_EOL && tt != TOK_SEMI && tt != TOK_RC) {
            pn2 = Expr(cx, ts, tc);
            if (!pn2)
                return NULL;
            tc->flags |= TCF_RETURN_EXPR;
            pn->pn_pos.end = pn2->pn_pos.end;
            pn->pn_kid = pn2;
        } else {
            tc->flags |= TCF_RETURN_VOID;
            pn->pn_kid = NULL;
        }

        if (JS_HAS_STRICT_OPTION(cx) &&
            (~tc->flags & (TCF_RETURN_EXPR | TCF_RETURN_VOID)) == 0) {
            /*
             * We must be in a frame with a non-native function, because
             * we're compiling one.
             */
            if (!ReportNoReturnValue(cx, ts))
                return NULL;
        }
        break;

      case TOK_LC:
        js_PushStatement(tc, &stmtInfo, STMT_BLOCK, -1);
        pn = Statements(cx, ts, tc);
        if (!pn)
            return NULL;

        MUST_MATCH_TOKEN(TOK_RC, JSMSG_CURLY_IN_COMPOUND);
        js_PopStatement(tc);
        return pn;

      case TOK_EOL:
      case TOK_SEMI:
        pn = NewParseNode(cx, ts, PN_UNARY, tc);
        if (!pn)
            return NULL;
        pn->pn_type = TOK_SEMI;
        pn->pn_kid = NULL;
        return pn;

#if JS_HAS_DEBUGGER_KEYWORD
      case TOK_DEBUGGER:
        pn = NewParseNode(cx, ts, PN_NULLARY, tc);
        if (!pn)
            return NULL;
        pn->pn_type = TOK_DEBUGGER;
        tc->flags |= TCF_FUN_HEAVYWEIGHT;
        break;
#endif /* JS_HAS_DEBUGGER_KEYWORD */

#if JS_HAS_XML_SUPPORT
      case TOK_DEFAULT:
        pn = NewParseNode(cx, ts, PN_UNARY, tc);
        if (!pn)
            return NULL;
        if (!js_MatchToken(cx, ts, TOK_NAME) ||
            CURRENT_TOKEN(ts).t_atom != cx->runtime->atomState.xmlAtom ||
            !js_MatchToken(cx, ts, TOK_NAME) ||
            CURRENT_TOKEN(ts).t_atom != cx->runtime->atomState.namespaceAtom ||
            !js_MatchToken(cx, ts, TOK_ASSIGN) ||
            CURRENT_TOKEN(ts).t_op != JSOP_NOP) {
            js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                        JSMSG_BAD_DEFAULT_XML_NAMESPACE);
            return NULL;
        }
        pn2 = Expr(cx, ts, tc);
        if (!pn2)
            return NULL;
        pn->pn_op = JSOP_DEFXMLNS;
        pn->pn_pos.end = pn2->pn_pos.end;
        pn->pn_kid = pn2;
        tc->flags |= TCF_HAS_DEFXMLNS;
        break;
#endif

      case TOK_ERROR:
        return NULL;

      default:
#if JS_HAS_XML_SUPPORT
      expression:
#endif
        js_UngetToken(ts);
        pn2 = Expr(cx, ts, tc);
        if (!pn2)
            return NULL;

        if (js_PeekToken(cx, ts) == TOK_COLON) {
            if (pn2->pn_type != TOK_NAME) {
                js_ReportCompileErrorNumber(cx, ts,
                                            JSREPORT_TS | JSREPORT_ERROR,
                                            JSMSG_BAD_LABEL);
                return NULL;
            }
            label = pn2->pn_atom;
            for (stmt = tc->topStmt; stmt; stmt = stmt->down) {
                if (stmt->type == STMT_LABEL && stmt->label == label) {
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_DUPLICATE_LABEL);
                    return NULL;
                }
            }
            (void) js_GetToken(cx, ts);

            /* Push a label struct and parse the statement. */
            js_PushStatement(tc, &stmtInfo, STMT_LABEL, -1);
            stmtInfo.label = label;
            pn = Statement(cx, ts, tc);
            if (!pn)
                return NULL;

            /* Pop the label, set pn_expr, and return early. */
            js_PopStatement(tc);
            pn2->pn_type = TOK_COLON;
            pn2->pn_pos.end = pn->pn_pos.end;
            pn2->pn_expr = pn;
            return pn2;
        }

        pn = NewParseNode(cx, ts, PN_UNARY, tc);
        if (!pn)
            return NULL;
        pn->pn_type = TOK_SEMI;
        pn->pn_pos = pn2->pn_pos;
        pn->pn_kid = pn2;
        break;
    }

    /* Check termination of this primitive statement. */
    if (ON_CURRENT_LINE(ts, pn->pn_pos)) {
        tt = js_PeekTokenSameLine(cx, ts);
        if (tt == TOK_ERROR)
            return NULL;
        if (tt != TOK_EOF && tt != TOK_EOL && tt != TOK_SEMI && tt != TOK_RC) {
            js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                        JSMSG_SEMI_BEFORE_STMNT);
            return NULL;
        }
    }

    (void) js_MatchToken(cx, ts, TOK_SEMI);
    return pn;
}

static JSParseNode *
Variables(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn, *pn2;
    JSObject *obj, *pobj;
    JSStackFrame *fp;
    JSFunction *fun;
    JSClass *clasp;
    JSPropertyOp getter, setter, currentGetter, currentSetter;
    JSAtom *atom;
    JSAtomListElement *ale;
    JSOp prevop;
    JSProperty *prop;
    JSScopeProperty *sprop;
    JSBool ok;

    /*
     * The tricky part of this code is to create special parsenode opcodes for
     * getting and setting variables (which will be stored as special slots in
     * the frame).  The complex special case is an eval() inside a function.
     * If the evaluated string references variables in the enclosing function,
     * then we need to generate the special variable opcodes.  We determine
     * this by looking up the variable id in the current variable scope.
     */
    JS_ASSERT(CURRENT_TOKEN(ts).type == TOK_VAR);
    pn = NewParseNode(cx, ts, PN_LIST, tc);
    if (!pn)
        return NULL;
    pn->pn_op = CURRENT_TOKEN(ts).t_op;
    PN_INIT_LIST(pn);

    /*
     * Skip eval and debugger frames when looking for the function whose code
     * is being compiled.  If we are called from FunctionBody, TCF_IN_FUNCTION
     * will be set in tc->flags, and we can be sure fp->fun is the function to
     * use.  But if a function calls eval, the string argument is treated as a
     * Program (per ECMA), so TCF_IN_FUNCTION won't be set.
     *
     * What's more, when the following code is reached from eval, cx->fp->fun
     * is eval's JSFunction (a native function), so we need to skip its frame.
     * We should find the scripted caller's function frame just below it, but
     * we code a loop out of paranoia.
     */
    for (fp = cx->fp; (fp->flags & JSFRAME_SPECIAL) && fp->down; fp = fp->down)
        continue;
    obj = fp->varobj;
    fun = fp->fun;
    clasp = OBJ_GET_CLASS(cx, obj);
    if (fun && clasp == &js_FunctionClass) {
        /* We are compiling code inside a function */
        getter = js_GetLocalVariable;
        setter = js_SetLocalVariable;
    } else if (fun && clasp == &js_CallClass) {
        /* We are compiling code from an eval inside a function */
        getter = js_GetCallVariable;
        setter = js_SetCallVariable;
    } else {
        getter = clasp->getProperty;
        setter = clasp->setProperty;
    }

    ok = JS_TRUE;
    do {
        currentGetter = getter;
        currentSetter = setter;
        MUST_MATCH_TOKEN(TOK_NAME, JSMSG_NO_VARIABLE_NAME);
        atom = CURRENT_TOKEN(ts).t_atom;

        ATOM_LIST_SEARCH(ale, &tc->decls, atom);
        if (ale) {
            prevop = ALE_JSOP(ale);
            if (JS_HAS_STRICT_OPTION(cx)
                ? pn->pn_op != JSOP_DEFVAR || prevop != JSOP_DEFVAR
                : pn->pn_op == JSOP_DEFCONST || prevop == JSOP_DEFCONST) {
                const char *name = js_AtomToPrintableString(cx, atom);
                if (!name ||
                    !js_ReportCompileErrorNumber(cx, ts,
                                                 (pn->pn_op != JSOP_DEFCONST &&
                                                  prevop != JSOP_DEFCONST)
                                                 ? JSREPORT_TS |
                                                   JSREPORT_WARNING |
                                                   JSREPORT_STRICT
                                                 : JSREPORT_TS | JSREPORT_ERROR,
                                                 JSMSG_REDECLARED_VAR,
                                                 (prevop == JSOP_DEFFUN ||
                                                  prevop == JSOP_CLOSURE)
                                                 ? js_function_str
                                                 : (prevop == JSOP_DEFCONST)
                                                 ? js_const_str
                                                 : js_var_str,
                                                 name)) {
                    return NULL;
                }
            }
            if (pn->pn_op == JSOP_DEFVAR && prevop == JSOP_CLOSURE)
                tc->flags |= TCF_FUN_CLOSURE_VS_VAR;
        } else {
            ale = js_IndexAtom(cx, atom, &tc->decls);
            if (!ale)
                return NULL;
        }
        ALE_SET_JSOP(ale, pn->pn_op);

        pn2 = NewParseNode(cx, ts, PN_NAME, tc);
        if (!pn2)
            return NULL;
        pn2->pn_op = JSOP_NAME;
        pn2->pn_atom = atom;
        pn2->pn_expr = NULL;
        pn2->pn_slot = -1;
        pn2->pn_attrs = (pn->pn_op == JSOP_DEFCONST)
                        ? JSPROP_PERMANENT | JSPROP_READONLY
                        : JSPROP_PERMANENT;
        PN_APPEND(pn, pn2);

        if (!fun) {
            /* Don't lookup global variables at compile time. */
            prop = NULL;
        } else if (OBJ_IS_NATIVE(obj)) {
            if (!js_LookupHiddenProperty(cx, obj, ATOM_TO_JSID(atom),
                                         &pobj, &prop)) {
                return NULL;
            }
        } else {
            if (!OBJ_LOOKUP_PROPERTY(cx, obj, ATOM_TO_JSID(atom), &pobj, &prop))
                return NULL;
        }
        if (prop && pobj == obj && OBJ_IS_NATIVE(pobj)) {
            sprop = (JSScopeProperty *)prop;
            if (sprop->getter == js_GetArgument) {
                const char *name = js_AtomToPrintableString(cx, atom);
                if (!name) {
                    ok = JS_FALSE;
                } else if (pn->pn_op == JSOP_DEFCONST) {
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_REDECLARED_PARAM,
                                                name);
                    ok = JS_FALSE;
                } else {
                    currentGetter = js_GetArgument;
                    currentSetter = js_SetArgument;
                    ok = js_ReportCompileErrorNumber(cx, ts,
                                                     JSREPORT_TS |
                                                     JSREPORT_WARNING |
                                                     JSREPORT_STRICT,
                                                     JSMSG_VAR_HIDES_ARG,
                                                     name);
                }
            } else {
                if (fun) {
                    /* Not an argument, must be a redeclared local var. */
                    if (clasp == &js_FunctionClass) {
                        JS_ASSERT(sprop->getter == js_GetLocalVariable);
                        JS_ASSERT((sprop->flags & SPROP_HAS_SHORTID) &&
                                  (uint16) sprop->shortid < fun->nvars);
                    } else if (clasp == &js_CallClass) {
                        if (sprop->getter == js_GetCallVariable) {
                            /*
                             * Referencing a variable introduced by a var
                             * statement in the enclosing function. Check
                             * that the slot number we have is in range.
                             */
                            JS_ASSERT((sprop->flags & SPROP_HAS_SHORTID) &&
                                      (uint16) sprop->shortid < fun->nvars);
                        } else {
                            /*
                             * A variable introduced through another eval:
                             * don't use the special getters and setters
                             * since we can't allocate a slot in the frame.
                             */
                            currentGetter = sprop->getter;
                            currentSetter = sprop->setter;
                        }
                    }

                    /* Override the old getter and setter, to handle eval. */
                    sprop = js_ChangeNativePropertyAttrs(cx, obj, sprop,
                                                         0, sprop->attrs,
                                                         currentGetter,
                                                         currentSetter);
                    if (!sprop)
                        ok = JS_FALSE;
                }
            }
        } else {
            /*
             * Property not found in current variable scope: we have not seen
             * this variable before.  Define a new local variable by adding a
             * property to the function's scope, allocating one slot in the
             * function's frame.  Global variables and any locals declared in
             * with statement bodies are handled at runtime, by script prolog
             * JSOP_DEFVAR bytecodes generated for slot-less vars.
             */
            sprop = NULL;
            if (prop) {
                OBJ_DROP_PROPERTY(cx, pobj, prop);
                prop = NULL;
            }
            if (currentGetter == js_GetCallVariable) {
                /* Can't increase fun->nvars in an active frame! */
                currentGetter = clasp->getProperty;
                currentSetter = clasp->setProperty;
            }
            if (currentGetter == js_GetLocalVariable &&
                atom != cx->runtime->atomState.argumentsAtom &&
                fp->scopeChain == obj &&
                !js_InWithStatement(tc)) {
                if (!js_AddHiddenProperty(cx, obj, ATOM_TO_JSID(atom),
                                          currentGetter, currentSetter,
                                          SPROP_INVALID_SLOT,
                                          pn2->pn_attrs | JSPROP_SHARED,
                                          SPROP_HAS_SHORTID, fun->nvars)) {
                    return NULL;
                }
                if (fun->nvars == JS_BITMASK(16)) {
                    JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                         JSMSG_TOO_MANY_FUN_VARS);
                    return NULL;
                }
                fun->nvars++;
            }
        }

        if (js_MatchToken(cx, ts, TOK_ASSIGN)) {
            if (CURRENT_TOKEN(ts).t_op != JSOP_NOP) {
                js_ReportCompileErrorNumber(cx, ts,
                                            JSREPORT_TS | JSREPORT_ERROR,
                                            JSMSG_BAD_VAR_INIT);
                ok = JS_FALSE;
            } else {
                pn2->pn_expr = AssignExpr(cx, ts, tc);
                if (!pn2->pn_expr) {
                    ok = JS_FALSE;
                } else {
                    pn2->pn_op = (pn->pn_op == JSOP_DEFCONST)
                                 ? JSOP_SETCONST
                                 : JSOP_SETNAME;
                    if (atom == cx->runtime->atomState.argumentsAtom)
                        tc->flags |= TCF_FUN_HEAVYWEIGHT;
                }
            }
        }

        if (prop)
            OBJ_DROP_PROPERTY(cx, pobj, prop);
        if (!ok)
            return NULL;
    } while (js_MatchToken(cx, ts, TOK_COMMA));

    pn->pn_pos.end = PN_LAST(pn)->pn_pos.end;
    return pn;
}

static JSParseNode *
Expr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn, *pn2;

    pn = AssignExpr(cx, ts, tc);
    if (pn && js_MatchToken(cx, ts, TOK_COMMA)) {
        pn2 = NewParseNode(cx, ts, PN_LIST, tc);
        if (!pn2)
            return NULL;
        pn2->pn_pos.begin = pn->pn_pos.begin;
        PN_INIT_LIST_1(pn2, pn);
        pn = pn2;
        do {
            pn2 = AssignExpr(cx, ts, tc);
            if (!pn2)
                return NULL;
            PN_APPEND(pn, pn2);
        } while (js_MatchToken(cx, ts, TOK_COMMA));
        pn->pn_pos.end = PN_LAST(pn)->pn_pos.end;
    }
    return pn;
}

static JSParseNode *
AssignExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn, *pn2;
    JSTokenType tt;
    JSOp op;

    CHECK_RECURSION();

    pn = CondExpr(cx, ts, tc);
    if (!pn)
        return NULL;

    tt = js_GetToken(cx, ts);
#if JS_HAS_GETTER_SETTER
    if (tt == TOK_NAME) {
        tt = CheckGetterOrSetter(cx, ts, TOK_ASSIGN);
        if (tt == TOK_ERROR)
            return NULL;
    }
#endif
    if (tt != TOK_ASSIGN) {
        js_UngetToken(ts);
        return pn;
    }

    op = CURRENT_TOKEN(ts).t_op;
    for (pn2 = pn; pn2->pn_type == TOK_RP; pn2 = pn2->pn_kid)
        continue;
    switch (pn2->pn_type) {
      case TOK_NAME:
        pn2->pn_op = JSOP_SETNAME;
        if (pn2->pn_atom == cx->runtime->atomState.argumentsAtom)
            tc->flags |= TCF_FUN_HEAVYWEIGHT;
        break;
      case TOK_DOT:
        pn2->pn_op = (pn2->pn_op == JSOP_GETMETHOD)
                     ? JSOP_SETMETHOD
                     : JSOP_SETPROP;
        break;
      case TOK_LB:
        pn2->pn_op = JSOP_SETELEM;
        break;
#if JS_HAS_LVALUE_RETURN
      case TOK_LP:
        pn2->pn_op = JSOP_SETCALL;
        break;
#endif
#if JS_HAS_XML_SUPPORT
      case TOK_UNARYOP:
        if (pn2->pn_op == JSOP_XMLNAME) {
            pn2->pn_op = JSOP_SETXMLNAME;
            break;
        }
        /* FALL THROUGH */
#endif
      default:
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_BAD_LEFTSIDE_OF_ASS);
        return NULL;
    }
    pn = NewBinary(cx, TOK_ASSIGN, op, pn2, AssignExpr(cx, ts, tc), tc);
    return pn;
}

static JSParseNode *
CondExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn, *pn1, *pn2, *pn3;
#if JS_HAS_IN_OPERATOR
    uintN oldflags;
#endif /* JS_HAS_IN_OPERATOR */

    pn = OrExpr(cx, ts, tc);
    if (pn && js_MatchToken(cx, ts, TOK_HOOK)) {
        pn1 = pn;
        pn = NewParseNode(cx, ts, PN_TERNARY, tc);
        if (!pn)
            return NULL;
#if JS_HAS_IN_OPERATOR
        /*
         * Always accept the 'in' operator in the middle clause of a ternary,
         * where it's unambiguous, even if we might be parsing the init of a
         * for statement.
         */
        oldflags = tc->flags;
        tc->flags &= ~TCF_IN_FOR_INIT;
#endif /* JS_HAS_IN_OPERATOR */
        pn2 = AssignExpr(cx, ts, tc);
#if JS_HAS_IN_OPERATOR
        tc->flags = oldflags | (tc->flags & TCF_FUN_FLAGS);
#endif /* JS_HAS_IN_OPERATOR */

        if (!pn2)
            return NULL;
        MUST_MATCH_TOKEN(TOK_COLON, JSMSG_COLON_IN_COND);
        pn3 = AssignExpr(cx, ts, tc);
        if (!pn3)
            return NULL;
        pn->pn_pos.begin = pn1->pn_pos.begin;
        pn->pn_pos.end = pn3->pn_pos.end;
        pn->pn_kid1 = pn1;
        pn->pn_kid2 = pn2;
        pn->pn_kid3 = pn3;
    }
    return pn;
}

static JSParseNode *
OrExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;

    pn = AndExpr(cx, ts, tc);
    if (pn && js_MatchToken(cx, ts, TOK_OR))
        pn = NewBinary(cx, TOK_OR, JSOP_OR, pn, OrExpr(cx, ts, tc), tc);
    return pn;
}

static JSParseNode *
AndExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;

    pn = BitOrExpr(cx, ts, tc);
    if (pn && js_MatchToken(cx, ts, TOK_AND))
        pn = NewBinary(cx, TOK_AND, JSOP_AND, pn, AndExpr(cx, ts, tc), tc);
    return pn;
}

static JSParseNode *
BitOrExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;

    pn = BitXorExpr(cx, ts, tc);
    while (pn && js_MatchToken(cx, ts, TOK_BITOR)) {
        pn = NewBinary(cx, TOK_BITOR, JSOP_BITOR, pn, BitXorExpr(cx, ts, tc),
                       tc);
    }
    return pn;
}

static JSParseNode *
BitXorExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;

    pn = BitAndExpr(cx, ts, tc);
    while (pn && js_MatchToken(cx, ts, TOK_BITXOR)) {
        pn = NewBinary(cx, TOK_BITXOR, JSOP_BITXOR, pn, BitAndExpr(cx, ts, tc),
                       tc);
    }
    return pn;
}

static JSParseNode *
BitAndExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;

    pn = EqExpr(cx, ts, tc);
    while (pn && js_MatchToken(cx, ts, TOK_BITAND))
        pn = NewBinary(cx, TOK_BITAND, JSOP_BITAND, pn, EqExpr(cx, ts, tc), tc);
    return pn;
}

static JSParseNode *
EqExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;
    JSOp op;

    pn = RelExpr(cx, ts, tc);
    while (pn && js_MatchToken(cx, ts, TOK_EQOP)) {
        op = CURRENT_TOKEN(ts).t_op;
        pn = NewBinary(cx, TOK_EQOP, op, pn, RelExpr(cx, ts, tc), tc);
    }
    return pn;
}

static JSParseNode *
RelExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;
    JSTokenType tt;
    JSOp op;
#if JS_HAS_IN_OPERATOR
    uintN inForInitFlag = tc->flags & TCF_IN_FOR_INIT;

    /*
     * Uses of the in operator in ShiftExprs are always unambiguous,
     * so unset the flag that prohibits recognizing it.
     */
    tc->flags &= ~TCF_IN_FOR_INIT;
#endif /* JS_HAS_IN_OPERATOR */

    pn = ShiftExpr(cx, ts, tc);
    while (pn &&
           (js_MatchToken(cx, ts, TOK_RELOP)
#if JS_HAS_IN_OPERATOR
            /*
             * Recognize the 'in' token as an operator only if we're not
             * currently in the init expr of a for loop.
             */
            || (inForInitFlag == 0 && js_MatchToken(cx, ts, TOK_IN))
#endif /* JS_HAS_IN_OPERATOR */
#if JS_HAS_INSTANCEOF
            || js_MatchToken(cx, ts, TOK_INSTANCEOF)
#endif /* JS_HAS_INSTANCEOF */
            )) {
        tt = CURRENT_TOKEN(ts).type;
        op = CURRENT_TOKEN(ts).t_op;
        pn = NewBinary(cx, tt, op, pn, ShiftExpr(cx, ts, tc), tc);
    }
#if JS_HAS_IN_OPERATOR
    /* Restore previous state of inForInit flag. */
    tc->flags |= inForInitFlag;
#endif /* JS_HAS_IN_OPERATOR */

    return pn;
}

static JSParseNode *
ShiftExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;
    JSOp op;

    pn = AddExpr(cx, ts, tc);
    while (pn && js_MatchToken(cx, ts, TOK_SHOP)) {
        op = CURRENT_TOKEN(ts).t_op;
        pn = NewBinary(cx, TOK_SHOP, op, pn, AddExpr(cx, ts, tc), tc);
    }
    return pn;
}

static JSParseNode *
AddExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;
    JSTokenType tt;
    JSOp op;

    pn = MulExpr(cx, ts, tc);
    while (pn &&
           (js_MatchToken(cx, ts, TOK_PLUS) ||
            js_MatchToken(cx, ts, TOK_MINUS))) {
        tt = CURRENT_TOKEN(ts).type;
        op = (tt == TOK_PLUS) ? JSOP_ADD : JSOP_SUB;
        pn = NewBinary(cx, tt, op, pn, MulExpr(cx, ts, tc), tc);
    }
    return pn;
}

static JSParseNode *
MulExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;
    JSTokenType tt;
    JSOp op;

    pn = UnaryExpr(cx, ts, tc);
    while (pn &&
           (js_MatchToken(cx, ts, TOK_STAR) ||
            js_MatchToken(cx, ts, TOK_DIVOP))) {
        tt = CURRENT_TOKEN(ts).type;
        op = CURRENT_TOKEN(ts).t_op;
        pn = NewBinary(cx, tt, op, pn, UnaryExpr(cx, ts, tc), tc);
    }
    return pn;
}

static JSParseNode *
SetLvalKid(JSContext *cx, JSTokenStream *ts, JSParseNode *pn, JSParseNode *kid,
           const char *name)
{
    while (kid->pn_type == TOK_RP)
        kid = kid->pn_kid;
    if (kid->pn_type != TOK_NAME &&
        kid->pn_type != TOK_DOT &&
#if JS_HAS_LVALUE_RETURN
        (kid->pn_type != TOK_LP || kid->pn_op != JSOP_CALL) &&
#endif
#if JS_HAS_XML_SUPPORT
        (kid->pn_type != TOK_UNARYOP || kid->pn_op != JSOP_XMLNAME) &&
#endif
        kid->pn_type != TOK_LB) {
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_BAD_OPERAND, name);
        return NULL;
    }
    pn->pn_kid = kid;
    return kid;
}

static const char *incop_name_str[] = {"increment", "decrement"};

static JSBool
SetIncOpKid(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc,
            JSParseNode *pn, JSParseNode *kid,
            JSTokenType tt, JSBool preorder)
{
    JSOp op;

    kid = SetLvalKid(cx, ts, pn, kid, incop_name_str[tt == TOK_DEC]);
    if (!kid)
        return JS_FALSE;
    switch (kid->pn_type) {
      case TOK_NAME:
        op = (tt == TOK_INC)
             ? (preorder ? JSOP_INCNAME : JSOP_NAMEINC)
             : (preorder ? JSOP_DECNAME : JSOP_NAMEDEC);
        if (kid->pn_atom == cx->runtime->atomState.argumentsAtom)
            tc->flags |= TCF_FUN_HEAVYWEIGHT;
        break;

      case TOK_DOT:
        op = (tt == TOK_INC)
             ? (preorder ? JSOP_INCPROP : JSOP_PROPINC)
             : (preorder ? JSOP_DECPROP : JSOP_PROPDEC);
        break;

#if JS_HAS_LVALUE_RETURN
      case TOK_LP:
        JS_ASSERT(kid->pn_op == JSOP_CALL);
        kid->pn_op = JSOP_SETCALL;
        /* FALL THROUGH */
#endif
#if JS_HAS_XML_SUPPORT
      case TOK_UNARYOP:
        if (kid->pn_op == JSOP_XMLNAME)
            kid->pn_op = JSOP_SETXMLNAME;
        /* FALL THROUGH */
#endif
      case TOK_LB:
        op = (tt == TOK_INC)
             ? (preorder ? JSOP_INCELEM : JSOP_ELEMINC)
             : (preorder ? JSOP_DECELEM : JSOP_ELEMDEC);
        break;

      default:
        JS_ASSERT(0);
        op = JSOP_NOP;
    }
    pn->pn_op = op;
    return JS_TRUE;
}

static JSParseNode *
UnaryExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSTokenType tt;
    JSParseNode *pn, *pn2;

    CHECK_RECURSION();

    ts->flags |= TSF_OPERAND;
    tt = js_GetToken(cx, ts);
    ts->flags &= ~TSF_OPERAND;

    switch (tt) {
      case TOK_UNARYOP:
      case TOK_PLUS:
      case TOK_MINUS:
        pn = NewParseNode(cx, ts, PN_UNARY, tc);
        if (!pn)
            return NULL;
        pn->pn_type = TOK_UNARYOP;      /* PLUS and MINUS are binary */
        pn->pn_op = CURRENT_TOKEN(ts).t_op;
        pn2 = UnaryExpr(cx, ts, tc);
        if (!pn2)
            return NULL;
        pn->pn_pos.end = pn2->pn_pos.end;
        pn->pn_kid = pn2;
        break;

      case TOK_INC:
      case TOK_DEC:
        pn = NewParseNode(cx, ts, PN_UNARY, tc);
        if (!pn)
            return NULL;
        pn2 = MemberExpr(cx, ts, tc, JS_TRUE);
        if (!pn2)
            return NULL;
        if (!SetIncOpKid(cx, ts, tc, pn, pn2, tt, JS_TRUE))
            return NULL;
        pn->pn_pos.end = pn2->pn_pos.end;
        break;

      case TOK_DELETE:
        pn = NewParseNode(cx, ts, PN_UNARY, tc);
        if (!pn)
            return NULL;
        pn2 = UnaryExpr(cx, ts, tc);
        if (!pn2)
            return NULL;
        pn->pn_pos.end = pn2->pn_pos.end;

        /*
         * Under ECMA3, deleting any unary expression is valid -- it simply
         * returns true. Here we strip off any parentheses.
         */
        while (pn2->pn_type == TOK_RP)
            pn2 = pn2->pn_kid;
        pn->pn_kid = pn2;
        break;

      case TOK_ERROR:
        return NULL;

      default:
        js_UngetToken(ts);
        pn = MemberExpr(cx, ts, tc, JS_TRUE);
        if (!pn)
            return NULL;

        /* Don't look across a newline boundary for a postfix incop. */
        if (ON_CURRENT_LINE(ts, pn->pn_pos)) {
            tt = js_PeekTokenSameLine(cx, ts);
            if (tt == TOK_INC || tt == TOK_DEC) {
                (void) js_GetToken(cx, ts);
                pn2 = NewParseNode(cx, ts, PN_UNARY, tc);
                if (!pn2)
                    return NULL;
                if (!SetIncOpKid(cx, ts, tc, pn2, pn, tt, JS_FALSE))
                    return NULL;
                pn2->pn_pos.begin = pn->pn_pos.begin;
                pn = pn2;
            }
        }
        break;
    }
    return pn;
}

static JSBool
ArgumentList(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc,
             JSParseNode *listNode)
{
    JSBool matched;

    ts->flags |= TSF_OPERAND;
    matched = js_MatchToken(cx, ts, TOK_RP);
    ts->flags &= ~TSF_OPERAND;
    if (!matched) {
        do {
            JSParseNode *argNode = AssignExpr(cx, ts, tc);
            if (!argNode)
                return JS_FALSE;
            PN_APPEND(listNode, argNode);
        } while (js_MatchToken(cx, ts, TOK_COMMA));

        if (js_GetToken(cx, ts) != TOK_RP) {
            js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                        JSMSG_PAREN_AFTER_ARGS);
            return JS_FALSE;
        }
    }
    return JS_TRUE;
}

static JSParseNode *
MemberExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc,
           JSBool allowCallSyntax)
{
    JSParseNode *pn, *pn2, *pn3;
    JSTokenType tt;

    CHECK_RECURSION();

    /* Check for new expression first. */
    ts->flags |= TSF_OPERAND;
    tt = js_PeekToken(cx, ts);
    ts->flags &= ~TSF_OPERAND;
    if (tt == TOK_NEW) {
        (void) js_GetToken(cx, ts);

        pn = NewParseNode(cx, ts, PN_LIST, tc);
        if (!pn)
            return NULL;
        pn2 = MemberExpr(cx, ts, tc, JS_FALSE);
        if (!pn2)
            return NULL;
        pn->pn_op = JSOP_NEW;
        PN_INIT_LIST_1(pn, pn2);
        pn->pn_pos.begin = pn2->pn_pos.begin;

        if (js_MatchToken(cx, ts, TOK_LP) && !ArgumentList(cx, ts, tc, pn))
            return NULL;
        if (pn->pn_count > ARGC_LIMIT) {
            JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                 JSMSG_TOO_MANY_CON_ARGS);
            return NULL;
        }
        pn->pn_pos.end = PN_LAST(pn)->pn_pos.end;
    } else {
        pn = PrimaryExpr(cx, ts, tc);
        if (!pn)
            return NULL;

        if (pn->pn_type == TOK_ANYNAME ||
            pn->pn_type == TOK_AT ||
            pn->pn_type == TOK_DBLCOLON) {
            pn2 = NewOrRecycledNode(cx, tc);
            if (!pn2)
                return NULL;
            pn2->pn_type = TOK_UNARYOP;
            pn2->pn_pos = pn->pn_pos;
            pn2->pn_op = JSOP_XMLNAME;
            pn2->pn_arity = PN_UNARY;
            pn2->pn_kid = pn;
            pn2->pn_next = NULL;
#if JS_HAS_XML_SUPPORT
            pn2->pn_ts = ts;
#endif
            pn = pn2;
        }
    }

    while ((tt = js_GetToken(cx, ts)) > TOK_EOF) {
        if (tt == TOK_DOT) {
            pn2 = NewParseNode(cx, ts, PN_NAME, tc);
            if (!pn2)
                return NULL;
#if JS_HAS_XML_SUPPORT
            pn3 = PrimaryExpr(cx, ts, tc);
            if (!pn3)
                return NULL;
            tt = pn3->pn_type;
            if (tt == TOK_NAME ||
                (tt == TOK_DBLCOLON &&
                 pn3->pn_arity == PN_NAME &&
                 pn3->pn_expr->pn_type == TOK_FUNCTION)) {
                pn2->pn_op = (tt == TOK_NAME) ? JSOP_GETPROP : JSOP_GETMETHOD;
                pn2->pn_expr = pn;
                pn2->pn_atom = pn3->pn_atom;
                RecycleTree(pn3, tc);
            } else {
                if (TOKEN_TYPE_IS_XML(tt)) {
                    pn2->pn_type = TOK_LB;
                    pn2->pn_op = JSOP_GETELEM;
                } else if (tt == TOK_RP) {
                    JSParseNode *group = pn3;

                    /* Recycle the useless TOK_RP/JSOP_GROUP node. */
                    pn3 = group->pn_kid;
                    group->pn_kid = NULL;
                    RecycleTree(group, tc);
                    pn2->pn_type = TOK_FILTER;
                    pn2->pn_op = JSOP_FILTER;
                } else {
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_NAME_AFTER_DOT);
                    return NULL;
                }
                pn2->pn_arity = PN_BINARY;
                pn2->pn_left = pn;
                pn2->pn_right = pn3;
            }
#else
            MUST_MATCH_TOKEN(TOK_NAME, JSMSG_NAME_AFTER_DOT);
            pn2->pn_op = JSOP_GETPROP;
            pn2->pn_expr = pn;
            pn2->pn_atom = CURRENT_TOKEN(ts).t_atom;
#endif
            pn2->pn_pos.begin = pn->pn_pos.begin;
            pn2->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
#if JS_HAS_XML_SUPPORT
        } else if (tt == TOK_DBLDOT) {
            pn2 = NewParseNode(cx, ts, PN_BINARY, tc);
            if (!pn2)
                return NULL;
            pn3 = PrimaryExpr(cx, ts, tc);
            if (!pn3)
                return NULL;
            tt = pn3->pn_type;
            if (tt == TOK_NAME) {
                pn3->pn_type = TOK_STRING;
                pn3->pn_arity = PN_NULLARY;
                pn3->pn_op = JSOP_STRING;
            } else if (!TOKEN_TYPE_IS_XML(tt)) {
                js_ReportCompileErrorNumber(cx, ts,
                                            JSREPORT_TS | JSREPORT_ERROR,
                                            JSMSG_NAME_AFTER_DOT);
                return NULL;
            }
            pn2->pn_op = JSOP_DESCENDANTS;
            pn2->pn_left = pn;
            pn2->pn_right = pn3;
            pn2->pn_pos.begin = pn->pn_pos.begin;
            pn2->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
#endif
        } else if (tt == TOK_LB) {
            pn2 = NewParseNode(cx, ts, PN_BINARY, tc);
            if (!pn2)
                return NULL;
            pn3 = Expr(cx, ts, tc);
            if (!pn3)
                return NULL;

            MUST_MATCH_TOKEN(TOK_RB, JSMSG_BRACKET_IN_INDEX);
            pn2->pn_pos.begin = pn->pn_pos.begin;
            pn2->pn_pos.end = CURRENT_TOKEN(ts).pos.end;

            /* Optimize o['p'] to o.p by rewriting pn2. */
            if (pn3->pn_type == TOK_STRING) {
                pn2->pn_type = TOK_DOT;
                pn2->pn_op = JSOP_GETPROP;
                pn2->pn_arity = PN_NAME;
                pn2->pn_expr = pn;
                pn2->pn_atom = pn3->pn_atom;
            } else {
                pn2->pn_op = JSOP_GETELEM;
                pn2->pn_left = pn;
                pn2->pn_right = pn3;
            }
        } else if (allowCallSyntax && tt == TOK_LP) {
            pn2 = NewParseNode(cx, ts, PN_LIST, tc);
            if (!pn2)
                return NULL;

            /* Pick JSOP_EVAL and flag tc as heavyweight if eval(...). */
            pn2->pn_op = JSOP_CALL;
            if (pn->pn_op == JSOP_NAME &&
                pn->pn_atom == cx->runtime->atomState.evalAtom) {
                pn2->pn_op = JSOP_EVAL;
                tc->flags |= TCF_FUN_HEAVYWEIGHT;
            }

            PN_INIT_LIST_1(pn2, pn);
            pn2->pn_pos.begin = pn->pn_pos.begin;

            if (!ArgumentList(cx, ts, tc, pn2))
                return NULL;
            if (pn2->pn_count > ARGC_LIMIT) {
                JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
                                     JSMSG_TOO_MANY_FUN_ARGS);
                return NULL;
            }
            pn2->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
        } else {
            js_UngetToken(ts);
            return pn;
        }

        pn = pn2;
    }
    if (tt == TOK_ERROR)
        return NULL;
    return pn;
}

static JSParseNode *
BracketedExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    uintN oldflags;
    JSParseNode *pn;

#if JS_HAS_IN_OPERATOR
    /*
     * Always accept the 'in' operator in a parenthesized expression,
     * where it's unambiguous, even if we might be parsing the init of a
     * for statement.
     */
    oldflags = tc->flags;
    tc->flags &= ~TCF_IN_FOR_INIT;
#endif
    pn = Expr(cx, ts, tc);
#if JS_HAS_IN_OPERATOR
    tc->flags = oldflags | (tc->flags & TCF_FUN_FLAGS);
#endif
    return pn;
}

#if JS_HAS_XML_SUPPORT

static JSParseNode *
EndBracketedExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;

    pn = BracketedExpr(cx, ts, tc);
    if (!pn)
        return NULL;

    MUST_MATCH_TOKEN(TOK_RB, JSMSG_BRACKET_AFTER_ATTR_EXPR);
    return pn;
}

/*
 * From the ECMA-357 grammar in 11.1.1 and 11.1.2:
 *
 *      AttributeIdentifier:
 *              @ PropertySelector
 *              @ QualifiedIdentifier
 *              @ [ Expression ]
 *
 *      PropertySelector:
 *              Identifier
 *              *
 *
 *      QualifiedIdentifier:
 *              PropertySelector :: PropertySelector
 *              PropertySelector :: [ Expression ]
 *
 * We adapt AttributeIdentifier and QualifiedIdentier to be LL(1), like so:
 *
 *      AttributeIdentifier:
 *              @ QualifiedIdentifier
 *              @ [ Expression ]
 *
 *      PropertySelector:
 *              Identifier
 *              *
 *
 *      QualifiedIdentifier:
 *              PropertySelector :: PropertySelector
 *              PropertySelector :: [ Expression ]
 *              PropertySelector
 *
 * Since PrimaryExpression: Identifier in ECMA-262 and we want the semantics
 * for that rule to result in a name node, but extend the grammar to include
 * PrimaryExpression: QualifiedIdentifier, we factor further:
 *
 *      QualifiedIdentifier:
 *              PropertySelector QualifiedSuffix
 *
 *      QualifiedSuffix:
 *              :: PropertySelector
 *              :: [ Expression ]
 *              /nothing/
 *
 * And use this production instead of PrimaryExpression: QualifiedIdentifier:
 *
 *      PrimaryExpression:
 *              Identifier QualifiedSuffix
 *
 * We hoists the :: match into callers of QualifiedSuffix, in order to tweak
 * PropertySelector vs. Identifier pn_arity, pn_op, and other members.
 */
static JSParseNode *
PropertySelector(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;

    pn = NewParseNode(cx, ts, PN_NULLARY, tc);
    if (!pn)
        return NULL;
    if (pn->pn_type == TOK_STAR) {
        pn->pn_type = TOK_ANYNAME;
        pn->pn_op = JSOP_ANYNAME;
        pn->pn_atom = cx->runtime->atomState.starAtom;
    } else {
        JS_ASSERT(pn->pn_type == TOK_NAME);
        pn->pn_op = JSOP_QNAMEPART;
        pn->pn_arity = PN_NAME;
        pn->pn_atom = CURRENT_TOKEN(ts).t_atom;
        pn->pn_expr = NULL;
        pn->pn_slot = -1;
        pn->pn_attrs = 0;
    }
    return pn;
}

static JSParseNode *
QualifiedSuffix(JSContext *cx, JSTokenStream *ts, JSParseNode *pn,
                JSTreeContext *tc)
{
    JSParseNode *pn2, *pn3;
    JSTokenType tt;

    JS_ASSERT(CURRENT_TOKEN(ts).type == TOK_DBLCOLON);
    pn2 = NewParseNode(cx, ts, PN_NAME, tc);
    if (!pn2)
        return NULL;

    /* Left operand of :: must be evaluated if it is an identifier. */
    if (pn->pn_op == JSOP_QNAMEPART)
        pn->pn_op = JSOP_NAME;

    tt = js_GetToken(cx, ts);
    if (tt == TOK_STAR || tt == TOK_NAME) {
        /* Inline and specialize PropertySelector for JSOP_QNAMECONST. */
        pn2->pn_op = JSOP_QNAMECONST;
        pn2->pn_atom = (tt == TOK_STAR)
                       ? cx->runtime->atomState.starAtom
                       : CURRENT_TOKEN(ts).t_atom;
        pn2->pn_expr = pn;
        pn2->pn_slot = -1;
        pn2->pn_attrs = 0;
        return pn2;
    }

    if (tt != TOK_LB) {
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_SYNTAX_ERROR);
        return NULL;
    }
    pn3 = EndBracketedExpr(cx, ts, tc);
    if (!pn3)
        return NULL;

    pn2->pn_op = JSOP_QNAME;
    pn2->pn_arity = PN_BINARY;
    pn2->pn_left = pn;
    pn2->pn_right = pn3;
    return pn2;
}

static JSParseNode *
QualifiedIdentifier(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;

    pn = PropertySelector(cx, ts, tc);
    if (!pn)
        return NULL;
    if (js_MatchToken(cx, ts, TOK_DBLCOLON))
        pn = QualifiedSuffix(cx, ts, pn, tc);
    return pn;
}

static JSParseNode *
AttributeIdentifier(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn, *pn2;
    JSTokenType tt;

    JS_ASSERT(CURRENT_TOKEN(ts).type == TOK_AT);
    pn = NewParseNode(cx, ts, PN_UNARY, tc);
    if (!pn)
        return NULL;
    pn->pn_op = JSOP_TOATTRNAME;
    tt = js_GetToken(cx, ts);
    if (tt == TOK_STAR || tt == TOK_NAME) {
        pn2 = QualifiedIdentifier(cx, ts, tc);
    } else if (tt == TOK_LB) {
        pn2 = EndBracketedExpr(cx, ts, tc);
    } else {
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_SYNTAX_ERROR);
        return NULL;
    }
    if (!pn2)
        return NULL;
    pn->pn_kid = pn2;
    return pn;
}

/*
 * Make a TOK_LC unary node whose pn_kid is an expression.
 */
static JSParseNode *
XMLExpr(JSContext *cx, JSTokenStream *ts, JSBool inTag, JSTreeContext *tc)
{
    JSParseNode *pn, *pn2;
    uintN oldflags;

    JS_ASSERT(CURRENT_TOKEN(ts).type == TOK_LC);
    pn = NewParseNode(cx, ts, PN_UNARY, tc);
    if (!pn)
        return NULL;

    /*
     * Turn off XML tag mode, but don't restore it after parsing this braced
     * expression.  Instead, simply restore ts's old flags.  This is required
     * because XMLExpr is called both from within a tag, and from within text
     * contained in an element, but outside of any start, end, or point tag.
     */
    oldflags = ts->flags;
    ts->flags = oldflags & ~TSF_XMLTAGMODE;
    pn2 = Expr(cx, ts, tc);
    if (!pn2)
        return NULL;

    MUST_MATCH_TOKEN(TOK_RC, JSMSG_CURLY_IN_XML_EXPR);
    ts->flags = oldflags;
    pn->pn_kid = pn2;
    pn->pn_op = inTag ? JSOP_XMLTAGEXPR : JSOP_XMLELTEXPR;
    return pn;
}

/*
 * Make a terminal node for oneof TOK_XMLNAME, TOK_XMLATTR, TOK_XMLSPACE,
 * TOK_XMLTEXT, TOK_XMLCDATA, TOK_XMLCOMMENT, or TOK_XMLPI.  When converting
 * parse tree to XML, we preserve a TOK_XMLSPACE node only if it's the sole
 * child of a container tag.
 */
static JSParseNode *
XMLAtomNode(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn;
    JSToken *tp;

    pn = NewParseNode(cx, ts, PN_NULLARY, tc);
    if (!pn)
        return NULL;
    tp = &CURRENT_TOKEN(ts);
    pn->pn_op = tp->t_op;
    pn->pn_atom = tp->t_atom;
    if (tp->type == TOK_XMLPI)
        pn->pn_atom2 = tp->t_atom2;
    return pn;
}

/*
 * Parse the productions:
 *
 *      XMLNameExpr:
 *              XMLName XMLNameExpr?
 *              { Expr } XMLNameExpr?
 *
 * Return a PN_LIST, PN_UNARY, or PN_NULLARY according as XMLNameExpr produces
 * a list of names and/or expressions, a single expression, or a single name.
 * If PN_LIST or PN_NULLARY, pn_type will be TOK_XMLNAME; if PN_UNARY, pn_type
 * will be TOK_LC.
 */
static JSParseNode *
XMLNameExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSParseNode *pn, *pn2, *list;
    JSTokenType tt;

    pn = list = NULL;
    do {
        tt = CURRENT_TOKEN(ts).type;
        if (tt == TOK_LC) {
            pn2 = XMLExpr(cx, ts, JS_TRUE, tc);
            if (!pn2)
                return NULL;
        } else {
            JS_ASSERT(tt == TOK_XMLNAME);
            pn2 = XMLAtomNode(cx, ts, tc);
            if (!pn2)
                return NULL;
        }

        if (!pn) {
            pn = pn2;
        } else {
            if (!list) {
                list = NewParseNode(cx, ts, PN_LIST, tc);
                if (!list)
                    return NULL;
                list->pn_type = TOK_XMLNAME;
                list->pn_pos.begin = pn->pn_pos.begin;
                PN_INIT_LIST_1(list, pn);
                list->pn_extra = PNX_CANTFOLD;
                pn = list;
            }
            pn->pn_pos.end = pn2->pn_pos.end;
            PN_APPEND(pn, pn2);
        }
    } while ((tt = js_GetToken(cx, ts)) == TOK_XMLNAME || tt == TOK_LC);

    js_UngetToken(ts);
    return pn;
}

/*
 * Macro to test whether an XMLNameExpr or XMLTagContent node can be folded
 * at compile time into a JSXML tree.
 */
#define XML_FOLDABLE(pn)        ((pn)->pn_arity == PN_LIST                    \
                                 ? ((pn)->pn_extra & PNX_CANTFOLD) == 0       \
                                 : (pn)->pn_type != TOK_LC)

/*
 * Parse the productions:
 *
 *      XMLTagContent:
 *              XMLNameExpr
 *              XMLTagContent S XMLNameExpr S? = S? XMLAttr
 *              XMLTagContent S XMLNameExpr S? = S? { Expr }
 *
 * Return a PN_LIST, PN_UNARY, or PN_NULLARY according to how XMLTagContent
 * produces a list of name and attribute values and/or braced expressions, a
 * single expression, or a single name.
 *
 * If PN_LIST or PN_NULLARY, pn_type will be TOK_XMLNAME for the case where
 * XMLTagContent: XMLNameExpr.  If pn_type is not TOK_XMLNAME but pn_arity is
 * PN_LIST, pn_type will be tagtype.  If PN_UNARY, pn_type will be TOK_LC and
 * we parsed exactly one expression.
 */
static JSParseNode *
XMLTagContent(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc,
              JSTokenType tagtype, JSAtom **namep)
{
    JSParseNode *pn, *pn2, *list;
    JSTokenType tt;

    pn = XMLNameExpr(cx, ts, tc);
    if (!pn)
        return NULL;
    *namep = (pn->pn_arity == PN_NULLARY) ? pn->pn_atom : NULL;
    list = NULL;

    while (js_MatchToken(cx, ts, TOK_XMLSPACE)) {
        tt = js_GetToken(cx, ts);
        if (tt != TOK_XMLNAME && tt != TOK_LC) {
            js_UngetToken(ts);
            break;
        }

        pn2 = XMLNameExpr(cx, ts, tc);
        if (!pn2)
            return NULL;
        if (!list) {
            list = NewParseNode(cx, ts, PN_LIST, tc);
            if (!list)
                return NULL;
            list->pn_type = tagtype;
            list->pn_pos.begin = pn->pn_pos.begin;
            PN_INIT_LIST_1(list, pn);
            pn = list;
        }
        PN_APPEND(pn, pn2);
        if (!XML_FOLDABLE(pn2))
            pn->pn_extra |= PNX_CANTFOLD;

        js_MatchToken(cx, ts, TOK_XMLSPACE);
        MUST_MATCH_TOKEN(TOK_ASSIGN, JSMSG_NO_ASSIGN_IN_XML_ATTR);
        js_MatchToken(cx, ts, TOK_XMLSPACE);

        tt = js_GetToken(cx, ts);
        if (tt == TOK_XMLATTR) {
            pn2 = XMLAtomNode(cx, ts, tc);
        } else if (tt == TOK_LC) {
            pn2 = XMLExpr(cx, ts, JS_TRUE, tc);
            pn->pn_extra |= PNX_CANTFOLD;
        } else {
            js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                        JSMSG_BAD_XML_ATTR_VALUE);
            return NULL;
        }
        if (!pn2)
            return NULL;
        pn->pn_pos.end = pn2->pn_pos.end;
        PN_APPEND(pn, pn2);
    }

    return pn;
}

#define XML_CHECK_FOR_ERROR_AND_EOF(tt,result)                                \
    JS_BEGIN_MACRO                                                            \
        if ((tt) <= TOK_EOF) {                                                \
            if ((tt) == TOK_EOF) {                                            \
                js_ReportCompileErrorNumber(cx, ts,                           \
                                            JSREPORT_TS | JSREPORT_ERROR,     \
                                            JSMSG_END_OF_XML_SOURCE);         \
            }                                                                 \
            return result;                                                    \
        }                                                                     \
    JS_END_MACRO

static JSParseNode *
XMLElementOrList(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc,
                 JSBool allowList);

/*
 * Consume XML element tag content, including the TOK_XMLETAGO (</) sequence
 * that opens the end tag for the container.
 */
static JSBool
XMLElementContent(JSContext *cx, JSTokenStream *ts, JSParseNode *pn,
                  JSTreeContext *tc)
{
    JSTokenType tt;
    JSParseNode *pn2;
    JSAtom *textAtom;

    ts->flags &= ~TSF_XMLTAGMODE;
    for (;;) {
        ts->flags |= TSF_XMLTEXTMODE;
        tt = js_GetToken(cx, ts);
        ts->flags &= ~TSF_XMLTEXTMODE;
        XML_CHECK_FOR_ERROR_AND_EOF(tt, JS_FALSE);

        JS_ASSERT(tt == TOK_XMLSPACE || tt == TOK_XMLTEXT);
        textAtom = CURRENT_TOKEN(ts).t_atom;
        if (textAtom) {
            /* Non-zero-length XML text scanned. */
            pn2 = XMLAtomNode(cx, ts, tc);
            if (!pn2)
                return JS_FALSE;
            pn->pn_pos.end = pn2->pn_pos.end;
            PN_APPEND(pn, pn2);
        }

        ts->flags |= TSF_OPERAND;
        tt = js_GetToken(cx, ts);
        ts->flags &= ~TSF_OPERAND;
        XML_CHECK_FOR_ERROR_AND_EOF(tt, JS_FALSE);
        if (tt == TOK_XMLETAGO)
            break;

        if (tt == TOK_LC) {
            pn2 = XMLExpr(cx, ts, JS_FALSE, tc);
            pn->pn_extra |= PNX_CANTFOLD;
        } else if (tt == TOK_XMLSTAGO) {
            pn2 = XMLElementOrList(cx, ts, tc, JS_FALSE);
            if (pn2) {
                pn2->pn_extra &= ~PNX_XMLROOT;
                pn->pn_extra |= pn2->pn_extra;
            }
        } else {
            JS_ASSERT(tt == TOK_XMLCDATA || tt == TOK_XMLCOMMENT ||
                      tt == TOK_XMLPI);
            pn2 = XMLAtomNode(cx, ts, tc);
        }
        if (!pn2)
            return JS_FALSE;
        pn->pn_pos.end = pn2->pn_pos.end;
        PN_APPEND(pn, pn2);
    }

    JS_ASSERT(CURRENT_TOKEN(ts).type == TOK_XMLETAGO);
    ts->flags |= TSF_XMLTAGMODE;
    return JS_TRUE;
}

/*
 * Return a PN_LIST node containing an XML or XMLList Initialiser.
 */
static JSParseNode *
XMLElementOrList(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc,
                 JSBool allowList)
{
    JSParseNode *pn, *pn2, *list;
    JSBool hadSpace;
    JSTokenType tt;
    JSAtom *startAtom, *endAtom;

    JS_ASSERT(CURRENT_TOKEN(ts).type == TOK_XMLSTAGO);
    pn = NewParseNode(cx, ts, PN_LIST, tc);
    if (!pn)
        return NULL;

    ts->flags |= TSF_XMLTAGMODE;
    hadSpace = js_MatchToken(cx, ts, TOK_XMLSPACE);
    tt = js_GetToken(cx, ts);
    if (tt == TOK_ERROR)
        return NULL;

    if (tt == TOK_XMLNAME || tt == TOK_LC) {
        /*
         * XMLElement.  Append the tag and its contents, if any, to pn.
         */
        pn2 = XMLTagContent(cx, ts, tc, TOK_XMLSTAGO, &startAtom);
        if (!pn2)
            return NULL;
        js_MatchToken(cx, ts, TOK_XMLSPACE);

        tt = js_GetToken(cx, ts);
        if (tt == TOK_XMLPTAGC) {
            /* Point tag (/>): recycle pn if pn2 is a list of tag contents. */
            if (pn2->pn_type == TOK_XMLSTAGO) {
                PN_INIT_LIST(pn);
                RecycleTree(pn, tc);
                pn = pn2;
            } else {
                JS_ASSERT(pn2->pn_type == TOK_XMLNAME ||
                          pn2->pn_type == TOK_LC);
                PN_INIT_LIST_1(pn, pn2);
                if (!XML_FOLDABLE(pn2))
                    pn->pn_extra |= PNX_CANTFOLD;
            }
            pn->pn_type = TOK_XMLPTAGC;
            pn->pn_extra |= PNX_XMLROOT;
        } else {
            /* We had better have a tag-close (>) at this point. */
            if (tt != TOK_XMLTAGC) {
                js_ReportCompileErrorNumber(cx, ts,
                                            JSREPORT_TS | JSREPORT_ERROR,
                                            JSMSG_BAD_XML_TAG_SYNTAX);
                return NULL;
            }
            pn2->pn_pos.end = CURRENT_TOKEN(ts).pos.end;

            /* Make sure pn2 is a TOK_XMLSTAGO list containing tag contents. */
            if (pn2->pn_type != TOK_XMLSTAGO) {
                PN_INIT_LIST_1(pn, pn2);
                if (!XML_FOLDABLE(pn2))
                    pn->pn_extra |= PNX_CANTFOLD;
                pn2 = pn;
                pn = NewParseNode(cx, ts, PN_LIST, tc);
                if (!pn)
                    return NULL;
            }

            /* Now make pn a nominal-root TOK_XMLELEM list containing pn2. */
            pn->pn_type = TOK_XMLELEM;
            PN_INIT_LIST_1(pn, pn2);
            if (!XML_FOLDABLE(pn2))
                pn->pn_extra |= PNX_CANTFOLD;
            pn->pn_extra |= PNX_XMLROOT;

            /* Get element contents and delimiting end-tag-open sequence. */
            if (!XMLElementContent(cx, ts, pn, tc))
                return NULL;

            js_MatchToken(cx, ts, TOK_XMLSPACE);
            tt = js_GetToken(cx, ts);
            XML_CHECK_FOR_ERROR_AND_EOF(tt, NULL);
            if (tt != TOK_XMLNAME && tt != TOK_LC) {
                js_ReportCompileErrorNumber(cx, ts,
                                            JSREPORT_TS | JSREPORT_ERROR,
                                            JSMSG_BAD_XML_TAG_SYNTAX);
                return NULL;
            }

            /* Parse end tag; check mismatch at compile-time if we can. */
            pn2 = XMLTagContent(cx, ts, tc, TOK_XMLETAGO, &endAtom);
            if (!pn2)
                return NULL;
            if (pn2->pn_type == TOK_XMLETAGO) {
                /* Oops, end tag has attributes! */
                js_ReportCompileErrorNumber(cx, ts,
                                            JSREPORT_TS | JSREPORT_ERROR,
                                            JSMSG_BAD_XML_TAG_SYNTAX);
                return NULL;
            }
            if (endAtom && startAtom && endAtom != startAtom) {
                /* End vs. start tag name mismatch: point to the tag name. */
                ++pn2->pn_pos.begin.index;
                js_ReportCompileErrorNumber(cx, pn2,
                                            JSREPORT_PN | JSREPORT_ERROR,
                                            JSMSG_XML_TAG_NAME_MISMATCH);
                return NULL;
            }

            /* Make a TOK_XMLETAGO list with pn2 as its single child. */
            JS_ASSERT(pn2->pn_type == TOK_XMLNAME || pn2->pn_type == TOK_LC);
            list = NewParseNode(cx, ts, PN_LIST, tc);
            if (!list)
                return NULL;
            list->pn_type = TOK_XMLETAGO;
            PN_INIT_LIST_1(list, pn2);
            PN_APPEND(pn, list);
            if (!XML_FOLDABLE(pn2)) {
                list->pn_extra |= PNX_CANTFOLD;
                pn->pn_extra |= PNX_CANTFOLD;
            }

            js_MatchToken(cx, ts, TOK_XMLSPACE);
            MUST_MATCH_TOKEN(TOK_XMLTAGC, JSMSG_BAD_XML_TAG_SYNTAX);
        }

        /* Set pn_op now that pn has been updated to its final value. */
        pn->pn_op = JSOP_TOXML;
    } else if (!hadSpace && allowList && tt == TOK_XMLTAGC) {
        /* XMLList Initialiser. */
        pn->pn_type = TOK_XMLLIST;
        pn->pn_op = JSOP_TOXMLLIST;
        PN_INIT_LIST(pn);
        pn->pn_extra |= PNX_XMLROOT;
        if (!XMLElementContent(cx, ts, pn, tc))
            return NULL;

        MUST_MATCH_TOKEN(TOK_XMLTAGC, JSMSG_BAD_XML_LIST_SYNTAX);
    } else {
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_BAD_XML_NAME_SYNTAX);
        return NULL;
    }

    pn->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
    ts->flags &= ~TSF_XMLTAGMODE;
    return pn;
}

static JSParseNode *
XMLElementOrListRoot(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc,
                     JSBool allowList)
{
    uint32 oldopts;
    JSParseNode *pn;

    /*
     * Force XML support to be enabled so that comments and CDATA literals
     * are recognized, instead of <! followed by -- starting an HTML comment
     * to end of line (used in script tags to hide content from old browsers
     * that don't recognize <script>).
     */
    oldopts = JS_SetOptions(cx, cx->options | JSOPTION_XML);
    pn = XMLElementOrList(cx, ts, tc, allowList);
    JS_SetOptions(cx, oldopts);
    return pn;
}

JS_FRIEND_API(JSParseNode *)
js_ParseXMLTokenStream(JSContext *cx, JSObject *chain, JSTokenStream *ts,
                       JSBool allowList)
{
    JSStackFrame *fp, frame;
    JSParseNode *pn;
    JSTreeContext tc;
    JSTokenType tt;

    /*
     * Push a compiler frame if we have no frames, or if the top frame is a
     * lightweight function activation, or if its scope chain doesn't match
     * the one passed to us.
     */
    fp = cx->fp;
    if (!fp || !fp->varobj || fp->scopeChain != chain) {
        memset(&frame, 0, sizeof frame);
        frame.varobj = frame.scopeChain = chain;
        if (cx->options & JSOPTION_VAROBJFIX) {
            while ((chain = JS_GetParent(cx, chain)) != NULL)
                frame.varobj = chain;
        }
        frame.down = fp;
        if (fp) {
            frame.flags = fp->flags & (JSFRAME_SPECIAL | JSFRAME_COMPILE_N_GO |
                                       JSFRAME_SCRIPT_OBJECT);
        }
        cx->fp = &frame;
    }

    JS_KEEP_ATOMS(cx->runtime);
    TREE_CONTEXT_INIT(&tc);

    /* Set XML-only mode to turn off special treatment of {expr} in XML. */
    ts->flags |= TSF_OPERAND | TSF_XMLONLYMODE;
    tt = js_GetToken(cx, ts);
    ts->flags &= ~TSF_OPERAND;

    if (tt != TOK_XMLSTAGO) {
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_BAD_XML_MARKUP);
        pn = NULL;
    } else {
        pn = XMLElementOrListRoot(cx, ts, &tc, allowList);
    }

    ts->flags &= ~TSF_XMLONLYMODE;
    TREE_CONTEXT_FINISH(&tc);
    JS_UNKEEP_ATOMS(cx->runtime);
    cx->fp = fp;
    return pn;
}

#endif /* JS_HAS_XMLSUPPORT */

static JSParseNode *
PrimaryExpr(JSContext *cx, JSTokenStream *ts, JSTreeContext *tc)
{
    JSTokenType tt;
    JSParseNode *pn, *pn2, *pn3;
#if JS_HAS_GETTER_SETTER
    JSAtom *atom;
    JSRuntime *rt;
#endif

#if JS_HAS_SHARP_VARS
    JSParseNode *defsharp;
    JSBool notsharp;

    defsharp = NULL;
    notsharp = JS_FALSE;
  again:
    /*
     * Control flows here after #n= is scanned.  If the following primary is
     * not valid after such a "sharp variable" definition, the tt switch case
     * should set notsharp.
     */
#endif

    CHECK_RECURSION();

    ts->flags |= TSF_OPERAND;
    tt = js_GetToken(cx, ts);
    ts->flags &= ~TSF_OPERAND;

#if JS_HAS_GETTER_SETTER
    if (tt == TOK_NAME) {
        tt = CheckGetterOrSetter(cx, ts, TOK_FUNCTION);
        if (tt == TOK_ERROR)
            return NULL;
    }
#endif

    switch (tt) {
#if JS_HAS_LEXICAL_CLOSURE || JS_HAS_XML_SUPPORT
      case TOK_FUNCTION:
#if JS_HAS_XML_SUPPORT
        if (js_MatchToken(cx, ts, TOK_DBLCOLON)) {
            pn2 = NewParseNode(cx, ts, PN_NULLARY, tc);
            if (!pn2)
                return NULL;
            pn2->pn_type = TOK_FUNCTION;
            pn = QualifiedSuffix(cx, ts, pn2, tc);
            if (!pn)
                return NULL;
            break;
        }
#endif
        pn = FunctionExpr(cx, ts, tc);
        if (!pn)
            return NULL;
        break;
#endif

#if JS_HAS_INITIALIZERS
      case TOK_LB:
      {
        JSBool matched;
        jsuint atomIndex;

        pn = NewParseNode(cx, ts, PN_LIST, tc);
        if (!pn)
            return NULL;
        pn->pn_type = TOK_RB;

#if JS_HAS_SHARP_VARS
        if (defsharp) {
            PN_INIT_LIST_1(pn, defsharp);
            defsharp = NULL;
        } else
#endif
            PN_INIT_LIST(pn);

        ts->flags |= TSF_OPERAND;
        matched = js_MatchToken(cx, ts, TOK_RB);
        ts->flags &= ~TSF_OPERAND;
        if (!matched) {
            for (atomIndex = 0; ; atomIndex++) {
                if (atomIndex == ATOM_INDEX_LIMIT) {
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_ARRAY_INIT_TOO_BIG);
                    return NULL;
                }

                ts->flags |= TSF_OPERAND;
                tt = js_PeekToken(cx, ts);
                ts->flags &= ~TSF_OPERAND;
                if (tt == TOK_RB) {
                    pn->pn_extra |= PNX_ENDCOMMA;
                    break;
                }

                if (tt == TOK_COMMA) {
                    /* So CURRENT_TOKEN gets TOK_COMMA and not TOK_LB. */
                    js_MatchToken(cx, ts, TOK_COMMA);
                    pn2 = NewParseNode(cx, ts, PN_NULLARY, tc);
                } else {
                    pn2 = AssignExpr(cx, ts, tc);
                }
                if (!pn2)
                    return NULL;
                PN_APPEND(pn, pn2);

                if (tt != TOK_COMMA) {
                    /* If we didn't already match TOK_COMMA in above case. */
                    if (!js_MatchToken(cx, ts, TOK_COMMA))
                        break;
                }
            }

            MUST_MATCH_TOKEN(TOK_RB, JSMSG_BRACKET_AFTER_LIST);
        }
        pn->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
        return pn;
      }

      case TOK_LC:
        pn = NewParseNode(cx, ts, PN_LIST, tc);
        if (!pn)
            return NULL;
        pn->pn_type = TOK_RC;

#if JS_HAS_SHARP_VARS
        if (defsharp) {
            PN_INIT_LIST_1(pn, defsharp);
            defsharp = NULL;
        } else
#endif
            PN_INIT_LIST(pn);

        if (!js_MatchToken(cx, ts, TOK_RC)) {
            do {
                JSOp op;

                tt = js_GetToken(cx, ts);
                switch (tt) {
                  case TOK_NUMBER:
                    pn3 = NewParseNode(cx, ts, PN_NULLARY, tc);
                    if (pn3)
                        pn3->pn_dval = CURRENT_TOKEN(ts).t_dval;
                    break;
                  case TOK_NAME:
#if JS_HAS_GETTER_SETTER
                    atom = CURRENT_TOKEN(ts).t_atom;
                    rt = cx->runtime;
                    if (atom == rt->atomState.getAtom ||
                        atom == rt->atomState.setAtom) {
                        op = (atom == rt->atomState.getAtom)
                             ? JSOP_GETTER
                             : JSOP_SETTER;
                        if (js_MatchToken(cx, ts, TOK_NAME)) {
                            pn3 = NewParseNode(cx, ts, PN_NAME, tc);
                            if (!pn3)
                                return NULL;
                            pn3->pn_atom = CURRENT_TOKEN(ts).t_atom;
                            pn3->pn_expr = NULL;

                            /* We have to fake a 'function' token here. */
                            CURRENT_TOKEN(ts).t_op = JSOP_NOP;
                            CURRENT_TOKEN(ts).type = TOK_FUNCTION;
                            pn2 = FunctionExpr(cx, ts, tc);
                            pn2 = NewBinary(cx, TOK_COLON, op, pn3, pn2, tc);
                            goto skip;
                        }
                    }
                    /* else fall thru ... */
#endif
                  case TOK_STRING:
                    pn3 = NewParseNode(cx, ts, PN_NULLARY, tc);
                    if (pn3)
                        pn3->pn_atom = CURRENT_TOKEN(ts).t_atom;
                    break;
                  case TOK_RC:
                    if (!js_ReportCompileErrorNumber(cx, ts,
                                                     JSREPORT_TS |
                                                     JSREPORT_WARNING |
                                                     JSREPORT_STRICT,
                                                     JSMSG_TRAILING_COMMA)) {
                        return NULL;
                    }
                    goto end_obj_init;
                  default:
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_BAD_PROP_ID);
                    return NULL;
                }

                tt = js_GetToken(cx, ts);
#if JS_HAS_GETTER_SETTER
                if (tt == TOK_NAME) {
                    tt = CheckGetterOrSetter(cx, ts, TOK_COLON);
                    if (tt == TOK_ERROR)
                        return NULL;
                }
#endif
                if (tt != TOK_COLON) {
                    js_ReportCompileErrorNumber(cx, ts,
                                                JSREPORT_TS | JSREPORT_ERROR,
                                                JSMSG_COLON_AFTER_ID);
                    return NULL;
                }
                op = CURRENT_TOKEN(ts).t_op;
                pn2 = NewBinary(cx, TOK_COLON, op, pn3, AssignExpr(cx, ts, tc),
                                tc);
#if JS_HAS_GETTER_SETTER
              skip:
#endif
                if (!pn2)
                    return NULL;
                PN_APPEND(pn, pn2);
            } while (js_MatchToken(cx, ts, TOK_COMMA));

            MUST_MATCH_TOKEN(TOK_RC, JSMSG_CURLY_AFTER_LIST);
        }
      end_obj_init:
        pn->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
        return pn;

#if JS_HAS_SHARP_VARS
      case TOK_DEFSHARP:
        if (defsharp)
            goto badsharp;
        defsharp = NewParseNode(cx, ts, PN_UNARY, tc);
        if (!defsharp)
            return NULL;
        defsharp->pn_kid = NULL;
        defsharp->pn_num = (jsint) CURRENT_TOKEN(ts).t_dval;
        goto again;

      case TOK_USESHARP:
        /* Check for forward/dangling references at runtime, to allow eval. */
        pn = NewParseNode(cx, ts, PN_NULLARY, tc);
        if (!pn)
            return NULL;
        pn->pn_num = (jsint) CURRENT_TOKEN(ts).t_dval;
        notsharp = JS_TRUE;
        break;
#endif /* JS_HAS_SHARP_VARS */
#endif /* JS_HAS_INITIALIZERS */

      case TOK_LP:
        pn = NewParseNode(cx, ts, PN_UNARY, tc);
        if (!pn)
            return NULL;
        pn2 = BracketedExpr(cx, ts, tc);
        if (!pn2)
            return NULL;

        MUST_MATCH_TOKEN(TOK_RP, JSMSG_PAREN_IN_PAREN);
        pn->pn_type = TOK_RP;
        pn->pn_pos.end = CURRENT_TOKEN(ts).pos.end;
        pn->pn_kid = pn2;
        break;

#if JS_HAS_XML_SUPPORT
      case TOK_STAR:
        pn = QualifiedIdentifier(cx, ts, tc);
        if (!pn)
            return NULL;
        notsharp = JS_TRUE;
        break;

      case TOK_AT:
        pn = AttributeIdentifier(cx, ts, tc);
        if (!pn)
            return NULL;
        notsharp = JS_TRUE;
        break;

      case TOK_XMLSTAGO:
        pn = XMLElementOrListRoot(cx, ts, tc, JS_TRUE);
        if (!pn)
            return NULL;
        notsharp = JS_TRUE;     /* XXXbe could be sharp? */
        break;
#endif /* JS_HAS_XML_SUPPORT */

      case TOK_STRING:
#if JS_HAS_SHARP_VARS
        notsharp = JS_TRUE;
        /* FALL THROUGH */
#endif

#if JS_HAS_XML_SUPPORT
      case TOK_XMLCDATA:
      case TOK_XMLCOMMENT:
      case TOK_XMLPI:
#endif
      case TOK_NAME:
      case TOK_OBJECT:
        pn = NewParseNode(cx, ts, PN_NULLARY, tc);
        if (!pn)
            return NULL;
        pn->pn_atom = CURRENT_TOKEN(ts).t_atom;
#if JS_HAS_XML_SUPPORT
        if (tt == TOK_XMLPI)
            pn->pn_atom2 = CURRENT_TOKEN(ts).t_atom2;
        else
#endif
            pn->pn_op = CURRENT_TOKEN(ts).t_op;
        if (tt == TOK_NAME) {
            pn->pn_arity = PN_NAME;
            pn->pn_expr = NULL;
            pn->pn_slot = -1;
            pn->pn_attrs = 0;

#if JS_HAS_XML_SUPPORT
            if (js_MatchToken(cx, ts, TOK_DBLCOLON)) {
                pn = QualifiedSuffix(cx, ts, pn, tc);
                if (!pn)
                    return NULL;
                break;
            }
#endif

            /* Unqualified __parent__ and __proto__ uses require activations. */
            if (pn->pn_atom == cx->runtime->atomState.parentAtom ||
                pn->pn_atom == cx->runtime->atomState.protoAtom) {
                tc->flags |= TCF_FUN_HEAVYWEIGHT;
            } else {
                JSAtomListElement *ale;
                JSStackFrame *fp;
                JSStmtInfo *stmt;

                /* Measure optimizable global variable uses. */
                ATOM_LIST_SEARCH(ale, &tc->decls, pn->pn_atom);
                if (ale &&
                    !(fp = cx->fp)->fun &&
                    fp->scopeChain == fp->varobj &&
                    !js_InWithStatement(tc) &&
                    !js_InCatchBlock(tc, pn->pn_atom)) {
                    tc->globalUses++;
                    for (stmt = tc->topStmt; stmt; stmt = stmt->down) {
 /* MUTANT (negate) */                        if(! (STMT_IS_LOOP(stmt))) {
                            tc->loopyGlobalUses++;
                            break;
                        }
                    }
                }
            }
        }
        break;

      case TOK_NUMBER:
        pn = NewParseNode(cx, ts, PN_NULLARY, tc);
        if (!pn)
            return NULL;
        pn->pn_dval = CURRENT_TOKEN(ts).t_dval;
#if JS_HAS_SHARP_VARS
        notsharp = JS_TRUE;
#endif
        break;

      case TOK_PRIMARY:
        pn = NewParseNode(cx, ts, PN_NULLARY, tc);
        if (!pn)
            return NULL;
        pn->pn_op = CURRENT_TOKEN(ts).t_op;
#if JS_HAS_SHARP_VARS
        notsharp = JS_TRUE;
#endif
        break;

#if !JS_HAS_EXPORT_IMPORT
      case TOK_EXPORT:
      case TOK_IMPORT:
#endif
      case TOK_ERROR:
        /* The scanner or one of its subroutines reported the error. */
        return NULL;

      default:
        js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                    JSMSG_SYNTAX_ERROR);
        return NULL;
    }

#if JS_HAS_SHARP_VARS
    if (defsharp) {
        if (notsharp) {
  badsharp:
            js_ReportCompileErrorNumber(cx, ts, JSREPORT_TS | JSREPORT_ERROR,
                                        JSMSG_BAD_SHARP_VAR_DEF);
            return NULL;
        }
        defsharp->pn_kid = pn;
        return defsharp;
    }
#endif
    return pn;
}

static JSBool
ContainsVarStmt(JSParseNode *pn)
{
    JSParseNode *pn2;

    if (!pn)
        return JS_FALSE;
    switch (pn->pn_arity) {
      case PN_LIST:
        if (pn->pn_type == TOK_VAR)
            return JS_TRUE;
        for (pn2 = pn->pn_head; pn2; pn2 = pn2->pn_next) {
            if (ContainsVarStmt(pn2))
                return JS_TRUE;
        }
        break;
      case PN_TERNARY:
        return ContainsVarStmt(pn->pn_kid1) ||
               ContainsVarStmt(pn->pn_kid2) ||
               ContainsVarStmt(pn->pn_kid3);
      case PN_BINARY:
        /*
         * Limit recursion if pn is a binary expression, which can't contain a
         * var statement.
         */
        if (pn->pn_op != JSOP_NOP)
            return JS_FALSE;
        return ContainsVarStmt(pn->pn_left) || ContainsVarStmt(pn->pn_right);
      case PN_UNARY:
        if (pn->pn_op != JSOP_NOP)
            return JS_FALSE;
        return ContainsVarStmt(pn->pn_kid);
      default:;
    }
    return JS_FALSE;
}

/*
 * Fold from one constant type to another.
 * XXX handles only strings and numbers for now
 */
static JSBool
FoldType(JSContext *cx, JSParseNode *pn, JSTokenType type)
{
    if (pn->pn_type != type) {
        switch (type) {
          case TOK_NUMBER:
            if (pn->pn_type == TOK_STRING) {
                jsdouble d;
                if (!js_ValueToNumber(cx, ATOM_KEY(pn->pn_atom), &d))
                    return JS_FALSE;
                pn->pn_dval = d;
                pn->pn_type = TOK_NUMBER;
                pn->pn_op = JSOP_NUMBER;
            }
            break;

          case TOK_STRING:
            if (pn->pn_type == TOK_NUMBER) {
                JSString *str = js_NumberToString(cx, pn->pn_dval);
                if (!str)
                    return JS_FALSE;
                pn->pn_atom = js_AtomizeString(cx, str, 0);
                if (!pn->pn_atom)
                    return JS_FALSE;
                pn->pn_type = TOK_STRING;
                pn->pn_op = JSOP_STRING;
            }
            break;

          default:;
        }
    }
    return JS_TRUE;
}

/*
 * Fold two numeric constants.  Beware that pn1 and pn2 are recycled, unless
 * one of them aliases pn, so you can't safely fetch pn2->pn_next, e.g., after
 * a successful call to this function.
 */
static JSBool
FoldBinaryNumeric(JSContext *cx, JSOp op, JSParseNode *pn1, JSParseNode *pn2,
                  JSParseNode *pn, JSTreeContext *tc)
{
    jsdouble d, d2;
    int32 i, j;
    uint32 u;

    JS_ASSERT(pn1->pn_type == TOK_NUMBER && pn2->pn_type == TOK_NUMBER);
    d = pn1->pn_dval;
    d2 = pn2->pn_dval;
    switch (op) {
      case JSOP_LSH:
      case JSOP_RSH:
        if (!js_DoubleToECMAInt32(cx, d, &i))
            return JS_FALSE;
        if (!js_DoubleToECMAInt32(cx, d2, &j))
            return JS_FALSE;
        j &= 31;
        d = (op == JSOP_LSH) ? i << j : i >> j;
        break;

      case JSOP_URSH:
        if (!js_DoubleToECMAUint32(cx, d, &u))
            return JS_FALSE;
        if (!js_DoubleToECMAInt32(cx, d2, &j))
            return JS_FALSE;
        j &= 31;
        d = u >> j;
        break;

      case JSOP_ADD:
        d += d2;
        break;

      case JSOP_SUB:
        d -= d2;
        break;

      case JSOP_MUL:
        d *= d2;
        break;

      case JSOP_DIV:
        if (d2 == 0) {
#if defined(XP_WIN)
            /* XXX MSVC miscompiles such that (NaN == 0) */
            if (JSDOUBLE_IS_NaN(d2))
                d = *cx->runtime->jsNaN;
            else
#endif
            if (d == 0 || JSDOUBLE_IS_NaN(d))
                d = *cx->runtime->jsNaN;
            else if ((JSDOUBLE_HI32(d) ^ JSDOUBLE_HI32(d2)) >> 31)
                d = *cx->runtime->jsNegativeInfinity;
            else
                d = *cx->runtime->jsPositiveInfinity;
        } else {
            d /= d2;
        }
        break;

      case JSOP_MOD:
        if (d2 == 0) {
            d = *cx->runtime->jsNaN;
        } else {
#if defined(XP_WIN)
          /* Workaround MS fmod bug where 42 % (1/0) => NaN, not 42. */
          if (!(JSDOUBLE_IS_FINITE(d) && JSDOUBLE_IS_INFINITE(d2)))
#endif
            d = fmod(d, d2);
        }
        break;

      default:;
    }

    /* Take care to allow pn1 or pn2 to alias pn. */
    if (pn1 != pn)
        RecycleTree(pn1, tc);
    if (pn2 != pn)
        RecycleTree(pn2, tc);
    pn->pn_type = TOK_NUMBER;
    pn->pn_op = JSOP_NUMBER;
    pn->pn_arity = PN_NULLARY;
    pn->pn_dval = d;
    return JS_TRUE;
}

#if JS_HAS_XML_SUPPORT

static JSBool
FoldXMLConstants(JSContext *cx, JSParseNode *pn, JSTreeContext *tc)
{
    JSTokenType tt;
    JSParseNode **pnp, *pn1, *pn2;
    JSString *accum, *str;
    uint32 i, j;

    JS_ASSERT(pn->pn_arity == PN_LIST);
    tt = pn->pn_type;
    pnp = &pn->pn_head;
    pn1 = *pnp;
    accum = NULL;
    if ((pn->pn_extra & PNX_CANTFOLD) == 0) {
        if (tt == TOK_XMLETAGO)
            accum = ATOM_TO_STRING(cx->runtime->atomState.etagoAtom);
        else if (tt == TOK_XMLSTAGO || tt == TOK_XMLPTAGC)
            accum = ATOM_TO_STRING(cx->runtime->atomState.stagoAtom);
    }

    for (pn2 = pn1, i = j = 0; pn2; pn2 = pn2->pn_next, i++) {
        /* The parser already rejected end-tags with attributes. */
        JS_ASSERT(tt != TOK_XMLETAGO || i == 0);
        switch (pn2->pn_type) {
          case TOK_XMLATTR:
            if (!accum)
                goto cantfold;
            /* FALL THROUGH */
          case TOK_XMLNAME:
          case TOK_XMLSPACE:
          case TOK_XMLTEXT:
          case TOK_STRING:
            if (pn2->pn_arity == PN_LIST)
                goto cantfold;
            str = ATOM_TO_STRING(pn2->pn_atom);
            break;

          case TOK_XMLCDATA:
            str = js_MakeXMLCDATAString(cx, ATOM_TO_STRING(pn2->pn_atom));
            if (!str)
                return JS_FALSE;
            break;

          case TOK_XMLCOMMENT:
            str = js_MakeXMLCommentString(cx, ATOM_TO_STRING(pn2->pn_atom));
            if (!str)
                return JS_FALSE;
            break;

          case TOK_XMLPI:
            str = js_MakeXMLPIString(cx, ATOM_TO_STRING(pn2->pn_atom),
                                         ATOM_TO_STRING(pn2->pn_atom2));
            if (!str)
                return JS_FALSE;
            break;

          cantfold:
          default:
            JS_ASSERT(*pnp == pn1);
            if ((tt == TOK_XMLSTAGO || tt == TOK_XMLPTAGC) &&
                (i & 1) ^ (j & 1)) {
#ifdef DEBUG_brendanXXX
                printf("1: %d, %d => %s\n",
                       i, j, accum ? JS_GetStringBytes(accum) : "NULL");
#endif
            } else if (accum && pn1 != pn2) {
                while (pn1->pn_next != pn2) {
                    pn1 = RecycleTree(pn1, tc);
                    --pn->pn_count;
                }
                pn1->pn_type = TOK_XMLTEXT;
                pn1->pn_op = JSOP_STRING;
                pn1->pn_arity = PN_NULLARY;
                pn1->pn_atom = js_AtomizeString(cx, accum, 0);
                if (!pn1->pn_atom)
                    return JS_FALSE;
                JS_ASSERT(pnp != &pn1->pn_next);
                *pnp = pn1;
            }
            pnp = &pn2->pn_next;
            pn1 = *pnp;
            accum = NULL;
            continue;
        }

        if (accum) {
            str = ((tt == TOK_XMLSTAGO || tt == TOK_XMLPTAGC) && i != 0)
                  ? js_AddAttributePart(cx, i & 1, accum, str)
                  : js_ConcatStrings(cx, accum, str);
            if (!str)
                return JS_FALSE;
#ifdef DEBUG_brendanXXX
            printf("2: %d, %d => %s (%u)\n",
                   i, j, JS_GetStringBytes(str), JSSTRING_LENGTH(str));
#endif
            ++j;
        }
        accum = str;
    }

    if (accum) {
        str = NULL;
        if ((pn->pn_extra & PNX_CANTFOLD) == 0) {
            if (tt == TOK_XMLPTAGC)
                str = ATOM_TO_STRING(cx->runtime->atomState.ptagcAtom);
            else if (tt == TOK_XMLSTAGO || tt == TOK_XMLETAGO)
                str = ATOM_TO_STRING(cx->runtime->atomState.tagcAtom);
        }
        if (str) {
            accum = js_ConcatStrings(cx, accum, str);
            if (!accum)
                return JS_FALSE;
        }

        JS_ASSERT(*pnp == pn1);
        while (pn1->pn_next) {
            pn1 = RecycleTree(pn1, tc);
            --pn->pn_count;
        }
        pn1->pn_type = TOK_XMLTEXT;
        pn1->pn_op = JSOP_STRING;
        pn1->pn_arity = PN_NULLARY;
        pn1->pn_atom = js_AtomizeString(cx, accum, 0);
        if (!pn1->pn_atom)
            return JS_FALSE;
        JS_ASSERT(pnp != &pn1->pn_next);
        *pnp = pn1;
    }

    if (pn1 && pn->pn_count == 1) {
        /*
         * Only one node under pn, and it has been folded: move pn1 onto pn
         * unless pn is an XML root (in which case we need it to tell the code
         * generator to emit a JSOP_TOXML or JSOP_TOXMLLIST op).  If pn is an
         * XML root *and* it's a point-tag, rewrite it to TOK_XMLELEM to avoid
         * extra "<" and "/>" bracketing at runtime.
         */
        if (!(pn->pn_extra & PNX_XMLROOT)) {
            PN_MOVE_NODE(pn, pn1);
        } else if (tt == TOK_XMLPTAGC) {
            pn->pn_type = TOK_XMLELEM;
            pn->pn_op = JSOP_TOXML;
        }
    }
    return JS_TRUE;
}

#endif /* JS_HAS_XML_SUPPORT */

JSBool
js_FoldConstants(JSContext *cx, JSParseNode *pn, JSTreeContext *tc)
{
    JSParseNode *pn1 = NULL, *pn2 = NULL, *pn3 = NULL;
    int stackDummy;

    if (!JS_CHECK_STACK_SIZE(cx, stackDummy)) {
        JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_OVER_RECURSED);
        return JS_FALSE;
    }

    switch (pn->pn_arity) {
      case PN_FUNC:
      {
        uint16 oldflags = tc->flags;

        tc->flags = (uint16) pn->pn_flags;
        if (!js_FoldConstants(cx, pn->pn_body, tc))
            return JS_FALSE;
        tc->flags = oldflags;
        break;
      }

      case PN_LIST:
#if 0 /* JS_HAS_XML_SUPPORT */
        switch (pn->pn_type) {
          case TOK_XMLELEM:
          case TOK_XMLLIST:
          case TOK_XMLPTAGC:
            /*
             * Try to fold this XML parse tree once, from the top down, into
             * a JSXML tree with just one object wrapping the tree root.
             *
             * Certain subtrees could be folded similarly, but we'd have to
             * ensure that none used namespace prefixes declared elsewhere in
             * its super-tree, and we would have to convert each XML object
             * created at runtime for such sub-trees back into a string, and
             * concatenate and re-parse anyway.
             */
            if ((pn->pn_extra & (PNX_XMLROOT | PNX_CANTFOLD)) == PNX_XMLROOT &&
                !(tc->flags & TCF_HAS_DEFXMLNS)) {
                JSObject *obj;
                JSAtom *atom;

                obj = js_ParseNodeToXMLObject(cx, pn);
                if (!obj)
                    return JS_FALSE;
                atom = js_AtomizeObject(cx, obj, 0);
                if (!atom)
                    return JS_FALSE;
                pn->pn_op = JSOP_XMLOBJECT;
                pn->pn_arity = PN_NULLARY;
                pn->pn_atom = atom;
                return JS_TRUE;
            }

            /*
             * Can't fold from parse node to XML tree -- try folding strings
             * as much as possible, and folding XML sub-trees bottom up to
             * minimize string concatenation and ToXML/ToXMLList operations
             * at runtime.
             */
            break;

          default:;
        }
#endif

        /* Save the list head in pn1 for later use. */
        for (pn1 = pn2 = pn->pn_head; pn2; pn2 = pn2->pn_next) {
            if (!js_FoldConstants(cx, pn2, tc))
                return JS_FALSE;
        }
        break;

      case PN_TERNARY:
        /* Any kid may be null (e.g. for (;;)). */
        pn1 = pn->pn_kid1;
        pn2 = pn->pn_kid2;
        pn3 = pn->pn_kid3;
        if (pn1 && !js_FoldConstants(cx, pn1, tc))
            return JS_FALSE;
        if (pn2 && !js_FoldConstants(cx, pn2, tc))
            return JS_FALSE;
        if (pn3 && !js_FoldConstants(cx, pn3, tc))
            return JS_FALSE;
        break;

      case PN_BINARY:
        /* First kid may be null (for default case in switch). */
        pn1 = pn->pn_left;
        pn2 = pn->pn_right;
        if (pn1 && !js_FoldConstants(cx, pn1, tc))
            return JS_FALSE;
        if (!js_FoldConstants(cx, pn2, tc))
            return JS_FALSE;
        break;

      case PN_UNARY:
        /* Our kid may be null (e.g. return; vs. return e;). */
        pn1 = pn->pn_kid;
        if (pn1 && !js_FoldConstants(cx, pn1, tc))
            return JS_FALSE;
        break;

      case PN_NAME:
        /*
         * Skip pn1 down along a chain of dotted member expressions to avoid
         * excessive recursion.  Our only goal here is to fold constants (if
         * any) in the primary expression operand to the left of the first
         * dot in the chain.
         */
        pn1 = pn->pn_expr;
        while (pn1 && pn1->pn_arity == PN_NAME)
            pn1 = pn1->pn_expr;
        if (pn1 && !js_FoldConstants(cx, pn1, tc))
            return JS_FALSE;
        break;

      case PN_NULLARY:
        break;
    }

    switch (pn->pn_type) {
      case TOK_IF:
        if (ContainsVarStmt(pn2) || ContainsVarStmt(pn3))
            break;
        /* FALL THROUGH */

      case TOK_HOOK:
        /* Reduce 'if (C) T; else E' into T for true C, E for false. */
        switch (pn1->pn_type) {
          case TOK_NUMBER:
            if (pn1->pn_dval == 0)
                pn2 = pn3;
            break;
          case TOK_STRING:
            if (JSSTRING_LENGTH(ATOM_TO_STRING(pn1->pn_atom)) == 0)
                pn2 = pn3;
            break;
          case TOK_PRIMARY:
            if (pn1->pn_op == JSOP_TRUE)
                break;
            if (pn1->pn_op == JSOP_FALSE || pn1->pn_op == JSOP_NULL) {
                pn2 = pn3;
                break;
            }
            /* FALL THROUGH */
          default:
            /* Early return to dodge common code that copies pn2 to pn. */
            return JS_TRUE;
        }

        if (pn2) {
            /* pn2 is the then- or else-statement subtree to compile. */
            PN_MOVE_NODE(pn, pn2);
        } else {
            /* False condition and no else: make pn an empty statement. */
            pn->pn_type = TOK_SEMI;
            pn->pn_arity = PN_UNARY;
            pn->pn_kid = NULL;
        }
        RecycleTree(pn2, tc);
        if (pn3 && pn3 != pn2)
            RecycleTree(pn3, tc);
        break;

      case TOK_PLUS:
        if (pn->pn_arity == PN_LIST) {
            size_t length, length2;
            jschar *chars;
            JSString *str, *str2;

            /*
             * Any string literal term with all others number or string means
             * this is a concatenation.  If any term is not a string or number
             * literal, we can't fold.
             */
            JS_ASSERT(pn->pn_count > 2);
            if (pn->pn_extra & PNX_CANTFOLD)
                return JS_TRUE;
            if (pn->pn_extra != PNX_STRCAT)
                goto do_binary_op;

            /* Ok, we're concatenating: convert non-string constant operands. */
            length = 0;
            for (pn2 = pn1; pn2; pn2 = pn2->pn_next) {
                if (!FoldType(cx, pn2, TOK_STRING))
                    return JS_FALSE;
                /* XXX fold only if all operands convert to string */
                if (pn2->pn_type != TOK_STRING)
                    return JS_TRUE;
                length += ATOM_TO_STRING(pn2->pn_atom)->length;
            }

            /* Allocate a new buffer and string descriptor for the result. */
            chars = (jschar *) JS_malloc(cx, (length + 1) * sizeof(jschar));
            if (!chars)
                return JS_FALSE;
            str = js_NewString(cx, chars, length, 0);
            if (!str) {
                JS_free(cx, chars);
                return JS_FALSE;
            }

            /* Fill the buffer, advancing chars and recycling kids as we go. */
            for (pn2 = pn1; pn2; pn2 = RecycleTree(pn2, tc)) {
                str2 = ATOM_TO_STRING(pn2->pn_atom);
                length2 = str2->length;
                js_strncpy(chars, str2->chars, length2);
                chars += length2;
            }
            *chars = 0;

            /* Atomize the result string and mutate pn to refer to it. */
            pn->pn_atom = js_AtomizeString(cx, str, 0);
            if (!pn->pn_atom)
                return JS_FALSE;
            pn->pn_type = TOK_STRING;
            pn->pn_op = JSOP_STRING;
            pn->pn_arity = PN_NULLARY;
            break;
        }

        /* Handle a binary string concatenation. */
        JS_ASSERT(pn->pn_arity == PN_BINARY);
        if (pn1->pn_type == TOK_STRING || pn2->pn_type == TOK_STRING) {
            JSString *left, *right, *str;

            if (!FoldType(cx, (pn1->pn_type != TOK_STRING) ? pn1 : pn2,
                          TOK_STRING)) {
                return JS_FALSE;
            }
            if (pn1->pn_type != TOK_STRING || pn2->pn_type != TOK_STRING)
                return JS_TRUE;
            left = ATOM_TO_STRING(pn1->pn_atom);
            right = ATOM_TO_STRING(pn2->pn_atom);
            str = js_ConcatStrings(cx, left, right);
            if (!str)
                return JS_FALSE;
            pn->pn_atom = js_AtomizeString(cx, str, 0);
            if (!pn->pn_atom)
                return JS_FALSE;
            pn->pn_type = TOK_STRING;
            pn->pn_op = JSOP_STRING;
            pn->pn_arity = PN_NULLARY;
            RecycleTree(pn1, tc);
            RecycleTree(pn2, tc);
            break;
        }

        /* Can't concatenate string literals, let's try numbers. */
        goto do_binary_op;

      case TOK_STAR:
        /* The * in 'import *;' parses as a nullary star node. */
        if (pn->pn_arity == PN_NULLARY)
            break;
        /* FALL THROUGH */

      case TOK_SHOP:
      case TOK_MINUS:
      case TOK_DIVOP:
      do_binary_op:
        if (pn->pn_arity == PN_LIST) {
            JS_ASSERT(pn->pn_count > 2);
            for (pn2 = pn1; pn2; pn2 = pn2->pn_next) {
                if (!FoldType(cx, pn2, TOK_NUMBER))
                    return JS_FALSE;
                /* XXX fold only if all operands convert to number */
                if (pn2->pn_type != TOK_NUMBER)
                    break;
            }
            if (!pn2) {
                JSOp op = pn->pn_op;

                pn2 = pn1->pn_next;
                pn3 = pn2->pn_next;
                if (!FoldBinaryNumeric(cx, op, pn1, pn2, pn, tc))
                    return JS_FALSE;
                while ((pn2 = pn3) != NULL) {
                    pn3 = pn2->pn_next;
                    if (!FoldBinaryNumeric(cx, op, pn, pn2, pn, tc))
                        return JS_FALSE;
                }
            }
        } else {
            JS_ASSERT(pn->pn_arity == PN_BINARY);
            if (!FoldType(cx, pn1, TOK_NUMBER) ||
                !FoldType(cx, pn2, TOK_NUMBER)) {
                return JS_FALSE;
            }
            if (pn1->pn_type == TOK_NUMBER && pn2->pn_type == TOK_NUMBER) {
                if (!FoldBinaryNumeric(cx, pn->pn_op, pn1, pn2, pn, tc))
                    return JS_FALSE;
            }
        }
        break;

      case TOK_UNARYOP:
        if (pn1->pn_type == TOK_NUMBER) {
            jsdouble d;
            int32 i;

            /* Operate on one numeric constant. */
            d = pn1->pn_dval;
            switch (pn->pn_op) {
              case JSOP_BITNOT:
                if (!js_DoubleToECMAInt32(cx, d, &i))
                    return JS_FALSE;
                d = ~i;
                break;

              case JSOP_NEG:
#ifdef HPUX
                /*
                 * Negation of a zero doesn't produce a negative
                 * zero on HPUX. Perform the operation by bit
                 * twiddling.
                 */
                JSDOUBLE_HI32(d) ^= JSDOUBLE_HI32_SIGNBIT;
#else
                d = -d;
#endif
                break;

              case JSOP_POS:
                break;

              case JSOP_NOT:
                pn->pn_type = TOK_PRIMARY;
                pn->pn_op = (d == 0) ? JSOP_TRUE : JSOP_FALSE;
                pn->pn_arity = PN_NULLARY;
                /* FALL THROUGH */

              default:
                /* Return early to dodge the common TOK_NUMBER code. */
                return JS_TRUE;
            }
            pn->pn_type = TOK_NUMBER;
            pn->pn_op = JSOP_NUMBER;
            pn->pn_arity = PN_NULLARY;
            pn->pn_dval = d;
            RecycleTree(pn1, tc);
        }
        break;

#if JS_HAS_XML_SUPPORT
      case TOK_XMLELEM:
      case TOK_XMLLIST:
      case TOK_XMLPTAGC:
      case TOK_XMLSTAGO:
      case TOK_XMLETAGO:
      case TOK_XMLNAME:
        if (pn->pn_arity == PN_LIST) {
            JS_ASSERT(pn->pn_type == TOK_XMLLIST || pn->pn_count != 0);
            if (!FoldXMLConstants(cx, pn, tc))
                return JS_FALSE;
        }
        break;

      case TOK_AT:
        if (pn1->pn_type == TOK_XMLNAME) {
            jsval v;
            JSAtom *atom;

            v = ATOM_KEY(pn1->pn_atom);
            if (!js_ToAttributeName(cx, &v))
                return JS_FALSE;
            JS_ASSERT(!JSVAL_IS_PRIMITIVE(v));
            atom = js_AtomizeObject(cx, JSVAL_TO_OBJECT(v), 0);
            if (!atom)
                return JS_FALSE;

            pn->pn_type = TOK_XMLNAME;
            pn->pn_op = JSOP_OBJECT;
            pn->pn_arity = PN_NULLARY;
            pn->pn_atom = atom;
            RecycleTree(pn1, tc);
        }
        break;
#endif /* JS_HAS_XML_SUPPORT */

      default:;
    }

    return JS_TRUE;
}
