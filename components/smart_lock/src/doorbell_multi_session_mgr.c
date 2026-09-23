#include <os/mem.h>
#include <os/os.h>
#include <os/str.h>

#include "doorbell_multi_session_mgr.h"
#include "app_display.h"
#include "bk_network_service/bk_ntwk_cs2_service/ntwk_cs2_service.h"

#define TAG "db-session-mgr"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#ifndef CONFIG_CS2_MAX_NUM_CONNECTIONS
#define CONFIG_CS2_MAX_NUM_CONNECTIONS 4
#endif

typedef struct
{
    ntwk_session_id_t sid;
    uint8_t connected;
    uint8_t camera_vote;
    uint8_t audio_vote;
    uint8_t lcd_vote;
    uint8_t speaker_owner;
} doorbell_session_ctx_t;

typedef struct
{
    beken_mutex_t lock;
    uint8_t lock_inited;
    doorbell_session_ctx_t clients[CONFIG_CS2_MAX_NUM_CONNECTIONS];
    uint32_t camera_vote_count;
    uint32_t audio_vote_count;
    uint32_t lcd_vote_count;
    ntwk_session_id_t speaker_owner_sid;
} doorbell_multi_session_mgr_t;

static doorbell_multi_session_mgr_t s_mgr;

static doorbell_session_ctx_t *doorbell_multi_session_find_locked(ntwk_session_id_t sid)
{
    for (int i = 0; i < CONFIG_CS2_MAX_NUM_CONNECTIONS; i++)
    {
        if (s_mgr.clients[i].connected && s_mgr.clients[i].sid == sid)
        {
            return &s_mgr.clients[i];
        }
    }

    return NULL;
}

static doorbell_session_ctx_t *doorbell_multi_session_alloc_locked(ntwk_session_id_t sid)
{
    doorbell_session_ctx_t *client = doorbell_multi_session_find_locked(sid);
    if (client)
    {
        return client;
    }

    for (int i = 0; i < CONFIG_CS2_MAX_NUM_CONNECTIONS; i++)
    {
        if (!s_mgr.clients[i].connected)
        {
            os_memset(&s_mgr.clients[i], 0, sizeof(s_mgr.clients[i]));
            s_mgr.clients[i].sid = sid;
            s_mgr.clients[i].connected = BK_TRUE;
            return &s_mgr.clients[i];
        }
    }

    return NULL;
}

bk_err_t doorbell_multi_session_mgr_init(void)
{
    if (s_mgr.lock_inited)
    {
        return BK_OK;
    }

    os_memset(&s_mgr, 0, sizeof(s_mgr));
    s_mgr.speaker_owner_sid = NTWK_INVALID_SESSION_ID;
    if (rtos_init_mutex(&s_mgr.lock) != BK_OK)
    {
        return BK_FAIL;
    }
    s_mgr.lock_inited = BK_TRUE;

    return BK_OK;
}

bk_err_t doorbell_multi_session_mgr_deinit(void)
{
    if (!s_mgr.lock_inited)
    {
        return BK_OK;
    }

    rtos_deinit_mutex(&s_mgr.lock);
    os_memset(&s_mgr, 0, sizeof(s_mgr));
    s_mgr.speaker_owner_sid = NTWK_INVALID_SESSION_ID;

    return BK_OK;
}

bk_err_t doorbell_multi_session_on_connect(ntwk_session_id_t sid)
{
    bk_err_t ret = BK_OK;

    doorbell_multi_session_mgr_init();
    rtos_lock_mutex(&s_mgr.lock);
    if (doorbell_multi_session_alloc_locked(sid) == NULL)
    {
        ret = BK_FAIL;
    }
    rtos_unlock_mutex(&s_mgr.lock);

    LOGI("%s sid:%d ret:%d\n", __func__, sid, ret);
    return ret;
}

