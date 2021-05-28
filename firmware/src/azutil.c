#include "azutil.h"

#define DISABLE_TELEMETRY
//#define DISABLE_PROPERTY

extern az_iot_pnp_client pnp_client;
extern volatile uint32_t telemetryInterval;

// used by led.c to communicate LED states changes
extern led_status_t led_status;

extern uint16_t packet_identifier;

extern OSAL_MUTEX_HANDLE_TYPE publish_mutex;

static char pnp_telemetry_topic_buffer[128];
static char pnp_telemetry_payload_buffer[128];

static char pnp_property_topic_buffer[128];
static char pnp_property_payload_buffer[256];

static char pnp_command_topic_buffer[128];
static char pnp_command_response_buffer[128];

// IoT Plug and Play properties

//static const az_span twin_desired_name = AZ_SPAN_LITERAL_FROM_STR("desired");

static const az_span empty_payload_span = AZ_SPAN_LITERAL_FROM_STR("\"\"");

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

// Command
static const az_span command_error_span               = AZ_SPAN_LITERAL_FROM_STR("Error");
static const az_span command_success_span             = AZ_SPAN_LITERAL_FROM_STR("success");
static const az_span command_name_reboot_span         = AZ_SPAN_LITERAL_FROM_STR("reboot");
static const az_span command_err_payload_missing_span = AZ_SPAN_LITERAL_FROM_STR("Delay time not found. Specify 'delay' in period format (PT5S for 5 sec)");
static const az_span command_err_not_supported_span   = AZ_SPAN_LITERAL_FROM_STR("{\"Status\":\"Unsupported Command\"}");
static const az_span command_status_span              = AZ_SPAN_LITERAL_FROM_STR("status");
static const az_span command_reboot_delay_span        = AZ_SPAN_LITERAL_FROM_STR("delay");

static int reboot_delay_seconds = 0;

static SYS_TIME_HANDLE reboot_task_handle = SYS_TIME_HANDLE_INVALID;

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

/**********************************************
* Build sensor telemetry JSON
**********************************************/
az_result build_sensor_telemetry_message(az_span* out_payload_span, int32_t temperature, int32_t light)
{
    az_json_writer jw;
    memset(&pnp_telemetry_payload_buffer, 0, sizeof(pnp_telemetry_payload_buffer));
    RETURN_ERR_IF_FAILED(start_json_object(&jw, AZ_SPAN_FROM_BUFFER(pnp_telemetry_payload_buffer)));
    RETURN_ERR_IF_FAILED(append_json_property_int32(&jw, telemetry_name_temperature_span, temperature));
    RETURN_ERR_IF_FAILED(append_json_property_int32(&jw, telemetry_name_light_span, light));
    RETURN_ERR_IF_FAILED(end_json_object(&jw));
    *out_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);
    return AZ_OK;
}

/**********************************************
* Create JSON document for error response
**********************************************/
static az_result build_error_response_payload(
    az_span  response_span,
    az_span  error_string_span,
    az_span* response_payload_span)
{
    az_json_writer jw;

    // Build the command response payload
    RETURN_ERR_IF_FAILED(start_json_object(&jw, response_span));
    RETURN_ERR_IF_FAILED(append_jason_property_string(&jw, command_error_span, error_string_span));
    RETURN_ERR_IF_FAILED(end_json_object(&jw));
    *response_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);
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

    debug_printGood("AZURE: Light: %d Temperature: %d", light, temp);

    RETURN_ERR_WITH_MESSAGE_IF_FAILED(
        build_sensor_telemetry_message(&telemetry_payload_span, temp, light),
        "Failed to build sensor telemetry JSON payload");

    rc = az_iot_pnp_client_telemetry_get_publish_topic(
        &pnp_client,
        AZ_SPAN_EMPTY,
        NULL,
        pnp_telemetry_topic_buffer,
        sizeof(pnp_telemetry_topic_buffer),
        NULL);

#ifndef DISABLE_TELEMETRY
    CLOUD_publishData((uint8_t*)pnp_telemetry_topic_buffer, az_span_ptr(telemetry_payload_span), az_span_size(telemetry_payload_span), QOS_PNP_TELEMETRY);
#endif
    return rc;
}

