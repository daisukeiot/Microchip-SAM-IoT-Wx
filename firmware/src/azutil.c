#include "azutil.h"

extern az_iot_pnp_client pnp_client;
extern volatile uint32_t telemetryInterval;

// used by led.c to communicate LED states changes
extern led_status_t led_status;

char jrTmp[256];
char telemetry_payload[256];

//static const az_span twin_desired_name = AZ_SPAN_LITERAL_FROM_STR("desired");

static const az_span telemetry_name_temperature_span = AZ_SPAN_LITERAL_FROM_STR("temperature");
static const az_span telemetry_name_light_span       = AZ_SPAN_LITERAL_FROM_STR("light");

// Telemetry Interval writable property
static const az_span property_telemetry_interval_span = AZ_SPAN_LITERAL_FROM_STR("telemetryInterval");

// LED Properties
static const az_span led_blue_property_name_span   = AZ_SPAN_LITERAL_FROM_STR("led_b");
static const az_span led_green_property_name_span  = AZ_SPAN_LITERAL_FROM_STR("led_g");
static const az_span led_yellow_property_name_span = AZ_SPAN_LITERAL_FROM_STR("led_y");
static const az_span led_red_property_name_span    = AZ_SPAN_LITERAL_FROM_STR("led_r");

static const az_span led_on_string_span    = AZ_SPAN_LITERAL_FROM_STR("On");
static const az_span led_off_string_span   = AZ_SPAN_LITERAL_FROM_STR("Off");
static const az_span led_blink_string_span = AZ_SPAN_LITERAL_FROM_STR("Blink");

// Plug and Play Connection Values
static uint32_t connection_request_id_int = 0;
static char     connection_request_id_buffer[16];

/**********************************************
* Initialize twin property data structure
**********************************************/
void init_twin_data(twin_properties_t* twin_properties)
{
    twin_properties->flag.AsUSHORT      = 0;
    twin_properties->version_num        = 0;
    twin_properties->desired_led_yellow = LED_NO_CHANGE;
    twin_properties->reported_led_red   = LED_NO_CHANGE;
    twin_properties->reported_led_blue  = LED_NO_CHANGE;
    twin_properties->reported_led_green = LED_NO_CHANGE;
}

/**************************************
 Start JSON_BUILDER for JSON Document
 This creates a new JSON with "{"
*************************************/
az_result start_json_object(
    az_json_writer* jw,
    az_span         az_span_buffer)
{
    RETURN_ERR_IF_FAILED(az_json_writer_init(jw, az_span_buffer, NULL));
    RETURN_ERR_IF_FAILED(az_json_writer_append_begin_object(jw));
    return AZ_OK;
}

/**********************************************
* Start JSON_BUILDER for JSON Document
* This adds "}" to the JSON
**********************************************/
az_result end_json_object(
    az_json_writer* jw)
{
    RETURN_ERR_IF_FAILED(az_json_writer_append_end_object(jw));
    return AZ_OK;
}

/**********************************************
*	Add a JSON key-value pair with int32 data
*	e.g. "property_name" : property_val (number)
**********************************************/
az_result append_json_property_int32(
    az_json_writer* jw,
    az_span         property_name_span,
    int32_t         property_val)
{
    RETURN_ERR_IF_FAILED(az_json_writer_append_property_name(jw, property_name_span));
    RETURN_ERR_IF_FAILED(az_json_writer_append_int32(jw, property_val));
    return AZ_OK;
}

/**********************************************
* Add a JSON key-value pair with string data
* e.g. "property_name" : "property_val (string)"
**********************************************/
az_result append_jason_property_string(
    az_json_writer* jw,
    az_span         property_name_span,
    az_span         property_val_span)
{
    RETURN_ERR_IF_FAILED(az_json_writer_append_property_name(jw, property_name_span));
    RETURN_ERR_IF_FAILED(az_json_writer_append_string(jw, property_val_span));
    return AZ_OK;
}

/**********************************************
* Add JSON for writable property response with int32 data
* e.g. "property_name" : property_val_int32
**********************************************/
static az_result append_reported_property_response_int32(
    az_json_writer* jw,
    az_span         property_name_span,
    int32_t         property_val,
    int32_t         ack_code,
    int32_t         ack_version,
    az_span         ack_description_span)
{
    //	RETURN_ERR_IF_FAILED(az_json_writer_append_begin_object(jw));
    RETURN_ERR_IF_FAILED(az_iot_pnp_client_property_builder_begin_reported_status(
        &pnp_client, jw, property_name_span, ack_code, ack_version, ack_description_span));
    RETURN_ERR_IF_FAILED(az_json_writer_append_int32(jw, property_val));
    RETURN_ERR_IF_FAILED(az_iot_pnp_client_property_builder_end_reported_status(&pnp_client, jw));
    //	RETURN_ERR_IF_FAILED(az_json_writer_append_end_object(jw));
    return AZ_OK;
}

