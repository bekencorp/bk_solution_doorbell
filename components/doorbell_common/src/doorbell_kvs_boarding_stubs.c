#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include "doorbell_boarding.h"

#if CONFIG_INTEGRATION_DOORBELL_KVS

int doorbell_boarding_init(void)
{
	return 0;
}

void doorbell_boarding_event_notify(uint16_t opcode, int status)
{
	(void)opcode;
	(void)status;
}

void doorbell_boarding_ble_disable(void)
{
}

int doorbell_boarding_notify(uint8_t *data, uint16_t length)
{
	(void)data;
	(void)length;
	return 0;
}

void doorbell_boarding_event_notify_with_data(uint16_t opcode, int status, char *payload, uint16_t length)
{
	(void)opcode;
	(void)status;
	(void)payload;
	(void)length;
}

void doorbell_boarding_operation_handle(uint16_t opcode, uint16_t length, uint8_t *data)
{
	(void)opcode;
	(void)length;
	(void)data;
}

#endif
