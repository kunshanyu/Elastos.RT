//=========================================================================
// Copyright (C) 2018 The Elastos Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//=========================================================================

#include <elspinlock.h>
#include "elcarrierapi.h"

#ifndef VALIDATE_NOT_NULL
#define VALIDATE_NOT_NULL(x) if (!x) { return E_INVALID_ARGUMENT; }
#endif

#ifndef CARRIER_NOT_READY
#define CARRIER_NOT_READY() if (!mIsReady) { return E_CARRIER_NOT_READY; }
#endif

#ifdef _android
#include <android/log.h>
#define CARRIER_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "ElCarrier", __VA_ARGS__)
#else
#define CARRIER_LOG printf
#endif

#define INVALID_PORT "0"

static SpinLock sCarrierInstanceLock;
ELAPI _CCarrier_GetInstance(
    /* [out] */ ICarrier** carrier)
{
    return Elastos::CCarrier::GetInstance(carrier);
}

_ELASTOS_NAMESPACE_BEGIN

static void OnConnectionChanged(
    /* [in] */ ElaCarrier *w,
    /* [in] */ ElaConnectionStatus status,
    /* [in] */ void *context)
{
    switch (status) {
    case ElaConnectionStatus_Connected:
        CARRIER_LOG("[OnConnectionChanged] Connected to carrier network.\n");
        break;

    case ElaConnectionStatus_Disconnected:
        CARRIER_LOG("[OnConnectionChanged] Disconnect from carrier network.\n");
        break;

    default:
        CARRIER_LOG("[OnConnectionChanged] Error!!! Got unknown connection status %d.\n", status);
    }
    CCarrier::GetLocalInstance()->DistributeOnConnectionChanged(status == 0);
}

static void OnFriendConnetionChanged(
    /* [in] */ ElaCarrier *w,
    /* [in] */ const char *friendid,
    /* [in] */ ElaConnectionStatus status,
    /* [in] */ void *context)
{
    switch (status) {
    case ElaConnectionStatus_Connected:
        CARRIER_LOG("[OnFriendConnetionChanged] Friend[%s] connection changed to be online\n", friendid);
        break;

    case ElaConnectionStatus_Disconnected:
        CARRIER_LOG("[OnFriendConnetionChanged] Friend[%s] connection changed to be offline.\n", friendid);
        break;

    default:
        CARRIER_LOG("[OnFriendConnetionChanged] Error!!! Got unknown connection status %d.\n", status);
    }
    CCarrier::GetLocalInstance()->DistributeOnFriendConnetionChanged(String(friendid), status == 0);
}

static void OnFriendRequest(
    /* [in] */ ElaCarrier *w,
    /* [in] */ const char *userid,
    /* [in] */ const ElaUserInfo *info,
    /* [in] */ const char *hello,
    /* [in] */ void *context)
{
    CARRIER_LOG("[OnFriendRequest] Friend request from user[%s] with HELLO: %s.\n",
           *info->name ? info->name : userid, hello);
    CARRIER_LOG("[OnFriendRequest] Reply use following commands:\n");
    CARRIER_LOG("[OnFriendRequest]   faccept %s\n", userid);
    CCarrier::GetLocalInstance()->DistributeOnFriendRequest(String(userid), String(hello));
}

const char *presence_name[] = {
    "none",    // None;
    "away",    // Away;
    "busy",    // Busy;
};

static const char *connection_name[] = {
    "online",
    "offline"
};

static void display_friend_info(
    /* [in] */ const ElaFriendInfo *fi)
{
    CARRIER_LOG("[display_friend_info]           ID: %s\n", fi->user_info.userid);
    CARRIER_LOG("[display_friend_info]         Name: %s\n", fi->user_info.name);
    CARRIER_LOG("[display_friend_info]  Description: %s\n", fi->user_info.description);
    CARRIER_LOG("[display_friend_info]       Gender: %s\n", fi->user_info.gender);
    CARRIER_LOG("[display_friend_info]        Phone: %s\n", fi->user_info.phone);
    CARRIER_LOG("[display_friend_info]        Email: %s\n", fi->user_info.email);
    CARRIER_LOG("[display_friend_info]       Region: %s\n", fi->user_info.region);
    CARRIER_LOG("[display_friend_info]        Label: %s\n", fi->label);
    CARRIER_LOG("[display_friend_info]     Presence: %s\n", presence_name[fi->presence]);
    CARRIER_LOG("[display_friend_info]   Connection: %s\n", connection_name[fi->status]);
}

static void OnInviteRequest(ElaCarrier *w, const char *from,
                            const void *data, size_t len, void *context)
{
    char service[32] = {0};
    char port[8] = {0};
    char *tmp = NULL;
    String user_id(from);
    String service_port;
    String invalid_port = String(INVALID_PORT);
    Boolean accept = FALSE;
    CCarrier *carrier = CCarrier::GetLocalInstance();

    if (carrier == NULL)
        goto return_exit;

    tmp = strchr((char*)data, ':');
    if (tmp == NULL)
        goto return_exit;

    CARRIER_LOG("Invite request from[%s] with data: %.*s.\n", from, (int)len, (char*)data);
    strncpy(service, (char*)data, tmp - (char*)data);
    strncpy(port, tmp + 1, len - (tmp - (char*)data + 1));
    service_port = port;
    carrier->DistributeOnPortForwardingRequest(user_id, service_port, &accept);
    if (!accept) {
        if (carrier->ReplyInvite(from, NULL, "refuse") < 0) {
            CARRIER_LOG("Reply invite request unsuccessfully.\n");
            goto distribute_exit;
        }

        goto return_exit;
    }

    // inform user when there is something wrong with session
    if (carrier->ReplyInvite(from, "ok", NULL) < 0) {
        goto distribute_exit;
    }

    carrier->SetPeerId(String(from));
    carrier->SetUsedService(service);
    carrier->SetLocalPort(port);
    goto return_exit;

distribute_exit:
    carrier->DistributeOnPortForwardingResult(user_id, service_port, invalid_port, E_CARRIER_ERROR);

return_exit:
    return;
}

