//=========================================================================
// Copyright (C) 2012 The Elastos Open Source Project
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

#ifndef __ELASTOS_NET_PLAINSOCKETIMPL_H__
#define __ELASTOS_NET_PLAINSOCKETIMPL_H__

#include "elastos/net/SocketImpl.h"

using Elastos::Core::ICloseGuard;

namespace Elastos {
namespace Net {

class PlainSocketImpl
    : public SocketImpl
    , public IPlainSocketImpl
{
    friend class CPlainSocketInputStream;
    friend class CPlainSocketOutputStream;

public:
    CAR_INTERFACE_DECL()

    PlainSocketImpl();

    ~PlainSocketImpl();

    CARAPI constructor(
        /* [in] */ IFileDescriptor* fd);

    CARAPI constructor(
        /* [in] */ IProxy* proxy);

    CARAPI constructor();

    CARAPI constructor(
        /* [in] */ IFileDescriptor* fd,
        /* [in] */ Int32 localport,
        /* [in] */ IInetAddress* addr,
        /* [in] */ Int32 port);

    CARAPI GetOption(
        /* [in] */ Int32 option,
        /* [out] */ IInterface** res);

    CARAPI SetOption(
        /* [in] */ Int32 option,
        /* [in] */ IInterface* value);

    CARAPI SocksAccept();

    CARAPI Read(
        /* [in] */ ArrayOf<Byte>* buffer,
        /* [in] */ Int32 offset,
        /* [in] */ Int32 count,
        /* [out] */ Int32* value);

    CARAPI Write(
        /* [in] */ ArrayOf<Byte> * buffer,
        /* [in] */ Int32 offset,
        /* [in] */ Int32 byteCount,
        /* [out] */ Int32* value);

protected:
    CARAPI Accept(
        /* [in] */ ISocketImpl* newImpl);

    CARAPI Available(
        /* [out] */ Int32* value);

    CARAPI Bind(
        /* [in] */ IInetAddress* address,
        /* [in] */ Int32 port);

    CARAPI OnBind(
        /* [in] */ IInetAddress* address,
        /* [in] */ Int32 port);

    CARAPI Close();

    CARAPI OnClose();

    CARAPI Connect(
        /* [in] */ const String& aHost,
        /* [in] */ Int32 aPort);

    CARAPI Connect(
        /* [in] */ IInetAddress* anAddr,
        /* [in] */ Int32 aPort);

    CARAPI OnConnect(
        /* [in] */ IInetAddress* anAddr,
        /* [in] */ Int32 aPort);

    CARAPI Create(
        /* [in] */ Boolean streaming);

    CARAPI Finalize();

    CARAPI GetInputStream(
        /* [out] */ IInputStream** in);

    CARAPI GetOutputStream(
        /* [out] */ IOutputStream** out);

    CARAPI Listen(
        /* [in] */ Int32 backlog);

    CARAPI ShutdownInput();

    CARAPI ShutdownOutput();

    CARAPI Connect(
        /* [in] */ ISocketAddress* remoteAddr,
        /* [in] */ Int32 timeout);

    CARAPI SupportsUrgentData(
        /* [out] */ Boolean* value);

    CARAPI SendUrgentData(
        /* [in] */ Int32 value);

private:
    Boolean UsingSocks();

    CARAPI CheckNotClosed();

    CARAPI Connect(
        /* [in] */ IInetAddress* anAddr,
        /* [in] */ Int32 aPort,
        /* [in] */ Int32 timeout);

    Int32 SocksGetServerPort();

    AutoPtr<IInetAddress> SocksGetServerAddress();

    CARAPI SocksConnect(
        /* [in] */ IInetAddress* applicationServerAddress,
        /* [in] */ Int32 applicationServerPort,
        /* [in] */ Int32 timeout);

    CARAPI SocksRequestConnection(
        /* [in] */ IInetAddress* applicationServerAddress,
        /* [in] */ Int32 applicationServerPort);

    CARAPI SocksBind();

    CARAPI SocksSendRequest(
        /* [in] */ Int32 command,
        /* [in] */ IInetAddress* address,
        /* [in] */ Int32 port);

    CARAPI SocksReadReply(
        /* [out] */ ISocks4Message** ppMessage);

private:
    static AutoPtr<IInetAddress> sLastConnectedAddress;
    static Int32 sLastConnectedPort;
    Boolean mStreaming;
    Boolean mShutdownInput;
    AutoPtr<IProxy> mProxy;
    AutoPtr<ICloseGuard> mGuard;

//    Int32 mIfd;
};

} // namespace Net
} // namespace Elastos

#endif //__ELASTOS_NET_PLAINSOCKETIMPL_H__
