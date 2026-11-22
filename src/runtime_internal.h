/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Copyright (C) 2026 Tom Needham */
#ifndef LIBHDDVD_RUNTIME_INTERNAL_H
#define LIBHDDVD_RUNTIME_INTERNAL_H
#include "internal.h"
#include "script_engine.h"

/* Shared by runtime.c (engine, sources, diagnostics) and runtime_dom.c (the
   host objects' document state and natives). */
#define HDDVD_RUNTIME_STATE_COUNT 5
#define HDDVD_RUNTIME_MAX_ISSUES 256

struct hddvd_runtime_style
{
    char *name;
    char *value;
};

/* Script and markup timing values for one markup node; NULL state slots
   hold no value. A script state value overrides markup timing while held,
   from setProperty until unsetProperty; after that it is the base value
   timing overrides. Script style values always override timing ones. */
struct hddvd_runtime_node
{
    char *state[HDDVD_RUNTIME_STATE_COUNT];
    unsigned char state_held[HDDVD_RUNTIME_STATE_COUNT];
    char *timing_state[HDDVD_RUNTIME_STATE_COUNT];
    struct hddvd_runtime_style *styles;
    size_t style_count;
    struct hddvd_runtime_style *timing_styles;
    size_t timing_style_count;
};

#define HDDVD_RUNTIME_MAX_COMMANDS 64
#define HDDVD_RUNTIME_MAX_APPLICATIONS 64
#define HDDVD_RUNTIME_MAX_FIELDS 16

/* A command with its own copy of the text. */
struct hddvd_runtime_pending
{
    hddvd_runtime_command command;
    char *text;
};

struct hddvd_runtime
{
    hddvd_engine *engine;
    hddvd_runtime_limits limits;
    hddvd_engine_report report;
    /* The document: borrowed markup, styles with script values applied, and
       an untouched copy to restore from. */
    const hddvd_xmu *xmu;
    hddvd_xmu_styles *styles;
    hddvd_xmu_styles *pristine;
    struct hddvd_runtime_node *nodes;
    size_t node_count;
    size_t focus;
    /* Counts documents loaded, so element objects can tell they are stale. */
    uint32_t documents;
    uint64_t generation;
    uint64_t now;
    hddvd_runtime_issue issues[HDDVD_RUNTIME_MAX_ISSUES];
    size_t issue_count;
    size_t dropped;
    /* Exceptions recorded by the current host call, and the first one's
       message (owned). */
    size_t call_exceptions;
    char *call_exception;
    /* Player objects (runtime_player.c): the borrowed playlist and a count
       of playlists loaded, so title objects can tell they are stale. */
    const hddvd_xpl *xpl;
    uint32_t playlists;
    hddvd_runtime_player player;
    char *applications[HDDVD_RUNTIME_MAX_APPLICATIONS];
    size_t application_count;
    size_t application_self;
    /* Commands not yet taken (oldest first) and taken but not acknowledged. */
    struct hddvd_runtime_pending queued[HDDVD_RUNTIME_MAX_COMMANDS];
    size_t queued_count;
    struct hddvd_runtime_pending taken[HDDVD_RUNTIME_MAX_COMMANDS];
    size_t taken_count;
    uint64_t next_command;
    /* The fields of the event being dispatched (borrowed). */
    const char *const *field_names;
    const char *const *field_values;
    size_t field_count;
};

/* runtime.c: runs a prelude entry with budgets, as one host call. */
int hddvd_runtime_run_entry(hddvd_runtime *, const char *entry, const hddvd_engine_value *args,
                            size_t count, double *result);
/* runtime_dom.c */
extern const char *const hddvd_runtime_native_names[];
extern const size_t hddvd_runtime_native_count;
int hddvd_runtime_native(void *opaque, unsigned index, const hddvd_engine_value *args, size_t count,
                         hddvd_engine_value *result, const char **message);
void hddvd_runtime_document_free(hddvd_runtime *);
void hddvd_runtime_issues_free(hddvd_runtime *);
void hddvd_runtime_add_issue(hddvd_runtime *, hddvd_runtime_issue_kind, size_t, const char *,
                             size_t);
/* runtime_player.c: natives from index HDDVD_RUNTIME_PLAYER_NATIVES on. */
#define HDDVD_RUNTIME_PLAYER_NATIVES 20u
int hddvd_runtime_player_native(hddvd_runtime *, unsigned index, const hddvd_engine_value *args,
                                size_t count, hddvd_engine_value *result, const char **message);
void hddvd_runtime_player_free(hddvd_runtime *);
#endif