static void OnInviteResponse(
    ElaCarrier *w,
    const char *friend_id,
    int status,
    const char *reason,
    const void *data,
    size_t len,
    void *context)
{
    CCarrier *carrier = CCarrier::GetLocalInstance();
    String user_id(friend_id);
    String local_port;
    String remote_port;
    Boolean change_mode = FALSE;

    if (carrier == NULL)
        goto return_exit;

    CARRIER_LOG("[OnInviteResponse] Invite response from friend[%s]: %s.\n", friend_id, (char*)data);

    if (status == 0) {
        ElaStreamType type = ElaStreamType_text;
        int options = 0;

        CARRIER_LOG("message within response: %.*s.\n", (int)len, (char*)data);

        if (carrier->NewSession(friend_id) == NULL) {
            CARRIER_LOG("Open port forwarding unsuccessfully.\n");
            goto clean_exit;
        }

        carrier->SetRunningMode(ACTIVE_MODE);
        options |= ELA_STREAM_RELIABLE;
        options |= ELA_STREAM_MULTIPLEXING;
        options |= ELA_STREAM_PORT_FORWARDING;

        if (carrier->AddStream(type, options) < 0) {
            CARRIER_LOG("Open port forwarding unsuccessfully.\n");
            change_mode = TRUE;
            goto close_exit;
        }

        carrier->SetPeerId(user_id);
        goto return_exit;
    } else {
        CARRIER_LOG("refused: %s.\n", reason);
        goto clean_exit;
    }

close_exit:
    carrier->CloseSession();

    if (change_mode)
        carrier->SetRunningMode(PASSIVE_MODE);

clean_exit:
    carrier->SetUsedService(NULL);
    carrier->SetLocalPort(NULL);
    carrier->SetRemotePort(NULL);

distribute_exit:
    local_port = carrier->GetLocalPort();
    remote_port = carrier->GetRemotePort();
    carrier->DistributeOnPortForwardingResult(user_id, local_port, remote_port, E_CARRIER_ERROR);

return_exit:
    return;
}

static void OnSessionRequest(ElaCarrier *w, const char *from,
            const char *sdp, size_t len, void *context)
{
    String remote_sdp(sdp, len);
    String user_id;
    String service;
    String local_port;
    String remote_port;
    ElaStreamType type = ElaStreamType_text;
    int options = 0;
    int stream_id;
    Boolean remove_stream = FALSE;
    CCarrier *carrier = CCarrier::GetLocalInstance();

    if (carrier == NULL)
        goto return_exit;

    if (carrier->NewSession(from) == NULL) {
        CARRIER_LOG("Open port forwarding unsuccessfully.\n");
        goto clean_exit;
    }

    // add service
    local_port = carrier->GetLocalPort();
    service = carrier->GetUsedService();
    if (carrier->AddService(service, "127.0.0.1", local_port) < 0) {
        CARRIER_LOG("Open port forwarding unsuccessfully.\n");
        // close session
        goto close_exit;
    }

    options |= ELA_STREAM_RELIABLE;
    options |= ELA_STREAM_MULTIPLEXING;
    options |= ELA_STREAM_PORT_FORWARDING;

    if ((stream_id = carrier->AddStream(type, options)) < 0) {
        CARRIER_LOG("Open port forwarding unsuccessfully.\n");
        // remove service, close session
        goto close_exit;
    }

    carrier->SetRemoteSdp(String(sdp));
    CARRIER_LOG("Session request from[%s] with SDP:\n%s.\n", from, remote_sdp.string());
    goto return_exit;

close_exit:
    carrier->CloseSession();

clean_exit:
    carrier->RemoveService();
    if (remove_stream) {
        carrier->RemoveStream(stream_id);
        carrier->SetRemoteSdp(String());
    }
    carrier->SetPeerId(String());
    carrier->SetUsedService(NULL);
    carrier->SetLocalPort(NULL);
    //carrier->SetRemotePort(NULL);

distribute_exit:
    user_id = from;
    remote_port = carrier->GetRemotePort();
    carrier->DistributeOnPortForwardingResult(user_id, local_port, remote_port, E_CARRIER_ERROR);

return_exit:
    return;
}

static void OnSessionRequestComplete(ElaSession *ws, int status,
                const char *reason, const char *sdp, size_t len, void *context)
{
    int rc = 0;
    int stream_id = 0;
    String remote_sdp(sdp, len);
    String user_id;
    String service_port;
    String invalid_port = String(INVALID_PORT);
    CCarrier *carrier = CCarrier::GetLocalInstance();

    if (carrier == NULL)
        goto return_exit;

    CARRIER_LOG("Session complete, status: %d, reason: %s.\n", status,
           reason ? reason : "null");

    if (status != 0)
        goto close_exit;

    carrier->SetRemoteSdp(remote_sdp);
    rc = carrier->StartSession();
    if (rc < 0)
        goto close_exit;
    else
        goto return_exit;

close_exit:
    stream_id = carrier->GetStreamId();
    carrier->RemoveStream(stream_id);
    carrier->CloseSession();

clean_exit:
    carrier->SetRunningMode(PASSIVE_MODE);
    carrier->SetUsedService(NULL);
    carrier->SetLocalPort(NULL);
    carrier->SetRemotePort(NULL);
    carrier->SetPeerId(String());

distribute_exit:
    user_id = carrier->GetPeerId();
    service_port = carrier->GetLocalPort();
    carrier->DistributeOnPortForwardingResult(user_id, service_port, invalid_port, E_CARRIER_ERROR);

return_exit:
    return;
}

