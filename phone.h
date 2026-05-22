/*
 * Crimson OS - Phone Subsystem Header
 * Cellular Voice, SMS, Data Stack
 */

#ifndef _CRIMSON_PHONE_H
#define _CRIMSON_PHONE_H

#include <crimson/types.h>

#define PHONE_VERSION           "1.0.0"

/* Call states */
#define CALL_STATE_IDLE         0
#define CALL_STATE_DIALING      1
#define CALL_STATE_RINGING      2
#define CALL_STATE_ACTIVE       3
#define CALL_STATE_HOLDING      4
#define CALL_STATE_HANGUP       5
#define CALL_STATE_BUSY         6
#define CALL_STATE_ERROR        7
#define CALL_STATE_ENCRYPTED    8

/* Call direction */
#define CALL_DIR_OUTGOING       1
#define CALL_DIR_INCOMING       2

/* SMS constants */
#define SMS_MAX_LEN             1600
#define SMS_MAX_THREADS         256
#define SMS_MAX_PER_THREAD      10000
#define SMS_STATUS_PENDING      1
#define SMS_STATUS_SENT         2
#define SMS_STATUS_DELIVERED    3
#define SMS_STATUS_FAILED       4
#define SMS_STATUS_READ         5

/* Encryption modes */
#define PHONE_ENCRYPT_NONE      0
#define PHONE_ENCRYPT_ZRTP      1

/* Emergency numbers */
#define EMERGENCY_POLICE        911
#define EMERGENCY_MEDICAL       112
#define EMERGENCY_FIRE          999

/* Opaque types — full definitions live in phone.c */
typedef struct phone_call    phone_call_t;
typedef struct phone_contact phone_contact_t;
typedef struct phone_state   phone_state_t;
typedef struct sms_message   sms_message_t;
typedef struct sms_thread    sms_thread_t;

/* Init & status */
void phone_init(void* modem);
void phone_shutdown(void);
void phone_get_status(void);

/* Voice calls */
int phone_dial(const char* number, uint32_t encrypted);
void phone_answer(void);
void phone_hangup(void);
void phone_hold(void);
void phone_resume(void);
void phone_mute(uint32_t enable);
void phone_speaker(uint32_t enable);
void phone_merge_calls(void);
void phone_swap_calls(void);
int phone_send_dtmf(char tone);

/* Emergency */
void phone_dial_emergency(void);
void phone_dial_number(uint32_t emergency_num);

/* SMS */
int phone_send_sms(const char* number, const char* text);
void phone_receive_sms(const char* number, const char* text);
void phone_mark_read(uint32_t thread_id);
void phone_delete_thread(uint32_t thread_id);

/* Contacts */
int phone_add_contact(const char* name, const char* number,
                      const char* email, uint32_t favourite);
void phone_remove_contact(uint32_t id);
void phone_list_contacts(void);
void phone_toggle_favorite(uint32_t id);
void phone_toggle_block(uint32_t id);

/* Network */
void phone_set_airplane_mode(uint32_t enable);
void phone_set_data_enabled(uint32_t enable);
void phone_set_roaming(uint32_t enable);
uint32_t phone_get_signal_strength(void);
const char* phone_get_network_type_str(uint32_t type);

/* ZRTP encryption */
void phone_set_zrtp_enabled(uint32_t enable);
void phone_zrtp_derive_keys(const uint8_t* shared_secret,
                             uint8_t* enc_key, uint8_t* auth_key);

/* Call recording */
void phone_set_recording_enabled(uint32_t enable);

/* Voicemail */
void phone_check_voicemail(void);
void phone_call_voicemail(void);

/* Utility */
void phone_normalize_number(const char* input, char* output, size_t out_len);
int phone_is_emergency(const char* number);
void phone_simulate_incoming_call(const char* number);
void phone_simulate_sms(const char* number, const char* text);

#endif /* _CRIMSON_PHONE_H */
