#include "sdkconfig.h"
#include <mdns.h>
#include <esp_http_server.h>
#include <esp_tls_crypto.h>
#include <esp_event.h>
#include <esp_sntp.h>
#include <esp_chip_info.h>
#include <esp_random.h>
#include <esp_log.h>
#include <string.h>
#include <fcntl.h>
#include <cJSON.h>
#include <esp_netif.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "fanctrlevents.h"
#include "fanconfig.h"
#include "network.h"
#include "pwm.h"
#include "target.h"
#include "espmsg.pb.h"

#define PORT 1234

static const char* TAG = "Network";

typedef enum {
    SOCK_STATE_AUTH = 0x01,
} sock_state_t;

typedef struct {
    int socket;
    struct sockaddr_storage source_addr;
    sock_state_t state;
    uint32_t pck_len;
    uint32_t pck_buf_len;
    uint32_t pck_buf[512];
    char challenge[8];
} sock_info_t;

static sock_info_t client_info[CONFIG_LWIP_MAX_SOCKETS];



esp_err_t start_rest_server(const char *base_path);

esp_err_t sock_recv(sock_info_t *client);
esp_err_t socket_close(sock_info_t *client);
int socket_send(sock_info_t *client, const char * data, const size_t len);


void vTaskTCPServer(void* pvParameters);

void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event %d",  sntp_get_sync_status());
    esp_event_post(TIME_EVENTS, TIME_EVENT_SYNC, NULL, 0, portMAX_DELAY);
}

esp_err_t StartNetwork(void)
{
    ESP_LOGI(TAG, "Starting Time Sync");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();


    ESP_LOGI(TAG, "Starting mDNS");
    esp_err_t err = mdns_init();
    if (err) {
        ESP_LOGW(TAG, "Error starting mDNS: %d", err);
        return err;
    }

    err = mdns_hostname_set(CONFIG_FANCTRL_HOSTNAME);
    if (err) {
        ESP_LOGW(TAG, "Error setting hostname: %d", err);
        return err;
    }
    err = mdns_instance_name_set("Fan Ctonroller");
    if (err) {
        ESP_LOGW(TAG, "Error setting instance name: %d", err);
        return err;
    }

    mdns_txt_item_t serviceTxtData[] = {
        {"board", "fanctrl"},
        {"path", "/"}
    };

    err = mdns_service_add("Fan Controller Webserver", "_http", "_tcp", 80, serviceTxtData,
                                     sizeof(serviceTxtData) / sizeof(serviceTxtData[0]));
    if (err) {
        ESP_LOGW(TAG, "Error adding service: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "Starting REST server");
    err = start_rest_server("/");
    if (err) {
        ESP_LOGW(TAG, "Error starting REST server: %d", err);
        return err;
    }
    
    ESP_LOGI(TAG, "Starting TCP server");
    xTaskCreate(vTaskTCPServer, "TCPServer", 4096, NULL, 5, NULL);


    return ESP_OK;
}

#define REST_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(a))                                                                      \
        {                                                                              \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                             \
        }                                                                              \
    } while (0)

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)
#define HTTPD_401      "401 UNAUTHORIZED"           /*!< HTTP Response 401 */

static char *http_auth_basic()
{
    int out;
    char *user_info = NULL;
    char *digest = NULL;
    size_t n = 0;
    xSemaphoreTake(configMutex, portMAX_DELAY);
    asprintf(&user_info, "%s:%s", deviceConfig.username, deviceConfig.password);
    xSemaphoreGive(configMutex);
    if (!user_info) {
        ESP_LOGE(TAG, "No enough memory for user information");
        return NULL;
    }
    esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));

    /* 6: The length of the "Basic " string
     * n: Number of bytes for a base64 encode format
     * 1: Number of bytes for a reserved which be used to fill zero
    */
    digest = calloc(1, 6 + n + 1);
    if (digest) {
        strcpy(digest, "Basic ");
        esp_crypto_base64_encode((unsigned char *)digest + 6, n, (size_t *)&out, (const unsigned char *)user_info, strlen(user_info));
    }
    free(user_info);
    return digest;
}