static void OnStreamStateChanged(ElaSession *ws, int stream,
        ElaStreamState state, void *context)
{
    const char *state_name[] = {
        "raw",
        "initialized",
        "transport_ready",
        "connecting",
        "connected",
        "deactivated",
        "closed",
        "failed"
    };
    RUNNING_MODE running_mode;
    Boolean remove_service = FALSE;
    Boolean remove_stream = FALSE;
    Boolean change_mode = FALSE;
    Boolean is_successful = FALSE;
    String user_id;
    String local_port;
    String remote_port;
    ECode code = E_CARRIER_ERROR;
    CCarrier *carrier = CCarrier::GetLocalInstance();

    if (carrier == NULL)
        goto return_exit;

    running_mode = carrier->GetRunningMode();
    user_id = carrier->GetPeerId();
    remote_port = carrier->GetRemotePort();
    CARRIER_LOG("Stream [%d] state changed to: %s.\n", stream, state_name[state]);

    if (state == ElaStreamState_transport_ready) {
        if (running_mode == PASSIVE_MODE) {
            if (carrier->StartSession() < 0) {
                remove_service = TRUE;
                remove_stream = TRUE;
                goto close_exit;
            }
        }
        
        goto return_exit;
    }

    if (state == ElaStreamState_initialized) {
        if (running_mode == ACTIVE_MODE) {
            if (carrier->RequestSession() < 0) {
                CARRIER_LOG("Open port forwarding unsuccessfully.\n");
                change_mode = TRUE;
                remove_stream = TRUE;
                goto close_exit;
            } else {
                goto return_exit;
            }
        } else if (running_mode == PASSIVE_MODE) {
            char *reply_arg[1] = {NULL};
            reply_arg[0] = (char *)"ok";
            if (carrier->ReplySessionRequest(1, reply_arg) < 0) {
                CARRIER_LOG("Open port forwarding unsuccessfully.\n");
                remove_service = TRUE;
                remove_stream = TRUE;
                goto close_exit;
            } else {
                goto return_exit;
            }
        } else {
            // Impossible to be here.
        }
    } else if (state == ElaStreamState_connected) {
        if (running_mode == ACTIVE_MODE) {
            int rc = 0;
            String service = carrier->GetUsedService();
            String port = carrier->GetLocalPort();

            rc = carrier->OpenPortForwarding(stream, service.string(),
                                "127.0.0.1", port.string(), PortForwardingProtocol_TCP);
            if (rc <= 0) {
                CARRIER_LOG("Open port forwarding unsuccessfully.\n");
                change_mode = TRUE;
                remove_stream = TRUE;
                goto close_exit;
            }

            code = NOERROR;
            carrier->SetPortForwardingSuccessful(TRUE);
            carrier->SetPortForwardingId(rc);
            CARRIER_LOG("Port forwarding was successfully established.\n");
            goto distribute_exit;
        } else {
            code = NOERROR;
            carrier->SetPortForwardingSuccessful(TRUE);
            CARRIER_LOG("Port forwarding was successfully established.\n");
            goto distribute_exit;
        }
    } else {
        goto return_exit;
    }

close_exit:
    if (remove_stream) {
        int stream_id = carrier->GetStreamId();
        carrier->RemoveStream(stream_id);
    }

    if (remove_service)
        carrier->RemoveService();

    carrier->CloseSession();

clean_exit:
    if (change_mode) {
        carrier->SetRunningMode(PASSIVE_MODE);
        remote_port = String(INVALID_PORT);
    }

    carrier->SetUsedService(NULL);
    carrier->SetLocalPort(NULL);
    carrier->SetRemotePort(NULL);
    carrier->SetRemoteSdp(String());
    carrier->SetPeerId(String());

distribute_exit:
    local_port = carrier->GetLocalPort();
    carrier->DistributeOnPortForwardingResult(user_id, local_port, remote_port, code);

return_exit:
    return;
}

ElaSession *CCarrier::GetSession()
{
    return mSession;
}

void CCarrier::SetSession(ElaSession *session)
{
    mSession = session;
}

String CCarrier::GetRemoteSdp()
{
    return mRemoteSdp;
}

void CCarrier::SetRemoteSdp(String remote_sdp)
{
    mRemoteSdp = remote_sdp;
}

RUNNING_MODE CCarrier::GetRunningMode()
{
    return mRunningMode;
}

void CCarrier::SetRunningMode(RUNNING_MODE mode)
{
    mRunningMode = mode;
}

Boolean CCarrier::IsPortForwardingSuccessful()
{
    return mOpenPortForwardingSuccessfully;
}

void CCarrier::SetPortForwardingSuccessful(Boolean is_successful)
{
    mOpenPortForwardingSuccessfully = is_successful;
}

int CCarrier::GetPortForwardingId()
{
    return mPortForwardingId;
}

void CCarrier::SetPortForwardingId(int port_forwarding_id)
{
    mPortForwardingId = port_forwarding_id;
}

int CCarrier::GetStreamId()
{
    return mStreamId;
}

void CCarrier::SetStreamId(int stream_id)
{
    mStreamId = stream_id;
}

String CCarrier::GetPeerId()
{
    return mPeerId;
}

void CCarrier::SetPeerId(String peer_id)
{
    mPeerId = peer_id;
}

String CCarrier::GetUsedService()
{
    return mUsedService;
}

void CCarrier::SetUsedService(const char *service)
{
    mUsedService = String(service);
}

String CCarrier::GetLocalPort()
{
    return mLocalPort;
}

void CCarrier::SetLocalPort(const char *local_port)
{
    mLocalPort = String(local_port);
}

String CCarrier::GetRemotePort()
{
    return mRemotePort;
}

void CCarrier::SetRemotePort(const char *remote_port)
{
    mRemotePort = String(remote_port);
}

int CCarrier::InitSession()
{
    int rc = 0;

    rc = ela_session_init(mElaCarrier, OnSessionRequest, mElaCarrier);
    if (rc < 0) {
        CARRIER_LOG("Session initialized unsuccessfully(0x%x).\n", ela_get_error());
    } else {
        CARRIER_LOG("Session initialized successfully.\n");
    }

    return rc;
}

ElaSession *CCarrier::NewSession(const char *friend_id)
{
    assert(friend_id);

    mSession = ela_session_new(mElaCarrier, friend_id);
    if (!mSession) {
        CARRIER_LOG("Create session unsuccessfully(0x%x).\n", ela_get_error());
    } else {
        CARRIER_LOG("Create session successfully.\n");
    }

    return mSession;
}

int CCarrier::StartSession()
{
    int rc = 0;

    rc = ela_session_start(mSession, mRemoteSdp.string(),
                           mRemoteSdp.GetLength());
    if (rc < 0) {
        CARRIER_LOG("Session started unsuccessfully(0x%x).\n", ela_get_error());
    } else {
        CARRIER_LOG("Session started successfully.\n");
    }

    return rc;
}

void CCarrier::CloseSession()
{
    ela_session_close(mSession);
}

int CCarrier::RequestSession()
{
    int rc = 0;

    rc = ela_session_request(mSession, OnSessionRequestComplete, mElaCarrier);
    if (rc < 0) {
        CARRIER_LOG("session request unsuccessfully(0x%x).\n", ela_get_error());
    } else {
        CARRIER_LOG("session request successfully.\n");
    }

    return rc;
}

int CCarrier::ReplySessionRequest(int argc, char *argv[])
{
    int rc = 0;

    if ((argc != 1) && (argc != 2)) {
        CARRIER_LOG("Invalid invocation.\n");
        return -1;
    }

    if ((strcmp(argv[0], "ok") == 0) && (argc == 1)) {
        rc = ela_session_reply_request(mSession, 0, NULL);
        if (rc < 0) {
            CARRIER_LOG("reply session request unsuccessfully.\n");
        } else {
            CARRIER_LOG("reply session request successfully.\n");

        }

        return rc;
    } else if ((strcmp(argv[0], "refuse") == 0) && (argc == 2)) {
        rc = ela_session_reply_request(mSession, 1, argv[2]);
        if (rc < 0) {
            CARRIER_LOG("reply session request unsuccessfully.\n");
        } else {
            CARRIER_LOG("reply session request successfully.\n");
        }

        return rc;
    } else {
        CARRIER_LOG("Invalid response.\n");
        return -1;
    }
}

