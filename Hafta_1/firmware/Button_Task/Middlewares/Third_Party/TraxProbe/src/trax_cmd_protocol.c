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
 * @file           : trax_cmd_protocol.c
 * @brief          : TraxProbe Command Protocol Implementation
 * @version        : 1.0.0
 ******************************************************************************
 * @attention
 * 
 * Implements frame parsing and command dispatching for commands received
 * from Traxcope via the transport interface.
 * 
 * Separation of concerns:
 *   - This file: Frame parsing and command dispatching
 *   - trax_cmd_handler.c: Command execution and response generation
 * 
 ******************************************************************************
 */

#include "trax_cmd_protocol.h"
#include "internal/trax_cmd_handler.h"
#include "trax_transport.h"      /* TRAX_TRANSPORT_READ selector */
#include "trax.h"
#include "trax_tid.h"
#include "trax_session.h"
#include "trax_frame.h"
#include "trax_buffer.h"
#include "trax_utility.h"        /* For PP_NARG and TRAX_PUT_ARG_AUTO */
#include "trax_session.h"        /* trax_session, TRAX_IS_SESSION_ACTIVE() */

/* TraxProbe disabled (TRAX_ENABLE==0): whole unit compiles out.
 * trax_wait_session_started() is provided as an inline no-op stub in trax.h. */
#if TRAX_ENABLE