static esp_err_t basic_auth_get_handler(httpd_req_t *req)
{
    char *buf = NULL;
    size_t buf_len = 0;

    buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
    if (buf_len > 1) {
        buf = calloc(1, buf_len);
        if (!buf) {
            ESP_LOGE(TAG, "No enough memory for basic authorization");
            return ESP_FAIL;
        }

        if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len) == ESP_OK) {
            ESP_LOGI(TAG, "Found header => Authorization: %s", buf);
        } else {
            ESP_LOGE(TAG, "No auth value received");
        }

        char *auth_credentials = http_auth_basic();
        if (!auth_credentials) {
            ESP_LOGE(TAG, "No enough memory for basic authorization credentials");
            free(buf);
            return ESP_FAIL;
        }

        if (strncmp(auth_credentials, buf, buf_len)) {
            ESP_LOGE(TAG, "Not authenticated");
            httpd_resp_set_status(req, HTTPD_401);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_set_hdr(req, "Connection", "keep-alive");
            httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"FanController\"");
            httpd_resp_send(req, NULL, 0);
        } else {
            ESP_LOGI(TAG, "Authenticated!");
            return ESP_OK;
        }
        free(auth_credentials);
        free(buf);
    } else {
        ESP_LOGE(TAG, "No auth header received");
        httpd_resp_set_status(req, HTTPD_401);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Connection", "keep-alive");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Hello\"");
        httpd_resp_send(req, NULL, 0);
    }

    return ESP_FAIL;
}

typedef struct rest_server_context {
    char base_path[64];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

/* handler to Set PWM Speed */
static esp_err_t pwm_post_handler(httpd_req_t *req)
{
    if (basic_auth_get_handler(req) != ESP_OK) {
        return ESP_FAIL;
    }

    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
        if (cJSON_HasObjectItem(root, "duty") == false) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing Duty Value");
        return ESP_FAIL;
    }
    if (cJSON_HasObjectItem(root, "channel") == false) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing Channel Value");
        return ESP_FAIL;
    }

    int channel = cJSON_GetObjectItem(root, "channel")->valueint;
    int duty = cJSON_GetObjectItem(root, "duty")->valueint;
    ESP_LOGI(TAG, "PWM control: Channel:%d Duty: %d", channel, duty);
    cJSON_Delete(root);
    esp_err_t err = target_send_duty(channel, duty);
    if (err) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set PWM duty");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* handler to Get PWM Speed */
static esp_err_t pwm_get_handler(httpd_req_t *req)
{
    if (basic_auth_get_handler(req) != ESP_OK) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    for (size_t index = 0; index < LEDC_TEST_CH_NUM; index++) {
        cJSON *pwm = cJSON_CreateObject();
        if (pwm == NULL) {
            goto end;
        }
        char channel[10];
        sprintf(channel, "%d", index);
        cJSON_AddItemToObject(root, channel, pwm);
        target_t data;
        ESP_ERROR_CHECK(target_get_data(index, &data));
        cJSON *value = cJSON_CreateNumber(data.duty);
        cJSON_AddItemToObject(pwm, "duty", value);
    }

    const char *pwm_json = cJSON_Print(root);
    httpd_resp_sendstr(req, pwm_json);
    free((void *)pwm_json);
end:
    cJSON_Delete(root);
    return ESP_OK;
}

/* handler to Set Temp */
static esp_err_t temp_post_handler(httpd_req_t *req)
{
    if (basic_auth_get_handler(req) != ESP_OK) {
        return ESP_FAIL;
    }

    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (cJSON_HasObjectItem(root, "temp") == false) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing Temp Value");
        return ESP_FAIL;
    }
    if (cJSON_HasObjectItem(root, "channel") == false) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Missing Channel Value");
        return ESP_FAIL;
    }

    int channel = cJSON_GetObjectItem(root, "channel")->valueint;
    float temp = cJSON_GetObjectItem(root, "temp")->valuedouble;
    ESP_LOGI(TAG, "Temp: Channel:%d Temp: %f", channel, temp);
    cJSON_Delete(root);
    esp_err_t err = target_send_temp(channel, temp);
    if (err) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set Temp");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "OK");
    return ESP_OK;
}