int CCarrier::Invite(const char *friend_id, const char *hello)
{
    int rc = 0;

    assert(friend_id);
    assert(hello);

    rc = ela_invite_friend(mElaCarrier, friend_id, hello, strlen(hello),
                               OnInviteResponse, NULL);
    if (rc < 0) {
        CARRIER_LOG("Send invite request unsuccessfully(0x%x).\n", ela_get_error());
    } else {
        CARRIER_LOG("Send invite request successfully.\n");
    }

    return rc;
}

int CCarrier::ReplyInvite(const char *friend_id, const char *msg, const char *reason)
{
    int rc = 0;
    int status = -1;
    size_t msg_len = 0;

    assert(friend_id);
    assert(msg || reason);

    if (msg) {
        status = 0;
        msg_len = strlen(msg);
    }

    rc = ela_reply_friend_invite(mElaCarrier, friend_id, status, reason, msg, msg_len);
    if (rc == 0)
        CARRIER_LOG("Send invite reply to inviter successfully.\n");
    else
        CARRIER_LOG("Send invite reply to inviter unsuccessfully(0x%x).\n", ela_get_error());

    return rc;
}

int CCarrier::AddStream(ElaStreamType type, int options)
{
    int rc = 0;
    ElaStreamCallbacks callbacks = {0};

    assert(type >= 0);
    assert(type <= 4);
    assert(options > 0);
    assert(options <= 31);

    callbacks.state_changed = OnStreamStateChanged;

    rc = ela_session_add_stream(mSession, type,
                                options, &callbacks, mElaCarrier);
    if (rc < 0) {
        CARRIER_LOG("Add stream unsuccessfully(0x%x).\n", ela_get_error());
    } else {
        CARRIER_LOG("Add stream successfully and stream id is %d.\n", rc);
        mStreamId = rc;
    }

    return rc;
}

int CCarrier::RemoveStream(int stream_id)
{
    int rc = 0;

    assert(stream_id  > 0);

    rc = ela_session_remove_stream(mSession, stream_id);
    if (rc < 0) {
        CARRIER_LOG("Remove stream unsuccessfully(0x%x).\n", ela_get_error());
    } else {
        CARRIER_LOG("Remove stream successfully and stream id is %d.\n", stream_id);
        mStreamId = -1;
    }

    return rc;
}

int CCarrier::AddService(const char *service, const char *host, const char *port)
{
    int rc = 0;

    rc = ela_session_add_service(mSession, service,
                    PortForwardingProtocol_TCP, host, port);
    if (rc < 0)
        CARRIER_LOG("Prepare service %s failed(%08X).\n",
              service, ela_get_error());
    else
        CARRIER_LOG("Add service %s(%s).\n", service, port);

    return rc;
}

void CCarrier::RemoveService()
{
    ela_session_remove_service(mSession, mUsedService);
}

int CCarrier::OpenPortForwarding(int stream, const char *service,
                                 const char *host, const char *port, PortForwardingProtocol protocol)
{
    int rc = 0;

    assert(service);
    assert(host);
    assert(port);
    assert(stream > 0);
    assert(protocol == PortForwardingProtocol_TCP);

    rc = ela_stream_open_port_forwarding(mSession, stream,
                service, protocol, host, port);
    if (rc <= 0) {
        CARRIER_LOG("Open portforwarding for service %s on %s:%s unsuccessfully(%08X).\n",
              service, "127.0.0.1", port, ela_get_error());
        // close session
        CCarrier::GetLocalInstance()->CloseSession();
    } else {
        CARRIER_LOG("Open portforwarding for service %s on %s:%s successfully.\n",
              mUsedService.string(), "127.0.0.1", mLocalPort.string());
    }

    return rc;
}

static void OnFriendAdded(
    /* [in] */ ElaCarrier *w,
    /* [in] */ const ElaFriendInfo *info,
    /* [in] */ void *context)
{
    CARRIER_LOG("[OnFriendAdded] New friend added. The friend information:\n");
    display_friend_info(info);
}

static void OnFriendMessage(
    /* [in] */ ElaCarrier *w,
    /* [in] */ const char *from,
    /* [in] */ const void *msg,
    /* [in] */ size_t len,
    /* [in] */ void *context)
{
    CARRIER_LOG("[OnFriendMessage] Message from friend[%s]: %.*s\n", from, (int)len, (const char*)msg);

    ArrayOf<Byte>* message = ArrayOf<Byte>::Alloc(len);
    if (!message) return;
    message->Copy((Byte*)msg, len);
    CCarrier::GetLocalInstance()->DistributeOnFriendMessage(String(from), *message);
    ArrayOf<Byte>::Free(message);
}

static void OnIdle(
    /* [in] */ ElaCarrier *carrier,
    /* [in] */ void *context)
{
    CCarrier::GetLocalInstance()->DistributeOnIdle();
}

static void OnReady(
    /* [in] */ ElaCarrier *carrier,
    /* [in] */ void *context)
{
    CCarrier::GetLocalInstance()->DistributeOnReady();
}

static bool OnFriendsList(
    /* [in] */ ElaCarrier *w,
    /* [in] */ const ElaFriendInfo *friend_info,
    /* [in] */ void *context)
{
    //TODO
    return true;
}

CCarrier* CCarrier::sGlobalCarrier = NULL;
CCarrier::CCarrier()
    : mElaCarrier(NULL)
    , m_carrierThreadId(0)
    , mIsOnline(FALSE)
    , mIsReady(FALSE)
{
    pthread_mutexattr_t recursiveAttr;
    pthread_mutexattr_init(&recursiveAttr);
    pthread_mutexattr_settype(&recursiveAttr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mListenersLock, &recursiveAttr);
    pthread_mutex_init(&mFriendListLock, &recursiveAttr);
    pthread_mutexattr_destroy(&recursiveAttr);

    mSession = NULL;
    mRunningMode = PASSIVE_MODE;
    mOpenPortForwardingSuccessfully = FALSE;
    mStreamId = 0;
}

