#ifndef LATENCY_H
#define LATENCY_H

#include "Throughput.h"

class PDV : public Throughput 
{
public:
    uint16_t frame_timeout; // if 0, normal PDV measurement is done; if >0, then frames with delay higher then frame_timeout are considered lost

    PDV() : Throughput(){};                        // default constructor
    int readCmdLine(int argc, const char *argv[]); // reads further one argument: frame_timeout

    // perform pdv measurement
    void measure(uint16_t leftport, uint16_t rightport);
};

struct rte_mbuf *mkPDVTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *direction,
    const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
    const uint32_t *src_ip, uint32_t *dst_ip);

struct rte_mbuf *mkPDVTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *direction,
    const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
    struct in6_addr *src_ip, struct in6_addr *dst_ip);

struct rte_mbuf *mkPDVTestIpv4inIpv6Tun(uint16_t length, rte_mempool *pkt_pool, const char *direction,
    const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
    struct in6_addr *src_ipv6, struct in6_addr *dst_ipv6,
    const uint32_t *src_ipv4, uint32_t *dst_ipv4);

void mkPDVData(uint8_t *data, uint16_t length);

class senderParametersPDV : public senderParameters
{
public:
  uint64_t **send_ts;
  senderParametersPDV(class senderCommonParameters *cp_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *direction_,
    struct ether_addr *dst_mac_, struct ether_addr *src_mac_, uint64_t **send_ts_);
  senderParametersPDV();
};

class receiverParametersPDV : public receiverParameters
{
public:
  uint64_t num_frames; // number of all frames, needed for the rte_zmalloc call for allocating receive_ts
  uint16_t frame_timeout;
  uint64_t **receive_ts;
  receiverParametersPDV(uint64_t finish_receiving_, uint8_t eth_id_, const char *direction_,
                        uint64_t num_frames_, uint16_t frame_timeout_, uint64_t **receive_ts_);
 receiverParametersPDV();
};

void evaluatePDV(uint64_t num_of_frames, uint64_t *send_ts, uint64_t *receive_ts, uint64_t hz, uint16_t frame_timeout, int penalty, const char *direction);

#endif
