// Copyright 2020-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "doorbell_ui_i18n.h"

#include "beken_ui.h"
#include "doorbell_ui.h"

#include "lvgl.h"

typedef struct {
    const char *home_settings;
    const char *home_week;
    const char *home_notify;
    const char *home_notify_body;
    const char *home_call;
    const char *home_records;
    const char *home_property;
    const char *home_monitor;
    const char *home_scenes;
    const char *home_access;

    const char *set_sec_cam;
    const char *set_motion;
    const char *set_chime;
    const char *set_night;
    const char *set_vol;
    const char *set_fw_val;
    const char *set_check;
    const char *set_restart;
    const char *set_reset;
    const char *set_general;
    const char *set_lang;
    const char *set_provision;

    const char *prov_title;
    const char *prov_instr;
    const char *prov_info;
    const char *prov_name;
    const char *prov_mac;
    const char *prov_model;
    const char *prov_proto;

    const char *call_name;
    const char *call_status;
    const char *call_decline;
    const char *call_answer;
    const char *call_locked;
    const char *call_unlocked;
} db_ui_pack_t;

static const db_ui_pack_t s_pack_zh = {
    .home_settings   = "设置",
    .home_week       = "星期一",
    .home_notify     = "通知",
    .home_notify_body = "暂无新消息",
    .home_call       = "通话",
    .home_records    = "记录",
    .home_property   = "物业",
    .home_monitor    = "监控",
    .home_scenes     = "场景",
    .home_access     = "门禁",

    .set_sec_cam     = "摄像与对讲",
    .set_motion      = "移动检测",
    .set_chime       = "门铃响铃",
    .set_night       = "夜视(IR)",
    .set_vol         = "扬声器音量",
    .set_fw_val      = "固件版本：V2.4.1",
    .set_check       = "升级",
    .set_restart     = "重启设备",
    .set_reset       = "恢复出厂",
    .set_general     = "通用设置",
    .set_lang        = "语言",
    .set_provision   = "配网",

    .prov_title      = "蓝牙配网",
    .prov_instr      = "打开App，点击添加设备，扫描此二维码通过蓝牙完成配网。",
    .prov_info       = "设备信息 (QR码内容)",
    .prov_name       = "设备名称",
    .prov_mac        = "MAC地址",
    .prov_model      = "设备型号",
    .prov_proto      = "协议",

    .call_name       = "主入口",
    .call_status     = "访客正在呼叫...",
    .call_decline    = "拒绝",
    .call_answer     = "接听",
    .call_locked     = "已锁定",
    .call_unlocked   = "已开锁",
};

static const db_ui_pack_t s_pack_en = {
    .home_settings   = "Settings",
    .home_week       = "Monday",
    .home_notify     = "Notices",
    .home_notify_body = "No new messages",
    .home_call       = "Call",
    .home_records    = "Records",
    .home_property   = "Property",
    .home_monitor    = "Monitor",
    .home_scenes     = "Scenes",
    .home_access     = "Access",

    .set_sec_cam     = "Camera & Talk",
    .set_motion      = "Motion detect",
    .set_chime       = "Doorbell chime",
    .set_night       = "Night vision (IR)",
    .set_vol         = "Speaker volume",
    .set_fw_val      = "Firmware: V2.4.1",
    .set_check       = "Upgrade",
    .set_restart     = "Restart",
    .set_reset       = "Factory reset",
    .set_general     = "General",
    .set_lang        = "Language",
    .set_provision   = "Provision",

    .prov_title      = "BLE Provisioning",
    .prov_instr      = "Open the App, tap Add Device, and scan this QR code to finish BLE provisioning.",
    .prov_info       = "Device info (QR payload)",
    .prov_name       = "Device name",
    .prov_mac        = "MAC address",
    .prov_model      = "Model",
    .prov_proto      = "Protocol",

    .call_name       = "Main entrance",
    .call_status     = "Visitor is calling...",
    .call_decline    = "Decline",
    .call_answer     = "Answer",
    .call_locked     = "Locked",
    .call_unlocked   = "Unlocked",
};

