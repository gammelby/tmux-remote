#include "nabtoshell_iam.h"

#include <apps/common/json_config.h>
#include <apps/common/random_string.h>
#include <apps/common/string_file.h>

#include <modules/iam/nm_iam_serializer.h>

#include <cjson/cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define LOGM "nabtoshell_iam"
#define NEWLINE "\n"

static void nabtoshell_iam_state_changed(struct nm_iam* iam, void* userData);
static bool load_iam_config(struct nabtoshell_iam* niam);
static void save_iam_state(struct nm_fs* file, const char* filename,
                           struct nm_iam_state* state, struct nn_log* logger);
static void* save_iam_state_worker(void* data);

void nabtoshell_iam_init(struct nabtoshell_iam* niam, NabtoDevice* device,
                         struct nm_fs* file, const char* iamStateFile,
                         struct nn_log* logger)
{
    memset(niam, 0, sizeof(struct nabtoshell_iam));
    niam->logger = logger;
    niam->iamStateFile = strdup(iamStateFile);
    niam->file = file;
    niam->device = device;
    pthread_mutex_init(&niam->saveMutex, NULL);
    pthread_cond_init(&niam->saveCond, NULL);
    niam->saveThreadStarted = false;
    niam->saveStop = false;
    niam->pendingState = NULL;

    if (pthread_create(&niam->saveThread, NULL, save_iam_state_worker, niam) == 0) {
        niam->saveThreadStarted = true;
    } else {
        printf("Failed to start IAM state saver worker" NEWLINE);
    }

    nm_iam_init(&niam->iam, device, logger);
    load_iam_config(niam);
    nm_iam_set_state_changed_callback(&niam->iam, nabtoshell_iam_state_changed, niam);
}

void nabtoshell_iam_deinit(struct nabtoshell_iam* niam)
{
    nm_iam_deinit(&niam->iam);

    pthread_mutex_lock(&niam->saveMutex);
    niam->saveStop = true;
    pthread_cond_signal(&niam->saveCond);
    pthread_mutex_unlock(&niam->saveMutex);

    if (niam->saveThreadStarted) {
        pthread_join(niam->saveThread, NULL);
        niam->saveThreadStarted = false;
    }

    if (niam->pendingState != NULL) {
        nm_iam_state_free(niam->pendingState);
        niam->pendingState = NULL;
    }
    pthread_cond_destroy(&niam->saveCond);
    pthread_mutex_destroy(&niam->saveMutex);
    free(niam->iamStateFile);
}

static void nabtoshell_iam_state_changed(struct nm_iam* iam, void* userData)
{
    (void)iam;
    struct nabtoshell_iam* niam = userData;
    struct nm_iam_state* state = nm_iam_dump_state(&niam->iam);
    if (state == NULL || niam->iamStateFile == NULL) {
        return;
    }

    if (!niam->saveThreadStarted) {
        save_iam_state(niam->file, niam->iamStateFile, state, niam->logger);
        nm_iam_state_free(state);
        return;
    }

    pthread_mutex_lock(&niam->saveMutex);
    if (niam->pendingState != NULL) {
        nm_iam_state_free(niam->pendingState);
    }
    niam->pendingState = state;
    pthread_cond_signal(&niam->saveCond);
    pthread_mutex_unlock(&niam->saveMutex);
}

static void save_iam_state(struct nm_fs* file, const char* filename,
                           struct nm_iam_state* state, struct nn_log* logger)
{
    (void)logger;
    char* str = NULL;
    if (nm_iam_serializer_state_dump_json(state, &str)) {
        enum nm_fs_error ec = file->write_file(file->impl, filename,
                                               (uint8_t*)str, strlen(str));
        if (ec != NM_FS_OK) {
            printf("Failed to save IAM state" NEWLINE);
        }
    }
    nm_iam_serializer_string_free(str);
}

