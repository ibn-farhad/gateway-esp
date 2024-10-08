/*
 * relay.c
 *
 *  Created on: Aug 20, 2023
 *      Author: markscheider
 */
#include "relay.h"

#ifdef CONFIG_RELAY

callb relay_cb;

enum Commands {
	GET_STATUS,
	SET_STATE,

	STATUS = 100,

};

enum RelayStatus{
	ONLINE,
	DEGRADED,
	OFFLINE
};

int relayStatus = OFFLINE;
bool relayState = false;

void relayHandleCommand(char *str, httpd_handle_t handle, int fd){
	/*
	 * getting barcode scanner status
	 */
	const cJSON *command = NULL;
	cJSON *data_json = cJSON_Parse(str);
	if (data_json == NULL)
	{
		const char *error_ptr = cJSON_GetErrorPtr();
		if (error_ptr != NULL)
		{
			printf("Error before: \n");
		}
	}
	command = cJSON_GetObjectItemCaseSensitive(data_json, "command");
	if ( cJSON_IsNumber(command))
	{
		printf("Checking relay json \"%d\"\n", command->valueint);

		switch (command->valueint)
		{
			case GET_STATUS:
			{
				cJSON *command1 = NULL;
				cJSON *status = NULL;
				cJSON *state = NULL;
				cJSON *response = cJSON_CreateObject();
				if (response == NULL)
				{
					return;
				}

				command1 = cJSON_CreateNumber(STATUS);
				if (command1 == NULL)
				{
					return;
				}
				/* after creation was successful, immediately add it to the monitor,
				 * thereby transferring ownership of the pointer to it */
				cJSON_AddItemToObject(response, "command", command1);

				status = cJSON_CreateNumber(relayStatus);
				if (status == NULL)
				{
					return;
				}
				/* after creation was successful, immediately add it to the monitor,
				 * thereby transferring ownership of the pointer to it */
				cJSON_AddItemToObject(response, "status", status);

				state = cJSON_CreateBool(relayState);
				if (state == NULL)
				{
					return;
				}
				cJSON_AddItemToObject(response, "state", state);


				relay_cb(cJSON_Print(response), handle, fd);
				cJSON_Delete(response);
				break;
			}

			case SET_STATE:
			{
				cJSON *set_state = cJSON_GetObjectItemCaseSensitive(data_json, "state");
				if(!cJSON_IsBool(set_state)){
//					set_state = (cJSON*)malloc(sizeof(cJSON));
//					set_state->valueint = 0;
					relayState = 0;
				}
				else
					relayState = set_state->valueint;

				if(RELAY_INVERSE_CONTROL)
					gpio_set_level(RELAY_PIN, !relayState);
				else
					gpio_set_level(RELAY_PIN, relayState);
				break;

			}
			default:
				break;
		}
	}
	cJSON_Delete(data_json);
}

void initRelay(callb callback){
	relay_cb = callback;

	gpio_config_t relay_config = {
			.intr_type = GPIO_INTR_DISABLE,
			.mode = GPIO_MODE_OUTPUT,
			.pin_bit_mask = RELAY_PIN_SEL,
			.pull_down_en = GPIO_PULLDOWN_ENABLE,
			.pull_up_en = GPIO_PULLUP_DISABLE
	};
//	gpio_reset_pin(RELAY_PIN);
	gpio_config(&relay_config);

	relayStatus = ONLINE;
	if (RELAY_INVERSE_CONTROL == 1)
		gpio_set_level(RELAY_PIN, 1);
	else
		gpio_set_level(RELAY_PIN, 0);
}

#endif