/* handler to Get Temp */
static esp_err_t temp_get_handler(httpd_req_t *req)
{
    if (basic_auth_get_handler(req) != ESP_OK) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    for (size_t index = 0; index < LEDC_TEST_CH_NUM; index++) {
        cJSON *pwm = cJSON_CreateObject();
        if (pwm == NULL) {
            goto end;
        }
        char channel[10];
        sprintf(channel, "%d", index);
        cJSON_AddItemToObject(root, channel, pwm);
        target_t data;
        ESP_ERROR_CHECK(target_get_data(index, &data));
        cJSON *value = cJSON_CreateNumber(data.temp);
        cJSON_AddItemToObject(pwm, "temp", value);
    }

    const char *pwm_json = cJSON_Print(root);
    httpd_resp_sendstr(req, pwm_json);
    free((void *)pwm_json);
end:
    cJSON_Delete(root);
    return ESP_OK;
}

/* handler to Get Data */
static esp_err_t data_get_handler(httpd_req_t *req)
{
    if (basic_auth_get_handler(req) != ESP_OK) {
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    for (size_t index = 0; index < LEDC_TEST_CH_NUM; index++) {
        cJSON *pwm = cJSON_CreateObject();
        if (pwm == NULL) {
            goto end;
        }
        char channel[10];
        sprintf(channel, "%d", index);
        cJSON_AddItemToObject(root, channel, pwm);
        target_t data;
        ESP_ERROR_CHECK(target_get_data(index, &data));
        cJSON *temp = cJSON_CreateNumber(data.temp);
        cJSON_AddItemToObject(pwm, "temp", temp);
        cJSON *duty = cJSON_CreateNumber(data.duty);
        cJSON_AddItemToObject(pwm, "duty", duty);
        cJSON *rpm = cJSON_CreateNumber(data.rpm);
        cJSON_AddItemToObject(pwm, "rpm", rpm);
        cJSON *load = cJSON_CreateNumber(data.load);
        cJSON_AddItemToObject(pwm, "load", load);
        cJSON *lastupdate = cJSON_CreateNumber(data.lastUpdate);
        cJSON_AddItemToObject(pwm, "lastupdate", lastupdate);
    }

    const char *pwm_json = cJSON_Print(root);
    httpd_resp_sendstr(req, pwm_json);
    free((void *)pwm_json);
end:
    cJSON_Delete(root);
    return ESP_OK;
}


/* handler to Get Data */
static esp_err_t config_get_handler(httpd_req_t *req)
{
    if (basic_auth_get_handler(req) != ESP_OK) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to get config");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *channels = cJSON_CreateNumber(NUM_TARGETS);
    cJSON_AddItemToObject(root, "channels", channels);

    cJSON *tz = cJSON_CreateString(deviceConfig.tz);
    cJSON_AddItemToObject(root, "timezone", tz);

    cJSON *username = cJSON_CreateString(deviceConfig.username);
    cJSON_AddItemToObject(root, "username", username);

    cJSON *password = cJSON_CreateBool(strlen(deviceConfig.password) > 0 ? true : false);
    cJSON_AddItemToObject(root, "passwordset", password);

    for (size_t index = 0; index < NUM_TARGETS; index++) {
        cJSON *pwm = cJSON_CreateObject();
        if (pwm == NULL) {
            goto end;
        }
        char channel[10];
        sprintf(channel, "%d", index);
        cJSON_AddItemToObject(root, channel, pwm);
        cJSON *enabled = cJSON_CreateBool(channelConfig[index].enabled);
        cJSON_AddItemToObject(pwm, "enabled", enabled);
        cJSON *lowTemp = cJSON_CreateNumber(channelConfig[index].lowTemp);
        cJSON_AddItemToObject(pwm, "lowTemp", lowTemp);
        cJSON *highTemp = cJSON_CreateNumber(channelConfig[index].highTemp);
        cJSON_AddItemToObject(pwm, "highTemp", highTemp);
        cJSON *minDuty = cJSON_CreateNumber(channelConfig[index].minDuty);
        cJSON_AddItemToObject(pwm, "minDuty", minDuty);
    }

    const char *pwm_json = cJSON_Print(root);
    httpd_resp_sendstr(req, pwm_json);
    free((void *)pwm_json);
end:
    cJSON_Delete(root);
    xSemaphoreGive(configMutex);
    return ESP_OK;
}


/* Simple handler for getting system handler */
static esp_err_t info_get_handler(httpd_req_t *req)
{
    if (basic_auth_get_handler(req) != ESP_OK) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    cJSON_AddStringToObject(root, "version", IDF_VER);
    cJSON_AddNumberToObject(root, "cores", chip_info.cores);
    const char *sys_info = cJSON_Print(root);
    httpd_resp_sendstr(req, sys_info);
    free((void *)sys_info);
    cJSON_Delete(root);
    return ESP_OK;
}


esp_err_t start_rest_server(const char *base_path)
{
    REST_CHECK(base_path, "wrong base path", err);
    rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
    REST_CHECK(rest_context, "No memory for rest context", err);
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server");
    REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);

    /* URI handler for fetching system info */
    httpd_uri_t info_get_uri = {
        .uri = "/api/v1/system/info",
        .method = HTTP_GET,
        .handler = info_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &info_get_uri);

    /* URI handler for fetching system config */
    httpd_uri_t config_get_uri = {
        .uri = "/api/v1/system/config",
        .method = HTTP_GET,
        .handler = config_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &config_get_uri);



    /* URI handler to Set PWM Value */
    httpd_uri_t pwm_post_uri = {
        .uri = "/api/v1/pwm",
        .method = HTTP_POST,
        .handler = pwm_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &pwm_post_uri);

    /* URI handler to Set PWM Value */
    httpd_uri_t pwm_get_uri = {
        .uri = "/api/v1/pwm",
        .method = HTTP_GET,
        .handler = pwm_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &pwm_get_uri);

    /* URI handler to Set Temp Value */
    httpd_uri_t temp_post_uri = {
        .uri = "/api/v1/temp",
        .method = HTTP_POST,
        .handler = temp_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &temp_post_uri);

    httpd_uri_t temp_get_uri  = {
        .uri = "/api/v1/temp",
        .method = HTTP_GET,
        .handler = temp_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &temp_get_uri);

    httpd_uri_t data_get_uri = {
        .uri = "/api/v1/data",
        .method = HTTP_GET,
        .handler = data_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &data_get_uri);

    // /* URI handler for getting web server files */
    // httpd_uri_t common_get_uri = {
    //     .uri = "/*",
    //     .method = HTTP_GET,
    //     .handler = rest_common_get_handler,
    //     .user_ctx = rest_context
    // };
    // httpd_register_uri_handler(server, &common_get_uri);

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}