bool mqtt_publish_message(const char* topic, az_span payload)
{
    mqttPublishPacket cloudPublishPacket;

    // Fixed header
    cloudPublishPacket.publishHeaderFlags.duplicate = 0;
    cloudPublishPacket.publishHeaderFlags.qos       = 0;
    cloudPublishPacket.publishHeaderFlags.retain    = 0;

    // Variable header
    cloudPublishPacket.topic = (uint8_t*)topic;

    // Payload
    cloudPublishPacket.payload       = az_span_ptr(payload);
    cloudPublishPacket.payloadLength = az_span_size(payload);

    if (MQTT_CreatePublishPacket(&cloudPublishPacket) != true)
    {
        debug_printError("  APP: Connection lost PUBLISH failed");
        return false;
    }

    return true;
}

/**********************************************
* Build sensor telemetry JSON
**********************************************/
az_result build_sensor_telemetry_message(az_span* out_payload_span, int32_t temperature, int32_t light)
{
    az_json_writer jw;
    memset(&telemetry_payload, 0, sizeof(telemetry_payload));
    RETURN_ERR_IF_FAILED(start_json_object(&jw, AZ_SPAN_FROM_BUFFER(telemetry_payload)));
    RETURN_ERR_IF_FAILED(append_json_property_int32(&jw, telemetry_name_temperature_span, temperature));
    RETURN_ERR_IF_FAILED(append_json_property_int32(&jw, telemetry_name_light_span, light));
    RETURN_ERR_IF_FAILED(end_json_object(&jw));
    *out_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);
    return AZ_OK;
}

/**********************************************
* Read sensor data and send telemetry to cloud
**********************************************/
az_result send_telemetry_message(void)
{
    az_result rc = AZ_OK;
    az_span   telemetry_payload_span;

    int16_t temp  = APP_GetTempSensorValue();
    int32_t light = APP_GetLightSensorValue();

    debug_print("  APP: Light: %d Temperature: %d", light, temp);

    RETURN_ERR_WITH_MESSAGE_IF_FAILED(
        build_sensor_telemetry_message(&telemetry_payload_span, temp, light),
        "Failed to build sensor telemetry JSON payload");

    // mqtt_publish_message(telemetry_payload_span);

    uint8_t* pBuf = az_span_ptr(telemetry_payload_span);

    CLOUD_publishData(pBuf, strlen((char*)pBuf));

    return rc;
}


/**********************************************
* Parse Desired Property (Writable Property)
**********************************************/
az_result parse_twin_property(
    uint8_t*           topic,
    uint8_t*           payload,
    twin_properties_t* twin_properties)
{
    az_result rc;
    az_span   topic_span;
    az_span   payload_span;
    az_span   component_name_span;

    az_iot_pnp_client_property_response property_response;

    az_json_reader jr;

    debug_printInfo("AZURE: %s() payload %s", __FUNCTION__, payload);

    topic_span   = az_span_create(topic, strlen((char*)topic));
    payload_span = az_span_create_from_str((char*)payload);

    rc = az_iot_pnp_client_property_parse_received_topic(&pnp_client,
                                                         topic_span,
                                                         &property_response);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "az_iot_pnp_client_property_parse_received_topic failed");

    rc = az_json_reader_init(&jr,
                             payload_span,
                             NULL);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "az_json_reader_init failed");

    rc = az_iot_pnp_client_property_get_property_version(&pnp_client,
                                                         &jr,
                                                         property_response.response_type,
                                                         &twin_properties->version_num);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "az_json_reader_init failed");

    twin_properties->flag.version_found = 1;

    rc = az_json_reader_init(&jr,
                             payload_span,
                             NULL);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "az_json_reader_init failed");

    if (az_result_succeeded(az_iot_pnp_client_property_get_next_component_property(
                                &pnp_client,
                                &jr,
                                property_response.response_type,
                                &component_name_span)))
    {
        while (jr.token.kind != AZ_JSON_TOKEN_END_OBJECT)
        {
            if (jr.token.kind == AZ_JSON_TOKEN_PROPERTY_NAME)
            {
                if (az_json_token_is_text_equal(&jr.token, property_telemetry_interval_span))
                {
                    uint32_t data;
                    // found writable property to adjust telemetry interval
                    RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
                    RETURN_ERR_IF_FAILED(az_json_token_get_uint32(&jr.token, &data));
                    twin_properties->flag.telemetry_interval_found = 1;
                    telemetryInterval                              = data;
                }

                else if (az_json_token_is_text_equal(&jr.token, led_yellow_property_name_span))
                {
                    // found writable property to control Yellow LED
                    RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
                    RETURN_ERR_IF_FAILED(az_json_token_get_int32(&jr.token, &twin_properties->desired_led_yellow));
                    twin_properties->flag.yellow_led_found = 1;
                }
            }
            RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
        }
    }

    return rc;
}

static az_span get_request_id(void)
{
    az_span remainder;
    az_span out_span = az_span_create(
        (uint8_t*)connection_request_id_buffer, sizeof(connection_request_id_buffer));

    az_result rc = az_span_u32toa(out_span, connection_request_id_int++, &remainder);
    EXIT_WITH_MESSAGE_IF_FAILED(rc, "AZURE:Failed to get request id");

    return az_span_slice(out_span, 0, az_span_size(out_span) - az_span_size(remainder));
}

