#ifndef TMUXREMOTE_IAM_H_
#define TMUXREMOTE_IAM_H_

#include <modules/iam/nm_iam.h>
#include <modules/iam/nm_iam_state.h>
#include <nabto/nabto_device.h>
#include <nn/log.h>
#include <pthread.h>

struct nm_fs;

/* Max paired users we track for change detection */
#define TMUXREMOTE_IAM_MAX_USERS 32

struct tmuxremote_iam {
    struct nn_log* logger;
    struct nm_iam iam;
    NabtoDevice* device;
    char* iamStateFile;
    struct nm_fs* file;
    pthread_mutex_t saveMutex;
    pthread_cond_t saveCond;
    pthread_t saveThread;
    bool saveThreadStarted;
    bool saveStop;
    struct nm_iam_state* pendingState;

    /* Track paired usernames for detecting new pairings */
    char* pairedUsers[TMUXREMOTE_IAM_MAX_USERS];
    int pairedUserCount;
};

void tmuxremote_iam_init(struct tmuxremote_iam* niam, NabtoDevice* device,
                         struct nm_fs* file, const char* iamStateFile,
                         struct nn_log* logger);
void tmuxremote_iam_deinit(struct tmuxremote_iam* niam);

bool tmuxremote_iam_load_state(struct tmuxremote_iam* niam);

void tmuxremote_iam_create_default_state(NabtoDevice* device, struct nm_fs* file,
                                         const char* filename, struct nn_log* logger,
                                         const char* username, const char* name);

void tmuxremote_iam_create_open_state(NabtoDevice* device, struct nm_fs* file,
                                      const char* filename, struct nn_log* logger);

char* tmuxremote_iam_create_invite_pairing_string(struct nm_iam* iam,
                                                  const char* productId,
                                                  const char* deviceId,
                                                  const char* username);

bool tmuxremote_iam_check_access(struct tmuxremote_iam* niam,
                                 NabtoDeviceCoapRequest* request,
                                 const char* action);

bool tmuxremote_iam_check_access_ref(struct tmuxremote_iam* niam,
                                     NabtoDeviceConnectionRef ref,
                                     const char* action);

#endif