static uint8_t s_lang = DB_UI_LANG_ZH;

static const db_ui_pack_t *db_ui_pack(void)
{
    return (s_lang == DB_UI_LANG_EN) ? &s_pack_en : &s_pack_zh;
}

static void db_ui_set_label(lv_obj_t *obj, const char *text)
{
    if (obj && lv_obj_is_valid(obj) && text)
    {
        lv_label_set_text(obj, text);
    }
}

uint8_t doorbell_ui_lang(void)
{
    return s_lang;
}

void doorbell_ui_set_lang(uint8_t lang)
{
    s_lang = (lang == DB_UI_LANG_EN) ? DB_UI_LANG_EN : DB_UI_LANG_ZH;
    doorbell_ui_apply_lang();
}

const char *doorbell_ui_str_lock(bool unlocked)
{
    const db_ui_pack_t *p = db_ui_pack();
    return unlocked ? p->call_unlocked : p->call_locked;
}

const char *doorbell_ui_str_prov_title(void)
{
    return db_ui_pack()->prov_title;
}

void doorbell_ui_apply_lang(void)
{
    const db_ui_pack_t *p = db_ui_pack();
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    db_ui_set_label(ui->home_st_set_lbl, p->home_settings);
    db_ui_set_label(ui->home_home_week, p->home_week);
    db_ui_set_label(ui->home_notify_hdr, p->home_notify);
    db_ui_set_label(ui->home_notify_body, p->home_notify_body);
    db_ui_set_label(ui->home_lb_call, p->home_call);
    db_ui_set_label(ui->home_lb_records, p->home_records);
    db_ui_set_label(ui->home_lb_property, p->home_property);
    db_ui_set_label(ui->home_lb_monitor, p->home_monitor);
    db_ui_set_label(ui->home_lb_scenes, p->home_scenes);
    db_ui_set_label(ui->home_lb_access, p->home_access);

    db_ui_set_label(ui->settings_sec_a, p->set_sec_cam);
    db_ui_set_label(ui->settings_lbl_motion, p->set_motion);
    db_ui_set_label(ui->settings_lbl_chime, p->set_chime);
    db_ui_set_label(ui->settings_lbl_night, p->set_night);
    db_ui_set_label(ui->settings_lbl_vol, p->set_vol);
    db_ui_set_label(ui->settings_fw_val, p->set_fw_val);
    db_ui_set_label(ui->settings_fw_upd_label, p->set_check);
    db_ui_set_label(ui->settings_btn_restart_label, p->set_restart);
    db_ui_set_label(ui->settings_btn_reset_label, p->set_reset);
    db_ui_set_label(ui->settings_sec_lang, p->set_general);
    db_ui_set_label(ui->settings_lbl_lang, p->set_lang);
    db_ui_set_label(ui->settings_btn_provision_label, p->set_provision);

    db_ui_set_label(ui->provisioning_qr_subtitle, p->prov_title);
    db_ui_set_label(ui->provisioning_qr_instr, p->prov_instr);
    db_ui_set_label(ui->provisioning_info_hdr, p->prov_info);
    db_ui_set_label(ui->provisioning_k_name, p->prov_name);
    db_ui_set_label(ui->provisioning_k_mac, p->prov_mac);
    db_ui_set_label(ui->provisioning_k_model, p->prov_model);
    db_ui_set_label(ui->provisioning_k_proto, p->prov_proto);

    db_ui_set_label(ui->incoming_call_caller_name, p->call_name);
    db_ui_set_label(ui->incoming_call_call_status, p->call_status);
    db_ui_set_label(ui->incoming_call_dec_lbl, p->call_decline);
    db_ui_set_label(ui->incoming_call_ans_lbl, p->call_answer);
    db_ui_set_label(ui->incoming_call_lock_lbl, doorbell_ui_str_lock(doorbell_ui_door_is_unlocked()));
}
