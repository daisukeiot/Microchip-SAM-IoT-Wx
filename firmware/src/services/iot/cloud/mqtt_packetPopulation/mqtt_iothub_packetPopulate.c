/*
	\file   mqtt_iothub_packetParameters.c

	\brief  MQTT Packet Parameters source file.

	(c) 2018 Microchip Technology Inc. and its subsidiaries.

	Subject to your compliance with these terms, you may use Microchip software and any
	derivatives exclusively with Microchip products. It is your responsibility to comply with third party
	license terms applicable to your use of third party software (including open source software) that
	may accompany Microchip software.

	THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
	EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY
	IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS
	FOR A PARTICULAR PURPOSE.

	IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
	INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
	WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP
	HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO
	THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL
	CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT
	OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS
	SOFTWARE.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include "mqtt/mqtt_core/mqtt_core.h"
#include "mqtt/mqtt_packetTransfer_interface.h"
#include "mqtt_packetPopulate.h"
#include "mqtt_iothub_packetPopulate.h"
#include "iot_config/IoT_Sensor_Node_config.h"
#include "debug_print.h"
#include "led.h"
#include "lib/basic/atca_basic.h"
#include "azure/iot/az_iot_pnp_client.h"
#include "azure/core/az_span.h"

pf_MQTT_CLIENT pf_mqtt_iothub_client = {
    MQTT_CLIENT_iothub_publish,
    MQTT_CLIENT_iothub_receive,
    MQTT_CLIENT_iothub_connect,
    MQTT_CLIENT_iothub_subscribe,
    MQTT_CLIENT_iothub_connected,
};

extern const az_span device_model_id_span;
extern void          APP_ReceivedFromCloud_methods(uint8_t* topic, uint8_t* payload);
extern void          APP_ReceivedFromCloud_twin(uint8_t* topic, uint8_t* payload);
extern void          APP_ReceivedFromCloud_patch(uint8_t* topic, uint8_t* payload);
static const az_span twin_request_id = AZ_SPAN_LITERAL_FROM_STR("initial_get");

char              mqtt_telemetry_topic_buf[64];
char              mqtt_get_topic_twin_buf[64];
char              username_buf[200];
uint8_t           device_id_buf[100];
az_span           device_id;
az_iot_pnp_client pnp_client;

/** \brief MQTT publish handler call back table.
 *
 * This callback table lists the callback function for to be called on reception
 * of a PUBLISH message for each topic which the application has subscribed to.
 * For each new topic which is subscribed to by the application, there needs to
 * be a corresponding publish handler.
 * E.g.: For a particular topic
 *       mchp/mySubscribedTopic/myDetailedPath
 *       Sample publish handler function  = void handlePublishMessage(uint8_t *topic, uint8_t *payload)
 */
publishReceptionHandler_t imqtt_publishReceiveCallBackTable[MAX_NUM_TOPICS_SUBSCRIBE];

void MQTT_CLIENT_iothub_publish(uint8_t* data, uint16_t len)
{
    az_result result;

    debug_printGood("  HUB: Publishing to '%s'", hub_hostname);

    result = az_iot_pnp_client_telemetry_get_publish_topic(
        &pnp_client,
        AZ_SPAN_EMPTY,
        NULL,
        mqtt_telemetry_topic_buf,
        sizeof(mqtt_telemetry_topic_buf),
        NULL);

    if (az_result_failed(result))
    {
        debug_printError("az_iot_pnp_client_telemetry_get_publish_topic failed");
        return;
    }

    mqttPublishPacket cloudPublishPacket;
    // Fixed header
    cloudPublishPacket.publishHeaderFlags.duplicate = 0;
    cloudPublishPacket.publishHeaderFlags.qos       = 1;
    cloudPublishPacket.publishHeaderFlags.retain    = 0;
    // Variable header
    cloudPublishPacket.topic = (uint8_t*)mqtt_telemetry_topic_buf;

    // Payload
    cloudPublishPacket.payload       = data;
    cloudPublishPacket.payloadLength = len;

    if (MQTT_CreatePublishPacket(&cloudPublishPacket) != true)
    {
        debug_printError("  HUB: MQTT_CLIENT_iothub_publish() failed");
    }
}

void MQTT_CLIENT_iothub_receive(uint8_t* data, uint16_t len)
{
    MQTT_GetReceivedData(data, len);
}

void MQTT_CLIENT_iothub_connect(char* deviceID)
{
    debug_printGood("  HUB: Connecting to '%s'", hub_hostname);
    const az_span iothub_hostname = az_span_create_from_str(hub_hostname);
    const az_span deviceID_parm   = az_span_create_from_str(deviceID);
    az_span       device_id       = AZ_SPAN_FROM_BUFFER(device_id_buf);

    LED_SetGreen(LED_STATE_BLINK_SLOW);

    az_span_copy(device_id, deviceID_parm);
    device_id = az_span_slice(device_id, 0, az_span_size(deviceID_parm));

    az_result result = az_iot_pnp_client_init(&pnp_client, iothub_hostname, device_id, device_model_id_span, NULL);

    if (az_result_failed(result))
    {
        debug_printError("  HUB: az_iot_pnp_client_init failed");
        return;
    }

    size_t username_buf_len;
    result = az_iot_pnp_client_get_user_name(&pnp_client, username_buf, sizeof(username_buf), &username_buf_len);
    if (az_result_failed(result))
    {
        debug_printError("  HUB: az_iot_pnp_client_get_user_name failed");
        return;
    }

    mqttConnectPacket cloudConnectPacket;
    memset(&cloudConnectPacket, 0, sizeof(mqttConnectPacket));
    cloudConnectPacket.connectVariableHeader.connectFlagsByte.All = 0x20;   // AZ_CLIENT_DEFAULT_MQTT_CONNECT_CLEAN_SESSION
    cloudConnectPacket.connectVariableHeader.keepAliveTimer       = AZ_IOT_DEFAULT_MQTT_CONNECT_KEEPALIVE_SECONDS;

    cloudConnectPacket.clientID       = az_span_ptr(device_id);
    cloudConnectPacket.password       = NULL;
    cloudConnectPacket.passwordLength = 0;
    cloudConnectPacket.username       = (uint8_t*)username_buf;
    cloudConnectPacket.usernameLength = (uint16_t)username_buf_len;

    MQTT_CreateConnectPacket(&cloudConnectPacket);
}