/* Helper macro to send command response frames (always works - no state check) */
#define TRAX_FRAME_ARGS_RESPONSE(tid, ...) \
	{ \
		TRAX_FRAME_ALLOC(tid, PP_NARG(__VA_ARGS__)) \
		if (p_wr != NULL) { \
			TRAX_PUT_ARG_AUTO(p_wr, ##__VA_ARGS__); \
		} \
		TRAX_FRAME_COMMIT() \
	}

/*=============================================================================
 ====================PARSER STATE MACHINE======================================
 ============================================================================*/

/**
 * @brief Down-channel read granularity, bytes.
 *
 * The parser is fed byte-by-byte but the transport is read in chunks: a
 * short read is the drain signal, which is why no separate
 * "bytes available" transport query is needed. Sized to comfortably hold
 * a whole command frame (TRAX_CMD_HEADER_SIZE + payload) in one read.
 */
#define TRAX_CMD_RX_CHUNK_SIZE  (TRAX_CMD_HEADER_SIZE + TRAX_CMD_MAX_PAYLOAD_SIZE)

/**
 * @brief Parser states
 */
enum parser_state_t {
	PARSER_STATE_PREFIX0,   /**< Waiting for first prefix byte (0xAA) */
	PARSER_STATE_PREFIX1,   /**< Waiting for second prefix byte (0x55) */
	PARSER_STATE_LENGTH_LO, /**< Waiting for length low byte */
	PARSER_STATE_LENGTH_HI, /**< Waiting for length high byte */
	PARSER_STATE_PAYLOAD    /**< Receiving payload bytes */
};

/*=============================================================================
 ====================GLOBAL PROTOCOL STATE=====================================
 ============================================================================*/

/* Protocol state uses trax_session flags - see trax_session.h */

/*=============================================================================
 ====================LOCAL VARIABLES===========================================
 ============================================================================*/

/** @brief Current parser state */
static enum parser_state_t s_parser_state = PARSER_STATE_PREFIX0;

/** @brief Current frame being received */
static struct trax_cmd_frame_t s_frame;

/** @brief Payload bytes received so far */
static uint16_t s_payload_index = 0;

/** @brief Expected payload length */
static uint16_t s_payload_length = 0;

/** @brief Debug counters */
static volatile uint32_t s_frames_received = 0;
static volatile uint32_t s_sync_errors = 0;

/*=============================================================================
 ====================LOCAL FUNCTION DECLARATIONS===============================
 ============================================================================*/

static void parser_reset(void);
static bool parser_process_byte(uint8_t byte);
static void dispatch_command(void);

/*=============================================================================
 ====================GLOBAL FUNCTION IMPLEMENTATION============================
 ============================================================================*/

int trax_cmd_protocol_init(void)
{
	/* Reset parser */
	parser_reset();

	/* Reset counters */
	s_frames_received = 0;
	s_sync_errors = 0;

	/* Initialize command handler module */
	trax_cmd_handler_init();

	/* Protocol state uses trax_session flags - initialized by trax_session_init() */

	return 0;
}

int trax_cmd_protocol_process(void)
{
	int commands_processed = 0;
	uint8_t chunk[TRAX_CMD_RX_CHUNK_SIZE];

	/* Drain the down-channel using read() alone. A read that returns less
	 * than the requested size means the transport had nothing more to
	 * give, so it doubles as the "no data left" signal — the transport
	 * contract needs no separate bytes-available query, and a busy pass
	 * costs one transport call per chunk instead of one per byte. */
	for (;;)
	{
		size_t read = TRAX_TRANSPORT_READ(chunk, sizeof(chunk));

		for (size_t i = 0; i < read; i++)
		{
			/* Feed byte to parser */
			if (parser_process_byte(chunk[i])) {

				/* Complete frame received */
				s_frames_received++;

				/* Dispatch command */
				dispatch_command();
				commands_processed++;

				/* Reset for next frame */
				parser_reset();
			}
		}

		if (read < sizeof(chunk)) {
			break;
		}
	}

	return commands_processed;
}

void trax_wait_session_started(void)
{
	while (!TRAX_IS_SESSION_ACTIVE())
	{
		/* Full process pass — not just command parse + pending-start.
		 *
		 * CMD_SESSION_START only LATCHES the start; process_pending_start
		 * (inside trax_process) allocates SESSION_START into the
		 * application ring and flips streaming on. The host's implicit
		 * ACK is those bytes on the wire. On a slow UART, allocating
		 * without trax_send_frames() leaves Traxcope waiting for
		 * SESSION_START until the app happens to call trax_process(),
		 * and a tight producer loop can overflow the ring first.
		 * trax_process() does parse, pending-start, AND drain in one
		 * pass — same reason STOP must drain SESSION_STOP before the
		 * host treats the stop as ACKed. */
		(void)trax_process();
	}
}

/*=============================================================================
 ====================LOCAL FUNCTION IMPLEMENTATION=============================
 ============================================================================*/

/**
 * @brief Reset parser to initial state
 */
static void parser_reset(void)
{
	s_parser_state = PARSER_STATE_PREFIX0;
//	EM_LOG_TRACE("parser state: PARSER_STATE_PREFIX0");
	s_payload_index = 0;
	s_payload_length = 0;
}

/**
 * @brief Process a single byte through the parser state machine
 * 
 * @param byte Input byte
 * @return true if a complete frame has been received
 */
static bool parser_process_byte(uint8_t byte)
{
	switch (s_parser_state)
	{
	case PARSER_STATE_PREFIX0:
		if (byte == TRAX_CMD_PREFIX_BYTE0)
		{
			s_frame.header.prefix[0] = byte;
			s_parser_state = PARSER_STATE_PREFIX1;
		}
		/* else: stay in PREFIX0, keep scanning */
		break;

	case PARSER_STATE_PREFIX1:
		if (byte == TRAX_CMD_PREFIX_BYTE1)
		{
			s_frame.header.prefix[1] = byte;
			s_parser_state = PARSER_STATE_LENGTH_LO;
		}
		else if (byte == TRAX_CMD_PREFIX_BYTE0)
		{
			/* Could be start of new frame, stay ready */
			s_frame.header.prefix[0] = byte;
			/* Stay in PREFIX1 state */
		}
		else
		{
			/* Sync error, reset */
			s_sync_errors++;
			parser_reset();
		}
		break;

	case PARSER_STATE_LENGTH_LO:
		s_payload_length = byte; /* Low byte */
		s_parser_state = PARSER_STATE_LENGTH_HI;
		break;

	case PARSER_STATE_LENGTH_HI:
		s_payload_length |= ((uint16_t)byte << 8); /* High byte */
		s_frame.header.length = s_payload_length;

		/* Validate length */
		if (s_payload_length
			== 0 || s_payload_length > TRAX_CMD_MAX_PAYLOAD_SIZE)
		{
			/* Invalid length, reset */
			s_sync_errors++;
			parser_reset();
		}
		else
		{
			s_payload_index = 0;
			s_parser_state = PARSER_STATE_PAYLOAD;
		}
		break;

	case PARSER_STATE_PAYLOAD:
		s_frame.payload[s_payload_index++] = byte;

		if (s_payload_index >= s_payload_length)
			{
			/* Frame complete */
			return true;
		}
		break;

	default:
		parser_reset();
		break;
	}

	return false;
}

/**
 * @brief Dispatch received command to handler
 */
static void dispatch_command(void)
{
	if (s_payload_length < 1) {
		return; /* No command byte */
	}

	enum trax_cmd_code_t cmd = (enum trax_cmd_code_t)s_frame.payload[0];
	const uint8_t *params =
		(s_payload_length > 1) ? &s_frame.payload[1] : (void*)0;
	uint16_t params_len =
		(s_payload_length > 1) ? (s_payload_length - 1) : 0;

	/* Dispatch to command handler module */
	trax_cmd_dispatch(cmd, params, params_len);
}

/*=============================================================================
 ====================RESPONSE FUNCTIONS=========================================
 ============================================================================*/

/* Metadata summary and item responses are in trax_meta_tx.c */

int32_t trax_cmd_send_ack(uint8_t cmd, uint8_t result)
{
	/* Send ACK frame for SET commands
	 * Format: [cmd(8b) | result(8b) | state(8b) | reserved(8b)] [session_id(32b)]
	 */
	uint32_t word0 = ((uint32_t)cmd) | 
	                 ((uint32_t)result << 8) |
	                 ((uint32_t)trax_session.streaming << 16);
	uint32_t word1 = trax_session.session_id;
	TRAX_FRAME_ARGS_RESPONSE(TRAX_TID_ACK, word0, word1);
	return 0;
}

int32_t trax_cmd_send_nak(uint8_t cmd, uint8_t err_code)
{
	/* Send NAK frame with command and error code */
	TRAX_FRAME_ARGS_RESPONSE(TRAX_TID_NAK, ((uint32_t)cmd << 8) | err_code);
	return 0;
}

#endif /* TRAX_ENABLE */