CCarrier::~CCarrier()
{
    sCarrierInstanceLock.Lock();
    sGlobalCarrier = NULL;
    sCarrierInstanceLock.Unlock();

    Cleanup();

    pthread_mutex_destroy(&mListenersLock);
    pthread_mutex_destroy(&mFriendListLock);
}

void CCarrier::Cleanup()
{
    Stop();

    //Clear up the listener list.
    pthread_mutex_lock(&mListenersLock);
    ListenerNode* next = NULL;
    ListenerNode* it = &mListeners;
    ListenerNode* head = &mListeners;
    while (NULL != it) {
        next = (ListenerNode*)(it->Next());
        it->mCarrierListener = NULL;
        if (it != head) {
            delete it;
        }
        it = next;
    }
    mListeners.Initialize();
    pthread_mutex_unlock(&mListenersLock);
}

void CCarrier::CleanFriendList()
{
    //Clear up the friend list.
    pthread_mutex_lock(&mFriendListLock);
    FriendNode* fNext = NULL;
    FriendNode* fIt = &mTempFriendList;
    FriendNode* fHead = &mTempFriendList;
    while (NULL != fIt) {
        fNext = (FriendNode*)(fIt->Next());
        fIt->mFriend = NULL;
        if (fIt != fHead) {
            delete fIt;
        }
        fIt = fNext;
    }
    mTempFriendList.Initialize();
    pthread_mutex_unlock(&mFriendListLock);
}

ECode CCarrier::GetInstance(
    /* [out] */ ICarrier** carrier)
{
    VALIDATE_NOT_NULL(carrier);
    *carrier = NULL;

    sCarrierInstanceLock.Lock();
    if (sGlobalCarrier == NULL) {
        sGlobalCarrier = new CCarrier();
        if (sGlobalCarrier == NULL) {
            sCarrierInstanceLock.Unlock();
            return E_OUT_OF_MEMORY;
        }
    }

    *carrier = sGlobalCarrier;
    (*carrier)->AddRef();
    sCarrierInstanceLock.Unlock();

    return NOERROR;
}

ECode CCarrier::GetCarrierHandle(
    /* [out] */ ElaCarrier** handle)
{
    VALIDATE_NOT_NULL(handle);
    *handle = mElaCarrier;
    return NOERROR;
}

ECode CCarrier::Start(
    /* [in] */ const String& carrierDataPath)
{
    if (mDataPath.Equals(carrierDataPath)) {
        return NOERROR;
    }

    if (!mDataPath.IsNullOrEmpty()) {
        CARRIER_LOG("You are changing the carrier data path, maybe will lose the friends' information.\n");
        //Change the carrier data path, it will cleanup the data.
        Stop();
    }

    mDataPath = carrierDataPath;
    if (pthread_create(&m_carrierThreadId, NULL, CarrierThread, (void *)this)) {
        CARRIER_LOG("Create thread failed.\n");
        return E_FAIL;
    }
    return NOERROR;
}

void *CCarrier::CarrierThread(void *arg)
{
    CCarrier* proxy = (CCarrier *)arg;
    int rc = 0;
    ElaCallbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.idle = OnIdle;
    callbacks.connection_status = OnConnectionChanged;
    callbacks.ready = OnReady;
    callbacks.friend_request = OnFriendRequest;
    callbacks.friend_connection = OnFriendConnetionChanged;
    callbacks.friend_list = OnFriendsList;
    callbacks.friend_message = OnFriendMessage;
    callbacks.friend_added = OnFriendAdded;
    callbacks.friend_invite = OnInviteRequest;

    ElaOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.udp_enabled = true;
    opts.persistent_location = proxy->mDataPath.string();
    opts.bootstraps_size = 5;
    opts.bootstraps = (BootstrapNode *)calloc(opts.bootstraps_size, sizeof(BootstrapNode) * 1);
    if (!opts.bootstraps) {
        CARRIER_LOG("out of memory.\n");
        proxy->mDataPath = "";
        return NULL;
    }

    opts.bootstraps[0].ipv4 = "13.58.208.50";
    opts.bootstraps[0].port = "33445";
    opts.bootstraps[0].public_key = "89vny8MrKdDKs7Uta9RdVmspPjnRMdwMmaiEW27pZ7gh";

    opts.bootstraps[1].ipv4 = "18.216.102.47";
    opts.bootstraps[1].port = "33445";
    opts.bootstraps[1].public_key = "G5z8MqiNDFTadFUPfMdYsYtkUDbX5mNCMVHMZtsCnFeb";

    opts.bootstraps[2].ipv4 = "18.216.6.197";
    opts.bootstraps[2].port = "33445";
    opts.bootstraps[2].public_key = "H8sqhRrQuJZ6iLtP2wanxt4LzdNrN2NNFnpPdq1uJ9n2";

    opts.bootstraps[3].ipv4 = "52.83.171.135";
    opts.bootstraps[3].port = "33445";
    opts.bootstraps[3].public_key = "5tuHgK1Q4CYf4K5PutsEPK5E3Z7cbtEBdx7LwmdzqXHL";

    opts.bootstraps[4].ipv4 = "52.83.191.228";
    opts.bootstraps[4].port = "33445";
    opts.bootstraps[4].public_key = "3khtxZo89SBScAMaHhTvD68pPHiKxgZT6hTCSZZVgNEm";

    proxy->mElaCarrier = ela_new(&opts, &callbacks, NULL);
    if (proxy->mElaCarrier == NULL) {
        CARRIER_LOG("Error start carrier loop: 0x%x\n", ela_get_error());
        proxy->mDataPath = "";
        free(opts.bootstraps);
        return NULL;
    }

    rc = ela_session_init(proxy->mElaCarrier, OnSessionRequest, proxy->mElaCarrier);
    if (rc < 0) {
        CARRIER_LOG("Session initialized unsuccessfully(0x%x).\n", ela_get_error());
        ela_kill(proxy->mElaCarrier);
        proxy->mElaCarrier = NULL;
        proxy->mDataPath = "";
        free(opts.bootstraps);
        return NULL;
    } else {
        CARRIER_LOG("Session initialized successfully.\n");
    }

    char buf[ELA_MAX_ADDRESS_LEN + 1];
    CARRIER_LOG("Carrier node identities:\n");
    CARRIER_LOG("   Node ID: %s\n", ela_get_nodeid(proxy->mElaCarrier, buf, sizeof(buf)));
    CARRIER_LOG("   User ID: %s\n", ela_get_userid(proxy->mElaCarrier, buf, sizeof(buf)));
    CARRIER_LOG("   Address: %s\n\n", ela_get_address(proxy->mElaCarrier, buf, sizeof(buf)));

    rc = ela_run(proxy->mElaCarrier, 10);
    if (rc != 0) {
        CARRIER_LOG("Error start carrier loop: 0x%x\n", ela_get_error());
        ela_kill(proxy->mElaCarrier);
        ela_session_cleanup(proxy->mElaCarrier);
        proxy->mElaCarrier = NULL;
        proxy->mDataPath = "";
        free(opts.bootstraps);
    }

    return NULL;
}

