#ifndef NABTOSHELL_IAM_H_
#define NABTOSHELL_IAM_H_

#include <modules/iam/nm_iam.h>
#include <modules/iam/nm_iam_state.h>
#include <nabto/nabto_device.h>
#include <nn/log.h>
#include <pthread.h>

struct nm_fs;

struct nabtoshell_iam {
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
};

void nabtoshell_iam_init(struct nabtoshell_iam* niam, NabtoDevice* device,
                         struct nm_fs* file, const char* iamStateFile,
                         struct nn_log* logger);
void nabtoshell_iam_deinit(struct nabtoshell_iam* niam);

bool nabtoshell_iam_load_state(struct nabtoshell_iam* niam);

void nabtoshell_iam_create_default_state(NabtoDevice* device, struct nm_fs* file,
                                         const char* filename, struct nn_log* logger,
                                         const char* username);

void nabtoshell_iam_create_open_state(NabtoDevice* device, struct nm_fs* file,
                                      const char* filename, struct nn_log* logger);

char* nabtoshell_iam_create_pairing_string(struct nm_iam* iam,
                                           const char* productId,
                                           const char* deviceId);

char* nabtoshell_iam_create_invite_pairing_string(struct nm_iam* iam,
                                                  const char* productId,
                                                  const char* deviceId,
                                                  const char* username);

bool nabtoshell_iam_check_access(struct nabtoshell_iam* niam,
                                 NabtoDeviceCoapRequest* request,
                                 const char* action);

bool nabtoshell_iam_check_access_ref(struct nabtoshell_iam* niam,
                                     NabtoDeviceConnectionRef ref,
                                     const char* action);

#endif