/**********************************************
* Check if LED status has changed or not.
**********************************************/
void check_led_status(twin_properties_t* twin_properties)
{
    twin_properties_t* twin_properties_ptr;
    twin_properties_t  twin_properties_local;

    bool b_force_sync = false;   // force LED status synchronization if this request is coming from Twin Get

    if (twin_properties == NULL)
    {
        // twin data is not provided by the caller
        init_twin_data(&twin_properties_local);
        twin_properties_ptr = &twin_properties_local;
    }
    else
    {
        // twin data is provided by the caller through Get Twin
        twin_properties_ptr = twin_properties;
    }

    if (led_status.change_flag.AsUSHORT == 0 && twin_properties_ptr->flag.isInitialGet == 0)
    {
        // no changes, nothing to update
        return;
    }

    debug_printInfo("  MAIN: %s() led_change %x", __func__, led_status.change_flag.AsUSHORT);

    // if this is from Get Twin, update according to Desired Property
    b_force_sync = twin_properties_ptr->flag.isInitialGet == 1 ? true : false;

    if (led_status.change_flag.AsUSHORT != 0 || b_force_sync)
    {
        if (led_status.change_flag.blue == 1 || b_force_sync)
        {
            if ((led_status.state_flag.blue & (LED_STATE_BLINK_SLOW | LED_STATE_BLINK_FAST)) != 0)
            {
                twin_properties_ptr->reported_led_blue = 3;
            }
            else if (led_status.state_flag.blue == LED_STATE_HOLD)
            {
                twin_properties_ptr->reported_led_blue = 1;
            }
            else
            {
                twin_properties_ptr->reported_led_blue = 2;
            }
        }

        if (led_status.change_flag.green == 1 || b_force_sync)
        {
            if ((led_status.state_flag.green & (LED_STATE_BLINK_SLOW | LED_STATE_BLINK_FAST)) != 0)
            {
                twin_properties_ptr->reported_led_green = 3;
            }
            else if (led_status.state_flag.green == LED_STATE_HOLD)
            {
                twin_properties_ptr->reported_led_green = 1;
            }
            else
            {
                twin_properties_ptr->reported_led_green = 2;
            }
        }

        if (led_status.change_flag.red == 1 || b_force_sync)
        {
            if ((led_status.state_flag.red & (LED_STATE_BLINK_SLOW | LED_STATE_BLINK_FAST)) != 0)
            {
                twin_properties_ptr->reported_led_red = 3;
            }
            else if (led_status.state_flag.red == LED_STATE_HOLD)
            {
                twin_properties_ptr->reported_led_red = 1;
            }
            else
            {
                twin_properties_ptr->reported_led_red = 2;
            }
        }

        // clear flags
        led_status.change_flag.AsUSHORT = 0;

        // if this is from Get Twin, Device Twin code path will update reported properties
        if (!b_force_sync)
        {
            send_reported_property(twin_properties_ptr);
        }
    }
}

/**********************************************
*	Process LED Update/Patch
**********************************************/
void update_leds(
    twin_properties_t* twin_properties)
{
    // If desired properties are not set, send current LED states.
    // Otherwise, set LED state based on Desired property
    if (twin_properties->flag.yellow_led_found == 1 && twin_properties->desired_led_yellow != LED_NO_CHANGE)
    {
        if (twin_properties->desired_led_yellow == 1)
        {
            LED_SetYellow(LED_STATE_HOLD);
        }
        else if (twin_properties->desired_led_yellow == 2)
        {
            LED_SetYellow(LED_STATE_OFF);
        }
        else if (twin_properties->desired_led_yellow == 3)
        {
            LED_SetYellow(LED_STATE_BLINK_FAST);
        }
    }

    // If this is Twin Get, populate LED states for Red, Blue, Green LEDs
    if (twin_properties->flag.isInitialGet == 1)
    {
        check_led_status(twin_properties);
    }
}


/**********************************************
* Send the response of the command invocation
**********************************************/
static int send_command_response(
    az_iot_pnp_client_command_request* request,
    uint16_t                           status,
    az_span                            response)
{
    // Get the response topic to publish the command response
    int rc = az_iot_pnp_client_commands_response_get_publish_topic(
        &pnp_client, request->request_id, status, pnp_command_topic_buffer,
        sizeof(pnp_command_topic_buffer), NULL);
    RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "AZURE: Unable to get command response publish topic");

    debug_printInfo("AZURE: Command Status: %u", status);

    // Send the commands response
    // if ((rc = mqtt_publish_message(pnp_command_topic_buffer, response, 0)) == 0)
    // {
    //     debug_printInfo("Sent command response");
    // }

    return rc;
}

