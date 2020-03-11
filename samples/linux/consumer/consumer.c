/*
 * Copyright 2010-2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

/**
 * ORIGINAL FILE subscribe_publish_sample.c
 * @file consumer.c
 */


/**
 * 	SUBSCRUBE API CALL
 *	IoT_Error_t aws_iot_mqtt_subscribe(
 *			AWS_IoT_Client *pClient,
 *			const char *pTopicName,
 *			uint16_t topicNameLen,
 *			QoS qos,
 *			pApplicationHandler_t pApplicationHandler,
 *			void *pApplicationHandlerData);
 *
 *  PUBLISH API CALL
 *	IoT_Error_t aws_iot_mqtt_publish(
 *			AWS_IoT_Client *pClient,
 *			const char *pTopicName,
 *			uint16_t topicNameLen,
 *			IoT_Publish_Message_Params *pParams);
 */


#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"
#include "json.h"

#define HOST_ADDRESS_SIZE 255
#define MAX_STRLEN 256
/**
 * @brief Default cert location
 */
char certDirectory[PATH_MAX + 1] = "../../../certs";

/**
 * @brief Default MQTT HOST URL is pulled from the aws_iot_config.h
 */
char HostAddress[HOST_ADDRESS_SIZE] = AWS_IOT_MQTT_HOST;

/**
 * @brief Default MQTT port is pulled from the aws_iot_config.h
 */
uint32_t port = AWS_IOT_MQTT_PORT;

/**
 * @brief This parameter will avoid infinite loop of publish and exit the program after certain number of publishes
 */
//uint32_t publishCount = 0;


void iot_subscribe_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen, IoT_Publish_Message_Params *params, void *pData);
void disconnectCallbackHandler(AWS_IoT_Client *pClient, void *data);

int publish(char *msg, char *tpc, AWS_IoT_Client *client);

void confirmMessage(IoT_Publish_Message_Params *params, AWS_IoT_Client *client);
void storeMessageInfo(json_value* value, int *mid, char **message, char **command);

void disconnectCallbackHandler(AWS_IoT_Client *pClient, void *data) {
	IOT_WARN("MQTT Disconnect");
	IoT_Error_t rc = FAILURE;

	if(NULL == pClient) {
		return;
	}

	IOT_UNUSED(data);

	if(aws_iot_is_autoreconnect_enabled(pClient)) {
		IOT_INFO("Auto Reconnect is enabled, Reconnecting attempt will start now");
	} else {
		IOT_WARN("Auto Reconnect not enabled. Starting manual reconnect...");
		rc = aws_iot_mqtt_attempt_reconnect(pClient);
		if(NETWORK_RECONNECTED == rc) {
			IOT_WARN("Manual Reconnect Successful");
		} else {
			IOT_WARN("Manual Reconnect Failed - %d", rc);
		}
	}
}

/**
 *	Message Handler - handle message after received
 *		Print and confirm message
 */
void iot_subscribe_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen, IoT_Publish_Message_Params *params, void *pData) {
	IOT_UNUSED(pData);
	IOT_INFO("Subscribe callback");
	IOT_INFO("%.*s\t%.*s", topicNameLen, topicName, (int) params->payloadLen, (char *) params->payload);
	confirmMessage(params, pClient);
}

/**
 * 	Parse and Store message info
 */
void storeMessageInfo(json_value* value, int *mid, char **message, char **command){
	if (value == NULL) {
        return;
    }
    int length, x;
    if (value->type == json_object) {
        length = value->u.object.length;
    	for (x = 0; x < length; x++) {
    		if (strcmp(value->u.object.values[x].name, "message") == 0){
    			*message = value->u.object.values[x].value->u.string.ptr;
    		} else if (strcmp(value->u.object.values[x].name, "message_id") == 0){
    			*mid = value->u.object.values[x].value->u.integer;
    		} else if (strcmp(value->u.object.values[x].name, "message_type") == 0){
    			if (strcmp(value->u.object.values[x].value->u.string.ptr, "COMMAND") == 0) {
    				*command = "true";
    			}
    		}
        }
    }
}

/**
 *  Confirm message if of type COMMAND
 *		Parse 
 */
void confirmMessage(IoT_Publish_Message_Params *params, AWS_IoT_Client *client){
	json_char* json = (json_char*)params->payload;
    json_value* value;
    char *message = NULL;
    int message_id = -1;
    char midstr[16];
    char *confirm_topic = "command/confirm";
    char *isCommand = "false";
    char conf_message[MAX_STRLEN];

    value = json_parse(json,params->payloadLen);
    if (value == NULL) {
            fprintf(stderr, "Unable to parse data\n");
    }
    storeMessageInfo(value, &message_id, &message, &isCommand);
    json_value_free(value);

    if (strcmp(isCommand,"true") == 0 && message_id >= 0){
    	sprintf(conf_message, "{\"message_type\":\"CONFIRMATION\", \"message_id\":%d}", message_id);
    	publish(conf_message, confirm_topic, client);
    }
}

int publish(char *msg, char *tpc, AWS_IoT_Client *client){
	printf("publishin'\n");
	char cPayload[MAX_STRLEN];
	sprintf(cPayload, "%s", msg);
	
	IoT_Publish_Message_Params paramsQOS1;
	paramsQOS1.qos = QOS1;
	paramsQOS1.payload = (void *) cPayload;
	paramsQOS1.payloadLen = strlen(cPayload);
	paramsQOS1.isRetained = 0;
	printf("Publishing message to topic[%s]: %s\n", tpc, msg);
	IoT_Error_t rc = aws_iot_mqtt_publish(client, tpc, strlen(tpc), &paramsQOS1);
	
	if (rc == MQTT_REQUEST_TIMEOUT_ERROR) {
		IOT_WARN("QOS1 publish ack not received.\n");
	} else {
		IOT_INFO("Publish ack received\n");
	}
	return rc;
}