bk_err_t doorbell_multi_session_camera_on(ntwk_session_id_t sid, camera_parameters_t *parameters)
{
    bool need_start = false;
    bk_err_t ret = BK_OK;

    doorbell_multi_session_mgr_init();
    rtos_lock_mutex(&s_mgr.lock);
    doorbell_session_ctx_t *client = doorbell_multi_session_alloc_locked(sid);
    if (client == NULL)
    {
        rtos_unlock_mutex(&s_mgr.lock);
        return BK_FAIL;
    }

    if (!client->camera_vote)
    {
        need_start = (s_mgr.camera_vote_count == 0);
        client->camera_vote = BK_TRUE;
        s_mgr.camera_vote_count++;
    }
    rtos_unlock_mutex(&s_mgr.lock);

    if (need_start)
    {
        ret = doorbell_camera_turn_on(parameters);
        if (ret == BK_OK)
        {
            ret = doorbell_video_transfer_turn_on();
            if (ret != BK_OK)
            {
                doorbell_camera_turn_off();
            }
        }

        if (ret != BK_OK)
        {
            rtos_lock_mutex(&s_mgr.lock);
            client = doorbell_multi_session_find_locked(sid);
            if (client && client->camera_vote)
            {
                client->camera_vote = BK_FALSE;
                if (s_mgr.camera_vote_count > 0)
                {
                    s_mgr.camera_vote_count--;
                }
            }
            rtos_unlock_mutex(&s_mgr.lock);
            return ret;
        }
    }

    ntwk_cs2_set_video_subscribe(sid, true);
    return BK_OK;
}

bk_err_t doorbell_multi_session_camera_off(ntwk_session_id_t sid)
{
    bool need_stop = false;
    bk_err_t ret = BK_OK;

    doorbell_multi_session_mgr_init();
    rtos_lock_mutex(&s_mgr.lock);
    doorbell_session_ctx_t *client = doorbell_multi_session_find_locked(sid);
    if (client && client->camera_vote)
    {
        client->camera_vote = BK_FALSE;
        if (s_mgr.camera_vote_count > 0)
        {
            s_mgr.camera_vote_count--;
        }
        need_stop = (s_mgr.camera_vote_count == 0);
    }
    rtos_unlock_mutex(&s_mgr.lock);

    ntwk_cs2_set_video_subscribe(sid, false);

    if (need_stop)
    {
        doorbell_video_transfer_turn_off();
        ret = doorbell_camera_turn_off();
    }

    return ret;
}

bk_err_t doorbell_multi_session_audio_on(ntwk_session_id_t sid, audio_parameters_t *parameters, bool *owner)
{
    bool need_start = false;
    bool become_owner = false;
    bk_err_t ret = BK_OK;

    if (owner)
    {
        *owner = false;
    }

    doorbell_multi_session_mgr_init();
    rtos_lock_mutex(&s_mgr.lock);
    doorbell_session_ctx_t *client = doorbell_multi_session_alloc_locked(sid);
    if (client == NULL)
    {
        rtos_unlock_mutex(&s_mgr.lock);
        return BK_FAIL;
    }

    if (!client->audio_vote)
    {
        need_start = (s_mgr.audio_vote_count == 0);
        client->audio_vote = BK_TRUE;
        s_mgr.audio_vote_count++;
    }

    if (s_mgr.speaker_owner_sid == NTWK_INVALID_SESSION_ID)
    {
        s_mgr.speaker_owner_sid = sid;
        client->speaker_owner = BK_TRUE;
        become_owner = true;
    }
    rtos_unlock_mutex(&s_mgr.lock);

    if (need_start)
    {
#ifdef CONFIG_VOICE_SERVICE
        ret = doorbell_audio_turn_on(parameters);
#else
        ret = BK_FAIL;
#endif
        if (ret != BK_OK)
        {
            rtos_lock_mutex(&s_mgr.lock);
            client = doorbell_multi_session_find_locked(sid);
            if (client && client->audio_vote)
            {
                client->audio_vote = BK_FALSE;
                client->speaker_owner = BK_FALSE;
                if (s_mgr.audio_vote_count > 0)
                {
                    s_mgr.audio_vote_count--;
                }
                if (s_mgr.speaker_owner_sid == sid)
                {
                    s_mgr.speaker_owner_sid = NTWK_INVALID_SESSION_ID;
                }
            }
            rtos_unlock_mutex(&s_mgr.lock);
            return ret;
        }
    }

    ntwk_cs2_set_audio_subscribe(sid, true);
    if (become_owner)
    {
        ntwk_cs2_set_speaker_owner(sid, true);
        if (owner)
        {
            *owner = true;
        }
    }

    return BK_OK;
}