static inline char* get_clients_address(sock_info_t *client) {
    static char address_str[128];
    char *res = NULL;
    // Convert ip address to string
//     if (client->source_addr.ss_family == PF_INET) {
//         res = inet_ntoa_r(((struct sockaddr_in *)client->source_addr.sin_addr, address_str, sizeof(address_str) - 1);
//     }
// #ifdef CONFIG_LWIP_IPV6
//     else if (client->source_addr.ss_family == PF_INET6) {
//         res = inet6_ntoa_r(((struct sockaddr_in6 *)client->source_addr).sin6_addr, address_str, sizeof(address_str) - 1);
//     }
// #endif
    if (!res) {
        address_str[0] = '\0'; // Returns empty string if conversion didn't succeed
    }
    return address_str;
}

esp_err_t send_infopck(sock_info_t *client) {
    char rx_buffer[512];
    espmsg_EspResult response = {};
    response.operation = espmsg_EspMsgType_OpInfo;
    response.which_op = espmsg_EspResult_Info_tag;
    response.op.Info.version = 1;
    esp_fill_random(response.op.Info.challenge, sizeof(response.op.Info.challenge)-1);
    memcpy(client->challenge, response.op.Info.challenge, sizeof(client->challenge)-1);
    pb_ostream_t output = pb_ostream_from_buffer((pb_byte_t*)&rx_buffer, sizeof(rx_buffer));
    if (!pb_encode(&output, espmsg_EspResult_fields, &response))
    {
        ESP_LOGW(TAG, "Encoding failed: %s\n", PB_GET_ERROR(&output));
        socket_close(client);
        return ESP_FAIL;
    }
    ESP_LOGV(TAG, "Sending %d bytes", output.bytes_written);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, response.op.Info.challenge, sizeof(response.op.Info.challenge), ESP_LOG_VERBOSE);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buffer, output.bytes_written, ESP_LOG_VERBOSE);
    int32_t err = socket_send(client, rx_buffer, output.bytes_written);
    if (err < 0) {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        socket_close(client);
        return ESP_FAIL;
    }
    return ESP_OK;


}