ECode CCarrier::Stop()
{
    if (mElaCarrier != NULL) {
        ela_kill(mElaCarrier);
        ela_session_cleanup(mElaCarrier);
        mElaCarrier = NULL;
    }

    pthread_join(m_carrierThreadId, NULL);
    m_carrierThreadId = 0;

    mDataPath = "";

    return NOERROR;
}

ECode CCarrier::RegenerateAddress()
{
    CARRIER_NOT_READY();
    static Int32 nospam = 1;
    ela_set_self_nospam(mElaCarrier, nospam++);
    return NOERROR;
}

ECode CCarrier::GetAddress(
    /* [out] */ String* myAddress)
{
    CARRIER_NOT_READY();
    VALIDATE_NOT_NULL(myAddress);

    char buf[ELA_MAX_ADDRESS_LEN + 1];
    ela_get_address(mElaCarrier, buf, sizeof(buf));
    *myAddress = String(buf);
    return NOERROR;
}

ECode CCarrier::AccpetFriendRequest(
    /* [in] */ const String& uid)
{
    CARRIER_NOT_READY();

    Int32 rc = ela_accept_friend(mElaCarrier, uid);
    if (rc == 0) {
        CARRIER_LOG("Accept friend request success.\n");
    }
    else {
        CARRIER_LOG("Accept friend request failed(0x%x).\n", ela_get_error());
    }
    return NOERROR;
}

ECode CCarrier::AddFriend(
    /* [in] */ const String& friendAddress,
    /* [in] */ const String& hello,
    /* [out] */ String* friendId)
{
    char id[ELA_MAX_ID_LEN + 1] = {0};
    char *user_id = NULL;
    Int32 rc = 0;
    CARRIER_NOT_READY();

    if (friendAddress.GetLength() == 0 || hello.GetLength() == 0 || friendId == NULL)
        return E_INVALID_ARGUMENT;

    user_id = ela_get_id_by_address(friendAddress.string(), id, ELA_MAX_ID_LEN + 1);
    if (user_id == NULL)
        return E_INVALID_ARGUMENT;

    rc = ela_add_friend(mElaCarrier, friendAddress.string(), hello.string());
    if (rc == 0) {
        CARRIER_LOG("Request to friend %s successfully.\n", user_id);
        *friendId = String((const char*)id, strlen(id));
        return NOERROR;
    } else {
        CARRIER_LOG("Request to friend %s unsuccessfully(0x%x).\n", user_id, ela_get_error());
        return E_CARRIER_ERROR;
    }
}

ECode CCarrier::RemoveFriend(
    /* [in] */ IFriend* _friend)
{
    CARRIER_NOT_READY();
    VALIDATE_NOT_NULL(_friend);

    String uid;
    _friend->GetUid(&uid);
    Int32 rc = ela_remove_friend(mElaCarrier, uid.string());
    if (rc == 0) {
        CARRIER_LOG("Remove friend %s success.\n", uid.string());
    }
    else {
        CARRIER_LOG("Remove friend %s failed (0x%x).\n", uid.string(), ela_get_error());
    }
    return NOERROR;
}

ECode CCarrier::IsOnline(
    /* [out] */ Boolean* online)
{
    VALIDATE_NOT_NULL(online);
    *online = mIsOnline;
    return NOERROR;
}

ECode CCarrier::GetFriend(
    /* [in] */ const String& uid,
    /* [out] */ IFriend** _friend)
{
    VALIDATE_NOT_NULL(_friend);
    *_friend = NULL;

    pthread_mutex_lock(&mFriendListLock);
    ElaFriendInfo fi;
    int rc = ela_get_friend_info(mElaCarrier, uid.string(), &fi);
    if (rc == 0) {
        *_friend = new CFriend(uid, fi.status == ElaConnectionStatus_Connected);
        if (!*_friend) {
            pthread_mutex_unlock(&mFriendListLock);
            return E_OUT_OF_MEMORY;
        }

        //The caller need to use AutoPtr<IFriend> xxx to get the interface.
        (*_friend)->AddRef();
        pthread_mutex_unlock(&mFriendListLock);
        return NOERROR;
    }

    pthread_mutex_unlock(&mFriendListLock);
    return E_INVALID_ARGUMENT;
}

static bool get_friends_callback(const ElaFriendInfo *friend_info, void *context)
{
    if (friend_info) {
        CCarrier::GetLocalInstance()->AddFriend2List(String(friend_info->user_info.userid),
                friend_info->status == ElaConnectionStatus_Connected);
    }
    return true;
}

ECode CCarrier::GetFriends(
    /* [out, callee] */ ArrayOf<IFriend*>** friends)
{
    VALIDATE_NOT_NULL(friends);
    *friends = NULL;

    ela_get_friends(mElaCarrier, get_friends_callback, NULL);

    pthread_mutex_lock(&mFriendListLock);
    Int32 count = 0;
    FriendNode* fIt = &mTempFriendList;
    while (NULL != fIt) {
        fIt = (FriendNode*)(fIt->Next());
        count++;
    }

    if (count > 1) {
        //count - 1: minus 1 which is the Head node's count.
        ArrayOf<IFriend*>* _friends = ArrayOf<IFriend*>::Alloc(count - 1);
        fIt = &mTempFriendList;
        fIt = (FriendNode*)(fIt->Next());
        Int32 i = 0;
        while (NULL != fIt) {
            _friends->Set(i++, fIt->mFriend);
            fIt = (FriendNode*)(fIt->Next());
        }
        *friends = _friends;
        (*friends)->AddRef();
    }

    pthread_mutex_unlock(&mFriendListLock);
    CleanFriendList();
    return NOERROR;
}