bk_err_t doorbell_multi_session_audio_off(ntwk_session_id_t sid)
{
    bool need_stop = false;
    bk_err_t ret = BK_OK;

    doorbell_multi_session_mgr_init();
    rtos_lock_mutex(&s_mgr.lock);
    doorbell_session_ctx_t *client = doorbell_multi_session_find_locked(sid);
    if (client && client->audio_vote)
    {
        client->audio_vote = BK_FALSE;
        if (s_mgr.audio_vote_count > 0)
        {
            s_mgr.audio_vote_count--;
        }
        need_stop = (s_mgr.audio_vote_count == 0);
    }

    if (client && client->speaker_owner)
    {
        client->speaker_owner = BK_FALSE;
        if (s_mgr.speaker_owner_sid == sid)
        {
            s_mgr.speaker_owner_sid = NTWK_INVALID_SESSION_ID;
        }
    }
    rtos_unlock_mutex(&s_mgr.lock);

    ntwk_cs2_set_audio_subscribe(sid, false);
    ntwk_cs2_set_speaker_owner(sid, false);

    if (need_stop)
    {
#ifdef CONFIG_VOICE_SERVICE
        ret = doorbell_audio_turn_off();
#else
        ret = BK_FAIL;
#endif
    }

    return ret;
}

bk_err_t doorbell_multi_session_lcd_on(ntwk_session_id_t sid)
{
    bool need_start = false;
    bk_err_t ret = BK_OK;

    doorbell_multi_session_mgr_init();
    rtos_lock_mutex(&s_mgr.lock);
    doorbell_session_ctx_t *client = doorbell_multi_session_alloc_locked(sid);
    if (client == NULL)
    {
        rtos_unlock_mutex(&s_mgr.lock);
        return BK_FAIL;
    }
    if (!client->lcd_vote)
    {
        need_start = (s_mgr.lcd_vote_count == 0);
        client->lcd_vote = BK_TRUE;
        s_mgr.lcd_vote_count++;
    }
    rtos_unlock_mutex(&s_mgr.lock);

    if (need_start)
    {
        ret = doorbell_display_turn_on(app_display_board_config_get());
        if (ret != BK_OK)
        {
            rtos_lock_mutex(&s_mgr.lock);
            client = doorbell_multi_session_find_locked(sid);
            if (client && client->lcd_vote)
            {
                client->lcd_vote = BK_FALSE;
                if (s_mgr.lcd_vote_count > 0)
                {
                    s_mgr.lcd_vote_count--;
                }
            }
            rtos_unlock_mutex(&s_mgr.lock);
        }
    }

    return ret;
}

bk_err_t doorbell_multi_session_lcd_off(ntwk_session_id_t sid)
{
    bool need_stop = false;
    bk_err_t ret = BK_OK;

    doorbell_multi_session_mgr_init();
    rtos_lock_mutex(&s_mgr.lock);
    doorbell_session_ctx_t *client = doorbell_multi_session_find_locked(sid);
    if (client && client->lcd_vote)
    {
        client->lcd_vote = BK_FALSE;
        if (s_mgr.lcd_vote_count > 0)
        {
            s_mgr.lcd_vote_count--;
        }
        need_stop = (s_mgr.lcd_vote_count == 0);
    }
    rtos_unlock_mutex(&s_mgr.lock);

    if (need_stop)
    {
        ret = doorbell_display_turn_off();
    }

    return ret;
}

bk_err_t doorbell_multi_session_on_disconnect(ntwk_session_id_t sid)
{
    LOGI("%s sid:%d\n", __func__, sid);

    doorbell_multi_session_camera_off(sid);
    doorbell_multi_session_audio_off(sid);
    doorbell_multi_session_lcd_off(sid);

    rtos_lock_mutex(&s_mgr.lock);
    doorbell_session_ctx_t *client = doorbell_multi_session_find_locked(sid);
    if (client)
    {
        os_memset(client, 0, sizeof(*client));
        client->sid = NTWK_INVALID_SESSION_ID;
    }
    rtos_unlock_mutex(&s_mgr.lock);

    return BK_OK;
}
