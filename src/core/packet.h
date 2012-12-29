#pragma once
#include <boost/asio/ip/udp.hpp>
#include <cstdint>


namespace core {

    using boost::asio::ip::udp;
    using std::chrono::system_clock;

    static const size_t cMaxUdpPacketSize = 512;


    struct PacketHeader
    {
        uint16_t protocol;
        uint16_t seqNum;
        uint16_t ack;
        uint32_t ackBits;
    };


    class Packet
    {
    public:

        explicit Packet(uint16_t protocol)
        {
            m_buffer.reserve(cMaxUdpPacketSize);
            m_buffer.resize(sizeof(PacketHeader));
            header().protocol = protocol;
        }

        Packet(uint8_t* data, size_t len)
        {
            if (len < sizeof(PacketHeader))
                throw std::runtime_error("Packet: input data size is too small");
            m_buffer.assign(data, data + len);
        }

        const PacketHeader& header() const
        {
            return *reinterpret_cast<const PacketHeader*>(m_buffer.data());
        }

        const std::vector<uint8_t>& buffer() const
        {
            return m_buffer;
        }

        PacketHeader& header()
        {
            return *reinterpret_cast<PacketHeader*>(m_buffer.data());
        }

        std::vector<uint8_t>& buffer()
        {
            return m_buffer;
        }

    protected:

        // everything that goes across the wire is stored here
        std::vector<uint8_t> m_buffer;
    };


    typedef std::shared_ptr<Packet> PacketPtr;

}