ECode CCarrier::AddCarrierNodeListener(
    /* [in] */ ICarrierListener* listener)
{
    pthread_mutex_lock(&mListenersLock);
    ListenerNode* it = &mListeners;
    it = (ListenerNode*)(it->Next());
    for (; NULL != it; it = (ListenerNode*)(it->Next())) {
        if (it->mCarrierListener.Get() == listener) {
            pthread_mutex_unlock(&mListenersLock);
            return NOERROR;
        }
    }

    ListenerNode* listenerNode = new ListenerNode;
    if (!listenerNode) {
        pthread_mutex_unlock(&mListenersLock);
        return E_OUT_OF_MEMORY;
    }
    listenerNode->mCarrierListener = listener;
    mListeners.InsertFirst(listenerNode);
    pthread_mutex_unlock(&mListenersLock);

    return NOERROR;
}

ECode CCarrier::RemoveCarrierNodeListener(
    /* [in] */ ICarrierListener* listener)
{
    pthread_mutex_lock(&mListenersLock);
    ListenerNode* prev = &mListeners;
    ListenerNode* head = &mListeners;
    ListenerNode* it = (ListenerNode*)(head->Next());
    for (; NULL != it; it = (ListenerNode*)(it->Next())) {
        if (it->mCarrierListener.Get() == listener) {
            it->mCarrierListener = NULL;

            if (it != head) {
                it->Detach(prev);
                delete it;
            }

            pthread_mutex_unlock(&mListenersLock);
            return NOERROR;
        }
        prev = it;
    }

    CARRIER_LOG("Remove a non-existent Listener.\n");
    pthread_mutex_unlock(&mListenersLock);
    return NOERROR;
}

ECode CCarrier::Import(
    /* [in] */ const String& dataFile)
{
    // TODO: Add your code here
    return E_NOT_IMPLEMENTED;
}

ECode CCarrier::Export(
    /* [out] */ String* dataFile)
{
    VALIDATE_NOT_NULL(dataFile);

    // TODO: Add your code here
    return E_NOT_IMPLEMENTED;
}

ECode CCarrier::GetUserid(
    /* [out] */ String* myUid)
{
    CARRIER_NOT_READY();
    VALIDATE_NOT_NULL(myUid);

    char buf[ELA_MAX_ID_LEN  + 1];
    ela_get_userid(mElaCarrier, buf, sizeof(buf));
    *myUid = String(buf);
    return NOERROR;
}

CARAPI CCarrier::OpenPortForwarding(
    /* [in] */ const String& uid,
    /* [in] */ const String& localPort,
    /* [in] */ const String& remotePort)
{
    char *end_ptr = NULL;
    char hello[32] = {0};
    char buf[64] = {0};
    time_t seconds = 0;
    String service;

    CARRIER_NOT_READY();
    VALIDATE_NOT_NULL(localPort.string());
    VALIDATE_NOT_NULL(remotePort.string());

    if (localPort.GetLength() == 0 || localPort.GetLength() > 5
        || remotePort.GetLength() == 0 || remotePort.GetLength() > 5)
        return E_INVALID_ARGUMENT;

    if (strtol(localPort.string(), &end_ptr, 10) <= 0)
        return E_INVALID_ARGUMENT;

    if (strtol(remotePort.string(), &end_ptr, 10) <= 0)
        return E_INVALID_ARGUMENT;

    time(&seconds);
    sprintf(buf, "%ld%s%s%s", (long)seconds, localPort.string(), "to", remotePort.string());
    service = String(buf, strlen(buf));
    sprintf(hello, "%s:%s", service.string(), remotePort.string());
    if (Invite(uid.string(), hello) < 0) {
        return E_INVITE;
    }

    mUsedService = service;
    mLocalPort = localPort;
    mRemotePort = remotePort;

    return NOERROR;
}

CARAPI CCarrier::ClosePortForwarding(
    /* [in] */ const String& uid,
    /* [in] */ const String& localPort,
    /* [in] */ const String& remotePort)
{
    int rc = 0;

    if (mStreamId == 0 || mPortForwardingId == 0)
        return E_INCORRECT_STATE;

    rc = ela_stream_close_port_forwarding(mSession, mStreamId,
                                     mPortForwardingId);
    if (rc < 0)
        return E_CLOSE_PORT_FORWARDING;

    return NOERROR;
}

ECode CCarrier::SendMessage(
    /* [in] */ const String& uid,
    /* [in] */ const ArrayOf<Byte>& message)
{
    if (uid.IsNullOrEmpty() || message.GetLength() == 0) {
        return E_INVALID_ARGUMENT;
    }

    CARRIER_NOT_READY();

    int rc = ela_send_friend_message(mElaCarrier,
            uid.string(), message.GetPayload(), message.GetLength());
    if (rc == 0)
        CARRIER_LOG("Send message success.\n");
    else
        CARRIER_LOG("Send message failed(0x%x).\n", ela_get_error());

    return NOERROR;
}

ECode CCarrier::DistributeOnConnectionChanged(
    /* [in] */ Boolean online)
{
    //Set the carrier's status
    mIsOnline = online;

    //Distribute the callback: OnConnectionChanged
    pthread_mutex_lock(&mListenersLock);
    ListenerNode* it = &mListeners;
    it = (ListenerNode*)(it->Next());
    for (; NULL != it; it = (ListenerNode*)(it->Next())) {
        it->mCarrierListener->OnConnectionChanged(online);
    }
    pthread_mutex_unlock(&mListenersLock);

    return NOERROR;
}

ECode CCarrier::DistributeOnIdle()
{
    //Distribute the callback: OnIdle.
    mIsReady = TRUE;
    pthread_mutex_lock(&mListenersLock);
    ListenerNode* it = &mListeners;
    it = (ListenerNode*)(it->Next());
    for (; NULL != it; it = (ListenerNode*)(it->Next())) {
        it->mCarrierListener->OnIdle();
    }
    pthread_mutex_unlock(&mListenersLock);

    return NOERROR;
}

ECode CCarrier::DistributeOnReady()
{
    //Distribute the callback: OnReady.
    mIsReady = TRUE;
    pthread_mutex_lock(&mListenersLock);
    ListenerNode* it = &mListeners;
    it = (ListenerNode*)(it->Next());
    for (; NULL != it; it = (ListenerNode*)(it->Next())) {
        it->mCarrierListener->OnReady();
    }
    pthread_mutex_unlock(&mListenersLock);

    return NOERROR;
}

ECode CCarrier::DistributeOnFriendRequest(
    /* [in] */ const String& uid,
    /* [in] */ const String& hello)
{
    //Distribute the callback: OnFriendRequest
    pthread_mutex_lock(&mListenersLock);
    ListenerNode* it = &mListeners;
    it = (ListenerNode*)(it->Next());
    for (; NULL != it; it = (ListenerNode*)(it->Next())) {
        it->mCarrierListener->OnFriendRequest(uid, hello);
    }
    pthread_mutex_unlock(&mListenersLock);

    return NOERROR;
}

