#include "sample_flow.h"
#include <zephyr/sys/printk.h>

#if defined(CONFIG_BOARD_NATIVE_SIM)
#include <nsi_main.h>
#endif

int main(void)
{
	struct sirena_lwm2m_sample_config config;
	struct sirena_lwm2m_sample_state state;
	int state_rc;
	int rc;

	sirena_lwm2m_sample_config_from_kconfig(&config);

	rc = sirena_lwm2m_sample_run(&config);

	state_rc = sirena_lwm2m_sample_state_read(&state);
	if (state_rc != 0) {
		printk("sirena lwm2m sample: the state could not be read: %d\n", state_rc);
	} else {
		printk("sirena lwm2m sample: finished registered=%d reports=%u notifications=%u "
		       "command=%d\n",
		       state.registered ? 1 : 0, state.telemetry_reports, state.notifications_sent,
		       state.command_handled ? 1 : 0);
	}

#if defined(CONFIG_BOARD_NATIVE_SIM)
	nsi_exit(rc == 0 && state_rc == 0 ? 0 : 1);
#endif

	return rc == 0 && state_rc == 0 ? 0 : 1;
}
