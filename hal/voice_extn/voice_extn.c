/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Not a contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef MULTI_VOICE_SESSION_ENABLED

#define LOG_TAG "voice_extn"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <sys/ioctl.h>
#include <sound/voice_params.h>

#include "audio_hw.h"
#include "voice.h"
#include "platform.h"
#include "platform_api.h"

#define AUDIO_PARAMETER_KEY_VSID        "vsid"
#define AUDIO_PARAMETER_KEY_CALL_STATE  "call_state"

#define VOICE2_VSID 0x10DC1000
#define VOLTE_VSID  0x10C02000
#define QCHAT_VSID  0x10803000
#define ALL_VSID    0xFFFFFFFF

/* Voice Session Indices */
#define VOICE2_SESS_IDX    (VOICE_SESS_IDX + 1)
#define VOLTE_SESS_IDX     (VOICE_SESS_IDX + 2)
#define QCHAT_SESS_IDX     (VOICE_SESS_IDX + 3)

/* Call States */
#define CALL_HOLD           (BASE_CALL_STATE + 2)
#define CALL_LOCAL_HOLD     (BASE_CALL_STATE + 3)

extern int start_call(struct audio_device *adev, audio_usecase_t usecase_id);
extern int stop_call(struct audio_device *adev, audio_usecase_t usecase_id);
int voice_extn_update_calls(struct audio_device *adev);

static bool is_valid_call_state(int call_state)
{
    if (call_state < CALL_INACTIVE || call_state > CALL_LOCAL_HOLD)
        return false;
    else
        return true;
}

static bool is_valid_vsid(uint32_t vsid)
{
    if (vsid == VOICE_VSID ||
        vsid == VOICE2_VSID ||
        vsid == VOLTE_VSID ||
        vsid == QCHAT_VSID)
        return true;
    else
        return false;
}

static audio_usecase_t voice_extn_get_usecase_for_session_idx(const int index)
{
    audio_usecase_t usecase_id = -1;

    switch(index) {
    case VOICE_SESS_IDX:
        usecase_id = USECASE_VOICE_CALL;
        break;

    case VOICE2_SESS_IDX:
        usecase_id = USECASE_VOICE2_CALL;
        break;

    case VOLTE_SESS_IDX:
        usecase_id = USECASE_VOLTE_CALL;
        break;

    case QCHAT_SESS_IDX:
        usecase_id = USECASE_QCHAT_CALL;
        break;

    default:
        ALOGE("%s: Invalid voice session index\n", __func__);
    }

    return usecase_id;
}

int voice_extn_get_active_session_id(struct audio_device *adev,
                                     uint32_t *session_id)
{
    struct voice_session *session = NULL;
    int i = 0;
    *session_id = 0;

    for (i = 0; i < MAX_VOICE_SESSIONS; i++) {
        session = &adev->voice.session[i];
        if(session->state.current == CALL_ACTIVE){
            *session_id = session->vsid;
            break;
        }
    }

    return 0;
}

int voice_extn_is_in_call(struct audio_device *adev, bool *in_call)
{
    struct voice_session *session = NULL;
    int i = 0;
    *in_call = false;

    for (i = 0; i < MAX_VOICE_SESSIONS; i++) {
        session = &adev->voice.session[i];
        if(session->state.current != CALL_INACTIVE){
            *in_call = true;
            break;
        }
    }

    return 0;
}

static int voice_extn_update_call_states(struct audio_device *adev,
                                    const uint32_t vsid, const int call_state)
{
    struct voice_session *session = NULL;
    int i = 0;
    bool is_in_call;

    for (i = 0; i < MAX_VOICE_SESSIONS; i++) {
        if (vsid == adev->voice.session[i].vsid) {
            session = &adev->voice.session[i];
            break;
        }
    }

    if (session) {
        session->state.new = call_state;
        voice_extn_is_in_call(adev, &is_in_call);
        if (is_in_call || adev->mode == AUDIO_MODE_IN_CALL) {
            /* Device routing is not triggered for voice calls on the subsequent
             * subs, Hence update the call states if voice call is already
             * active on other sub.
             */
            voice_extn_update_calls(adev);
        }
    } else {
        return -EINVAL;
    }

    return 0;

}

void voice_extn_init(struct audio_device *adev)
{
    adev->voice.session[VOICE_SESS_IDX].vsid =  VOICE_VSID;
    adev->voice.session[VOICE2_SESS_IDX].vsid = VOICE2_VSID;
    adev->voice.session[VOLTE_SESS_IDX].vsid =  VOLTE_VSID;
    adev->voice.session[QCHAT_SESS_IDX].vsid =  QCHAT_VSID;
}

int voice_extn_get_session_from_use_case(struct audio_device *adev,
                                               const audio_usecase_t usecase_id,
                                               struct voice_session **session)
{

    switch(usecase_id)
    {
    case USECASE_VOICE_CALL:
        *session = &adev->voice.session[VOICE_SESS_IDX];
        break;

    case USECASE_VOICE2_CALL:
        *session = &adev->voice.session[VOICE2_SESS_IDX];
        break;

    case USECASE_VOLTE_CALL:
        *session = &adev->voice.session[VOLTE_SESS_IDX];
        break;

    case USECASE_QCHAT_CALL:
        *session = &adev->voice.session[QCHAT_SESS_IDX];
        break;

    default:
        ALOGE("%s: Invalid usecase_id:%d\n", __func__, usecase_id);
        *session = NULL;
        return -EINVAL;
    }

