#include "patrol_fault_stop.h"
#include <string.h>

enum { STOP_SHUTDOWN, STOP_ZERO_LEFT, STOP_ZERO_RIGHT, STOP_READ_STATUS, STOP_DONE };

// 根据步骤返回唯一允许访问的对象，保持所有写入只涉及撤使能和行走零速，不写调参对象。
static uint16_t stop_index(uint8_t step)
{
    return step == STOP_SHUTDOWN ? 0x6040U : step == STOP_ZERO_LEFT ? 0x60FFU :
        step == STOP_ZERO_RIGHT ? 0x68FFU : 0x6041U;
}

// 完成一个安全写步骤。shutdown 无应答时也会有界转入状态读取，避免已执行却丢 ACK 的节点永远无法确认。
static void advance_write(PatrolFaultStopNode* node, uint8_t number, uint32_t now)
{
    node->step = node->step == STOP_SHUTDOWN ?
        (number == 2U ? STOP_ZERO_LEFT : STOP_READ_STATUS) : (uint8_t)(node->step + 1U);
    node->pending = 0U;
    node->attempts = 0U;
    node->at_ms = (uint16_t)now;
}

// 清空独立故障停机上下文；调用者负责确保不是在未确认撤使能时用于清锁。
void PatrolFaultStop_Init(PatrolFaultStop* stop) { memset(stop, 0, sizeof(*stop)); }

// 登记首个停机起点，随后重复故障不续期。掩码只选择本构建实际编译的电机节点。
void PatrolFaultStop_Begin(PatrolFaultStop* stop, uint8_t nodes, uint32_t now)
{
    if (stop->active || stop->required_mask) return;
    stop->required_mask = nodes & 3U;
    stop->started_ms = now;
    stop->active = stop->required_mask != 0U;
    stop->nodes[0].at_ms = stop->nodes[1].at_ms = (uint16_t)now;
}

// 独立处理左右节点的漏应答。仅安全命令可重试；锁存期限过后仍保持运动禁止并继续停机尝试。
void PatrolFaultStop_Tick(PatrolFaultStop* stop, uint32_t now)
{
    if (!stop->active) return;
    if (now - stop->started_ms >= PATROL_FAULT_STOP_LATCH_MS) stop->latch_due = 1U;
    for (uint8_t i = 0U; i < 2U; ++i) {
        PatrolFaultStopNode* n = &stop->nodes[i];
        if (!n->pending || (uint16_t)((uint16_t)now - n->at_ms) < PATROL_FAULT_STOP_TRANSACTION_MS) continue;
        n->pending = 0U;
        n->at_ms = (uint16_t)now;
        if (n->step == STOP_READ_STATUS) {
            if (n->attempts >= 3U || stop->latch_due) {
                n->step = STOP_SHUTDOWN;
                n->attempts = 0U;
            }
        } else if (n->attempts >= 3U) advance_write(n, (uint8_t)(i + 1U), now);
    }
}

// 先分别提交 Node 2、Node 1 的 shutdown，之后才推进零速和状态读取；单个节点最多一条在途 SDO。
uint8_t PatrolFaultStop_Take(PatrolFaultStop* stop, uint32_t now, PatrolCanFrame* frame)
{
    if (!stop->active) return 0U;
    for (uint8_t pass = 0U; pass < 2U; ++pass) {
        for (uint8_t order = 0U; order < 2U; ++order) {
            uint8_t i = (uint8_t)(1U - order), number = (uint8_t)(i + 1U);
            PatrolFaultStopNode* n = &stop->nodes[i];
            if (!(stop->required_mask & (1U << i)) || n->step == STOP_DONE || n->pending ||
                (int16_t)((uint16_t)now - n->at_ms) < 0 || (pass == 0U && n->step != STOP_SHUTDOWN)) continue;
            uint16_t index = stop_index(n->step);
            *frame = n->step == STOP_SHUTDOWN ? PatrolCanopen_SdoWrite16(number, index, 0U, 6U) :
                n->step == STOP_READ_STATUS ? PatrolCanopen_SdoRead(number, index, 0U) :
                PatrolCanopen_SdoWrite32(number, index, 0U, 0);
            n->pending = 1U;
            n->at_ms = (uint16_t)now;
            ++n->attempts;
            return 1U;
        }
    }
    return 0U;
}

// 提交失败不视作已执行；延后至少 10 ms，再由独立调度重试，不占满主控安全任务。
void PatrolFaultStop_TxFailed(PatrolFaultStop* stop, uint8_t number, uint32_t now)
{
    if (number < 1U || number > 2U) return;
    PatrolFaultStopNode* n = &stop->nodes[number - 1U];
    n->pending = 0U;
    n->at_ms = (uint16_t)(now + 10U);
    /* HAL 拒绝提交的帧没有进入驱动器，不消耗应答重试次数。 */
    if (n->attempts) --n->attempts;
}

// 只接受匹配本节点、对象、子索引及长度的应答；写 ACK 本身不能当作撤使能完成。
uint8_t PatrolFaultStop_OnFrame(PatrolFaultStop* stop, const PatrolCanFrame* frame, uint32_t now)
{
    if (!stop->active || frame->id < 0x581U || frame->id > 0x582U || frame->dlc < 4U) return 0U;
    uint8_t number = (uint8_t)(frame->id - 0x580U);
    PatrolFaultStopNode* n = &stop->nodes[number - 1U];
    uint16_t index = stop_index(n->step);
    if (!n->pending || frame->data[1] != (uint8_t)index || frame->data[2] != (uint8_t)(index >> 8) ||
        frame->data[3] != 0U) return 0U;
    if (frame->data[0] == 0x80U && frame->dlc == 8U) {
        n->pending = 0U;
        if (n->step != STOP_READ_STATUS) advance_write(n, number, now);
        else { n->step = STOP_SHUTDOWN; n->attempts = 0U; n->at_ms = (uint16_t)(now + 40U); }
        return 1U;
    }
    if (n->step != STOP_READ_STATUS) {
        if (frame->data[0] != 0x60U || frame->dlc != 8U) return 0U;
        advance_write(n, number, now);
        return 1U;
    }
    int32_t status; uint8_t width;
    if (!PatrolCanopen_ParseSdoValue(frame, number, index, 0U, &status, &width) || width != 2U) return 0U;
    n->pending = 0U;
    n->attempts = 0U;
    if (((uint16_t)status & 4U) == 0U) {
        n->step = STOP_DONE;
        stop->disabled_mask |= (uint8_t)(1U << (number - 1U));
        if ((stop->disabled_mask & stop->required_mask) == stop->required_mask) stop->active = 0U;
    } else {
        /* shutdown 的 ACK 可以早于真实撤使能数百毫秒。期限内只轮询状态，
         * 不反复改写已确认的 shutdown；超期锁存后才节流重试安全写入。 */
        n->step = stop->latch_due ? STOP_SHUTDOWN : STOP_READ_STATUS;
        n->at_ms = (uint16_t)(now + (stop->latch_due ? 100U : 20U));
    }
    return 1U;
}