/**********************************************
* Send Reported Property 
**********************************************/
az_result send_reported_property(
    twin_properties_t* twin_properties)
{
    az_result rc;
    char      property_patch_topic_buffer[128];
    char      reported_property_payload_buffer[128];

    debug_printGood("AZURE:%s", __FUNCTION__);

    rc = az_iot_pnp_client_property_patch_get_publish_topic(&pnp_client,
                                                            get_request_id(),
                                                            property_patch_topic_buffer,
                                                            sizeof(property_patch_topic_buffer),
                                                            NULL);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "AZURE:Failed to get property PATCH topic");

    // Clear buffer and initialize JSON Payload.	This creates "{"
    az_json_writer jw;
    memset(reported_property_payload_buffer, 0, sizeof(reported_property_payload_buffer));
    az_span payload_span = AZ_SPAN_FROM_BUFFER(reported_property_payload_buffer);

    rc = start_json_object(&jw, payload_span);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "AZURE:Unable to initialize json_builder for property PATCH");

    if (twin_properties->flag.telemetry_interval_found)
    {
        if (az_result_failed(
                rc = append_reported_property_response_int32(
                    &jw,
                    property_telemetry_interval_span,
                    telemetryInterval,
                    200,
                    twin_properties->version_num,
                    AZ_SPAN_FROM_STR("Success"))))
        {
            debug_printError("AZURE: Unable to add property for telemetry interval, return code 0x%08x", rc);
            return rc;
        }
    }
    else if (twin_properties->flag.isGet)
    {
        if (az_result_failed(
                rc = append_reported_property_response_int32(
                    &jw,
                    property_telemetry_interval_span,
                    telemetryInterval,
                    200,
                    1,
                    AZ_SPAN_FROM_STR("Success"))))
        {
            debug_printError("AZURE: Unable to add property for telemetry interval, return code 0x%08x", rc);
            return rc;
        }
    }

    // Add Yellow LED to the reported property
    // Example with integer Enum
    if (twin_properties->desired_led_yellow != LED_NO_CHANGE)
    {
        int32_t yellow_led;

        if ((led_status.state_flag.yellow & (LED_STATE_BLINK_SLOW | LED_STATE_BLINK_FAST)) != 0)
        {
            yellow_led = 3;   // blink
        }
        else if (led_status.state_flag.yellow == LED_STATE_HOLD)
        {
            yellow_led = 1;   // on
        }
        else
        {
            yellow_led = 2;   // off
        }

        if (az_result_failed(
                rc = append_reported_property_response_int32(
                    &jw,
                    led_yellow_property_name_span,
                    yellow_led,
                    200,
                    twin_properties->version_num,
                    AZ_SPAN_FROM_STR("Success"))))
        {
            debug_printError("AZURE: Unable to add property for Yellow LED, return code 0x%08x", rc);
            return rc;
        }
    }

    // Add Red LED
    // Example with String Enum
    if (twin_properties->reported_led_red != LED_NO_CHANGE)
    {
        az_span red_led_value_span;

        switch (twin_properties->reported_led_red)
        {
            case 1:
                red_led_value_span = led_on_string_span;
                break;

            case 2:
                red_led_value_span = led_off_string_span;
                break;

            case 3:
                red_led_value_span = led_blink_string_span;
                break;
        }

        if (az_result_failed(
                rc = append_jason_property_string(
                    &jw,
                    led_red_property_name_span,
                    red_led_value_span)))
        {
            debug_printError("AZURE: Unable to add property for Red LED, return code  0x%08x", rc);
            return rc;
        }
    }

    // Add Blue LED
    if (twin_properties->reported_led_blue != LED_NO_CHANGE)
    {
        if (az_result_failed(
                rc = append_json_property_int32(
                    &jw,
                    led_blue_property_name_span,
                    twin_properties->reported_led_blue)))
        {
            debug_printError("AZURE: Unable to add property for Blue LED, return code  0x%08x", rc);
            return rc;
        }
    }

    // Add Green LED
    if (twin_properties->reported_led_green != LED_NO_CHANGE)
    {
        if (az_result_failed(
                rc = append_json_property_int32(
                    &jw,
                    led_green_property_name_span,
                    twin_properties->reported_led_green)))
        {
            debug_printError("AZURE: Unable to add property for Green LED, return code  0x%08x", rc);
            return rc;
        }
    }

    // Close JSON Payload (appends "}")
    if (az_result_failed(rc = end_json_object(&jw)))
    {
        debug_printError("AZURE: Unable to append end object, return code  0x%08x", rc);
        return rc;
    }

    az_span json_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);

    // Publish the reported property payload to IoT Hub
    debug_printInfo("AZURE: Sending twin reported property : %s", az_span_ptr(json_payload_span));

    rc = az_iot_pnp_client_property_patch_get_publish_topic(
        &pnp_client,
        get_request_id(),
        property_patch_topic_buffer,
        sizeof(property_patch_topic_buffer),
        NULL);

    // Send the reported property
    if (mqtt_publish_message(property_patch_topic_buffer, json_payload_span) == false)
    {
        debug_printInfo("AZURE: PUBLISH : Reported property");
    }

    return rc;
}