    return 0;
}

int voice_extn_update_calls(struct audio_device *adev)
{
    int i = 0;
    audio_usecase_t usecase_id = 0;
    enum voice_lch_mode lch_mode;
    struct voice_session *session = NULL;
    int fd = 0;
    int ret = 0;

    ALOGD("%s: enter:", __func__);

    for (i = 0; i < MAX_VOICE_SESSIONS; i++) {
        usecase_id = voice_extn_get_usecase_for_session_idx(i);
        session = &adev->voice.session[i];
        ALOGV("%s: cur_state=%d new_state=%d vsid=%x",
              __func__, session->state.current, session->state.new, session->vsid);

        switch(session->state.new)
        {
        case CALL_ACTIVE:
            switch(session->state.current)
            {
            case CALL_INACTIVE:
                ALOGD("%s: INACTIVE ->ACTIVE vsid:%x", __func__, session->vsid);
                ret = start_call(adev, usecase_id);
                if(ret < 0) {
                    ALOGE("%s: voice_start_call() failed for usecase: %d\n",
                          __func__, usecase_id);
                }
                session->state.current = session->state.new;
                break;

            case CALL_HOLD:
                ALOGD("%s: HOLD ->ACTIVE vsid:%x", __func__, session->vsid);
                session->state.current = session->state.new;
                break;

            case CALL_LOCAL_HOLD:
                ALOGD("%s: LOCAL_HOLD ->ACTIVE vsid:%x", __func__, session->vsid);
                lch_mode = VOICE_LCH_STOP;
                if (pcm_ioctl(session->pcm_tx, SNDRV_VOICE_IOCTL_LCH, &lch_mode) < 0) {
                    ALOGE("LOCAL_HOLD ->ACTIVE failed");
                } else {
                    session->state.current = session->state.new;
                }
                break;

            default:
                ALOGV("%s: CALL_ACTIVE cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        case CALL_INACTIVE:
            switch(session->state.current)
            {
            case CALL_ACTIVE:
            case CALL_HOLD:
            case CALL_LOCAL_HOLD:
                ALOGD("%s: ACTIVE/HOLD/LOCAL_HOLD ->INACTIVE vsid:%x", __func__, session->vsid);
                ret = stop_call(adev, usecase_id);
                if(ret < 0) {
                    ALOGE("%s: voice_end_call() failed for usecase: %d\n",
                          __func__, usecase_id);
                }
                session->state.current = session->state.new;
                break;

            default:
                ALOGV("%s: CALL_INACTIVE cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        case CALL_HOLD:
            switch(session->state.current)
            {
            case CALL_ACTIVE:
                ALOGD("%s: CALL_ACTIVE ->HOLD vsid:%x", __func__, session->vsid);
                session->state.current = session->state.new;
                break;

            case CALL_LOCAL_HOLD:
                ALOGD("%s: CALL_LOCAL_HOLD ->HOLD vsid:%x", __func__, session->vsid);
                lch_mode = VOICE_LCH_STOP;
                if (pcm_ioctl(session->pcm_tx, SNDRV_VOICE_IOCTL_LCH, &lch_mode) < 0) {
                    ALOGE("LOCAL_HOLD ->HOLD failed");
                } else {
                    session->state.current = session->state.new;
                }
                break;

            default:
                ALOGV("%s: CALL_HOLD cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        case CALL_LOCAL_HOLD:
            switch(session->state.current)
            {
            case CALL_ACTIVE:
            case CALL_HOLD:
                ALOGD("%s: ACTIVE/CALL_HOLD ->LOCAL_HOLD vsid:%x", __func__, session->vsid);
                lch_mode = VOICE_LCH_START;
                if (pcm_ioctl(session->pcm_tx, SNDRV_VOICE_IOCTL_LCH, &lch_mode) < 0) {
                    ALOGE("LOCAL_HOLD ->HOLD failed");
                } else {
                    session->state.current = session->state.new;
                }
                break;

            default:
                ALOGV("%s: CALL_LOCAL_HOLD cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        default:
            break;
        } //end out switch loop
    } //end for loop

    return ret;
}

int voice_extn_set_parameters(struct audio_device *adev,
                              struct str_parms *parms)
{
    char *str;
    int value;
    int ret = 0;

    ALOGV("%s: enter: %s", __func__, str_parms_to_str(parms));

    ret = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_VSID, &value);
    if (ret >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_VSID);
        int vsid = value;
        int call_state = -1;
        ret = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_CALL_STATE, &value);
        if (ret >= 0) {
            call_state = value;
            //validate callstate
        } else {
            ALOGE("%s: call_state key not found", __func__);
            ret = -EINVAL;
            goto done;
        }

        if (is_valid_vsid(vsid) && is_valid_call_state(call_state)) {
            pthread_mutex_lock(&adev->lock);
            voice_extn_update_call_states(adev, vsid, call_state);
            pthread_mutex_unlock(&adev->lock);
        } else {
            ALOGE("%s: invalid vsid or call_state", __func__);
            ret = -EINVAL;
            goto done;
        }
    } else {
        ALOGD("%s: Not handled here", __func__);
    }

done:
    ALOGV("%s: exit with code(%d)", __func__, ret);
    return ret;
}

#endif