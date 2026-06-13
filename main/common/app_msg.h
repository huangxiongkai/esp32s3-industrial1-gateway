#ifndef APP_MSG_H
#define APP_MSG_H
//==================== 队列结构体 ====================

#define MSG_TOPIC_MAX   128
#define MSG_PAYLOAD_MAX 512

typedef struct {
    char topic[MSG_TOPIC_MAX];        //MQTT主题
    char payload[MSG_PAYLOAD_MAX];    //MQTT消息内容
} app_msg_t;

#endif


