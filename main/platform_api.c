#include "platform_api.h"

static const char *TAG = "platform_api";

static size_t response_length = 0;
static char *response_buffer = NULL, *bearer_token = NULL, *host_url = NULL, *query = NULL;

static esp_http_client_config_t http_client_cfg;
static esp_http_client_handle_t http_client = NULL;

const char *http_events[] = {
    [HTTP_EVENT_ERROR] = "HTTP_EVENT_ERROR",
    [HTTP_EVENT_ON_CONNECTED] = "HTTP_EVENT_ON_CONNECTED",
    [HTTP_EVENT_HEADER_SENT] = "HTTP_EVENT_HEADER_SENT",
    [HTTP_EVENT_ON_HEADER] = "HTTP_EVENT_ON_HEADER",
    [HTTP_EVENT_ON_DATA] = "HTTP_EVENT_ON_DATA",
    [HTTP_EVENT_ON_FINISH] = "HTTP_EVENT_ON_FINISH",
    [HTTP_EVENT_DISCONNECTED] = "HTTP_EVENT_DISCONNECTED",
};

esp_err_t _caps_common_err(esp_err_t err_code, const char *tag, const char *err_file, int err_line, const char *func, const char *expression)
{
  ESP_LOGE(tag, "%s:%d on %s, %s failed with %s", err_file, err_line, func, expression, esp_err_to_name(err_code));
  return err_code;
}

esp_err_t http_event_handler(esp_http_client_event_t *http_client_event)
{
  switch (http_client_event->event_id)
  {
  case HTTP_EVENT_ERROR:
    __attribute__((fallthrough));
  case HTTP_EVENT_ON_CONNECTED:
    __attribute__((fallthrough));
  case HTTP_EVENT_HEADER_SENT:
    __attribute__((fallthrough));
  case HTTP_EVENT_ON_HEADER:
    __attribute__((fallthrough));
  case HTTP_EVENT_ON_FINISH:
    __attribute__((fallthrough));
  case HTTP_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "Received event: %s. Ignoring...", http_events[http_client_event->event_id]);
    break;
  case HTTP_EVENT_ON_DATA:
    ESP_LOGI(TAG, "Received event: %s. Processing...", http_events[http_client_event->event_id]);
    if (!esp_http_client_is_chunked_response(http_client_event->client))
    {
      int content_length = esp_http_client_get_content_length(http_client_event->client);
      if (!response_buffer)
      {
        response_buffer = (char *)malloc(content_length + 1);
        if (!response_buffer)
        {
          ESP_LOGE(TAG, "Failed to allocate memory for response buffer");
          ESP_LOGE(TAG, "Failed event: %s. Aborting...", http_events[http_client_event->event_id]);
          break;
        }
        memset(response_buffer, 0, content_length + 1);
      }

      memcpy(response_buffer + response_length, http_client_event->data, http_client_event->data_len);
      response_length += http_client_event->data_len;
      ESP_LOGI(TAG, "Success event: %s. Received %d bytes!", http_events[http_client_event->event_id], response_length);
      ESP_LOGI(TAG, "Buffer %s", response_buffer);
    }
    break;
  default:
    ESP_LOGW(TAG, "Unrecognized event: %d. Ignoring...", http_client_event->event_id);
    break;
  }
  return ESP_OK;
}

