#include "sample_flow.h"
#include <zephyr/sys/printk.h>

#if defined(CONFIG_BOARD_NATIVE_SIM)
#include <nsi_main.h>
#endif

int main(void)
{
	struct sirena_lwm2m_sample_config config;
	const struct sirena_lwm2m_sample_state *state;
	int rc;

	sirena_lwm2m_sample_config_from_kconfig(&config);

	rc = sirena_lwm2m_sample_run(&config);

	state = sirena_lwm2m_sample_state();
	printk("sirena lwm2m sample: finished registered=%d reports=%u notifications=%u "
	       "command=%d\n",
	       state->registered ? 1 : 0, state->telemetry_reports, state->notifications_sent,
	       state->command_handled ? 1 : 0);

#if defined(CONFIG_BOARD_NATIVE_SIM)
	nsi_exit(rc == 0 ? 0 : 1);
#endif

	return rc;
}