void reboot_task_callback(uintptr_t context)
{
    debug_printWarn("AZURE: Rebooting...");
    NVIC_SystemReset();
}

/**********************************************
*	Handle reboot command
**********************************************/
static az_result process_reboot_command(
    az_span  payload_span,
    az_span  response_span,
    az_span* out_response_span)
{
    char           reboot_delay[32];
    az_json_writer jw;
    az_json_reader jr;

    debug_printInfo("AZURE: %s() : Payload %s", __func__, az_span_ptr(payload_span));

    if (az_span_size(payload_span) == 0 || (az_span_size(payload_span) == 2 && az_span_is_content_equal(empty_payload_span, payload_span)))
    {
        RETURN_ERR_IF_FAILED(build_error_response_payload(response_span, command_err_payload_missing_span, out_response_span));
        return AZ_ERROR_ITEM_NOT_FOUND;
    }

    RETURN_ERR_IF_FAILED(az_json_reader_init(&jr, payload_span, NULL));

    while (jr.token.kind != AZ_JSON_TOKEN_END_OBJECT)
    {
        if (jr.token.kind == AZ_JSON_TOKEN_PROPERTY_NAME)
        {
            if (az_json_token_is_text_equal(&jr.token, command_reboot_delay_span))
            {
                RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
                RETURN_ERR_IF_FAILED(az_json_token_get_string(&jr.token, reboot_delay, sizeof(reboot_delay), NULL));
                break;
            }
        }
        else if (jr.token.kind == AZ_JSON_TOKEN_STRING)
        {
            RETURN_ERR_IF_FAILED(az_json_token_get_string(&jr.token, reboot_delay, sizeof(reboot_delay), NULL));
            break;
        }
        RETURN_ERR_IF_FAILED(az_json_reader_next_token(&jr));
    }

    if (reboot_delay[0] != 'P' || reboot_delay[1] != 'T' || reboot_delay[strlen(reboot_delay) - 1] != 'S')
    {
        debug_printError("AZURE: Reboot Delay wrong format");
        RETURN_ERR_IF_FAILED(build_error_response_payload(response_span, command_err_payload_missing_span, out_response_span));
        return AZ_ERROR_ARG;
    }

    reboot_delay_seconds = atoi((const char*)&reboot_delay[2]);

    debug_printInfo("AZURE: Setting reboot delay for %d sec", reboot_delay_seconds);
    reboot_task_handle = SYS_TIME_CallbackRegisterMS(reboot_task_callback, 0, reboot_delay_seconds * 1000, SYS_TIME_SINGLE);

    start_json_object(&jw, response_span);
    append_jason_property_string(&jw, command_status_span, command_success_span);
    append_json_property_int32(&jw, command_reboot_delay_span, reboot_delay_seconds);
    end_json_object(&jw);

    *out_response_span = az_json_writer_get_bytes_used_in_destination(&jw);

    return AZ_OK;
}

/**********************************************
* Parse Desired Property (Writable Property)
**********************************************/
az_result process_direct_method_command(
    uint8_t*                           payload,
    az_iot_pnp_client_command_request* command_request)
{
    az_result rc                    = AZ_OK;
    uint16_t  response_status       = 404;   // assume error
    az_span   command_response_span = AZ_SPAN_FROM_BUFFER(pnp_command_response_buffer);
    az_span   payload_span          = az_span_create_from_str((char*)payload);

    debug_printInfo("AZURE: Processing Command '%s'", command_request->command_name);

    if (az_span_is_content_equal(command_name_reboot_span, command_request->command_name))
    {
        rc = process_reboot_command(payload_span, command_response_span, &command_response_span);

        if (az_result_failed(rc))
        {
            debug_printError("AZURE: Failed process_reboot_command, status 0x%08x", rc);
            if (az_span_size(command_response_span) == 0)
            {
                // if response is empty, payload was not in the right format.
                if (az_result_failed(rc = build_error_response_payload(command_response_span, command_err_payload_missing_span, &command_response_span)))
                {
                    debug_printError("  MAIN: Fail build error response. (0x%08x)", rc);
                }
            }
        }
    }
    else
    {
        // Unsupported command
        debug_printError("AZURE: Unsupported command received: %s.", az_span_ptr(command_request->command_name));

        if ((rc = send_command_response(command_request, response_status, command_err_not_supported_span)) != 0)
        {
            debug_printError("  MAIN: Unable to send %d response, status %d", response_status, rc);
        }
    }


    return rc;
}
// typedef enum
// {
//   AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_TYPE_GET = 1,
//   AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_TYPE_DESIRED_PROPERTIES = 2,
//   AZ_IOT_HUB_CLIENT_TWIN_RESPONSE_TYPE_REPORTED_PROPERTIES = 3,
// } az_iot_hub_client_twin_response_type;

