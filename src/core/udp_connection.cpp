#include "stdafx.h"
#include "core/udp_connection.h"
#include "core/udp_packet_dispatcher.h"

namespace core
{

    UdpConnection::UdpConnection(const SocketPtr& socket, const udp::endpoint& peer)
        : m_socket(socket), m_peer(peer), m_seqNum(0), m_ack(0), m_ackBits(0)
    {}


    void UdpConnection::send(UdpPacketBase&& packet, bool reliable)
    {
        auto& header = packet.header();
        header.seqNum = m_seqNum++;
        header.ack = m_ack;
        header.ackBits = m_ackBits;

        m_socket->async_send_to(
            boost::asio::buffer(packet.buffer()), m_peer,
            boost::bind(&UdpConnection::handleSend, this, header.seqNum,
                boost::asio::placeholders::error));

        m_sent.push_front(std::make_pair(std::move(packet), reliable));
    }

    // handler for logging errors during async_send_to
    void UdpConnection::handleSend(uint16_t seqNum, const boost::system::error_code& error)
    {
        if (error)
        {
            cError() << error.message();

            m_sent.remove_if([&seqNum](const std::pair<UdpPacketBase, bool>& p){
                return p.first.header().seqNum == seqNum;
            });
        }
    }

    
    // usually we receive packets in order, so most recent come later
    // so it makes sense to search for insertion point from most recent to oldest
    void UdpConnection::handleReceive(UdpPacketBase&& packet)
    {
        processHeader(packet.header());
        
        // find first packet older than this one
        auto it = m_received.begin();
        for (; it != m_received.end(); ++it)
        {
            if (more_recent(packet, *it))
                break;
        }

        m_received.insert(it, std::move(packet));
    }

    void UdpConnection::processHeader(const UdpPacketHeader& header)
    {
        adjustMyAck(header.seqNum);
        processPeerAcks(header.ack, header.ackBits);
    }


    void UdpConnection::adjustMyAck(uint16_t seqNum)
    {
        if (more_recent(seqNum, m_ack)) // seqNum more recent than ack
        {
            uint16_t delta = seqNum - m_ack;
            m_ackBits = delta < 32 ? m_ackBits >> delta : 0;
            m_ack = seqNum;
        }
        else // seqNum is older than ack
        {
            uint16_t delta = m_ack - seqNum;
            if (delta < 32)
                m_ackBits |= 1 << delta;
        }
    }


    void UdpConnection::processPeerAcks(uint16_t peerAck, uint32_t peerAckBits)
    {
        // peerAck = latest seqNum that peer received
        // peerAckBits = next 32 acks after latest
        
        // for each packet in queue, starting from most recent
        for (auto it = m_sent.begin(); it != m_sent.end();)
        {
            uint16_t seqNum = it->first.header().seqNum;
            if (more_recent(seqNum, peerAck))
            {
                // too young to receive ack
                ++it;
                continue;
            }

            uint16_t delta = m_ack - seqNum;
            if (delta >= 32)
            {
                // too old to receive ack
                for (; it != m_sent.end();)
                {
                    // async resend, will add this packet in front of the queue
                    // note: will not invalidate list::iterator
                    
                    if (it->second)
                    {
                        send(std::move(it->first), true);
                    }

                    it = m_sent.erase(it);
                }

                break;
            }
            
            if (delta == 0)
            {
                // acknowledge this packet
                cDebug() << "acknowledged" << seqNum;
                it = m_sent.erase(it);
            }
            else
            {
                uint32_t bit = 1 << (delta - 1);
                if ((peerAckBits & bit) == bit)
                {
                    // acknowledge this packet
                    cDebug() << "acknowledged" << seqNum;
                    it = m_sent.erase(it);
                }
            }
        }
    }

    
    // dispatch all packets from oldest to most recent, to all active listeners
    void UdpConnection::dispatchReceivedPackets(const UdpPacketDispatcher& dispatcher)
    {
        // go in reverse, from oldest to most recent
        for (auto it = m_received.rbegin(); it != m_received.rend(); ++it)
            dispatcher.dispatchPacket(*this, *it);

        // clear receive queue
        m_received.clear();
    }

}