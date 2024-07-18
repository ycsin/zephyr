/*
 * Copyright (c) 2023 KNS Group LLC (YADRO)
 * Copyright (c) 2020 Yonatan Goldschmidt <yon.goldschmidt@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/shell/shell.h>

struct perf_data {
	struct k_timer timer;

	const struct shell *sh;

	struct k_work_delayable dwork;

	uintptr_t buf[CONFIG_PROFILING_PERF_BUFFER_SIZE];
	size_t trace_length;
	size_t buf_idx;
	bool buf_full;
};

static struct perf_data perf;

static bool save_ra(void *arg, unsigned long ra)
{
	struct perf_data *ctx = arg;
	size_t size = CONFIG_PROFILING_PERF_BUFFER_SIZE - ctx->buf_idx;
	size_t buf_idx = ctx->buf_idx;

	if (size < 2U) {
		ctx->buf_full = true;
		return false;
	}

	ctx->buf[buf_idx + ctx->trace_length++] = ra;

	if (ctx->trace_length >= size) {
		ctx->buf_full = true;
		return false;
	}

	return true;
}

static void perf_tracer(struct k_timer *timer)
{
	struct perf_data *ctx = k_timer_user_data_get(timer);
	struct arch_esf *esf = (struct arch_esf *)stack_pointer_before_interrupt;
	const size_t trace_hdr_idx = ctx->buf_idx;

	ctx->trace_length = 0;
	if (++ctx->buf_idx < CONFIG_PROFILING_PERF_BUFFER_SIZE) {
		arch_stack_walk(save_ra, ctx, _current, esf);
	}

	if (ctx->trace_length > 2) {
		ctx->buf[trace_hdr_idx] = ctx->trace_length;
		ctx->buf_idx += ctx->trace_length;
	} else {
		// ctx->buf_idx--;
		ctx->buf_idx -= ctx->trace_length;
		if (ctx->buf_full) {
			k_timer_stop(timer);
			k_work_reschedule(&ctx->dwork, K_NO_WAIT);
		}
	}
}

static void perf_dwork_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct perf_data *ctx = CONTAINER_OF(dwork, struct perf_data, dwork);

	k_timer_stop(&ctx->timer);
	if ((ctx->buf_idx + 2) >= CONFIG_PROFILING_PERF_BUFFER_SIZE) {
		shell_error(ctx->sh, "Perf buf full");
	} else {
		shell_print(ctx->sh, "Perf done");
	}
}

static int perf_init(void)
{
	k_timer_init(&perf.timer, perf_tracer, NULL);
	k_work_init_delayable(&perf.dwork, perf_dwork_handler);

	return 0;
}

static int cmd_perf_record(const struct shell *sh, size_t argc, char **argv)
{
	if (k_work_delayable_is_pending(&perf.dwork)) {
		shell_warn(sh, "Perf is running");
		return -EINPROGRESS;
	}

	if ((perf.buf_idx + 2) >= CONFIG_PROFILING_PERF_BUFFER_SIZE) {
		shell_warn(sh, "Perf buf full");
		return -ENOBUFS;
	}

	k_timeout_t duration = K_MSEC(strtoll(argv[1], NULL, 10));
	k_timeout_t period = K_NSEC(1000000000LL / strtoll(argv[2], NULL, 10));

	perf.sh = sh;

	k_timer_user_data_set(&perf.timer, &perf);
	k_timer_start(&perf.timer, K_NO_WAIT, period);

	k_work_schedule(&perf.dwork, duration);

	shell_print(sh, "Perf started");

	return 0;
}

static int cmd_perf_reset(const struct shell *sh, size_t argc, char **argv)
{
	if (k_work_delayable_is_pending(&perf.dwork)) {
		shell_warn(sh, "Perf is running");
		return -EINPROGRESS;
	}

	perf.buf_idx = 0;

	return 0;
}

static int cmd_perf_print(const struct shell *sh, size_t argc, char **argv)
{
	if (k_work_delayable_is_pending(&perf.dwork)) {
		shell_warn(sh, "Perf is running");
		return -EINPROGRESS;
	}

	shell_print(sh, "Perf buf length %zu", perf.buf_idx);
	for (size_t i = 0; i < perf.buf_idx; i++) {
		shell_print(sh, "%016lx", perf.buf[i]);
	}

	perf.buf_idx = 0;

	return 0;
}

#define CMD_RECORD_HELP                                                                            \
	"Start recording for <duration> ms at <frequency> Hz\n"                                    \
	"Usage: record <duration> <frequency>"

SHELL_STATIC_SUBCMD_SET_CREATE(m_sub_perf,
	SHELL_CMD_ARG(record, NULL, CMD_RECORD_HELP, cmd_perf_record, 3, 0),
	SHELL_CMD_ARG(printbuf, NULL, "Print and reset the buffer", cmd_perf_print, 0, 0),
	SHELL_CMD_ARG(reset, NULL, "Reset the buffer", cmd_perf_reset, 0, 0),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_ARG_REGISTER(perf, &m_sub_perf, "Performance analyzing tool", NULL, 0, 0);

SYS_INIT(perf_init, APPLICATION, 0);
