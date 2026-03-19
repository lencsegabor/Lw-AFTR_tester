#ifndef LATENCY_H
#define LATENCY_H

#include "Throughput.h"

class Latency : public Throughput
{
public:
    uint16_t first_tagged_delay; // time period while frames are sent, but no timestamps are used; then timestaps are used in the "test_duration-first_tagged_delay" length interval
    uint16_t num_of_tagged;      // number of tagged frames, 1-50000 is accepted, RFC 8219 requires at least 500, RFC 2544 requires 1

    Latency() : Throughput(){};                    // default constructor
    int readCmdLine(int argc, const char *argv[]); // reads further two arguments
    virtual int senderPoolSize();                  // adds num_of_tagged, too

    // perform latency measurement
    void measure(uint16_t leftport, uint16_t rightport);
};

struct rte_mbuf *mkLatencyTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                             const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                             const uint32_t *src_ip, uint32_t *dst_ip, uint16_t id);

struct rte_mbuf *mkLatencyTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                            const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                            struct in6_addr *src_ip, struct in6_addr *dst_ip, uint16_t id);

struct rte_mbuf *mkLatencyTestIpv4inIpv6Tun(uint16_t length, rte_mempool *pkt_pool, const char *direction,
                                const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                                struct in6_addr *src_ipv6, struct in6_addr *dst_ipv6,
                                const uint32_t *src_ipv4, uint32_t *dst_ipv4, uint16_t id);

void mkLatencyData(uint8_t *data, uint16_t length, uint16_t latency_frame_id);


void evaluateLatency(uint16_t num_of_tagged, uint64_t *send_ts, uint64_t *receive_ts, uint64_t hz, int penalty, const char *direction);


class senderCommonParametersLatency : public senderCommonParameters
{
public:
    uint16_t first_tagged_delay; //The amount of delay before sending the first tagged frame
    uint16_t num_of_tagged; // The number of tagged frames

    senderCommonParametersLatency(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t test_duration_,
        uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_, uint32_t number_of_lwB4s_, lwB4_data *lwB4_array_,
        struct in6_addr *ipv6_tunnel_, uint32_t *ipv4_server_, in6_addr *ipv6_left_bg_, struct in6_addr *ipv6_right_bg_,
                        uint16_t fwd_sport_min_, uint16_t fwd_sport_max_, uint16_t fwd_dport_min_, uint16_t fwd_dport_max_,
                        uint16_t rev_sport_min_, uint16_t rev_sport_max_, uint16_t rev_dport_min_, uint16_t rev_dport_max_,
        uint16_t first_tagged_delay_, uint16_t num_of_tagged_
        );
    senderCommonParametersLatency();
};

class senderParametersLatency : public senderParameters
{
public:
    uint64_t *send_ts; // pointer to the send timestamps

    senderParametersLatency(class senderCommonParameters *cp_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *direction_,
    struct ether_addr *dst_mac_, struct ether_addr *src_mac_,  uint64_t *send_ts_
    );
    senderParametersLatency(); 
};

class receiverParametersLatency : public receiverParameters
{
public:
    uint16_t num_of_tagged;
    uint64_t *receive_ts; // pointer to the receive timestamps
    
    receiverParametersLatency(uint64_t finish_receiving_, uint8_t eth_id_, const char *direction_, uint16_t num_of_tagged_, uint64_t *receive_ts_);
    receiverParametersLatency();
};

#endif
