/*
** Copyright (C) 2007-2019 SWGEmu
** See file COPYING for copying conditions.
*/

#ifndef DATAGRAMSERVICECLIENT_H_
#define DATAGRAMSERVICECLIENT_H_

#include "system/lang.h"

#include "ServiceClient.h"

namespace engine {
  namespace service {

	class DatagramServiceClient : public ServiceClient {
	protected:
		bool doRun = true;

		// datagram counters for SOE net-status reporting (display stats, races tolerated)
		uint64 sentPacketCount = 0;
		uint64 receivedPacketCount = 0;

	public:
		DatagramServiceClient();
		DatagramServiceClient(const String& host, int port);
		DatagramServiceClient(Socket* sock, const SocketAddress& addr);

		virtual ~DatagramServiceClient();

		void stop() {
			doRun = false;
		}

		inline void countReceivedPacket() {
			++receivedPacketCount;
		}

		inline uint64 getSentPacketCount() const {
			return sentPacketCount;
		}

		inline uint64 getReceivedPacketCount() const {
			return receivedPacketCount;
		}

		void recieveMessages();

		void handleMessage(Packet* message);

		// socket methods
		int send(Packet* pack);

		bool read(Packet* pack);
	};

  } // namespace service
} // namespace engine

using namespace engine::service;

#endif /*DATAGRAMSERVICECLIENT_H_*/