ECode CCarrier::DistributeOnFriendConnetionChanged(
    /* [in] */ const String& uid,
    /* [in] */ Boolean online)
{
    //Distribute the callback: OnFriendConnetionChanged
    pthread_mutex_lock(&mListenersLock);
    ListenerNode* it = &mListeners;
    it = (ListenerNode*)(it->Next());
    for (; NULL != it; it = (ListenerNode*)(it->Next())) {
        it->mCarrierListener->OnFriendConnetionChanged(uid, online);
    }
    pthread_mutex_unlock(&mListenersLock);

    return NOERROR;
}

CARAPI CCarrier::DistributeOnPortForwardingRequest(
    /* [in] */ String &uid,
    /* [in] */ String &servicePort,
    /* [out] */ Boolean *accept)
{
    //Distribute the callback: OnPortForwardingRequest
    pthread_mutex_lock(&mListenersLock);
    ListenerNode* it = &mListeners;
    it = (ListenerNode*)(it->Next());
    for (; NULL != it; it = (ListenerNode*)(it->Next())) {

        it->mCarrierListener->OnPortForwardingRequest(uid, servicePort, accept);
    }
    pthread_mutex_unlock(&mListenersLock);

    return NOERROR;
}

ECode CCarrier::DistributeOnPortForwardingResult(
    /* [in] */ String &uid,
    /* [in] */ String &localPort,
    /* [in] */ String &remotePort,
    /* [in] */ ECode code)
{
    //Distribute the callback: OnPortForwardingResult
    pthread_mutex_lock(&mListenersLock);
    ListenerNode* it = &mListeners;
    it = (ListenerNode*)(it->Next());
    for (; NULL != it; it = (ListenerNode*)(it->Next())) {
        it->mCarrierListener->OnPortForwardingResult(uid, localPort, remotePort, code);
    }
    pthread_mutex_unlock(&mListenersLock);

    return NOERROR;
}

ECode CCarrier::DistributeOnFriendMessage(
    /* [in] */ const String& uid,
    /* [in] */ const ArrayOf<Byte>& message)
{
    //Distribute the callback: OnFriendMessage
    pthread_mutex_lock(&mListenersLock);
    ListenerNode* it = &mListeners;
    it = (ListenerNode*)(it->Next());
    for (; NULL != it; it = (ListenerNode*)(it->Next())) {
        it->mCarrierListener->OnMessageReceived(uid, message);
    }
    pthread_mutex_unlock(&mListenersLock);

    return NOERROR;
}

CCarrier* CCarrier::GetLocalInstance()
{
    assert(sGlobalCarrier != NULL);
    return sGlobalCarrier;
}

PInterface CCarrier::Probe(
    /* [in] */ REIID riid)
{
    if (riid == EIID_IInterface) {
        return (PInterface)this;
    }
    else if (riid == EIID_ICarrier) {
        return (ICarrier*)this;
    }

    return NULL;
}

UInt32 CCarrier::AddRef()
{
    return ElRefBase::AddRef();
}

UInt32 CCarrier::Release()
{
    return ElRefBase::Release();
}

ECode CCarrier::GetInterfaceID(
    /* [in] */ IInterface* object,
    /* [out] */ InterfaceID* iid)
{
    VALIDATE_NOT_NULL(iid);
    if (object == (IInterface *)(ICarrier *)this) {
        *iid = EIID_ICarrier;
    }
    else {
        return E_INVALID_ARGUMENT;
    }
    return NOERROR;
}

void CCarrier::AddFriend2List(
    /* [in] */ const String& uid,
    /* [in] */ Boolean online)
{
    pthread_mutex_lock(&mFriendListLock);

    //Add the new friend to the table.
    FriendNode* fn = new FriendNode();
    if (!fn) goto exit;
    fn->mUid = uid;

    //Update the online by constructor.
    fn->mFriend = new CFriend(uid, online);
    if (!fn->mFriend) goto exit;
    mTempFriendList.InsertFirst(fn);

exit:
    pthread_mutex_unlock(&mFriendListLock);
}


CCarrier::CFriend::CFriend(
    /* [in] */ const String& uid,
    /* [in] */ Boolean online)
    : mUid(uid)
    , mIsOnline(online)
{}

ECode CCarrier::CFriend::GetUid(
    /* [out] */ String* uid)
{
    VALIDATE_NOT_NULL(uid);
    *uid = mUid;
    return NOERROR;
}

ECode CCarrier::CFriend::GetName(
    /* [out] */ String* name)
{
    VALIDATE_NOT_NULL(name);
    *name = mName;
    return NOERROR;
}

ECode CCarrier::CFriend::SetLabel(
    /* [in] */ const String& label)
{
    mLabel = label;
    return NOERROR;
}

ECode CCarrier::CFriend::GetLabel(
    /* [out] */ String* label)
{
    VALIDATE_NOT_NULL(label);
    *label = mLabel;
    return NOERROR;
}

ECode CCarrier::CFriend::IsOnline(
    /* [out] */ Boolean* online)
{
    VALIDATE_NOT_NULL(online);
    *online = mIsOnline;
    return NOERROR;
}

ECode CCarrier::CFriend::UpdateStatus(
    /* [in] */ Boolean online)
{
    mIsOnline = online;
    return NOERROR;
}

UInt32 CCarrier::CFriend::AddRef()
{
    return ElRefBase::AddRef();
}

UInt32 CCarrier::CFriend::Release()
{
    return ElRefBase::Release();
}

PInterface CCarrier::CFriend::Probe(
    /* [in] */ REIID riid)
{
    if (riid == EIID_IInterface) {
        return (PInterface)this;
    }
    else if (riid == EIID_IFriend) {
        return (IFriend*)this;
    }

    return NULL;
}

ECode CCarrier::CFriend::GetInterfaceID(
    /* [in] */ IInterface* object,
    /* [out] */ InterfaceID* iid)
{
    VALIDATE_NOT_NULL(iid);
    if (object == (IInterface *)(IFriend *)this) {
        *iid = EIID_IFriend;
    }
    else {
        return E_INVALID_ARGUMENT;
    }
    return NOERROR;
}

ECode CCarrier::FriendNode::Get(
    /* [out] */ IFriend** _friend)
{
    VALIDATE_NOT_NULL(_friend);
    *_friend = mFriend.Get();
    (*_friend)->AddRef();
    return NOERROR;
}

_ELASTOS_NAMESPACE_END