bool MQTT_CLIENT_iothub_subscribe()
{
    mqttSubscribePacket cloudSubscribePacket;

    debug_printGood("  HUB: Subscribing to '%s'", hub_hostname);

    // Variable header
    cloudSubscribePacket.packetIdentifierLSB = 1;
    cloudSubscribePacket.packetIdentifierMSB = 0;

    cloudSubscribePacket.subscribePayload[0].topic        = (uint8_t*)AZ_IOT_PNP_CLIENT_COMMANDS_SUBSCRIBE_TOPIC;
    cloudSubscribePacket.subscribePayload[0].topicLength  = sizeof(AZ_IOT_PNP_CLIENT_COMMANDS_SUBSCRIBE_TOPIC) - 1;
    cloudSubscribePacket.subscribePayload[0].requestedQoS = 0;
    cloudSubscribePacket.subscribePayload[1].topic        = (uint8_t*)AZ_IOT_PNP_CLIENT_PROPERTY_PATCH_SUBSCRIBE_TOPIC;
    cloudSubscribePacket.subscribePayload[1].topicLength  = sizeof(AZ_IOT_PNP_CLIENT_PROPERTY_PATCH_SUBSCRIBE_TOPIC) - 1;
    cloudSubscribePacket.subscribePayload[1].requestedQoS = 0;
    cloudSubscribePacket.subscribePayload[2].topic        = (uint8_t*)AZ_IOT_PNP_CLIENT_PROPERTY_RESPONSE_SUBSCRIBE_TOPIC;
    cloudSubscribePacket.subscribePayload[2].topicLength  = sizeof(AZ_IOT_PNP_CLIENT_PROPERTY_RESPONSE_SUBSCRIBE_TOPIC) - 1;
    cloudSubscribePacket.subscribePayload[2].requestedQoS = 0;

    imqtt_publishReceiveCallBackTable[0].topic                         = (uint8_t*)AZ_IOT_PNP_CLIENT_COMMANDS_SUBSCRIBE_TOPIC;
    imqtt_publishReceiveCallBackTable[0].mqttHandlePublishDataCallBack = APP_ReceivedFromCloud_methods;
    imqtt_publishReceiveCallBackTable[1].topic                         = (uint8_t*)AZ_IOT_PNP_CLIENT_PROPERTY_PATCH_SUBSCRIBE_TOPIC;
    imqtt_publishReceiveCallBackTable[1].mqttHandlePublishDataCallBack = APP_ReceivedFromCloud_patch;
    imqtt_publishReceiveCallBackTable[2].topic                         = (uint8_t*)AZ_IOT_PNP_CLIENT_PROPERTY_RESPONSE_SUBSCRIBE_TOPIC;
    imqtt_publishReceiveCallBackTable[2].mqttHandlePublishDataCallBack = APP_ReceivedFromCloud_twin;
    MQTT_SetPublishReceptionHandlerTable(imqtt_publishReceiveCallBackTable);

    bool ret = MQTT_CreateSubscribePacket(&cloudSubscribePacket);
    if (ret == true)
    {
        debug_printInfo("  HUB: SUBSCRIBE packet created");
    }

    return ret;
}

void MQTT_CLIENT_iothub_connected()
{
    // get the current state of the device twin
    debug_printGood("  HUB: MQTT_CLIENT_iothub_connected()");
    az_result result = az_iot_pnp_client_property_document_get_publish_topic(&pnp_client,
                                                                             twin_request_id,
                                                                             mqtt_get_topic_twin_buf,
                                                                             sizeof(mqtt_get_topic_twin_buf),
                                                                             NULL);

    if (az_result_failed(result))
    {
        debug_printError("  HUB: az_iot_pnp_client_property_document_get_publish_topic failed");
        return;
    }

    mqttPublishPacket cloudPublishPacket;
    // Fixed header
    cloudPublishPacket.publishHeaderFlags.duplicate = 0;
    cloudPublishPacket.publishHeaderFlags.qos       = 0;
    cloudPublishPacket.publishHeaderFlags.retain    = 0;
    // Variable header
    cloudPublishPacket.topic = (uint8_t*)mqtt_get_topic_twin_buf;

    // Payload
    cloudPublishPacket.payload       = NULL;
    cloudPublishPacket.payloadLength = 0;

    if (MQTT_CreatePublishPacket(&cloudPublishPacket) != true)
    {
        debug_printError("  HUB: PUBLISH failed");
        LED_SetGreen(LED_STATE_OFF);
    }
    else
    {
        LED_SetGreen(LED_STATE_HOLD);
    }
}