esp_err_t send_response(sock_info_t *client, espmsg_EspReq_Msg *request) {
    char rx_buffer[512];
    espmsg_EspResult response = {};
    if (request->operation == espmsg_EspMsgType_OpLogin) {
        ESP_LOGI(TAG, "Sending Login Response");
        if (client->state != SOCK_STATE_AUTH) {
            ESP_LOGE(TAG, "Client not authenticated");
            socket_close(client);
            return ESP_FAIL;
        }
        response.operation = espmsg_EspMsgType_OpLogin;
        response.which_op = espmsg_ESPResult_LoginResult_result_tag;
        response.op.Login.success = true;
    } else if (request->operation == espmsg_EspMsgType_OpGetConfig) {
        ESP_LOGI(TAG, "Sending Config Response");
        response.operation = espmsg_EspMsgType_OpGetConfig;
        response.which_op = espmsg_EspResult_Config_tag;
        response.id = request->id;
        response.op.Config.channels = NUM_TARGETS;
        strncpy(response.op.Config.tz, deviceConfig.tz, sizeof(response.op.Config.tz));
        if (xSemaphoreTake(configMutex, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to take config mutex");
            socket_close(client);
            return ESP_FAIL;
        }
        for (int i = 0; i < NUM_TARGETS; i++ ) {
            response.op.Config.CfgConfig[i].enabled = channelConfig[i].enabled;
            response.op.Config.CfgConfig[i].lowTemp = channelConfig[i].lowTemp;
            response.op.Config.CfgConfig[i].highTemp = channelConfig[i].highTemp;
            response.op.Config.CfgConfig[i].minDuty = channelConfig[i].minDuty;
        }
        xSemaphoreGive(configMutex);
    } else if (request->operation == espmsg_EspMsgType_OPSetPerf || request->operation == espmsg_EspMsgType_OPGetStatus) {
        ESP_LOGI(TAG, "Sending Status Response");
        response.operation = espmsg_EspMsgType_OPGetStatus;
        response.which_op = espmsg_EspResult_Status_tag;
        response.id = request->id;
        target_t data;
        ESP_ERROR_CHECK(target_get_data(response.id, &data));
        response.op.Status.duty = data.duty;
        response.op.Status.temp = data.temp;
        response.op.Status.rpm = data.rpm;
        response.op.Status.load = data.load;
    } else {
        ESP_LOGE(TAG, "Unhandled Response %d", request->operation);
        return ESP_OK;
    }
    pb_ostream_t output = pb_ostream_from_buffer((pb_byte_t*)&rx_buffer, sizeof(rx_buffer));
    if (!pb_encode(&output, espmsg_EspResult_fields, &response))
    {
        ESP_LOGW(TAG, "Encoding failed: %s\n", PB_GET_ERROR(&output));
        socket_close(client);
        return ESP_FAIL;
    }
    int32_t err = socket_send(client, rx_buffer, output.bytes_written);
    if (err < 0) {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        socket_close(client);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Sent %d (%d) bytes", err, output.bytes_written);
    return ESP_OK;
}


esp_err_t process_perfpkt(sock_info_t *client, espmsg_EspReq_Msg *request) {
    ESP_LOGI(TAG, "Perf Packet: Channel: %d, Temp: %f, Load: %f", request->id, request->op.Perf.temp, request->op.Perf.load);
    ESP_ERROR_CHECK(target_send_temp(request->id, request->op.Perf.temp));
    ESP_ERROR_CHECK(target_send_load(request->id, request->op.Perf.load));
    vTaskDelay(pdMS_TO_TICKS(1));
    return send_response(client, request);
}

esp_err_t process_dutypkt(sock_info_t *client, espmsg_EspReq_Msg *request) {
    ESP_LOGI(TAG, "Duty Packet: Channel: %d, Duty: %f", request->id, request->op.Duty.duty);
    ESP_ERROR_CHECK(target_send_duty(request->id, request->op.Duty.duty));
    vTaskDelay(pdMS_TO_TICKS(1));
    return send_response(client, request);
}

esp_err_t process_loginpkt(sock_info_t *client, espmsg_EspReq_Msg *request) {
    ESP_LOGI(TAG, "Login Packet: User: %s, Pass: %s", request->op.Login.username, request->op.Login.token);
    xSemaphoreTake(configMutex, portMAX_DELAY);
    if (strcmp(request->op.Login.token, deviceConfig.agenttoken) == 0) {
        client->state = SOCK_STATE_AUTH;
    } else {
        ESP_LOGW(TAG, "Invalid Agent Token");
    }
    xSemaphoreGive(configMutex);

    return send_response(client, request);
}

esp_err_t process_statuspkt(sock_info_t *client, espmsg_EspReq_Msg *request) {
    ESP_LOGI(TAG, "Status Packet: Channel: %d", request->id);

    return send_response(client, request);
}

esp_err_t process_configpkt(sock_info_t *client, espmsg_EspReq_Msg *request) {
    ESP_LOGI(TAG, "Config Packet: Channel: %d", request->id);

    return send_response(client, request);
}

esp_err_t check_auth(sock_info_t *client) {
    if (client->state != SOCK_STATE_AUTH) {
        ESP_LOGE(TAG, "Client %s not authenticated", get_clients_address(client));
        socket_close(client);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t process_request(sock_info_t *client, espmsg_EspReq_Msg *request) {
        ESP_LOGI(TAG, "Processing request %d from %s", request->operation, get_clients_address(client));
        switch (request->operation)
        {
            case espmsg_EspMsgType_OpInvalid:
                ESP_LOGW(TAG, "Invalid Operation");
                socket_close(client);
                return ESP_OK;
                break;
            case espmsg_EspMsgType_OpInfo:
                ESP_LOGW(TAG, "Info Operation Recieved From Client. Closing");
                socket_close(client);
                return ESP_FAIL;
                break;
            case espmsg_EspMsgType_OpLogin:
                ESP_LOGI(TAG, "Login Packet");
                process_loginpkt(client, request);
                return ESP_OK;
                break;
            case espmsg_EspMsgType_OPSetPerf:
                if (check_auth(client) != ESP_OK) return ESP_FAIL;
                process_perfpkt(client, request);
                return ESP_OK;
                break;
            case espmsg_EspMsgType_OPSetDuty:
                if (check_auth(client) != ESP_OK) return ESP_FAIL;
                process_dutypkt(client, request);
                return ESP_OK;
                break;
            case espmsg_EspMsgType_OPGetStatus:
                if (check_auth(client) != ESP_OK) return ESP_FAIL;
                process_statuspkt(client, request);
                return ESP_OK;
                break;
            case espmsg_EspMsgType_OpGetConfig:
                //if (check_auth(client) != ESP_OK) return ESP_FAIL;
                process_configpkt(client, request);
                return ESP_OK;
                break;
        }
        ESP_LOGW(TAG, "Unknown Operation %d", request->operation);
        return ESP_OK;
}



esp_err_t socket_close(sock_info_t *client) {
    ESP_LOGI(TAG, "Closing Socket %d for %s", client->socket, get_clients_address(client));
    if (client->socket != -1) {
        close(client->socket);
        client->socket = -1;
        client->state = 0;
        client->pck_buf_len = 0;
        client->pck_len = 0;
    }
    return ESP_OK;
}

int socket_send(sock_info_t *client, const char * data, const size_t len)
{
    uint32_t buf;
    buf = htonl(len);
    ESP_LOGV(TAG, "Sending %d bytes to %s", len, get_clients_address(client));
    int err = send(client->socket, &buf, sizeof(buf), 0);
    if (err < 0 && err != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGE(TAG, "Error occurred during sending packet length: errno %d", errno);
        return -1;
    }

    int to_write = len;
    while (to_write > 0) {
        int written = send(client->socket, data + (len - to_write), to_write, 0);
        if (written < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGW(TAG, "Error occurred during sending: %d", errno);
            return -1;
        }
        to_write -= written;
    }
    return len;
}




esp_err_t sock_recv(sock_info_t *client) {
    if (client->pck_len == 0) {
        ESP_LOGV(TAG, "Pending Header");
        char buf[4];
        int len = recv(client->socket, &buf, sizeof(buf), MSG_PEEK);
        ESP_LOGV(TAG, "Header: Peeked %d", len);
        if (len >= sizeof(int32_t)) {
            len = recv(client->socket, &buf, sizeof(buf), 0);
            ESP_LOGV(TAG, "Header: Took %d", len);
            if (len == 0) {
                ESP_LOGI(TAG, "Get Header: Connection closed");
                socket_close(client);
                return ESP_OK;
            } else if (len < 0) {
                if (errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
                    ESP_LOGE(TAG, "Get Header: Error occurred during receiving: errno %d", errno);
                    socket_close(client);
                    return ESP_ERR_INVALID_STATE;
                }
                ESP_LOGE(TAG, "Get Header: Would Block %d", errno);
                return ESP_OK;
            } else {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_VERBOSE);
                client->pck_len = ntohl(*((int32_t*)buf));
                ESP_LOGV(TAG, "Header Said %d bytes data", client->pck_len);
                /* make sure pck_len isn't bigger than our buffer */
                if (client->pck_len > sizeof(client->pck_buf)) {
                    ESP_LOGE(TAG, "Get Header: Packet too big");
                    socket_close(client);
                    return ESP_ERR_INVALID_SIZE;
                }
            }
        } else if (len == 0) {
            ESP_LOGE(TAG, "Header Peek: Connection closed");
            socket_close(client);
            return ESP_OK;
        } else if (len < 0) {
            if (errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGE(TAG, "Header Peek: Error occurred during receiving: errno %d", errno);
                socket_close(client);
                return ESP_ERR_INVALID_STATE;
            }
            ESP_LOGE(TAG, "Header Peek: Would Block %d", errno);
            return ESP_OK;
        }
    }
    ESP_LOGV(TAG, "Client State Now: Want %d, Have %d", client->pck_len, client->pck_buf_len);
    if (client->pck_len > 0) {
        ESP_LOGV(TAG, "Pending Data Have: %d - Want additional: %d", client->pck_buf_len, client->pck_len - client->pck_buf_len);
        int len = recv(client->socket, &client->pck_buf + client->pck_buf_len, client->pck_len - client->pck_buf_len, 0);
        if (len == 0) {
            ESP_LOGE(TAG, "Get Data: Connection closed");
            socket_close(client);
            return ESP_OK;
        } else if (len < 0) {
            if (errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGE(TAG, "Get Data: Error occurred during receiving: errno %d", errno);
                socket_close(client);
                return ESP_ERR_INVALID_STATE;
            }
            return ESP_OK;
        } else {
            ESP_LOGV(TAG, "Took %d Data", len);
            client->pck_buf_len += len;
            if (client->pck_buf_len == client->pck_len) {
                ESP_LOGV(TAG, "Got Full Packet");
                espmsg_EspReq_Msg request = {};
                pb_istream_t input = pb_istream_from_buffer((pb_byte_t*)&client->pck_buf, client->pck_buf_len);
                if (!pb_decode(&input, espmsg_EspReq_Msg_fields, &request))
                {
                    ESP_LOGW(TAG, "Decoding failed: %s\n", PB_GET_ERROR(&input));
                    socket_close(client);
                    return ESP_ERR_INVALID_ARG;
                }
                process_request(client, &request);
                client->pck_len = 0;
                client->pck_buf_len = 0;
                return ESP_OK;
            } else {
                ESP_LOGV(TAG, "Got Partial Packet");
            }
        }
    }
    return ESP_OK;
}




void vTaskTCPServer(void* pvParameters) {
    int addr_family = AF_INET;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; i++) {
        client_info[i].socket = -1;
        client_info[i].state = 0;
        client_info[i].pck_buf_len = 0;
        client_info[i].pck_len = 0;
    }

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(PORT);
    ip_protocol = IPPROTO_IP;

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Marking the socket as non-blocking
    int flags = fcntl(listen_sock, F_GETFL);
    if (fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        ESP_LOGW(TAG, "Unable to set socket non blocking: %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        close(listen_sock);
        vTaskDelete(NULL);
        return;
    }
    while (1) {
        fd_set read_fds;
        int max_fd = listen_sock;
        FD_ZERO(&read_fds);
        FD_SET(listen_sock, &read_fds);
        for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; i++) {
            if (client_info[i].socket != -1) {
                FD_SET(client_info[i].socket, &read_fds);
                if (client_info[i].socket > max_fd) {
                    max_fd = client_info[i].socket;
                }
            }
        }
        ESP_LOGV(TAG, "Starting Select with max_fd %d", max_fd);
        int ret = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        ESP_LOGV(TAG, "Select Returned: %d", ret);
        switch (ret) {
            case -1:
                ESP_LOGE(TAG, "Select failed: %d", errno);
                break;
            case 0:
                ESP_LOGE(TAG, "Select timeout");
                break;
            default:
                if (FD_ISSET(listen_sock, &read_fds)) {
                    /* find the first available client struct */
                    sock_info_t *client = NULL;
                    for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; i++) {
                        if (client_info[i].socket == -1) {
                            client = &client_info[i];
                            break;
                        }
                    }
                    if (client == NULL) {
                        ESP_LOGE(TAG, "No more clients available");
                        int i = accept(listen_sock, NULL, 0);
                        if (i >= 0) {
                            close(i);
                        }
                        break;
                    }
                    ESP_LOGV(TAG, "About to Accept");
                    socklen_t socklen = sizeof(client->source_addr);
                    client->socket = accept(listen_sock, (struct sockaddr *)&client->source_addr, &socklen);
                    if (client->socket < 0) {
                        ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
                        break;
                    }
                    // Set tcp keepalive option
                    int keepAlive = 1;
                    int keepIdle = 5;
                    int keepInterval = 5;
                    int keepCount = 3;
                    setsockopt(client->socket, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
                    setsockopt(client->socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
                    setsockopt(client->socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
                    setsockopt(client->socket, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

                    flags = fcntl(client->socket, F_GETFL);
                    if (fcntl(client->socket, F_SETFL, flags | O_NONBLOCK) == -1) {
                        ESP_LOGW(TAG, "Unable to set socket %d non blocking %d", client->socket, errno);
                        socket_close(client);
                        break;
                    }                    
                    ESP_ERROR_CHECK(send_infopck(client));
                    ESP_LOGI(TAG, "Socket %d accepted from %s", client->socket, get_clients_address(client));
                }
                for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; i++) {
                    if (client_info[i].socket != -1 && FD_ISSET(client_info[i].socket, &read_fds)) {
                        ESP_LOGV(TAG, "Socket %d has data", client_info[i].socket);
                        sock_recv(&client_info[i]);
                    }
                }
                break;
        }

    }
    ESP_LOGI(TAG, "Shutting Down Network");
    if (listen_sock != -1) {
        ESP_LOGE(TAG, "Shutting down socket and restarting...");
        shutdown(listen_sock, 0);
        close(listen_sock);
    }
    vTaskDelete(NULL);
}