#include "mqtt_qmtrecv.h"

#include <string.h>

static const char *skip_space(const char *value)
{
    while ((*value == ' ') || (*value == '\t')) value++;
    return value;
}

static char decode_outer_escape(char value)
{
    switch (value)
    {
        case 'b': return '\b';
        case 'f': return '\f';
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        default: return value;
    }
}

uint8_t MqttQmtRecv_DecodeLine(const char *line,
                               char *topic, size_t topic_capacity,
                               char *payload, size_t payload_capacity,
                               MqttQmtRecvFrame_t *frame)
{
    const char *prefix;
    const char *topic_start;
    const char *topic_end;
    const char *cursor;
    const char *json_start = 0;
    size_t topic_length;
    size_t used = 0U;
    uint16_t scan_limit = 0U;
    uint16_t depth = 0U;
    uint8_t in_string = 0U;
    uint8_t json_escape = 0U;
    uint8_t outer_escaped = 0U;

    if ((line == 0) || (topic == 0) || (payload == 0) || (frame == 0) ||
        (topic_capacity < 2U) || (payload_capacity < 2U)) return 0U;
    memset(frame, 0, sizeof(*frame));
    topic[0] = '\0';
    payload[0] = '\0';

    prefix = strstr(line, "+QMTRECV:");
    if (prefix == 0) return 0U;
    topic_start = strchr(prefix, '"');
    if (topic_start == 0) return 0U;
    topic_start++;
    topic_end = strchr(topic_start, '"');
    if (topic_end == 0) return 0U;
    topic_length = (size_t)(topic_end - topic_start);
    if ((topic_length == 0U) || (topic_length >= topic_capacity)) return 0U;
    memcpy(topic, topic_start, topic_length);
    topic[topic_length] = '\0';

    cursor = topic_end + 1;
    while ((*cursor != '\0') && (scan_limit++ < 48U))
    {
        if ((*cursor == '{') || (*cursor == '['))
        {
            json_start = cursor;
            break;
        }
        cursor++;
    }
    if (json_start == 0) return 0U;

    cursor = skip_space(json_start + 1);
    if ((cursor[0] == '\\') && (cursor[1] == '"')) outer_escaped = 1U;

    cursor = json_start;
    while (*cursor != '\0')
    {
        char value = *cursor++;
        if (outer_escaped && (value == '\\') && (*cursor != '\0'))
        {
            value = decode_outer_escape(*cursor++);
        }
        if (used >= payload_capacity - 1U) return 0U;
        payload[used++] = value;

        if (in_string)
        {
            if (json_escape) json_escape = 0U;
            else if (value == '\\') json_escape = 1U;
            else if (value == '"') in_string = 0U;
            continue;
        }
        if (value == '"') in_string = 1U;
        else if ((value == '{') || (value == '[')) depth++;
        else if ((value == '}') || (value == ']'))
        {
            if (depth == 0U) return 0U;
            depth--;
            if (depth == 0U)
            {
                frame->complete = 1U;
                break;
            }
        }
    }
    payload[used] = '\0';
    frame->topic_length = topic_length;
    frame->payload_length = used;
    frame->outer_escaped = outer_escaped;
    return (used > 0U) ? 1U : 0U;
}
