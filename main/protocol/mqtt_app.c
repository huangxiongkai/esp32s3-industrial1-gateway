#include <string.h>
#include "esp_log.h"
#include "mqtt_app.h"

//消费mqtt队列消息，调用 esp_mqtt_client_publish 发布到 云平台

static const char *TAG = "MQTT";

static esp_mqtt_client_handle_t s_client = NULL;  // MQTT 客户端句柄（内部保存）
//用来保存配置结构体
static gateway_config_t s_cfg;
//把状态传给状态机
static QueueHandle_t s_sys_event_queue = NULL;


//=================== 内部事件处理函数 ====================
//MQTT 客户端的所有事件都通过这个函数分发
//组件只负责转发，日志和业务逻辑由用户回调处理
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{

    esp_mqtt_event_handle_t event = event_data;//拿到客户端句柄

    switch ((esp_mqtt_event_id_t)event->event_id) 
    {

    //连接成功 → 订阅 OneNET 标准 topic
    //【上报回复】 + 【属性设置】
    case MQTT_EVENT_CONNECTED:
        char topic_reply[128];
        char topic_set[128];
        snprintf(topic_reply, sizeof(topic_reply),
            "$sys/%s/%s/thing/property/post/reply",
            s_cfg.mqtt_user, s_cfg.mqtt_client_id);
        snprintf(topic_set, sizeof(topic_set),
            "$sys/%s/%s/thing/property/set",
            s_cfg.mqtt_user, s_cfg.mqtt_client_id);
        esp_mqtt_client_subscribe(s_client, topic_reply, 0);
        esp_mqtt_client_subscribe(s_client, topic_set, 0);
    
    ESP_LOGI(TAG, "Connected, subscribed topics");

    //给状态机发送状态
    ESP_LOGW(TAG, "MQTT connected");
            if (s_sys_event_queue) {
                sys_event_t ev = SYS_EVENT_MQTT_CONNECTED;
                xQueueSend(s_sys_event_queue, &ev, 0);  // 满了直接丢
            }
    break;

    //平台接收成功 → 打印 msg_id
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "Published message, msg_id=%d", event->msg_id);
        break;

    //收到消息 → 打印主题和内容
    case MQTT_EVENT_DATA:

        //检查是否是 property/set 发来的
        if (strstr(event->topic, "/thing/property/set")) 
        {
            ESP_LOGI(TAG, "Received set command: %.*s", event->data_len, event->data);
            // M2 阶段先只打印日志，不回复
        }
        //打印应答
        //code 非 0 即拒收（如 2306 标识符不存在 / 2254 步长不符 / 2405 id 非法）
        else if (strstr(event->topic, "/thing/property/post/reply"))
        {
            ESP_LOGW(TAG, "POST_REPLY: %.*s", event->data_len, event->data);
        }
        break;

    //订阅成功确认
    case MQTT_EVENT_SUBSCRIBED:
        break;

    //断开连接
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Disconnected from broker");
        
        ESP_LOGW(TAG, "MQTT disconnected");
            if (s_sys_event_queue) {
                sys_event_t ev = SYS_EVENT_MQTT_DISCONNECTED;
                xQueueSend(s_sys_event_queue, &ev, 0);  // 满了直接丢
            }
        break;

    default:
        break;
    

    }
}

// ==================== 公开 API ====================
error_t mqtt_app_stop(void)
{
    s_sys_event_queue = NULL;          // 阻断 destroy 触发的 DISCONNECTED 事件（M5 踩坑）

    //停止并销毁MQTT客户端
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        //触发断联
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }

    ESP_LOGI(TAG, "MQTT app stopped");
    return ERR_OK;
    
}

// ==================== 公开 API ====================
// 启动 MQTT 客户端：配置 → 初始化 → 注册事件 → 启动
error_t mqtt_app_start(const gateway_config_t *cfg, QueueHandle_t sys_event_queue)
{
    // 支持重入：如果已有 client 先清理
    if (s_client) 
    {
        mqtt_app_stop();
    }

    s_sys_event_queue = sys_event_queue;   // 通知状态机连接/断开事件
    //保存缓存配置
    config_manager_get(&s_cfg);
    // MQTT 配置（从 缓存RAM 中读取）
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri      = s_cfg.mqtt_broker,           //云平台url
        .credentials.username    = s_cfg.mqtt_user,             //用户名
        .credentials.client_id   = s_cfg.mqtt_client_id,        //id
        .credentials.authentication.password = s_cfg.mqtt_pass, //token
        .network.disable_auto_reconnect = true,                 //关闭自动重连
        .network.timeout_ms = 3000,
    };
    

    //初始化客户端
    s_client = esp_mqtt_client_init(&mqtt_cfg);
        if (s_client == NULL) {
        ESP_LOGE(TAG, "MQTT client init failed");
        return ERR_MQTT_CONN;
    }

    //注册事件处理函数（ESP_EVENT_ANY_ID = 监听所有事件）
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    //启动客户端（内部创建任务处理网络通信）
    esp_mqtt_client_start(s_client);

    return ERR_OK;
}


/**
 * @brief 同步发布一条消息（供 cache_task 调用）
 * @param topic   发布主题
 * @param payload 消息内容（以 '\0' 结尾的 JSON 字符串）
 * @return ERR_OK 发布已受理；ERR_MQTT_CONN 客户端未就绪或发布失败
 * @note 线程安全：esp_mqtt_client_publish 内部投递给 mqtt 任务，可从任意任务调用
 */
error_t mqtt_app_publish(const char *topic, const char *payload)
{
    if (s_client == NULL)
    {
        return ERR_MQTT_CONN;
    }

    //2.成功返回 msg_id（QoS1 时 ≥1），失败返回 -1 
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);
    if (msg_id == -1)
    {
        return ERR_MQTT_CONN;
    }

    return ERR_OK;
}