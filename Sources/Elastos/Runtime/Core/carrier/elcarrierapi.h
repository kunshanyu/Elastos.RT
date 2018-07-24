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

#ifndef __ELCARRIERAPI_H__
#define __ELCARRIERAPI_H__

#include <unistd.h>
#include <elcontainer.h>
#include <elrefbase.h>
#include "ela_carrier.h"
#include "ela_session.h"
#include "hashtable.h"

_ELASTOS_NAMESPACE_BEGIN

typedef enum RUNNING_MODE {
    ACTIVE_MODE,
    PASSIVE_MODE
} RUNNING_MODE;

/** @} */
/** @addtogroup CARRef
  *   @{
  */
class CCarrier
    : public ElRefBase
    , public ICarrier
{
public:
    STATIC CARAPI GetInstance(
        /* [out] */ ICarrier** carrier);

    CARAPI Start(
        /* [in] */ const String& carrierDataPath);

    CARAPI Stop();

    CARAPI RegenerateAddress();

    CARAPI GetAddress(
        /* [out] */ String* myAddress);

    CARAPI AccpetFriendRequest(
        /* [in] */ const String& uid);

    CARAPI AddFriend(
        /* [in] */ const String& friendAddress,
        /* [in] */ const String& hello,
        /* [out] */ String* friendId);

    CARAPI RemoveFriend(
        /* [in] */ IFriend* _friend);

    CARAPI IsOnline(
        /* [out] */ Boolean* online);

    CARAPI GetFriend(
        /* [in] */ const String& uid,
        /* [out] */ IFriend** _friend);

    CARAPI GetFriends(
        /* [out, callee] */ ArrayOf<IFriend*>** friends);

    CARAPI AddCarrierNodeListener(
        /* [in] */ ICarrierListener* listener);

    CARAPI RemoveCarrierNodeListener(
        /* [in] */ ICarrierListener* listener);

    CARAPI Import(
        /* [in] */ const String& dataFile);

    CARAPI Export(
        /* [out] */ String* dataFile);

    CARAPI GetUserid(
        /* [out] */ String* myUid);

    CARAPI OpenPortForwarding(
        /* [in] */ const String& uid,
        /* [in] */ const String& localPort,
        /* [in] */ const String& remotePort);

    CARAPI ClosePortForwarding(
        /* [in] */ const String& uid,
        /* [in] */ const String& localPort,
        /* [in] */ const String& remotePort);

    CARAPI SendMessage(
        /* [in] */ const String& uid,
        /* [in] */ const ArrayOf<Byte>& message);

    CARAPI_(UInt32) AddRef();

    CARAPI_(UInt32) Release();

    CARAPI_(PInterface) Probe(
        /* [in] */ REIID riid);

    CARAPI GetInterfaceID(
        /* [in] */ IInterface* object,
        /* [out] */ InterfaceID* iid);

    ECode GetCarrierHandle(
        /* [out] */ ElaCarrier** carrier);

    CARAPI DistributeOnIdle();

    //Local functions for distribute the callback.
    CARAPI DistributeOnConnectionChanged(
        /* [in] */ Boolean online);

    CARAPI DistributeOnReady();

    CARAPI DistributeOnFriendRequest(
        /* [in] */ const String& uid,
        /* [in] */ const String& hello);

    CARAPI DistributeOnFriendConnetionChanged(
        /* [in] */ const String& uid,
        /* [in] */ Boolean online);

    CARAPI DistributeOnPortForwardingRequest(
        /* [in] */ String& uid,
        /* [in] */ String& servicePort,
        /* [out] */ Boolean* accept);

    CARAPI DistributeOnPortForwardingResult(
        /* [in] */ String& uid,
        /* [in] */ String& localPort,
        /* [in] */ String& remotePort,
        /* [in] */ ECode code);

    CARAPI DistributeOnFriendMessage(
        /* [in] */ const String& uid,
        /* [in] */ const ArrayOf<Byte>& message);

    static CARAPI_(CCarrier*) GetLocalInstance();

    ~CCarrier();

    CARAPI_(void) AddFriend2List(
        /* [in] */ const String& uid,
        /* [in] */ Boolean online);

private:
    //CFriend: include the friend's information.
    class CFriend
        : public ElRefBase
        , public IFriend
    {
    public:
        CFriend(
            /* [in] */ const String& uid,
            /* [in] */ Boolean online);

        CARAPI GetUid(
            /* [out] */ String* uid);

        CARAPI GetName(
            /* [out] */ String* name);

        CARAPI SetLabel(
            /* [in] */ const String& label);

        CARAPI GetLabel(
            /* [out] */ String* label);

        CARAPI IsOnline(
            /* [out] */ Boolean* online);

        CARAPI UpdateStatus(
            /* [in] */ Boolean online);

        CARAPI_(UInt32) AddRef();

        CARAPI_(UInt32) Release();

        CARAPI_(PInterface) Probe(
            /* [in] */ REIID riid);

        CARAPI GetInterfaceID(
            /* [in] */ IInterface* object,
            /* [out] */ InterfaceID* iid);

    private:
        String mUid;
        String mLabel;
        String mName;
        Boolean mIsOnline;
    };

    class ListenerNode : public SLinkNode
    {
    public:
        ListenerNode(){};

    public:
        AutoPtr<ICarrierListener> mCarrierListener;
    };

    class FriendNode : public SLinkNode
    {
    public:
        FriendNode(){}

        CARAPI Get(
            /* [out] */ IFriend** _friend);

    public:
        String mUid;
        AutoPtr<CFriend> mFriend;
    };

private:
    CCarrier();
    CCarrier(CCarrier const&);
    CCarrier& operator=(CCarrier const&);
    static void *CarrierThread(void *arg);
    CARAPI_(void) Cleanup();
    CARAPI_(void) CleanFriendList();

public:
    void SetReadyToStartSession(Boolean is_ready);
    Boolean GetReadyToStartSession();
    ElaSession *GetSession();
    void SetSession(ElaSession *session);
    String GetRemoteSdp();
    void SetRemoteSdp(String remote_sdp);
    RUNNING_MODE GetRunningMode();
    void SetRunningMode(RUNNING_MODE mode);
    Boolean IsPortForwardingSuccessful();
    void SetPortForwardingSuccessful(Boolean is_successful);
    int GetPortForwardingId();
    void SetPortForwardingId(int portForwarding_id);
    int GetStreamId();
    void SetStreamId(int stream_id);
    String GetPeerId();
    void SetPeerId(String peer_id);
    String GetUsedService();
    void SetUsedService(const char *service);
    String GetLocalPort();
    void SetLocalPort(const char *local_port);
    String GetRemotePort();
    void SetRemotePort(const char *remote_port);
    int InitSession();
    ElaSession *NewSession(const char *friend_id);
    int StartSession();
    void CloseSession();
    int RequestSession();
    int ReplySessionRequest(int argc, char *argv[]);
    int Invite(const char *friend_id, const char *hello);
    int ReplyInvite(const char *friend_id, const char *msg, const char *reason);
    int AddStream(ElaStreamType type, int options);
    int RemoveStream(int stream_id);
    int AddService(const char *service, const char *host, const char *port);
    void RemoveService();
    int OpenPortForwarding(int stream, const char *service,
                                 const char *host, const char *port, PortForwardingProtocol protocol);

private:
    //This instance: distributing the callbacks.
    static CCarrier* sGlobalCarrier;

    ElaCarrier* mElaCarrier;
    ListenerNode mListeners;
    pthread_mutex_t mListenersLock;

    FriendNode mTempFriendList;
    pthread_mutex_t mFriendListLock;

    //Carrier thread id
    pthread_t m_carrierThreadId;

    //Carrier's data path
    String mDataPath;
    //Carrier's status
    Boolean mIsOnline;

    //Carrier is ready
    Boolean mIsReady;

    ElaSession *mSession;
    String mRemoteSdp;
    RUNNING_MODE mRunningMode;
    Boolean mOpenPortForwardingSuccessfully;
    int mStreamId;
    int mPortForwardingId;
    String mPeerId;
    String mUsedService;
    String mLocalPort;
    String mRemotePort;
};

_ELASTOS_NAMESPACE_END

#endif // __ELCARRIERAPI_H__
/** @} */
