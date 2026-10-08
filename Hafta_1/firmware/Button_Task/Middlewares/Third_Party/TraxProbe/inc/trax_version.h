/*
 * SPDX-License-Identifier: LicenseRef-TraxProbe-Commercial
 * Copyright (c) 2026 Embedya. All rights reserved.
 *
 * TraxProbe core engine. CONFIDENTIAL and proprietary to Embedya.
 * Licensed under the TraxProbe Commercial License (see LICENSE-COMMERCIAL.txt).
 * No use, copying, modification, redistribution, decompilation, or reverse
 * engineering is permitted except as expressly authorized by that agreement.
 */

/**
 ******************************************************************************
 * @file           : trax_version.h
 * @brief          : TraxProbe protocol + library versions
 ******************************************************************************
 * @attention
 *
 * Single source of truth for both TraxProbe versions:
 *
 *   TRAX_VERSION        — PROTOCOL (wire-format) version. Sent in the session
 *                         header; Traxcope uses it to decode the stream.
 *   TRAX_PROBE_VERSION  — LIBRARY (release) version of this vendored copy.
 *                         Identifies which TraxProbe source drop a firmware
 *                         or workspace is carrying.
 *
 * Included by trax_meta_tx.h — no need to include this file directly.
 *
 * Protocol bump rules:
 *   MAJOR — wire-format breaking change (Traxcope must be updated)
 *   MINOR — backward-compatible addition (new frame type, new metadata field)
 *   PATCH — bug fix with no protocol change
 *
 * Library bump rules (plain semver, independent of the protocol version):
 *   MAJOR — breaking API/config change (user code or trax_config.h must adapt)
 *   MINOR — backward-compatible feature
 *   PATCH — bug fix
 *
 * When bumping the library version, also update the VERSION file at the
 * library root — it must always match TRAX_PROBE_VERSION_STRING.
 *
 ******************************************************************************
 */

#ifndef TRAX_VERSION_H_
#define TRAX_VERSION_H_

/* v2.0.0 — appended build_version[16] + build_id[16] to trax_session_header_t
 *          (160 -> 192 bytes).  Wire-format breaking; old hosts parse every
 *          subsequent metadata section at the wrong offset.  Bumped MAJOR
 *          rather than MINOR to make the incompatibility loud. */
/* v3.0.0 — TID range remap: LOG grew by 4096 slots (0x0100-0x1FFF) and every
 *          range above shifted up by 0x1000 (VAR 0x2000, STREAM 0x3000,
 *          ISR 0x4000, MARKER 0x5000, KERNEL 0x6000, SM 0x7000-0x70FF).
 *          Wire-format breaking: hosts and firmware must be upgraded in
 *          lockstep, and recordings/ELFs from older builds decode wrong. */
#define TRAX_VERSION_MAJOR      3
#define TRAX_VERSION_MINOR      0
#define TRAX_VERSION_PATCH      0

/** @brief Packed 32-bit version: 0x00MMNNPP (Major, Minor, Patch) */
#define TRAX_VERSION            ((TRAX_VERSION_MAJOR << 16) | \
                                 (TRAX_VERSION_MINOR <<  8) | \
                                  TRAX_VERSION_PATCH)

/* ------------------------------------------------------------------------ */
/* Library (release) version — tracks the TraxProbe source drop itself.     */
/* Keep in sync with the VERSION file at the library root.                  */
/* ------------------------------------------------------------------------ */
/* v2.0.0 — session-level API renamed so it can no longer be confused with
 *          the per-stream TRAX_STREAM_* API: trax_start_stream() and friends
 *          became trax_session_start/stop/pause/resume(),
 *          trax_wait_stream_started() became trax_wait_session_started(),
 *          TRAX_IS_STREAMING() became TRAX_IS_SESSION_ACTIVE(), and
 *          TRAX_TID_STREAM_START/STOP/GAP became TRAX_TID_SESSION_*.
 *          Source-breaking for user code; wire values are unchanged. */
#define TRAX_PROBE_VERSION_MAJOR    2
#define TRAX_PROBE_VERSION_MINOR    0
#define TRAX_PROBE_VERSION_PATCH    0

/** @brief Packed 32-bit library version: 0x00MMNNPP (Major, Minor, Patch) */
#define TRAX_PROBE_VERSION          ((TRAX_PROBE_VERSION_MAJOR << 16) | \
                                     (TRAX_PROBE_VERSION_MINOR <<  8) | \
                                      TRAX_PROBE_VERSION_PATCH)

#define TRAX_STR_HELPER(x)          #x
#define TRAX_STR(x)                 TRAX_STR_HELPER(x)

/** @brief Library version as a string literal, e.g. "1.0.0" */
#define TRAX_PROBE_VERSION_STRING   TRAX_STR(TRAX_PROBE_VERSION_MAJOR) "." \
                                    TRAX_STR(TRAX_PROBE_VERSION_MINOR) "." \
                                    TRAX_STR(TRAX_PROBE_VERSION_PATCH)

#endif /* TRAX_VERSION_H_ */