/**********************************************
* Parse Desired Property (Writable Property)
**********************************************/
az_result process_device_twin_property(
    uint8_t*           topic,
    uint8_t*           payload,
    twin_properties_t* twin_properties)
{
    az_result rc;
    az_span   property_topic_span;
    az_span   payload_span;
    az_span   component_name_span;

    az_iot_pnp_client_property_response property_response;

    az_json_reader jr;

    property_topic_span = az_span_create(topic, strlen((char*)topic));
    payload_span        = az_span_create_from_str((char*)payload);

    rc = az_iot_pnp_client_property_parse_received_topic(&pnp_client,
                                                         property_topic_span,
                                                         &property_response);

    if (az_result_succeeded(rc))
    {
        debug_printInfo("AZURE: Property Topic  : %s", az_span_ptr(property_topic_span));
        debug_printInfo("AZURE: Property Type   : %d", property_response.response_type);
        debug_printInfo("AZURE: Property Payload: %s", (char*)payload);
    }
    else
    {
        debug_printError("AZURE: Failed to parse property topic 0x%08x.", rc);
        debug_printError("AZURE: Topic: '%s' Payload: '%s'", (char*)topic, (char*)payload);
        return rc;
    }

    if (property_response.response_type == AZ_IOT_PNP_CLIENT_PROPERTY_RESPONSE_TYPE_GET)
    {
        debug_printWarn("AZURE: Property GET");
        if (az_span_is_content_equal_ignoring_case(property_response.request_id, twin_request_id_span))
        {
            debug_printGood("AZURE: INITIAL GET");
            twin_properties->flag.isInitialGet = 1;
        }
    }
    else if (property_response.response_type == AZ_IOT_PNP_CLIENT_PROPERTY_RESPONSE_TYPE_DESIRED_PROPERTIES)
    {
        debug_printWarn("AZURE: Property DESIRED Status %d ID %s Version %s",
                        property_response.status,
                        az_span_ptr(property_response.request_id),
                        az_span_ptr(property_response.version));
    }
    else if (property_response.response_type == AZ_IOT_PNP_CLIENT_PROPERTY_RESPONSE_TYPE_REPORTED_PROPERTIES)
    {
        if (!az_iot_status_succeeded(property_response.status))
        {
            debug_printWarn("AZURE: Property REPORTED Status %d ID %s Version %s",
                            property_response.status,
                            az_span_ptr(property_response.request_id),
                            az_span_ptr(property_response.version));
        }
        return rc;
    }
    else
    {
        debug_printWarn("AZURE: Type %d Status %d ID %s Version %s", property_response.response_type, property_response.status, az_span_ptr(property_response.request_id), az_span_ptr(property_response.version));
    }

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

int32_t get_led_value(unsigned short led_flag)
{
    int32_t led_property_value;

    if ((led_flag & (LED_STATE_BLINK_SLOW | LED_STATE_BLINK_FAST)) != 0)
    {
        led_property_value = 3;   // blink
    }
    else if (led_flag == LED_STATE_HOLD)
    {
        led_property_value = 1;   // on
    }
    else
    {
        led_property_value = 2;   // off
    }

    return led_property_value;
}

/**********************************************
* Send Reported Property 
**********************************************/
az_result send_reported_property(
    twin_properties_t* twin_properties)
{
    az_result      rc;
    az_json_writer jw;
    az_span        identifier_span;
    int32_t        led_property_value;

    if (twin_properties->flag.AsUSHORT == 0)
    {
        // Nothing to do.
        debug_printGood("AZURE: No property update");
        return AZ_OK;
    }

    debug_printGood("AZURE: Sending Property flag 0x%x", twin_properties->flag.AsUSHORT);

    // Clear buffer and initialize JSON Payload.	This creates "{"
    memset(pnp_property_payload_buffer, 0, sizeof(pnp_property_payload_buffer));
    az_span payload_span = AZ_SPAN_FROM_BUFFER(pnp_property_payload_buffer);

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
    else if (twin_properties->flag.isInitialGet)
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
        led_property_value = get_led_value(led_status.state_flag.yellow);

        if (az_result_failed(
                rc = append_reported_property_response_int32(
                    &jw,
                    led_yellow_property_name_span,
                    led_property_value,
                    200,
                    twin_properties->version_num,
                    AZ_SPAN_FROM_STR("Success"))))
        {
            debug_printError("AZURE: Unable to add property for Yellow LED, return code 0x%08x", rc);
            return rc;
        }
    }
    else if (twin_properties->flag.isInitialGet)
    {
        led_property_value = get_led_value(led_status.state_flag.yellow);

        if (az_result_failed(
                rc = append_reported_property_response_int32(
                    &jw,
                    led_yellow_property_name_span,
                    led_property_value,
                    200,
                    1,
                    AZ_SPAN_FROM_STR("Success"))))
        {
            debug_printError("AZURE: Unable to add property for telemetry interval, return code 0x%08x", rc);
            return rc;
        }
    }

    // Add Red LED
    // Example with String Enum
    if (twin_properties->flag.isInitialGet || twin_properties->reported_led_red != LED_NO_CHANGE)
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
    if (twin_properties->flag.isInitialGet || twin_properties->reported_led_blue != LED_NO_CHANGE)
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
    if (twin_properties->flag.isInitialGet || twin_properties->reported_led_green != LED_NO_CHANGE)
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
    if (az_result_failed(rc = az_iot_pnp_client_property_builder_end_reported_status(&pnp_client, &jw)))
    {
        debug_printError("AZURE: Unable to append end object, return code  0x%08x", rc);
        return rc;
    }

    az_span property_payload_span = az_json_writer_get_bytes_used_in_destination(&jw);

    // Publish the reported property payload to IoT Hub
    //debug_printInfo("AZURE: Sending twin reported property : %s", az_span_ptr(property_payload_span));

    if (OSAL_RESULT_TRUE == MUTEX_Lock(&publish_mutex, OSAL_WAIT_FOREVER))
    {
        debug_printGood("AZURE: >> PUBLISH Mutex Locked - P");
        identifier_span = get_publish_packet_id();
        rc              = az_iot_pnp_client_property_patch_get_publish_topic(&pnp_client,
                                                                identifier_span,
                                                                pnp_property_topic_buffer,
                                                                sizeof(pnp_property_topic_buffer),
                                                                NULL);
        RETURN_ERR_WITH_MESSAGE_IF_FAILED(rc, "AZURE:Failed to get property PATCH topic");

#ifndef DISABLE_PROPERTY
        // Send the reported property
        CLOUD_publishData((uint8_t*)pnp_property_topic_buffer, az_span_ptr(property_payload_span), az_span_size(property_payload_span), QOS_PNP_PROPERTY);
#endif
    }
    return rc;
}

/**********************************************
* Acquire Mutex
**********************************************/
void MSDelay(uint32_t ms)
{
    SYS_TIME_HANDLE tmrHandle = SYS_TIME_HANDLE_INVALID;

    if (SYS_TIME_SUCCESS != SYS_TIME_DelayMS(ms, &tmrHandle))
    {
        return;
    }

    while (true != SYS_TIME_DelayIsComplete(tmrHandle))
    {
        continue;
    }
}

OSAL_RESULT MUTEX_Lock(OSAL_MUTEX_HANDLE_TYPE* mutexID, uint16_t waitMS)
{
    OSAL_RESULT result  = OSAL_RESULT_FALSE;
    int32_t     counter = 0;
    if (waitMS == OSAL_WAIT_FOREVER)
    {
        while (true)
        {
            result = OSAL_MUTEX_Lock(mutexID, waitMS);
            if (result == OSAL_RESULT_TRUE)
            {
                debug_printWarn("Mutex held");
                break;
            }
            MSDelay(200);
            counter++;

            if (counter > 100)
            {
                debug_printWarn("Mutex not held");
                break;
            }
        }
    }
    return result;
}