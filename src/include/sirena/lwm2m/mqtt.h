#ifndef SIRENA_LWM2M_MQTT_H
#define SIRENA_LWM2M_MQTT_H

#include <stddef.h>

int sirena_lwm2m_mqtt_set_client_id(const char *client_id);
int sirena_lwm2m_mqtt_set_topic_prefix(const char *prefix);
int sirena_lwm2m_mqtt_start(void);
int sirena_lwm2m_mqtt_stop(void);

#endif