static void* save_iam_state_worker(void* data)
{
    struct nabtoshell_iam* niam = data;
    while (true) {
        struct nm_iam_state* state = NULL;
        pthread_mutex_lock(&niam->saveMutex);
        while (niam->pendingState == NULL && !niam->saveStop) {
            pthread_cond_wait(&niam->saveCond, &niam->saveMutex);
        }
        if (niam->pendingState != NULL) {
            state = niam->pendingState;
            niam->pendingState = NULL;
        } else if (niam->saveStop) {
            pthread_mutex_unlock(&niam->saveMutex);
            break;
        }
        pthread_mutex_unlock(&niam->saveMutex);

        if (state != NULL) {
            save_iam_state(niam->file, niam->iamStateFile, state, niam->logger);
            nm_iam_state_free(state);
        }
    }
    return NULL;
}

void nabtoshell_iam_create_default_state(NabtoDevice* device, struct nm_fs* file,
                                         const char* filename, struct nn_log* logger,
                                         const char* username)
{
    struct nm_iam_state* state = nm_iam_state_new();
    nm_iam_state_set_password_invite_pairing(state, true);
    nm_iam_state_set_local_open_pairing(state, false);
    nm_iam_state_set_password_open_pairing(state, false);
    nm_iam_state_set_local_initial_pairing(state, false);
    nm_iam_state_set_friendly_name(state, "NabtoShell");

    /* Create the initial user with invite pairing credentials */
    struct nm_iam_user* user = nm_iam_state_user_new(username);
    nm_iam_state_user_set_role(user, "Owner");
    char* pwd = random_password(12);
    nm_iam_state_user_set_password(user, pwd);
    free(pwd);

    char* sct = NULL;
    nabto_device_create_server_connect_token(device, &sct);
    nm_iam_state_user_set_sct(user, sct);
    nabto_device_string_free(sct);

    nm_iam_state_add_user(state, user);

    save_iam_state(file, filename, state, logger);
    nm_iam_state_free(state);
}

void nabtoshell_iam_create_open_state(NabtoDevice* device, struct nm_fs* file,
                                      const char* filename, struct nn_log* logger)
{
    struct nm_iam_state* state = nm_iam_state_new();
    nm_iam_state_set_open_pairing_role(state, "Owner");
    nm_iam_state_set_friendly_name(state, "NabtoShell");
    nm_iam_state_set_local_initial_pairing(state, false);
    nm_iam_state_set_local_open_pairing(state, false);
    char* openPwd = random_password(12);
    nm_iam_state_set_password_open_password(state, openPwd);
    free(openPwd);
    nm_iam_state_set_password_open_pairing(state, true);
    nm_iam_state_set_password_invite_pairing(state, false);

    char* sct = NULL;
    nabto_device_create_server_connect_token(device, &sct);
    nm_iam_state_set_password_open_sct(state, sct);
    nabto_device_string_free(sct);

    save_iam_state(file, filename, state, logger);
    nm_iam_state_free(state);
}

bool nabtoshell_iam_load_state(struct nabtoshell_iam* niam)
{
    if (!json_config_exists(niam->file, niam->iamStateFile)) {
        printf("IAM state file not found: %s" NEWLINE, niam->iamStateFile);
        printf("Run nabtoshell-agent --init to create initial configuration." NEWLINE);
        return false;
    }

    bool status = true;
    char* str = NULL;
    if (!string_file_load(niam->file, niam->iamStateFile, &str)) {
        NN_LOG_ERROR(niam->logger, LOGM,
                     "Failed to load IAM state file: %s", niam->iamStateFile);
        return false;
    }

    struct nm_iam_state* is = nm_iam_state_new();
    if (is == NULL) {
        free(str);
        return false;
    }

    if (!nm_iam_serializer_state_load_json(is, str, niam->logger)) {
        NN_LOG_ERROR(niam->logger, LOGM,
                     "Failed to parse IAM state. Try deleting %s", niam->iamStateFile);
        status = false;
    }

    if (status && !nm_iam_load_state(&niam->iam, is)) {
        NN_LOG_ERROR(niam->logger, LOGM, "Failed to load state into IAM module");
        status = false;
    }

    if (!status) {
        nm_iam_state_free(is);
    }
    free(str);
    return status;
}

