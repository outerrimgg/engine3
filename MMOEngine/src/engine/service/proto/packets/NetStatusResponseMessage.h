/*
** Copyright (C) 2007-2019 SWGEmu
** See file COPYING for copying conditions.
*/

#ifndef NETSTATUSRESPONSEMESSAGE_H_
#define NETSTATUSRESPONSEMESSAGE_H_

#include "engine/service/proto/BaseProtocol.h"

namespace engine {
  namespace service {
    namespace proto {

	class NetStatusResponseMessage : public BasePacket {
	public:
		// Layout mirrors the client UdpLibrary's UdpPacketClockReflect: timeStamp echo,
		// serverSyncStampLong (our ms clock), then yourSent/yourReceived (the counters the
		// client just reported) and ourSent/ourReceived (our datagram counters for it).
		// All fields big-endian; the 64-bit counters are composed from two net-order ints.
		NetStatusResponseMessage(sys::uint16 tick, sys::uint32 serverSyncStamp,
				sys::uint64 clientSent, sys::uint64 clientReceived,
				sys::uint64 serverSent, sys::uint64 serverReceived) : BasePacket(40) {
			insertShort(0x0800);
			insertShortNet(tick);

			insertIntNet(serverSyncStamp);

			insertLongNet(clientSent);
			insertLongNet(clientReceived);
			insertLongNet(serverSent);
			insertLongNet(serverReceived);

		    setSequencing(false);
			setCompression(true);
			setCRCChecking(false);
		}

		inline static sys::uint16 parseTick(Packet* pack) {
			return pack->parseNetShort(2);
		}

	private:
		inline void insertLongNet(sys::uint64 val) {
			insertIntNet((sys::uint32) (val >> 32));
			insertIntNet((sys::uint32) (val & 0xFFFFFFFF));
		}
	};

    } // namespace proto
  } // namespace service
} // namespace engine

using namespace engine::service::proto;

#endif /*NETSTATUSRESPONSEMESSAGE_H_*/