int main(int argc, char **argv) {

	// emtpy strings of max length PATH_MAX to store certs and keys
	char rootCA[PATH_MAX + 1];
	char clientCRT[PATH_MAX + 1];
	char clientKey[PATH_MAX + 1];
	char CurrentWD[PATH_MAX + 1];
	char cPayload[100]; //QUESTION: Why is this of len 100?

	// Client init/connect variables
	//  Set to default here, re-defined below
	IoT_Error_t rc = FAILURE;
	AWS_IoT_Client client;
	IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;
	IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;

	// Print AWS SDK info 
	IOT_INFO("\nAWS IoT SDK Version %d.%d.%d-%s\n", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

	// Get current working directory
	// store path to rootCA, clientCRT, and clientKey in those string buffers
	getcwd(CurrentWD, sizeof(CurrentWD));
	snprintf(rootCA, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_ROOT_CA_FILENAME);
	snprintf(clientCRT, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_CERTIFICATE_FILENAME);
	snprintf(clientKey, PATH_MAX + 1, "%s/%s/%s", CurrentWD, certDirectory, AWS_IOT_PRIVATE_KEY_FILENAME);

	IOT_DEBUG("rootCA %s", rootCA);
	IOT_DEBUG("clientCRT %s", clientCRT);
	IOT_DEBUG("clientKey %s", clientKey);
	
	// init variables re-defined here
	mqttInitParams.enableAutoReconnect = false; // QUESTION: We enable this later below - WHY?
	mqttInitParams.pHostURL = HostAddress;
	mqttInitParams.port = port;
	mqttInitParams.pRootCALocation = rootCA;
	mqttInitParams.pDeviceCertLocation = clientCRT;
	mqttInitParams.pDevicePrivateKeyLocation = clientKey;
	mqttInitParams.mqttCommandTimeout_ms = 20000;
	mqttInitParams.tlsHandshakeTimeout_ms = 5000;
	mqttInitParams.isSSLHostnameVerify = true;
	mqttInitParams.disconnectHandler = disconnectCallbackHandler;
	mqttInitParams.disconnectHandlerData = NULL;

	// API Call to initialize client
	rc = aws_iot_mqtt_init(&client, &mqttInitParams);
	if(SUCCESS != rc) {
		IOT_ERROR("aws_iot_mqtt_init returned error : %d ", rc);
		return rc;
	}

	// connect variables re-defined here
	connectParams.keepAliveIntervalInSec = 600;
	connectParams.isCleanSession = true;
	connectParams.MQTTVersion = MQTT_3_1_1;
	connectParams.pClientID = AWS_IOT_MQTT_CLIENT_ID;
	connectParams.clientIDLen = (uint16_t) strlen(AWS_IOT_MQTT_CLIENT_ID);
	connectParams.isWillMsgPresent = false;

	// API Call to connect client
	IOT_INFO("Connecting...");
	rc = aws_iot_mqtt_connect(&client, &connectParams);
	if(SUCCESS != rc) {
		IOT_ERROR("Error(%d) connecting to %s:%d", rc, mqttInitParams.pHostURL, mqttInitParams.port);
		return rc;
	}
	/*
	 * Enable Auto Reconnect functionality. Minimum and Maximum time of Exponential backoff are set in aws_iot_config.h
	 *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
	 *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
	 */
	rc = aws_iot_mqtt_autoreconnect_set_status(&client, true);
	if(SUCCESS != rc) {
		IOT_ERROR("Unable to set Auto Reconnect to true - %d", rc);
		return rc;
	}


	char line[MAX_STRLEN];
	char input_str[MAX_STRLEN];
	char *tpc;
	int topic_count = 0;
	int topic_len = 0;

	publish("{\"hello\":\"world\"}", "command/confirm", &client);

	// Get topics names from stdin to subscribe to
	printf("Enter topic names to subscribe to seperated by a space. Topic name format is topic/name\n");
	while(true){
		if (fgets(line, sizeof(line), stdin)) {
			int num_args = sscanf(line, "%[^\n]", input_str);
		    if (num_args >= 1) {
		    	// successfully/safely got some imput

		    	tpc = strtok(input_str, " ");
		        while(tpc != NULL){
		    		if (strchr(tpc, '/') == NULL){
		    			IOT_WARN("%s is not a valid topic name. Correct format is [topic]/[name]", tpc);
		    		} else {
		    			IOT_INFO("Subscribing to topic %s\n", tpc);
		    			topic_len = strlen(tpc);
		    			rc = aws_iot_mqtt_subscribe(&client, tpc, topic_len, QOS1, iot_subscribe_callback_handler, NULL);
						if(SUCCESS != rc) {
							IOT_ERROR("Error subscribing : %d ", rc);
						}
		    			topic_count++;
		    		}
		    		tpc = strtok(NULL, " ");
		    	}
		    	if (topic_count > 0) { break; }
		    } else {
		    	// couldn't successfully read input
		    	IOT_WARN("Error reading input. Try again.");
		    }
		} else {
			// fgets failed
			IOT_ERROR("Error in command line argument parsing");
		}
	}

	
	while(true) {
		// Indefinitely listen for messages
		rc = aws_iot_mqtt_yield(&client, 100);
		if(NETWORK_ATTEMPTING_RECONNECT == rc) {
			// ERROR HANDLE HERE
			continue;
		}
		sleep(1);
	}

	return rc;
}