static bool load_iam_config(struct nabtoshell_iam* niam)
{
    struct nm_iam_configuration* conf = nm_iam_configuration_new();

    /* Pairing policy */
    {
        struct nm_iam_policy* p = nm_iam_configuration_policy_new("Pairing");
        struct nm_iam_statement* s = nm_iam_configuration_policy_create_statement(p, NM_IAM_EFFECT_ALLOW);
        nm_iam_configuration_statement_add_action(s, "IAM:GetPairing");
        nm_iam_configuration_statement_add_action(s, "IAM:PairingPasswordInvite");
        nm_iam_configuration_add_policy(conf, p);
    }

    /* Terminal policy */
    {
        struct nm_iam_policy* p = nm_iam_configuration_policy_new("Terminal");
        struct nm_iam_statement* s = nm_iam_configuration_policy_create_statement(p, NM_IAM_EFFECT_ALLOW);
        nm_iam_configuration_statement_add_action(s, "Terminal:Connect");
        nm_iam_configuration_statement_add_action(s, "Terminal:ListSessions");
        nm_iam_configuration_statement_add_action(s, "Terminal:CreateSession");
        nm_iam_configuration_statement_add_action(s, "Terminal:Resize");
        nm_iam_configuration_statement_add_action(s, "Terminal:Status");
        nm_iam_configuration_add_policy(conf, p);
    }

    /* Unpaired role: can only pair */
    {
        struct nm_iam_role* r = nm_iam_configuration_role_new("Unpaired");
        nm_iam_configuration_role_add_policy(r, "Pairing");
        nm_iam_configuration_add_role(conf, r);
    }

    /* Owner role: full access */
    {
        struct nm_iam_role* r = nm_iam_configuration_role_new("Owner");
        nm_iam_configuration_role_add_policy(r, "Pairing");
        nm_iam_configuration_role_add_policy(r, "Terminal");
        nm_iam_configuration_add_role(conf, r);
    }

    nm_iam_configuration_set_unpaired_role(conf, "Unpaired");

    return nm_iam_load_configuration(&niam->iam, conf);
}

char* nabtoshell_iam_create_invite_pairing_string(struct nm_iam* iam,
                                                  const char* productId,
                                                  const char* deviceId,
                                                  const char* username)
{
    struct nm_iam_state* state = nm_iam_dump_state(iam);
    if (state == NULL) {
        return NULL;
    }

    /* Find the user and get their password and SCT */
    struct nm_iam_user* user = NULL;
    NN_LLIST_FOREACH(user, &state->users) {
        if (user->username != NULL && strcmp(user->username, username) == 0) {
            break;
        }
    }

    if (user == NULL || user->password == NULL || user->sct == NULL) {
        nm_iam_state_free(state);
        return NULL;
    }

    char* pairStr = (char*)calloc(1, 512);
    if (pairStr != NULL) {
        snprintf(pairStr, 511, "p=%s,d=%s,u=%s,pwd=%s,sct=%s",
                 productId, deviceId, username,
                 user->password, user->sct);
    }

    nm_iam_state_free(state);
    return pairStr;
}

bool nabtoshell_iam_check_access(struct nabtoshell_iam* niam,
                                 NabtoDeviceCoapRequest* request,
                                 const char* action)
{
    NabtoDeviceConnectionRef ref = nabto_device_coap_request_get_connection_ref(request);
    return nm_iam_check_access(&niam->iam, ref, action, NULL);
}

bool nabtoshell_iam_check_access_ref(struct nabtoshell_iam* niam,
                                     NabtoDeviceConnectionRef ref,
                                     const char* action)
{
    return nm_iam_check_access(&niam->iam, ref, action, NULL);
}