esp_err_t platform_api_set_token(const char *token)
{
  if (!token)
  {
    ESP_LOGE(TAG, "Token cannot be NULL");
    return ESP_ERR_INVALID_ARG;
  }

  if (strstr(token, "Bearer") != NULL)
  {
    ESP_LOGE(TAG, "Token cannot contain 'Bearer'");
    return ESP_ERR_INVALID_ARG;
  }

  if (bearer_token)
  {
    free(bearer_token);
    bearer_token = NULL;
  }

  if (asprintf(&bearer_token, "Bearer %s", token) <= 0)
  {
    ESP_LOGE(TAG, "Failed to allocate memory for bearer token");
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

esp_err_t platform_api_set_query_string(const char *field, const char *value)
{
  if (!field || !value)
  {
    ESP_LOGE(TAG, "Field or value cannot be NULL");
    return ESP_ERR_INVALID_ARG;
  }

  char *new_url = NULL;
  int bytes = 0;
  if (query)
  {
    bytes = asprintf(&new_url, "%s&%s=%s", query, field, value);
  }
  else
  {
    bytes = asprintf(&new_url, "%s=%s", field, value);
  }

  if (bytes <= 0)
  {
    ESP_LOGE(TAG, "Failed to allocate memory for new url");
    return ESP_ERR_NO_MEM;
  }

  if (query)
    free(query);
  query = new_url;
  return ESP_OK;
}

esp_err_t platform_api_perform_request(void)
{
  char *new_url = NULL;
  if (!host_url || !bearer_token)
  {
    if (!host_url)
      ESP_LOGE(TAG, "Host url missing!");
    if (!bearer_token)
      ESP_LOGE(TAG, "Bearer token missing!");

    ESP_LOGE(TAG, "Please initialize this library properly!");
    return ESP_ERR_INVALID_ARG;
  }

  if (query)
  {
    if (strstr(query, "type=device") && !strstr(query, "deviceId="))
    {
      ESP_LOGE(TAG, "If you fetching device settings, you need to provide the deviceId");
      return ESP_ERR_INVALID_ARG;
    }
  }

  if (response_buffer)
  {
    free(response_buffer);
    response_buffer = NULL;
    response_length = 0;
  }
  if (query)
  {
    asprintf(&new_url, "%s%s", host_url, query);
    http_client_cfg.url = new_url;
    ESP_LOGI(TAG, "HOST URL: %s", new_url);
  }
  else
  {
    ESP_LOGW(TAG, "No query has been specified. Please call 'platform_api_set_query_string' before calling 'platform_api_perform_request'");
    http_client_cfg.url = host_url;
  }

  http_client = esp_http_client_init(&http_client_cfg);
  if (!http_client)
  {
    ESP_LOGE(TAG, "Failed to allocate http_client");
  }
  if (response_buffer)
  {
    free(response_buffer);
    response_buffer = NULL;
    response_length = 0;
  }
  ERR_CHECK(TAG, esp_http_client_set_method(http_client, HTTP_METHOD_GET));
  ERR_CHECK(TAG, esp_http_client_set_header(http_client, "authorization", bearer_token));
  ERR_CHECK(TAG, esp_http_client_set_header(http_client, "content-type", "application/json"));
  ERR_CHECK(TAG, esp_http_client_perform(http_client));
  if (esp_http_client_get_status_code(http_client) != 200)
  {
    ESP_LOGE(TAG, "Request failed with status code: %d", esp_http_client_get_status_code(http_client));
    return ESP_FAIL;
  }
  if (new_url)
    free(new_url);
  http_client_cfg.url = NULL;
  if (query)
  {
    free(query);
    query = NULL;
  }

  return ESP_OK;
}

esp_err_t platform_api_cleanup(void)
{
  if (response_buffer)
  {
    free(response_buffer);
    response_buffer = NULL;
    response_length = 0;
  }
  if (host_url)
  {
    free(host_url);
    host_url = NULL;
  }
  if (bearer_token)
  {
    free(bearer_token);
    bearer_token = NULL;
  }
  if (http_client)
  {
    ERR_CHECK(TAG, esp_http_client_cleanup(http_client));
    http_client = NULL;
  }
  return ESP_OK;
}

esp_err_t platform_api_retrieve_val(const char *field, cjson_types_t type, bool json_parse, void *output, void **string)
{
  if (!response_buffer)
  {
    ESP_LOGE(TAG, "Please perform a request first!");
    return ESP_ERR_INVALID_ARG;
  }

  if (!json_parse)
  {
    output = (void *)strdup(response_buffer);
    return ESP_OK;
  }

  cJSON *json = cJSON_Parse(response_buffer);
  if (!json)
  {
    ESP_LOGE(TAG, "Failed to parse JSON response");
    return ESP_FAIL;
  }

  esp_err_t ret = ESP_OK;
  switch (type)
  {
  case INT:
    memcpy(output, &cJSON_GetObjectItem(json, field)->valueint, sizeof(int));
    break;
  case STRING:
    if (heap_caps_check_integrity_addr((intptr_t)(*string), false))
    {
      free(*string);
      output = NULL;
    }
    if (cJSON_GetObjectItem(json, field)->valuestring == NULL)
    {
      ESP_LOGE(TAG, "Field %s is NULL. Maybe try with INT", field);
      ret = ESP_ERR_INVALID_ARG;
      break;
    }
    if (asprintf((char **)string, "%s", cJSON_GetObjectItem(json, field)->valuestring) <= 0)
    {
      ESP_LOGE(TAG, "Failed to allocate memory for string");
      ret = ESP_ERR_NO_MEM;
      break;
    }
    break;
  default:
    ESP_LOGE(TAG, "Unsupported type");
    ret = ESP_ERR_NOT_SUPPORTED;
    break;
  }
  cJSON_Delete(json);
  return ret;
}

esp_err_t platform_api_init(const char *endpoint)
{
  if (response_buffer)
  {
    free(response_buffer);
    response_buffer = NULL;
    response_length = 0;
  }
  if (host_url)
  {
    free(host_url);
    host_url = NULL;
  }

  if (asprintf(&host_url, "%s?", endpoint) <= 0)
  {
    ESP_LOGE(TAG, "Failed to allocate memory for endpoint url");
    return ESP_ERR_NO_MEM;
  }

  http_client_cfg.disable_auto_redirect = false;
  http_client_cfg.max_redirection_count = 5;
  http_client_cfg.event_handler = http_event_handler;
  http_client_cfg.buffer_size_tx = 4096;
  return ESP_OK;
}